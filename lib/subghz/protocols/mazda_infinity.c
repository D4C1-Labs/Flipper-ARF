#include "mazda_infinity.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <furi.h>
#include <furi_hal.h>
#include <string.h>

// ============================================================================
// Mazda Infinity SubGHz keyfob protocol (decoder + encoder)
//
// Reverse-engineered from the PANDORA_MAX firmware, TX dispatcher handler
// 0xf0d0 + RF bit-banger 0x8fc4. Whitening/interleave verified against the
// firmware routines at 0x12a90 (counter interleave + data whitening) and
// 0x10994 (popcount parity of the checksum byte).
//
// Wire (verified against 0x8fc4):
//   - modulation: OOK/ASK (AM650), bands 315 / 433 MHz.
//   - Manchester, Te = 250 us (te_delta ~62).
//       bit1 = HIGH 250 -> LOW 250
//       bit0 = LOW 250 -> HIGH 250
//   - Frame on the air:
//       12x Manchester of byte 0xFF  (all-ones preamble)          [RAW]
//       ~50 unit gap
//       sync bytes FF FF D7                                        [RAW]
//       8 payload bytes, each ONE'S-COMPLEMENTED (255-x) on wire   [INVERTED]
//       trailer byte 0x5A                                          [RAW]
//     repeated up to 4 times.
//
// Logical 8-byte payload (before interleave/whitening/inversion):
//   b0..b3 = UID / serial
//   b4     = button/command. On-wire codes: Lock/Unlock/Trunk -> 0x10/0x20/0x40
//            (best-effort mapping; the raw b4 is always stored & replayed).
//   b5     = counter high, b6 = counter low  (bit-interleaved on the wire)
//   b7     = checksum = sum(b0..b6) & 0xFF
//
// WHITENING applied when BUILDING the frame (reversed on decode):
//   1) checksum: b7 = sum(b0..b6) & 0xFF
//   2) counter interleave (0x12a90):
//        new_b5 = (b5 & 0xAA) | (b6 & 0x55)
//        new_b6 = (b5 & 0x55) | (b6 & 0xAA)
//      (self-inverse: applying the same merge again restores b5/b6)
//   3) data whitening (0x12a90, keyed by popcount(b7)&1):
//        p = popcount(b7) & 1
//        if p==1: for k in 0..5      : b[k] ^= b[6]
//        else   : for k in {0,1,2,3,4,6}: b[k] ^= b[5]
//      (XOR is self-inverse; b7 is never modified so p is recoverable directly)
//   4) each of the 8 payload bytes is one's-complemented (255-x) on the wire;
//      preamble / sync / trailer are RAW.
//
// The 8 on-air data bytes = 64 bits -> min_count_bit_for_found = 64 and the
// decoded logical payload fits generic.data. We also keep the raw logical bytes
// for get_string and exact re-encode.
// ============================================================================

#define MAZDA_INF_TE_SHORT   250U
#define MAZDA_INF_TE_DELTA   62U
// Bit-sync gap between the 0xFF preamble and the FF FF D7 sync. On the original
// hardware this is a "~50 unit" busy-loop delay (0xe654); on the Flipper it just
// has to be a clean LOW long enough to separate preamble from sync. The decoder
// does not need the preamble (it aligns on the sync burst), so this gap simply
// resets the collector before the real sync+data+trailer window arrives.
#define MAZDA_INF_GAP_US     5000U
#define MAZDA_INF_PREAMBLE_BYTES 12U
#define MAZDA_INF_PAYLOAD_BYTES  8U
#define MAZDA_INF_PAYLOAD_BITS   64U
#define MAZDA_INF_TRAILER        0x5AU

// Sync bytes FF FF D7 (RAW, not inverted).
#define MAZDA_INF_SYNC0 0xFFU
#define MAZDA_INF_SYNC1 0xFFU
#define MAZDA_INF_SYNC2 0xD7U

// Button on-wire codes.
#define MAZDA_INF_BTN_LOCK   0x10U
#define MAZDA_INF_BTN_UNLOCK 0x20U
#define MAZDA_INF_BTN_TRUNK  0x40U

#define MAZDA_INF_DEFAULT_REPEAT 4U

// Total wire bytes fed as Manchester cells: preamble(12) + sync(3) + data(8) +
// trailer(1) = 24 bytes -> 24*8 bits -> 24*8*2 cells. We only align/decode the
// data window, so the decoder scans a sliding cell buffer for FF FF D7.
#define MAZDA_INF_WIRE_BYTES (MAZDA_INF_PREAMBLE_BYTES + 3U + MAZDA_INF_PAYLOAD_BYTES + 1U)

// Sliding cell window sized to hold sync(3) + data(8) + trailer(1) = 12 bytes.
#define MAZDA_INF_WINDOW_BYTES 12U
#define MAZDA_INF_WINDOW_CELLS (MAZDA_INF_WINDOW_BYTES * 8U * 2U)

// Encoder capacity: (12+3+8+1) bytes * 8 bits * 2 cells + gap markers + margin.
#define MAZDA_INF_UPLOAD_CAPACITY \
    ((MAZDA_INF_WIRE_BYTES * 8U * 2U) + 8U)

#define MAZDA_INF_DATA_FIELD "Data"

static const SubGhzBlockConst subghz_protocol_mazda_infinity_const = {
    .te_short = MAZDA_INF_TE_SHORT,
    .te_long = MAZDA_INF_TE_SHORT * 2U,
    .te_delta = MAZDA_INF_TE_DELTA,
    .min_count_bit_for_found = MAZDA_INF_PAYLOAD_BITS,
};

typedef enum {
    MazdaInfDecoderStepReset = 0,
    MazdaInfDecoderStepData,
} MazdaInfDecoderStep;

struct SubGhzProtocolDecoderMazdaInfinity {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t cells[MAZDA_INF_WINDOW_CELLS];
    uint16_t cell_count;

    uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]; // de-whitened logical payload
    uint8_t last_logical[MAZDA_INF_PAYLOAD_BYTES];
    bool last_valid;
    bool checksum_ok;
};

struct SubGhzProtocolEncoderMazdaInfinity {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t logical[MAZDA_INF_PAYLOAD_BYTES];
};

const SubGhzProtocolDecoder subghz_protocol_mazda_infinity_decoder = {
    .alloc = subghz_protocol_decoder_mazda_infinity_alloc,
    .free = subghz_protocol_decoder_mazda_infinity_free,
    .feed = subghz_protocol_decoder_mazda_infinity_feed,
    .reset = subghz_protocol_decoder_mazda_infinity_reset,
    .get_hash_data = subghz_protocol_decoder_mazda_infinity_get_hash_data,
    .serialize = subghz_protocol_decoder_mazda_infinity_serialize,
    .deserialize = subghz_protocol_decoder_mazda_infinity_deserialize,
    .get_string = subghz_protocol_decoder_mazda_infinity_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_mazda_infinity_encoder = {
    .alloc = subghz_protocol_encoder_mazda_infinity_alloc,
    .free = subghz_protocol_encoder_mazda_infinity_free,
    .deserialize = subghz_protocol_encoder_mazda_infinity_deserialize,
    .stop = subghz_protocol_encoder_mazda_infinity_stop,
    .yield = subghz_protocol_encoder_mazda_infinity_yield,
};

const SubGhzProtocol subghz_protocol_mazda_infinity = {
    .name = MAZDA_INFINITY_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_mazda_infinity_decoder,
    .encoder = &subghz_protocol_mazda_infinity_encoder,
};

// ---------------------------------------------------------------------------
// Whitening / interleave primitives (verified against 0x12a90 & 0x10994).
// All are self-inverse, so the same routine is used to build and to reverse.
// ---------------------------------------------------------------------------

static uint8_t mazda_inf_parity(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return (uint8_t)(v & 1U);
}

static uint8_t mazda_inf_checksum(const uint8_t b[MAZDA_INF_PAYLOAD_BYTES]) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < 7U; i++) {
        sum += b[i];
    }
    return (uint8_t)(sum & 0xFFU);
}

// Step 2: counter interleave (self-inverse merge of b5/b6).
static void mazda_inf_interleave(uint8_t b[MAZDA_INF_PAYLOAD_BYTES]) {
    const uint8_t b5 = b[5];
    const uint8_t b6 = b[6];
    b[5] = (uint8_t)((b5 & 0xAAU) | (b6 & 0x55U));
    b[6] = (uint8_t)((b5 & 0x55U) | (b6 & 0xAAU));
}

// Step 3: data whitening keyed by popcount(b7)&1 (self-inverse XOR).
static void mazda_inf_whiten(uint8_t b[MAZDA_INF_PAYLOAD_BYTES]) {
    const uint8_t p = mazda_inf_parity(b[7]);
    if(p == 1U) {
        for(size_t k = 0U; k < 6U; k++) {
            b[k] ^= b[6];
        }
    } else {
        b[0] ^= b[5];
        b[1] ^= b[5];
        b[2] ^= b[5];
        b[3] ^= b[5];
        b[4] ^= b[5];
        b[6] ^= b[5];
    }
}

// Build the 8 whitened+inverted on-wire data bytes from the logical payload.
//   logical -> checksum -> interleave -> whiten -> one's-complement
static void mazda_inf_logical_to_wire(
    const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES],
    uint8_t wire[MAZDA_INF_PAYLOAD_BYTES]) {
    uint8_t b[MAZDA_INF_PAYLOAD_BYTES];
    memcpy(b, logical, MAZDA_INF_PAYLOAD_BYTES);
    b[7] = mazda_inf_checksum(b); // step 1
    mazda_inf_interleave(b); // step 2
    mazda_inf_whiten(b); // step 3
    for(size_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        wire[i] = (uint8_t)(255U - b[i]); // step 4: one's complement
    }
}

// Recover the logical payload from the 8 on-wire data bytes.
//   wire -> un-invert -> un-whiten -> de-interleave  (b7 stays the checksum)
static void mazda_inf_wire_to_logical(
    const uint8_t wire[MAZDA_INF_PAYLOAD_BYTES],
    uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    uint8_t b[MAZDA_INF_PAYLOAD_BYTES];
    for(size_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        b[i] = (uint8_t)(255U - wire[i]); // inverse of step 4
    }
    // b now holds { whitened b0..b6, checksum b7 }. b7 was never whitened, so
    // popcount(b7)&1 is directly usable to undo step 3 (XOR self-inverse).
    mazda_inf_whiten(b); // inverse of step 3
    mazda_inf_interleave(b); // inverse of step 2
    memcpy(logical, b, MAZDA_INF_PAYLOAD_BYTES);
}

static const char* mazda_inf_button_name(uint8_t btn) {
    switch(btn) {
    case MAZDA_INF_BTN_LOCK:
        return "Lock";
    case MAZDA_INF_BTN_UNLOCK:
        return "Unlock";
    case MAZDA_INF_BTN_TRUNK:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static uint32_t mazda_inf_uid(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    return ((uint32_t)logical[0] << 24U) | ((uint32_t)logical[1] << 16U) |
           ((uint32_t)logical[2] << 8U) | logical[3];
}

static uint16_t mazda_inf_counter(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    return (uint16_t)(((uint16_t)logical[5] << 8U) | logical[6]);
}

static void mazda_inf_decode_fields(SubGhzBlockGeneric* generic, const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    generic->serial = mazda_inf_uid(logical);
    generic->btn = logical[4];
    generic->cnt = mazda_inf_counter(logical);
    generic->data_count_bit = MAZDA_INF_PAYLOAD_BITS;
    // 64-bit logical payload fits in generic.data (MSB-first over b0..b7).
    uint64_t data = 0U;
    for(size_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        data = (data << 8U) | logical[i];
    }
    generic->data = data;
}

static bool mazda_inf_frame_valid(const uint8_t logical[MAZDA_INF_PAYLOAD_BYTES]) {
    const uint32_t uid = mazda_inf_uid(logical);
    return uid != 0U && uid != UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Decoder: collect Manchester half-bit cells, slide a window and look for the
// FF FF D7 sync followed by 8 data bytes + 0x5A trailer. Cell pairs decode as:
//   HIGH,LOW -> 1 ; LOW,HIGH -> 0   (matches the encoder / RE polarity).
// ---------------------------------------------------------------------------

static bool mazda_inf_duration_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, MAZDA_INF_TE_SHORT) < MAZDA_INF_TE_DELTA;
}

static bool mazda_inf_duration_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, MAZDA_INF_TE_SHORT * 2U) < MAZDA_INF_TE_DELTA;
}

static void mazda_inf_clear_cells(SubGhzProtocolDecoderMazdaInfinity* instance) {
    instance->cell_count = 0U;
    memset(instance->cells, 0, sizeof(instance->cells));
}

// Decode `bytes` logical bytes MSB-first from cells starting at cell offset
// `cell_off`. Returns false if any pair is not a valid Manchester cell.
static bool mazda_inf_cells_to_bytes(
    const uint8_t* cells,
    uint16_t cell_off,
    uint8_t* out,
    size_t bytes) {
    for(size_t bit = 0U; bit < bytes * 8U; bit++) {
        const uint8_t first = cells[cell_off + bit * 2U];
        const uint8_t second = cells[cell_off + bit * 2U + 1U];
        if(first == second) return false;
        // HIGH,LOW -> 1 ; LOW,HIGH -> 0
        const bool value = (first != 0U) && (second == 0U);
        if(bit % 8U == 0U) out[bit / 8U] = 0U;
        if(value) {
            out[bit / 8U] |= (uint8_t)(1U << (7U - (bit & 7U)));
        }
    }
    return true;
}

static void mazda_inf_try_decode(SubGhzProtocolDecoderMazdaInfinity* instance) {
    // Need sync(3) + data(8) + trailer(1) = 12 bytes = 192 bits = 384 cells.
    const uint16_t need_cells = MAZDA_INF_WINDOW_BYTES * 8U * 2U;
    if(instance->cell_count < need_cells) return;

    // Try every byte-aligned offset within the buffered cells so the sync can be
    // found regardless of how many preamble cells preceded it.
    const uint16_t max_off = (uint16_t)(instance->cell_count - need_cells);
    for(uint16_t off = 0U; off <= max_off; off += 2U) {
        uint8_t win[MAZDA_INF_WINDOW_BYTES];
        if(!mazda_inf_cells_to_bytes(instance->cells, off, win, MAZDA_INF_WINDOW_BYTES)) {
            continue;
        }
        if(win[0] != MAZDA_INF_SYNC0 || win[1] != MAZDA_INF_SYNC1 || win[2] != MAZDA_INF_SYNC2) {
            continue;
        }
        if(win[MAZDA_INF_WINDOW_BYTES - 1U] != MAZDA_INF_TRAILER) {
            continue;
        }

        const uint8_t* wire = &win[3]; // 8 inverted+whitened data bytes
        uint8_t logical[MAZDA_INF_PAYLOAD_BYTES];
        mazda_inf_wire_to_logical(wire, logical);

        if(!mazda_inf_frame_valid(logical)) continue;

        const bool cs_ok = (logical[7] == mazda_inf_checksum(logical));

        if(instance->last_valid &&
           memcmp(instance->last_logical, logical, MAZDA_INF_PAYLOAD_BYTES) == 0) {
            return; // repeated identical frame
        }

        memcpy(instance->logical, logical, MAZDA_INF_PAYLOAD_BYTES);
        memcpy(instance->last_logical, logical, MAZDA_INF_PAYLOAD_BYTES);
        instance->last_valid = true;
        instance->checksum_ok = cs_ok;
        mazda_inf_decode_fields(&instance->generic, logical);

        if(instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
        return;
    }
}

static void mazda_inf_push_cell(SubGhzProtocolDecoderMazdaInfinity* instance, bool level) {
    if(instance->cell_count < MAZDA_INF_WINDOW_CELLS) {
        instance->cells[instance->cell_count++] = level ? 1U : 0U;
    } else {
        memmove(instance->cells, &instance->cells[1], MAZDA_INF_WINDOW_CELLS - 1U);
        instance->cells[MAZDA_INF_WINDOW_CELLS - 1U] = level ? 1U : 0U;
    }
    mazda_inf_try_decode(instance);
}

void* subghz_protocol_decoder_mazda_infinity_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMazdaInfinity* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderMazdaInfinity));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mazda_infinity;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_mazda_infinity_reset(instance);
    return instance;
}

void subghz_protocol_decoder_mazda_infinity_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;
    free(instance);
}

void subghz_protocol_decoder_mazda_infinity_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;
    instance->decoder.parser_step = MazdaInfDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->last_valid = false;
    instance->checksum_ok = false;
    memset(instance->logical, 0, sizeof(instance->logical));
    memset(instance->last_logical, 0, sizeof(instance->last_logical));
    mazda_inf_clear_cells(instance);
}

void subghz_protocol_decoder_mazda_infinity_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;

    switch(instance->decoder.parser_step) {
    case MazdaInfDecoderStepReset:
        if(mazda_inf_duration_is_short(duration)) {
            mazda_inf_clear_cells(instance);
            instance->decoder.parser_step = MazdaInfDecoderStepData;
            mazda_inf_push_cell(instance, level);
        } else if(mazda_inf_duration_is_long(duration)) {
            mazda_inf_clear_cells(instance);
            instance->decoder.parser_step = MazdaInfDecoderStepData;
            mazda_inf_push_cell(instance, level);
            mazda_inf_push_cell(instance, level);
        }
        break;

    case MazdaInfDecoderStepData:
        if(mazda_inf_duration_is_short(duration)) {
            mazda_inf_push_cell(instance, level);
        } else if(mazda_inf_duration_is_long(duration)) {
            mazda_inf_push_cell(instance, level);
            mazda_inf_push_cell(instance, level);
        } else {
            // Inter-frame gap or noise: attempt a final decode then reset so the
            // fob's next repeat can be captured.
            mazda_inf_try_decode(instance);
            mazda_inf_clear_cells(instance);
            instance->decoder.parser_step = MazdaInfDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_mazda_infinity_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        hash ^= instance->logical[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_mazda_infinity_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    // Store the decoded LOGICAL payload; the encoder re-derives the wire from it.
    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, MAZDA_INF_DATA_FIELD, instance->logical, MAZDA_INF_PAYLOAD_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    if(!flipper_format_insert_or_update_uint32(
           flipper_format, "Serial", &instance->generic.serial, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t btn = instance->generic.btn;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t cnt = instance->generic.cnt;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus subghz_protocol_decoder_mazda_infinity_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mazda_infinity_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, MAZDA_INF_DATA_FIELD, instance->logical, MAZDA_INF_PAYLOAD_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!mazda_inf_frame_valid(instance->logical)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->checksum_ok = (instance->logical[7] == mazda_inf_checksum(instance->logical));
    mazda_inf_decode_fields(&instance->generic, instance->logical);
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_mazda_infinity_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderMazdaInfinity* instance = context;

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%08lX Btn:[%s]\r\n"
        "Cnt:%04lX CS:%02X[%s]\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        MAZDA_INF_PAYLOAD_BITS,
        (unsigned long)instance->generic.serial,
        mazda_inf_button_name((uint8_t)instance->generic.btn),
        (unsigned long)instance->generic.cnt,
        instance->logical[7],
        instance->checksum_ok ? "OK" : "BAD",
        instance->logical[0],
        instance->logical[1],
        instance->logical[2],
        instance->logical[3],
        instance->logical[4],
        instance->logical[5],
        instance->logical[6],
        instance->logical[7]);
}

// ---------------------------------------------------------------------------
// Encoder: exact inverse of the decode path.
//   logical -> (checksum, interleave, whiten, invert) -> 8 wire data bytes,
//   then emit: 12x Manchester(0xFF) + FF FF D7 + 8 wire bytes + 0x5A trailer,
//   Manchester Te=250, repeat 4.
//     bit1 -> HIGH 250 , LOW 250
//     bit0 -> LOW 250 , HIGH 250
// Replay-capable: the stored logical bytes reproduce the exact wire.
// ---------------------------------------------------------------------------

static size_t mazda_inf_emit_byte(LevelDuration* upload, size_t index, uint8_t byte) {
    const uint32_t te = MAZDA_INF_TE_SHORT;
    for(uint8_t bit = 0U; bit < 8U; bit++) {
        const bool value = ((byte >> (7U - bit)) & 1U) != 0U;
        if(value) {
            // bit1 = HIGH then LOW
            upload[index++] = level_duration_make(true, te);
            upload[index++] = level_duration_make(false, te);
        } else {
            // bit0 = LOW then HIGH
            upload[index++] = level_duration_make(false, te);
            upload[index++] = level_duration_make(true, te);
        }
    }
    return index;
}

static bool mazda_inf_encoder_build_upload(SubGhzProtocolEncoderMazdaInfinity* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    uint8_t wire[MAZDA_INF_PAYLOAD_BYTES];
    mazda_inf_logical_to_wire(instance->logical, wire);

    size_t index = 0U;

    // 12x Manchester of 0xFF (all-ones preamble).
    for(uint8_t i = 0U; i < MAZDA_INF_PREAMBLE_BYTES; i++) {
        index = mazda_inf_emit_byte(upload, index, 0xFFU);
    }

    // Bit-sync gap (LOW) between preamble and sync.
    upload[index++] = level_duration_make(false, MAZDA_INF_GAP_US);

    // Sync FF FF D7 (RAW).
    index = mazda_inf_emit_byte(upload, index, MAZDA_INF_SYNC0);
    index = mazda_inf_emit_byte(upload, index, MAZDA_INF_SYNC1);
    index = mazda_inf_emit_byte(upload, index, MAZDA_INF_SYNC2);

    // 8 payload bytes (already inverted/whitened wire bytes).
    for(uint8_t i = 0U; i < MAZDA_INF_PAYLOAD_BYTES; i++) {
        index = mazda_inf_emit_byte(upload, index, wire[i]);
    }

    // Trailer 0x5A (RAW).
    index = mazda_inf_emit_byte(upload, index, MAZDA_INF_TRAILER);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
    return true;
}

void* subghz_protocol_encoder_mazda_infinity_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMazdaInfinity* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderMazdaInfinity));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mazda_infinity;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = MAZDA_INF_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_mazda_infinity_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMazdaInfinity* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_mazda_infinity_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMazdaInfinity* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

LevelDuration subghz_protocol_encoder_mazda_infinity_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMazdaInfinity* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0 ||
       instance->encoder.size_upload == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        // Endless/breakless TX: while OK is held do not consume repeats.
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0U;
    }

    return ret;
}

SubGhzProtocolStatus subghz_protocol_encoder_mazda_infinity_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderMazdaInfinity* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0U;

    flipper_format_rewind(flipper_format);
    FuriString* protocol_name = furi_string_alloc();
    bool protocol_ok = flipper_format_read_string(flipper_format, "Protocol", protocol_name) &&
                       furi_string_equal(protocol_name, instance->base.protocol->name);
    furi_string_free(protocol_name);
    if(!protocol_ok) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t bit_count = 0U;
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count, 1) ||
       bit_count != MAZDA_INF_PAYLOAD_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, MAZDA_INF_DATA_FIELD, instance->logical, MAZDA_INF_PAYLOAD_BYTES) ||
       !mazda_inf_frame_valid(instance->logical)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = mazda_inf_uid(instance->logical);
    instance->generic.btn = instance->logical[4];
    instance->generic.cnt = mazda_inf_counter(instance->logical);

    // [PROTOPIRATE_PORT] custom_btn support.
    // Mazda Infinity buttons: Lock=0x10, Unlock=0x20, Trunk=0x40. OK replays the
    // captured b4. The D-pad selects a known button; on a change the 16-bit
    // rolling counter is advanced so the re-encoded frame is a valid new press.
    // b5/b6 hold the logical counter (high/low); interleave/whiten/checksum are
    // recomputed inside mazda_inf_logical_to_wire, so any change round-trips.
    const uint8_t original_btn = instance->logical[4];
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(original_btn);
    }
    subghz_custom_btn_set_max(3);
    const uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t new_btn = original_btn;
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:    new_btn = MAZDA_INF_BTN_LOCK; break;
    case SUBGHZ_CUSTOM_BTN_DOWN:  new_btn = MAZDA_INF_BTN_UNLOCK; break;
    case SUBGHZ_CUSTOM_BTN_LEFT:  new_btn = MAZDA_INF_BTN_TRUNK; break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:                      new_btn = original_btn; break; // exact replay
    }

    if(new_btn != original_btn) {
        instance->logical[4] = new_btn;
        // Advance the rolling counter for a genuine (non-replay) press, honoring
        // an explicit framework override if one is set.
        uint32_t new_counter = instance->generic.cnt;
        uint32_t override_cnt = 0U;
        if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
            new_counter = override_cnt;
        } else {
            new_counter += (uint32_t)furi_hal_subghz_get_rolling_counter_mult();
        }
        new_counter &= 0xFFFFU;
        instance->logical[5] = (uint8_t)((new_counter >> 8U) & 0xFFU);
        instance->logical[6] = (uint8_t)(new_counter & 0xFFU);
        instance->logical[7] = mazda_inf_checksum(instance->logical);
        instance->generic.btn = new_btn;
        instance->generic.cnt = (uint16_t)new_counter;
    }

    mazda_inf_decode_fields(&instance->generic, instance->logical);

    uint32_t repeat = MAZDA_INF_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? MAZDA_INF_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(MAZDA_INF_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!mazda_inf_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

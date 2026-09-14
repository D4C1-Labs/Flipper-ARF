#include "hundai.h"

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
// Hundai (new Hyundai) SubGHz keyfob protocol (decoder + encoder)
//
// Reverse-engineered from the PANDORA_MAX firmware:
//   - button/session dispatcher handler @ 0xf236
//   - Manchester RF bit-banger (TX engine) @ 0x8290
//
// This is the NEW Hyundai keyfob, distinct from our kia_v0..v7 family.
//
// Wire (verified against the RF engine @ 0x8290):
//   - modulation: OOK/ASK (AM650 preset), bands 315 / 433 MHz.
//   - Manchester, Te = 500 us (te_delta ~125).
//       bit1 = HIGH 500 -> LOW 500 (falling edge mid-bit)
//       bit0 = LOW 500  -> HIGH 500 (rising edge mid-bit)
//   - Frame on the air (per press, sent TWICE):
//       header/sync : LOW 4500 -> HIGH 1000 -> LOW 500
//       preamble    : 32 bytes of 0x55 (= 256 alternating Manchester bits), MSB-first
//       body        : 9 payload bytes, MSB-first Manchester
//     inter-frame gap between the two frames: LOW 2500 -> HIGH 1000 -> LOW 500
//
// Logical 9-byte payload:
//   b0..b4, b8 = UID / serial (+ possible check byte b8; display, never rejected)
//   b5 low nibble = button: btn0 -> |=0x01, btn1 -> |=0x02, btn2 -> |=0x09
//                   (bits 0 and 3). b5 high nibble preserved.
//   b6 = session / frame counter (++ per press)
//   b7 high nibble = rolling KEY INDEX 1..15 (advances when b6 wraps to 0:
//                    new_hi = 1 + old_hi; if it would be 0, reset to 1 i.e. 0x10).
//   b7 low nibble  = channel / DIP nibble (device state; stored/replayed as captured).
//
// 9 bytes = 72 bits > 64, so the raw 9 bytes live in raw_data[] and are
// serialized as a "Data" hex field (mirrors fiat_v2.c "Raw" / mercedes.c "Data").
// Rolling code with no known key -> emulation is a byte-identical REPLAY (the
// D-pad can re-stamp b5 button nibble and advance b6/b7 for a fresh press).
// ============================================================================

#define HUNDAI_TE_SHORT       500U
#define HUNDAI_TE_DELTA       125U
#define HUNDAI_HDR_LOW        4500U
#define HUNDAI_HDR_HIGH       1000U
#define HUNDAI_HDR_LOW2       500U
#define HUNDAI_IFG_LOW        2500U
#define HUNDAI_IFG_HIGH       1000U
#define HUNDAI_IFG_LOW2       500U
#define HUNDAI_PREAMBLE_BYTES 32U
#define HUNDAI_PREAMBLE_BYTE  0x55U
#define HUNDAI_WIRE_BYTES     9U
#define HUNDAI_WIRE_BITS      72U

// Boundary between a Manchester cell (<= ~1000us) and a real gap / header LOW.
#define HUNDAI_BOUNDARY_MIN_US 1500U

// Button nibble codes (OR'd into b5 low nibble).
#define HUNDAI_BTN_0 0x01U
#define HUNDAI_BTN_1 0x02U
#define HUNDAI_BTN_2 0x09U

#define HUNDAI_DEFAULT_REPEAT 2U

// Encoder capacity for ONE frame: header(3) + 32*8*2 preamble cells + 72*2 body
// cells + trailing gap. The whole thing is emitted twice, plus the inter-frame
// gap markers, all inside a single upload buffer.
#define HUNDAI_FRAME_CELLS \
    (3U + (HUNDAI_PREAMBLE_BYTES * 8U * 2U) + (HUNDAI_WIRE_BITS * 2U))
#define HUNDAI_UPLOAD_CAPACITY ((HUNDAI_FRAME_CELLS * 2U) + 8U)

#define HUNDAI_DATA_FIELD "Data"

// Decoder alignment anchor: require this many trailing preamble 0x55 bytes
// immediately before the 9-byte body so the byte alignment is unambiguous.
#define HUNDAI_SYNC_PREAMBLE_BYTES 4U

// Cell buffer: we collect all Manchester half-cells of a frame (header +
// 32-byte preamble + 9-byte body). At the end-of-frame gap the body is decoded
// from the TAIL of the buffer: the last (anchor + 9) bytes, anchored on the 4
// trailing preamble 0x55 bytes. Buffer sized for the whole frame plus margin.
#define HUNDAI_ANCHOR_BYTES HUNDAI_SYNC_PREAMBLE_BYTES
#define HUNDAI_TAIL_BYTES   (HUNDAI_ANCHOR_BYTES + HUNDAI_WIRE_BYTES)
#define HUNDAI_CELL_CAPACITY \
    (((HUNDAI_PREAMBLE_BYTES + HUNDAI_WIRE_BYTES + 4U) * 8U * 2U) + 8U)

static const SubGhzBlockConst subghz_protocol_hundai_const = {
    .te_short = HUNDAI_TE_SHORT,
    .te_long = HUNDAI_TE_SHORT * 2U,
    .te_delta = HUNDAI_TE_DELTA,
    .min_count_bit_for_found = HUNDAI_WIRE_BITS,
};

typedef enum {
    HundaiDecoderStepReset = 0,
    HundaiDecoderStepData,
} HundaiDecoderStep;

struct SubGhzProtocolDecoderHundai {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    // Manchester half-cell buffer. Each entry is a captured half-cell level
    // (0/1); merged long pulses are split into two identical half-cells before
    // being pushed. Decoded from the tail at the end-of-frame gap.
    uint8_t cells[HUNDAI_CELL_CAPACITY];
    uint16_t cell_count;

    uint8_t raw_data[HUNDAI_WIRE_BYTES];

    // Duplicate/integrity check: the fob sends two identical frames per press.
    uint8_t prev_frame[HUNDAI_WIRE_BYTES];
    bool prev_frame_valid;
    bool dup_confirmed; // second identical frame was seen

    // Last reported frame, to suppress endless re-report of the same press.
    uint8_t last_reported[HUNDAI_WIRE_BYTES];
    bool last_reported_valid;
};

struct SubGhzProtocolEncoderHundai {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[HUNDAI_WIRE_BYTES];
};

const SubGhzProtocolDecoder subghz_protocol_hundai_decoder = {
    .alloc = subghz_protocol_decoder_hundai_alloc,
    .free = subghz_protocol_decoder_hundai_free,
    .feed = subghz_protocol_decoder_hundai_feed,
    .reset = subghz_protocol_decoder_hundai_reset,
    .get_hash_data = subghz_protocol_decoder_hundai_get_hash_data,
    .serialize = subghz_protocol_decoder_hundai_serialize,
    .deserialize = subghz_protocol_decoder_hundai_deserialize,
    .get_string = subghz_protocol_decoder_hundai_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_hundai_encoder = {
    .alloc = subghz_protocol_encoder_hundai_alloc,
    .free = subghz_protocol_encoder_hundai_free,
    .deserialize = subghz_protocol_encoder_hundai_deserialize,
    .stop = subghz_protocol_encoder_hundai_stop,
    .yield = subghz_protocol_encoder_hundai_yield,
};

const SubGhzProtocol subghz_protocol_hundai = {
    .name = HUNDAI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_hundai_decoder,
    .encoder = &subghz_protocol_hundai_encoder,
};

// ---------------------------------------------------------------------------
// Field helpers
// ---------------------------------------------------------------------------

static uint32_t hundai_uid(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    // b0..b3 as the 32-bit UID view (b4/b8 are extra serial/check bytes).
    return ((uint32_t)raw[0] << 24U) | ((uint32_t)raw[1] << 16U) | ((uint32_t)raw[2] << 8U) |
           raw[3];
}

static uint8_t hundai_button(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    return (uint8_t)(raw[5] & 0x0FU);
}

static uint8_t hundai_key_index(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    return (uint8_t)((raw[7] >> 4U) & 0x0FU);
}

static uint8_t hundai_channel(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    return (uint8_t)(raw[7] & 0x0FU);
}

static const char* hundai_button_name(uint8_t btn_nibble) {
    switch(btn_nibble) {
    case HUNDAI_BTN_0:
        return "Lock";
    case HUNDAI_BTN_1:
        return "Unlock";
    case HUNDAI_BTN_2:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static bool hundai_frame_valid(const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    const uint32_t uid = hundai_uid(raw);
    return uid != 0U && uid != UINT32_MAX;
}

static void hundai_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[HUNDAI_WIRE_BYTES]) {
    generic->serial = hundai_uid(raw);
    generic->btn = hundai_button(raw);
    generic->cnt = raw[6];
    generic->data_count_bit = HUNDAI_WIRE_BITS;
    // 72 bits do not fit generic.data; keep a 64-bit digest (b1..b8) for hashing.
    uint64_t data = 0U;
    for(size_t i = 1U; i < HUNDAI_WIRE_BYTES; i++) data = (data << 8U) | raw[i];
    generic->data = data;
}

// b7 high-nibble key-index rollover: advance when the session counter b6 wraps
// to 0. new_hi = 1 + old_hi; if that would be 0 (old_hi == 15), reset to 1.
// Applied together with the b6 increment for a genuine (non-replay) press.
static void hundai_advance_counter(uint8_t raw[HUNDAI_WIRE_BYTES]) {
    const uint8_t new_b6 = (uint8_t)(raw[6] + 1U);
    raw[6] = new_b6;
    if(new_b6 == 0U) {
        uint8_t hi = (uint8_t)((raw[7] >> 4U) & 0x0FU);
        uint8_t new_hi = (uint8_t)((hi + 1U) & 0x0FU);
        if(new_hi == 0U) new_hi = 1U; // never 0; wrap 15 -> 1
        raw[7] = (uint8_t)((new_hi << 4U) | (raw[7] & 0x0FU)); // preserve channel nibble
    }
}

// ---------------------------------------------------------------------------
// Decoder: collect Manchester half-cells into a sliding window (mazda_infinity
// style) and scan byte-aligned offsets for the tail of the 0x55 preamble
// followed by the 9-byte body. This gives EXACT, self-inverse polarity control
// and copes with the alternating 0x55 preamble (which the self-synchronizing
// firmware manchester state machine cannot decode from merged pulses).
//
// Cell-pair -> bit mapping (matches the encoder / RE polarity):
//   HIGH,LOW -> 1 ; LOW,HIGH -> 0
// i.e. bit1 = HIGH 500 -> LOW 500, bit0 = LOW 500 -> HIGH 500. Verified to
// round-trip exactly against the encoder for all 9 body bytes.
//
// A merged long pulse (two adjacent same-level half-cells) is split into two
// identical half-cells before being pushed, so the window always holds true
// half-cells regardless of pulse merging on capture.
// ---------------------------------------------------------------------------

static bool hundai_is_short(uint32_t d) {
    return DURATION_DIFF(d, HUNDAI_TE_SHORT) < HUNDAI_TE_DELTA;
}

static bool hundai_is_long(uint32_t d) {
    return DURATION_DIFF(d, HUNDAI_TE_SHORT * 2U) < HUNDAI_TE_DELTA;
}

static void hundai_clear_cells(SubGhzProtocolDecoderHundai* instance) {
    instance->cell_count = 0U;
    memset(instance->cells, 0, sizeof(instance->cells));
}

// Decode `bytes` logical bytes MSB-first from cells starting at cell offset
// `cell_off`. Returns false if any pair is not a valid Manchester cell.
static bool
    hundai_cells_to_bytes(const uint8_t* cells, uint16_t cell_off, uint8_t* out, size_t bytes) {
    for(size_t bit = 0U; bit < bytes * 8U; bit++) {
        const uint8_t first = cells[cell_off + bit * 2U];
        const uint8_t second = cells[cell_off + bit * 2U + 1U];
        if(first == second) return false; // not a valid Manchester cell
        // HIGH,LOW -> 1 ; LOW,HIGH -> 0
        const bool value = (first != 0U) && (second == 0U);
        if(bit % 8U == 0U) out[bit / 8U] = 0U;
        if(value) {
            out[bit / 8U] |= (uint8_t)(1U << (7U - (bit & 7U)));
        }
    }
    return true;
}

static void hundai_commit(SubGhzProtocolDecoderHundai* instance, const uint8_t frame[HUNDAI_WIRE_BYTES]) {
    // Confirm the second repeated frame matches (dup / integrity check).
    if(instance->prev_frame_valid &&
       memcmp(instance->prev_frame, frame, HUNDAI_WIRE_BYTES) == 0) {
        instance->dup_confirmed = true;
    }
    memcpy(instance->prev_frame, frame, HUNDAI_WIRE_BYTES);
    instance->prev_frame_valid = true;

    // Suppress endless re-report of the same press.
    if(instance->last_reported_valid &&
       memcmp(instance->last_reported, frame, HUNDAI_WIRE_BYTES) == 0) {
        return;
    }

    memcpy(instance->raw_data, frame, HUNDAI_WIRE_BYTES);
    memcpy(instance->last_reported, frame, HUNDAI_WIRE_BYTES);
    instance->last_reported_valid = true;

    hundai_decode_fields(&instance->generic, instance->raw_data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// Decode the body from the TAIL of the collected cells: the last (anchor + 9)
// bytes, anchored on the 4 trailing preamble 0x55 bytes. This is unambiguous
// even when the body's leading bytes happen to be 0x55.
static void hundai_try_decode(SubGhzProtocolDecoderHundai* instance) {
    const uint16_t tail_cells = HUNDAI_TAIL_BYTES * 8U * 2U;
    if(instance->cell_count < tail_cells) return;

    const uint16_t off = (uint16_t)(instance->cell_count - tail_cells);
    uint8_t win[HUNDAI_TAIL_BYTES];
    if(!hundai_cells_to_bytes(instance->cells, off, win, HUNDAI_TAIL_BYTES)) return;

    for(uint8_t i = 0U; i < HUNDAI_ANCHOR_BYTES; i++) {
        if(win[i] != HUNDAI_PREAMBLE_BYTE) return; // preamble anchor mismatch
    }

    const uint8_t* frame = &win[HUNDAI_ANCHOR_BYTES]; // 9 body bytes
    if(!hundai_frame_valid(frame)) return;

    hundai_commit(instance, frame);
}

static void hundai_push_cell(SubGhzProtocolDecoderHundai* instance, bool level) {
    if(instance->cell_count < HUNDAI_CELL_CAPACITY) {
        instance->cells[instance->cell_count++] = level ? 1U : 0U;
    } else {
        memmove(instance->cells, &instance->cells[1], HUNDAI_CELL_CAPACITY - 1U);
        instance->cells[HUNDAI_CELL_CAPACITY - 1U] = level ? 1U : 0U;
    }
}

void* subghz_protocol_decoder_hundai_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHundai* instance = calloc(1, sizeof(SubGhzProtocolDecoderHundai));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_hundai;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_hundai_reset(instance);
    return instance;
}

void subghz_protocol_decoder_hundai_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHundai* instance = context;
    free(instance);
}

void subghz_protocol_decoder_hundai_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHundai* instance = context;
    instance->decoder.parser_step = HundaiDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->prev_frame_valid = false;
    instance->last_reported_valid = false;
    instance->dup_confirmed = false;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    memset(instance->prev_frame, 0, sizeof(instance->prev_frame));
    memset(instance->last_reported, 0, sizeof(instance->last_reported));
    hundai_clear_cells(instance);
}

void subghz_protocol_decoder_hundai_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHundai* instance = context;

    switch(instance->decoder.parser_step) {
    case HundaiDecoderStepReset:
        // Wait for the header / inter-frame gap LOW (>= 1500us) then start
        // collecting cells; the header HIGH/LOW and remaining preamble are
        // absorbed by the sliding-window scan.
        if(!level && duration >= HUNDAI_BOUNDARY_MIN_US) {
            hundai_clear_cells(instance);
            instance->decoder.parser_step = HundaiDecoderStepData;
        }
        break;

    case HundaiDecoderStepData:
        if(hundai_is_short(duration)) {
            hundai_push_cell(instance, level);
        } else if(hundai_is_long(duration)) {
            // Merged long pulse = two identical half-cells.
            hundai_push_cell(instance, level);
            hundai_push_cell(instance, level);
        } else {
            // Header HIGH (~1000us is a long, handled above), inter-frame gap, or
            // end of frame. On a real gap (>=1500us LOW), try a final decode then
            // start a fresh collection window for the repeated frame.
            hundai_try_decode(instance);
            hundai_clear_cells(instance);
            if(!level && duration >= HUNDAI_BOUNDARY_MIN_US) {
                instance->decoder.parser_step = HundaiDecoderStepData;
            } else {
                instance->decoder.parser_step = HundaiDecoderStepReset;
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_hundai_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHundai* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0; i < HUNDAI_WIRE_BYTES; i++) hash ^= instance->raw_data[i];
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_hundai_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHundai* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, HUNDAI_DATA_FIELD, instance->raw_data, HUNDAI_WIRE_BYTES)) {
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

SubGhzProtocolStatus
    subghz_protocol_decoder_hundai_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHundai* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_hundai_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, HUNDAI_DATA_FIELD, instance->raw_data, HUNDAI_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!hundai_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    hundai_decode_fields(&instance->generic, instance->raw_data);
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_hundai_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderHundai* instance = context;

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%08lX Btn:[%s]\r\n"
        "Cnt:%02X KeyIdx:%X Ch:%X Dup:%s\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        HUNDAI_WIRE_BITS,
        (unsigned long)instance->generic.serial,
        hundai_button_name(hundai_button(instance->raw_data)),
        instance->raw_data[6],
        hundai_key_index(instance->raw_data),
        hundai_channel(instance->raw_data),
        instance->dup_confirmed ? "OK" : "single",
        instance->raw_data[0],
        instance->raw_data[1],
        instance->raw_data[2],
        instance->raw_data[3],
        instance->raw_data[4],
        instance->raw_data[5],
        instance->raw_data[6],
        instance->raw_data[7],
        instance->raw_data[8]);
}

// ---------------------------------------------------------------------------
// Encoder: rebuild the exact TX wire and send the frame TWICE.
//   Per frame:
//     header  : LOW 4500 -> HIGH 1000 -> LOW 500
//     preamble: 32x 0x55 Manchester (MSB-first)
//     body    : 9 bytes Manchester (MSB-first)
//       bit1 -> HIGH 500 , LOW 500
//       bit0 -> LOW 500 , HIGH 500
//   inter-frame gap between the two frames: LOW 2500 -> HIGH 1000 -> LOW 500
// Replay-based: the stored 9 bytes reproduce the wire exactly. A D-pad button
// change re-stamps b5's button nibble and advances b6 (+ b7 key-index on wrap).
// ---------------------------------------------------------------------------

static size_t hundai_emit_byte_msb(LevelDuration* upload, size_t index, uint8_t byte) {
    const uint32_t te = HUNDAI_TE_SHORT;
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

static size_t hundai_emit_frame(LevelDuration* upload, size_t index, const uint8_t raw[HUNDAI_WIRE_BYTES], bool first_frame) {
    // Header: LOW (4500 frame / 2500 inter-frame) -> HIGH 1000 -> LOW 500.
    upload[index++] =
        level_duration_make(false, first_frame ? HUNDAI_HDR_LOW : HUNDAI_IFG_LOW);
    upload[index++] = level_duration_make(true, HUNDAI_HDR_HIGH);
    upload[index++] = level_duration_make(false, HUNDAI_HDR_LOW2);

    // 32 x 0x55 preamble (MSB-first Manchester).
    for(uint8_t i = 0U; i < HUNDAI_PREAMBLE_BYTES; i++) {
        index = hundai_emit_byte_msb(upload, index, HUNDAI_PREAMBLE_BYTE);
    }

    // 9 body bytes (MSB-first Manchester).
    for(uint8_t i = 0U; i < HUNDAI_WIRE_BYTES; i++) {
        index = hundai_emit_byte_msb(upload, index, raw[i]);
    }
    return index;
}

static bool hundai_encoder_build_upload(SubGhzProtocolEncoderHundai* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    size_t index = 0U;
    // Frame sent TWICE per press. The first frame opens with the 4500us header;
    // the second is preceded by the inter-frame gap (2500us) header sequence.
    index = hundai_emit_frame(upload, index, instance->raw_data, true);
    index = hundai_emit_frame(upload, index, instance->raw_data, false);

    // Trailing gap so consecutive repeats are separable.
    upload[index++] = level_duration_make(false, HUNDAI_IFG_LOW);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
    return true;
}

void* subghz_protocol_encoder_hundai_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHundai* instance = calloc(1, sizeof(SubGhzProtocolEncoderHundai));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_hundai;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = HUNDAI_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_hundai_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderHundai* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_hundai_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderHundai* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

LevelDuration subghz_protocol_encoder_hundai_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderHundai* instance = context;

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

SubGhzProtocolStatus
    subghz_protocol_encoder_hundai_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderHundai* instance = context;

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
       bit_count != HUNDAI_WIRE_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, HUNDAI_DATA_FIELD, instance->raw_data, HUNDAI_WIRE_BYTES) ||
       !hundai_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = hundai_uid(instance->raw_data);
    instance->generic.btn = hundai_button(instance->raw_data);
    instance->generic.cnt = instance->raw_data[6];

    // [PROTOPIRATE_PORT] custom_btn support. Hundai exposes 3 buttons via the b5
    // low nibble: Lock=0x01, Unlock=0x02, Trunk=0x09. OK replays the captured
    // frame byte-identically. A D-pad change re-stamps b5's button nibble (high
    // nibble preserved) and advances the session counter b6 (with b7 key-index
    // rollover on wrap) so the emitted frame is a valid fresh press.
    const uint8_t original_btn = hundai_button(instance->raw_data);
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(original_btn);
    }
    subghz_custom_btn_set_max(3);
    const uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t new_btn = original_btn;
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:   new_btn = HUNDAI_BTN_0; break;
    case SUBGHZ_CUSTOM_BTN_DOWN: new_btn = HUNDAI_BTN_1; break;
    case SUBGHZ_CUSTOM_BTN_LEFT: new_btn = HUNDAI_BTN_2; break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:                     new_btn = original_btn; break; // exact replay
    }

    if(new_btn != original_btn) {
        // Re-stamp the button nibble (preserve b5 high nibble).
        instance->raw_data[5] = (uint8_t)((instance->raw_data[5] & 0xF0U) | (new_btn & 0x0FU));
        // Advance the session counter (honoring an explicit override if set).
        uint32_t override_cnt = 0U;
        if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
            instance->raw_data[6] = (uint8_t)(override_cnt & 0xFFU);
        } else {
            hundai_advance_counter(instance->raw_data);
        }
        instance->generic.btn = hundai_button(instance->raw_data);
        instance->generic.cnt = instance->raw_data[6];
    }

    hundai_decode_fields(&instance->generic, instance->raw_data);

    uint32_t repeat = HUNDAI_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? HUNDAI_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(HUNDAI_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!hundai_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

#include "mercedes.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <furi.h>
#include <string.h>

// ============================================================================
// Mercedes / Maybach SubGHz keyfob protocol (decoder + encoder)
//
// Reverse-engineered from the PANDORA_MAX firmware, TX engine handler 0xf144
// (RF bit-banger at 0x8ebc). Maybach uses the same Mercedes keyfob system, so
// this is a single protocol.
//
// Wire (verified against the RE bit-banger at 0x8ebc):
//   - modulation: OOK/ASK (AM650 preset), bands 315 / 433 / 868 MHz.
//   - Manchester, Te = 500 us (te_delta ~125).
//       * movw r0,#3700  -> 3700 us lead gap.
//       * loop cmp r4,#55 -> 55 preamble cycles of {HIGH 500 / LOW 500}.
//       * sync boundary   -> an extra LOW 500 then HIGH 500 (the 8f06..8f16 tail).
//       * loop cmp r4,#112 -> 112 Manchester data bits, MSB-first over 14 bytes.
//   - Per-bit polarity (spec, matches the RE branch that swaps 0x80d0/0x8160):
//       bit1 = LOW 500 -> HIGH 500   (rising edge mid-bit)
//       bit0 = HIGH 500 -> LOW 500   (falling edge mid-bit)
//
// 14-byte payload layout (shared Pandora struct convention):
//   b0..b3  = UID / serial
//   b4      = button / command
//   b5..b6  = 16-bit rolling counter (b5 = high, b6 = low)
//   b7      = checksum = sum(b0..b6) & 0xFF   (display only, never rejected)
//   b8..b13 = additional rolling / auth bytes (opaque; replayed as-is)
//
// 112 bits do not fit in generic.data (uint64), so the 14 raw bytes are stored
// in raw_data[] and serialized as a "Data" hex field (mirrors fiat_v2.c "Raw").
//
// Rolling code with no known key -> emulation is a byte-identical REPLAY of the
// captured 14 bytes (optionally re-stamping only b4 for the D-pad).
// ============================================================================

#define MERCEDES_TE_SHORT      500U
#define MERCEDES_TE_DELTA      125U
#define MERCEDES_LEAD_GAP_US   3700U
#define MERCEDES_PREAMBLE_PAIRS 55U
#define MERCEDES_WIRE_BYTES    14U
#define MERCEDES_WIRE_BITS     112U

// Boundary between a preamble/half-bit cell and a real gap.
#define MERCEDES_BOUNDARY_MIN_US 1500U

// Encoder capacity: lead gap + 55 preamble pairs + sync (2) + 112 bits * 2 cells
// + trailing gap, generous margin.
#define MERCEDES_UPLOAD_CAPACITY \
    (1U + (MERCEDES_PREAMBLE_PAIRS * 2U) + 4U + (MERCEDES_WIRE_BITS * 2U) + 2U)

#define MERCEDES_DEFAULT_REPEAT 4U

#define MERCEDES_DATA_FIELD "Data"

static const SubGhzBlockConst subghz_protocol_mercedes_const = {
    .te_short = MERCEDES_TE_SHORT,
    .te_long = MERCEDES_TE_SHORT * 2U,
    .te_delta = MERCEDES_TE_DELTA,
    .min_count_bit_for_found = MERCEDES_WIRE_BITS,
};

typedef enum {
    MercedesDecoderStepReset = 0,
    MercedesDecoderStepPreamble,
    MercedesDecoderStepData,
} MercedesDecoderStep;

struct SubGhzProtocolDecoderMercedes {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;

    ManchesterState manchester_state;
    uint8_t bytes[MERCEDES_WIRE_BYTES];
    uint16_t bit_count;

    uint8_t raw_data[MERCEDES_WIRE_BYTES];
    uint8_t last_raw_data[MERCEDES_WIRE_BYTES];
    bool last_raw_valid;
};

struct SubGhzProtocolEncoderMercedes {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[MERCEDES_WIRE_BYTES];
};

const SubGhzProtocolDecoder subghz_protocol_mercedes_decoder = {
    .alloc = subghz_protocol_decoder_mercedes_alloc,
    .free = subghz_protocol_decoder_mercedes_free,
    .feed = subghz_protocol_decoder_mercedes_feed,
    .reset = subghz_protocol_decoder_mercedes_reset,
    .get_hash_data = subghz_protocol_decoder_mercedes_get_hash_data,
    .serialize = subghz_protocol_decoder_mercedes_serialize,
    .deserialize = subghz_protocol_decoder_mercedes_deserialize,
    .get_string = subghz_protocol_decoder_mercedes_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_mercedes_encoder = {
    .alloc = subghz_protocol_encoder_mercedes_alloc,
    .free = subghz_protocol_encoder_mercedes_free,
    .deserialize = subghz_protocol_encoder_mercedes_deserialize,
    .stop = subghz_protocol_encoder_mercedes_stop,
    .yield = subghz_protocol_encoder_mercedes_yield,
};

const SubGhzProtocol subghz_protocol_mercedes = {
    .name = MERCEDES_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_mercedes_decoder,
    .encoder = &subghz_protocol_mercedes_encoder,
};

// ---------------------------------------------------------------------------
// Shared field helpers
// ---------------------------------------------------------------------------

static uint8_t mercedes_checksum(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < 7U; i++) {
        sum += raw[i];
    }
    return (uint8_t)(sum & 0xFFU);
}

static uint32_t mercedes_uid(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    return ((uint32_t)raw[0] << 24U) | ((uint32_t)raw[1] << 16U) | ((uint32_t)raw[2] << 8U) |
           raw[3];
}

static uint16_t mercedes_counter(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    return (uint16_t)(((uint16_t)raw[5] << 8U) | raw[6]);
}

static void mercedes_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    generic->serial = mercedes_uid(raw);
    generic->btn = raw[4];
    generic->cnt = mercedes_counter(raw);
    generic->data_count_bit = MERCEDES_WIRE_BITS;
    // generic.data cannot hold 112 bits; keep the most useful 64 bits (uid|cnt)
    // for hashing/display symmetry. The authoritative payload lives in raw_data.
    generic->data = ((uint64_t)generic->serial << 16U) | generic->cnt;
}

static bool mercedes_frame_valid(const uint8_t raw[MERCEDES_WIRE_BYTES]) {
    // The checksum is display-only (may be opaque on real captures), so validity
    // is intentionally loose: reject only the empty / all-ones degenerate frames.
    const uint32_t uid = mercedes_uid(raw);
    return uid != 0U && uid != UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Decoder: Manchester demod via the firmware manchester_decoder state machine
// (same approach as kia_v2.c). The state machine self-synchronizes and natively
// handles both short (Te) and long (2*Te) pulses, so it copes with the sync HIGH
// cell merging into an adjacent data cell. Verified by simulation to recover the
// exact 112 encoded bits for every input (guaranteed round-trip).
//
// Pulse -> event mapping (matches the chosen wire polarity, seed = Mid1):
//   short: level ? ShortHigh : ShortLow
//   long : level ? LongHigh  : LongLow
// yielding: bit1 = LOW 500 -> HIGH 500, bit0 = HIGH 500 -> LOW 500.
// ---------------------------------------------------------------------------

static bool mercedes_duration_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, MERCEDES_TE_SHORT) < MERCEDES_TE_DELTA;
}

static bool mercedes_duration_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, MERCEDES_TE_SHORT * 2U) < MERCEDES_TE_DELTA;
}

static void mercedes_data_reset(SubGhzProtocolDecoderMercedes* instance) {
    instance->bit_count = 0U;
    instance->manchester_state = ManchesterStateMid1;
    memset(instance->bytes, 0, sizeof(instance->bytes));
}

static void mercedes_add_bit(SubGhzProtocolDecoderMercedes* instance, bool bit) {
    if(instance->bit_count >= MERCEDES_WIRE_BITS) return;
    if(bit) {
        instance->bytes[instance->bit_count >> 3U] |=
            (uint8_t)(1U << (7U - (instance->bit_count & 7U)));
    }
    instance->bit_count++;
}

static void mercedes_report(SubGhzProtocolDecoderMercedes* instance) {
    if(instance->bit_count < MERCEDES_WIRE_BITS) return;

    if(!mercedes_frame_valid(instance->bytes)) return;

    // Accept repeated identical frames silently (fob repeats each press).
    if(instance->last_raw_valid &&
       memcmp(instance->last_raw_data, instance->bytes, MERCEDES_WIRE_BYTES) == 0) {
        return;
    }

    memcpy(instance->raw_data, instance->bytes, MERCEDES_WIRE_BYTES);
    memcpy(instance->last_raw_data, instance->bytes, MERCEDES_WIRE_BYTES);
    instance->last_raw_valid = true;
    mercedes_decode_fields(&instance->generic, instance->raw_data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

static void mercedes_feed_manchester(
    SubGhzProtocolDecoderMercedes* instance,
    bool level,
    bool is_long) {
    ManchesterEvent event;
    if(is_long) {
        event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
    } else {
        event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
    }

    bool data_bit;
    if(manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        mercedes_add_bit(instance, data_bit);
        if(instance->bit_count == MERCEDES_WIRE_BITS) {
            mercedes_report(instance);
        }
    }
}

void* subghz_protocol_decoder_mercedes_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMercedes* instance = calloc(1, sizeof(SubGhzProtocolDecoderMercedes));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mercedes;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_mercedes_reset(instance);
    return instance;
}

void subghz_protocol_decoder_mercedes_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;
    free(instance);
}

void subghz_protocol_decoder_mercedes_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;
    instance->decoder.parser_step = MercedesDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->preamble_count = 0U;
    instance->last_raw_valid = false;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    memset(instance->last_raw_data, 0, sizeof(instance->last_raw_data));
    mercedes_data_reset(instance);
}

void subghz_protocol_decoder_mercedes_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;

    switch(instance->decoder.parser_step) {
    case MercedesDecoderStepReset:
        // Wait for the 3700 us lead gap (a long LOW), then start counting the
        // 55x {HIGH 500 / LOW 500} preamble.
        if(!level && duration >= MERCEDES_BOUNDARY_MIN_US) {
            instance->decoder.parser_step = MercedesDecoderStepPreamble;
            instance->preamble_count = 0U;
        }
        break;

    case MercedesDecoderStepPreamble:
        // The preamble is 55 cycles of { HIGH 500 / LOW 500 } = alternating
        // short cells. The sync boundary is an EXTRA LOW 500 that merges with the
        // trailing preamble LOW into a single 1000 us (te_long) LOW, immediately
        // followed by a HIGH 500 sync cell and then the 112 data bits.
        if(mercedes_duration_is_short(duration)) {
            instance->preamble_count++;
        } else if(
            !level && mercedes_duration_is_long(duration) &&
            instance->preamble_count >= (MERCEDES_PREAMBLE_PAIRS * 2U - 6U)) {
            // te_long LOW after a solid preamble = the sync double-low. Start the
            // Manchester state machine; the following sync HIGH cell is absorbed
            // by the self-synchronizing decoder before the 112 data bits.
            instance->decoder.parser_step = MercedesDecoderStepData;
            mercedes_data_reset(instance);
        } else if(!level && duration >= MERCEDES_BOUNDARY_MIN_US) {
            // Another lead gap: restart the preamble search.
            instance->preamble_count = 0U;
        } else {
            instance->decoder.parser_step = MercedesDecoderStepReset;
            instance->preamble_count = 0U;
        }
        break;

    case MercedesDecoderStepData:
        if(mercedes_duration_is_short(duration)) {
            mercedes_feed_manchester(instance, level, false);
        } else if(mercedes_duration_is_long(duration)) {
            mercedes_feed_manchester(instance, level, true);
        } else {
            // End of frame / gap: try to report whatever we have, then reset to
            // catch the fob's repeat.
            mercedes_report(instance);
            mercedes_data_reset(instance);
            instance->decoder.parser_step = MercedesDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_mercedes_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0U; i < MERCEDES_WIRE_BYTES; i++) {
        hash ^= instance->raw_data[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_mercedes_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, MERCEDES_DATA_FIELD, instance->raw_data, MERCEDES_WIRE_BYTES)) {
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
    subghz_protocol_decoder_mercedes_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_mercedes_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, MERCEDES_DATA_FIELD, instance->raw_data, MERCEDES_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!mercedes_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    mercedes_decode_fields(&instance->generic, instance->raw_data);
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_mercedes_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderMercedes* instance = context;

    const uint8_t cs = instance->raw_data[7];
    const uint8_t cs_calc = mercedes_checksum(instance->raw_data);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%08lX Btn:%02X\r\n"
        "Cnt:%04lX CS:%02X[%s]\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X\r\n"
        "     %02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        MERCEDES_WIRE_BITS,
        (unsigned long)instance->generic.serial,
        instance->generic.btn,
        (unsigned long)instance->generic.cnt,
        cs,
        (cs == cs_calc) ? "OK" : "opaque",
        instance->raw_data[0],
        instance->raw_data[1],
        instance->raw_data[2],
        instance->raw_data[3],
        instance->raw_data[4],
        instance->raw_data[5],
        instance->raw_data[6],
        instance->raw_data[7],
        instance->raw_data[8],
        instance->raw_data[9],
        instance->raw_data[10],
        instance->raw_data[11],
        instance->raw_data[12],
        instance->raw_data[13]);
}

// ---------------------------------------------------------------------------
// Encoder: rebuild the exact TX wire.
//   3700 us lead gap
//   + 55x { HIGH 500 / LOW 500 } preamble
//   + sync boundary (LOW 500 then HIGH 500)
//   + 112 Manchester bits of the 14 raw bytes (MSB-first)
//       bit1 -> LOW 500 , HIGH 500
//       bit0 -> HIGH 500 , LOW 500
//   + trailing gap
// Replay-based (rolling code / no key). D-pad only re-stamps b4.
// ---------------------------------------------------------------------------

static bool mercedes_encoder_build_upload(SubGhzProtocolEncoderMercedes* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    size_t index = 0U;
    const uint32_t te = MERCEDES_TE_SHORT;

    // Lead gap (LOW).
    upload[index++] = level_duration_make(false, MERCEDES_LEAD_GAP_US);

    // 55 preamble cycles { HIGH 500 / LOW 500 }.
    for(uint32_t i = 0U; i < MERCEDES_PREAMBLE_PAIRS; i++) {
        upload[index++] = level_duration_make(true, te);
        upload[index++] = level_duration_make(false, te);
    }

    // Sync boundary: extra LOW 500 then HIGH 500.
    upload[index++] = level_duration_make(false, te);
    upload[index++] = level_duration_make(true, te);

    // 112 Manchester data bits, MSB-first.
    for(uint8_t bit_index = 0U; bit_index < MERCEDES_WIRE_BITS; bit_index++) {
        const bool bit =
            ((instance->raw_data[bit_index >> 3U] >> (7U - (bit_index & 7U))) & 1U) != 0U;
        if(bit) {
            // bit1 = LOW then HIGH
            upload[index++] = level_duration_make(false, te);
            upload[index++] = level_duration_make(true, te);
        } else {
            // bit0 = HIGH then LOW
            upload[index++] = level_duration_make(true, te);
            upload[index++] = level_duration_make(false, te);
        }
    }

    // Trailing gap so consecutive repeats are separable.
    upload[index++] = level_duration_make(false, MERCEDES_LEAD_GAP_US);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
    return true;
}

void* subghz_protocol_encoder_mercedes_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMercedes* instance = calloc(1, sizeof(SubGhzProtocolEncoderMercedes));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mercedes;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = MERCEDES_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_mercedes_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMercedes* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_mercedes_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMercedes* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

LevelDuration subghz_protocol_encoder_mercedes_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMercedes* instance = context;

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
    subghz_protocol_encoder_mercedes_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderMercedes* instance = context;

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
       bit_count != MERCEDES_WIRE_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, MERCEDES_DATA_FIELD, instance->raw_data, MERCEDES_WIRE_BYTES) ||
       !mercedes_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = mercedes_uid(instance->raw_data);
    instance->generic.btn = instance->raw_data[4];
    instance->generic.cnt = mercedes_counter(instance->raw_data);

    // [PROTOPIRATE_PORT] custom_btn support (replay-safe).
    // Mercedes button codes are not precisely known, so only OK is guaranteed to
    // replay the captured b4 byte-identically. The D-pad re-stamps b4 with a
    // best-effort code and refreshes the checksum b7 so the emitted frame still
    // round-trips through this decoder. set_max(4).
    const uint8_t original_btn = instance->raw_data[4];
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(original_btn);
    }
    subghz_custom_btn_set_max(4);
    const uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t new_btn = original_btn;
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:    new_btn = 0x01U; break; // best-effort: Lock
    case SUBGHZ_CUSTOM_BTN_DOWN:  new_btn = 0x02U; break; // best-effort: Unlock
    case SUBGHZ_CUSTOM_BTN_LEFT:  new_btn = 0x03U; break; // best-effort: Trunk
    case SUBGHZ_CUSTOM_BTN_RIGHT: new_btn = 0x04U; break; // best-effort: Aux
    case SUBGHZ_CUSTOM_BTN_OK:
    default:                      new_btn = original_btn; break; // exact replay
    }

    if(new_btn != original_btn) {
        instance->raw_data[4] = new_btn;
        // Refresh the sum-type checksum so the modified frame stays consistent.
        instance->raw_data[7] = mercedes_checksum(instance->raw_data);
        instance->generic.btn = new_btn;
    }

    instance->generic.data =
        ((uint64_t)instance->generic.serial << 16U) | instance->generic.cnt;
    instance->generic.data_count_bit = MERCEDES_WIRE_BITS;

    uint32_t repeat = MERCEDES_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? MERCEDES_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(MERCEDES_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!mercedes_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

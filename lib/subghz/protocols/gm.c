#include "gm.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <furi.h>
#include <string.h>

// ============================================================================
// General Motors (Chevrolet / GMC / Buick / Cadillac) car-remote RKE.
//
// Frame format reverse-engineered by the rtl_433 project (clean-room, public):
//   github.com/merbanan/rtl_433  src/devices/gm_car_remote.c  (Ethan Halsall).
//   VERIFIED source; the constants below are copied from that decoder.
//
// Wire:
//   - modulation : OOK / PPM (pulse-position). Each data bit is a fixed-width
//                  HIGH pulse followed by a LOW gap whose length encodes the bit:
//                      gap ~= 300 us  -> bit 0   (short_width)
//                      gap ~= 500 us  -> bit 1   (long_width)
//   - frequency  : 315 MHz band (~314.9 MHz). AM/OOK preset.
//   - a long OOK "wake-up" preamble precedes the payload and MAY BE TRUNCATED,
//     so the decoder anchors from the END of the burst and keeps only the last
//     112 bits (14 bytes). byte[0] of that window must be 0xFF (wake-up sanity).
//   - reset/guard: a LOW gap >= ~20 ms ends the frame.
//
// 14-byte payload layout (rtl_433 gm_car_remote.c):
//   b0        = 0xFF wake-up sanity byte
//   b1        = 8-bit unknown (possibly high ID byte)
//   b2        = high nibble: 4-bit checksum of button ; low nibble: button code
//   b3..b6    = 32-bit ID / serial
//   b7..b9    = 24-bit sequence counter (UNENCRYPTED, plaintext rolling counter)
//   b10..b12  = 24-bit encrypted field (cipher NOT public)
//   b13       = 8-bit checksum of the whole payload
//
// Integrity (both ADDITIVE, mod-256 / mod-16 - NOT XOR, NOT a CRC):
//   button check : (nibble_sum(b2))       & 0x0F == 0
//   full check   : (byte_sum(b1..b13))    & 0xFF == 0
//
// Because the 24-bit rolling field is encrypted with an undisclosed cipher, this
// protocol is DECODE + REPLAY only: we display ID / sequence / button and can
// re-emit the captured 14 bytes byte-identically. We cannot forge a fresh
// rolling code (no key), so emulation replays the captured frame.
// ============================================================================

#define GM_TE_SHORT       300U // short gap -> bit 0
#define GM_TE_LONG        500U // long  gap -> bit 1
#define GM_TE_DELTA       120U
#define GM_PULSE_MIN_US   100U // fixed HIGH pulse lower bound
#define GM_PULSE_MAX_US   1000U // fixed HIGH pulse upper bound
#define GM_RESET_US       12000U // end-of-frame LOW gap (< rtl_433 reset 20 ms)

#define GM_WIRE_BYTES     14U
#define GM_WIRE_BITS      112U

#define GM_DEFAULT_REPEAT 5U
#define GM_DATA_FIELD     "Data"

// Encoder capacity: a short lead, 112 bits * 2 cells (pulse + gap), trailing gap.
#define GM_UPLOAD_CAPACITY (2U + (GM_WIRE_BITS * 2U) + 2U)

static const SubGhzBlockConst subghz_protocol_gm_const = {
    .te_short = GM_TE_SHORT,
    .te_long = GM_TE_LONG,
    .te_delta = GM_TE_DELTA,
    .min_count_bit_for_found = GM_WIRE_BITS,
};

typedef enum {
    GMDecoderStepReset = 0,
    GMDecoderStepData,
} GMDecoderStep;

struct SubGhzProtocolDecoderGM {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    // Rolling bit window: we keep the LAST GM_WIRE_BITS bits seen, MSB-first,
    // packed into a 14-byte ring so a truncated wake-up preamble is tolerated.
    uint8_t bits[GM_WIRE_BYTES];
    uint16_t bit_count; // total bits collected this burst (saturates)

    uint8_t raw_data[GM_WIRE_BYTES];
    uint8_t last_raw_data[GM_WIRE_BYTES];
    bool last_raw_valid;
};

struct SubGhzProtocolEncoderGM {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[GM_WIRE_BYTES];
};

const SubGhzProtocolDecoder subghz_protocol_gm_decoder = {
    .alloc = subghz_protocol_decoder_gm_alloc,
    .free = subghz_protocol_decoder_gm_free,
    .feed = subghz_protocol_decoder_gm_feed,
    .reset = subghz_protocol_decoder_gm_reset,
    .get_hash_data = subghz_protocol_decoder_gm_get_hash_data,
    .serialize = subghz_protocol_decoder_gm_serialize,
    .deserialize = subghz_protocol_decoder_gm_deserialize,
    .get_string = subghz_protocol_decoder_gm_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_gm_encoder = {
    .alloc = subghz_protocol_encoder_gm_alloc,
    .free = subghz_protocol_encoder_gm_free,
    .deserialize = subghz_protocol_encoder_gm_deserialize,
    .stop = subghz_protocol_encoder_gm_stop,
    .yield = subghz_protocol_encoder_gm_yield,
};

const SubGhzProtocol subghz_protocol_gm = {
    .name = GM_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_gm_decoder,
    .encoder = &subghz_protocol_gm_encoder,
};

// ---------------------------------------------------------------------------
// Field helpers (match rtl_433 gm_car_remote.c layout)
// ---------------------------------------------------------------------------

// NOTE: rtl_433's add_nibbles/add_bytes return the FULL (untruncated) sum. The
// validity test then checks `sum == 0` (degenerate/empty frame) OR
// `(sum & 0xFF) != 0` (checksum mismatch). A VALID frame has a non-zero full
// sum whose low 8 bits (low 4 bits for the nibble sum) are zero. We therefore
// keep the running sum as a wide integer and never truncate before the test.
static uint32_t gm_add_nibbles(const uint8_t* b, size_t n) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < n; i++) {
        sum += (uint32_t)(b[i] >> 4U) + (uint32_t)(b[i] & 0x0FU);
    }
    return sum;
}

static uint32_t gm_add_bytes(const uint8_t* b, size_t n) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < n; i++) {
        sum += b[i];
    }
    return sum;
}

static bool gm_frame_valid(const uint8_t raw[GM_WIRE_BYTES]) {
    // wake-up sanity byte
    if(raw[0] != 0xFFU) return false;

    // button nibble checksum: nibble_sum(b2) non-zero and low nibble == 0.
    const uint32_t btn_ck = gm_add_nibbles(&raw[2], 1U);
    if(btn_ck == 0U || (btn_ck & 0x0FU) != 0U) return false;

    // full payload checksum: byte_sum(b1..b13) non-zero and low byte == 0.
    const uint32_t full_ck = gm_add_bytes(&raw[1], 13U);
    if(full_ck == 0U || (full_ck & 0xFFU) != 0U) return false;

    return true;
}

static uint32_t gm_id(const uint8_t raw[GM_WIRE_BYTES]) {
    return ((uint32_t)raw[3] << 24U) | ((uint32_t)raw[4] << 16U) | ((uint32_t)raw[5] << 8U) |
           raw[6];
}

static uint32_t gm_sequence(const uint8_t raw[GM_WIRE_BYTES]) {
    return ((uint32_t)raw[7] << 16U) | ((uint32_t)raw[8] << 8U) | raw[9];
}

static uint8_t gm_button(const uint8_t raw[GM_WIRE_BYTES]) {
    return (uint8_t)(raw[2] & 0x07U);
}

static void gm_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[GM_WIRE_BYTES]) {
    generic->serial = gm_id(raw);
    generic->btn = gm_button(raw);
    generic->cnt = (uint16_t)(gm_sequence(raw) & 0xFFFFU);
    generic->data_count_bit = GM_WIRE_BITS;
    // 112 bits do not fit in generic.data (uint64); keep id|seq16 for hashing.
    generic->data = ((uint64_t)generic->serial << 16U) | generic->cnt;
}

// ---------------------------------------------------------------------------
// Decoder: PPM demodulation with a rolling last-112-bit window.
// ---------------------------------------------------------------------------

static void gm_data_reset(SubGhzProtocolDecoderGM* instance) {
    instance->bit_count = 0U;
    memset(instance->bits, 0, sizeof(instance->bits));
}

// Push one bit into the MSB-first ring: shift the whole 14-byte buffer left by
// one bit and OR the new bit into the LSB. This keeps exactly the last 112 bits.
static void gm_push_bit(SubGhzProtocolDecoderGM* instance, bool bit) {
    uint8_t carry = bit ? 1U : 0U;
    for(int i = (int)GM_WIRE_BYTES - 1; i >= 0; i--) {
        const uint8_t new_carry = (uint8_t)((instance->bits[i] >> 7U) & 1U);
        instance->bits[i] = (uint8_t)((instance->bits[i] << 1U) | carry);
        carry = new_carry;
    }
    if(instance->bit_count < 0xFFFFU) instance->bit_count++;
}

static void gm_report(SubGhzProtocolDecoderGM* instance) {
    if(instance->bit_count < GM_WIRE_BITS) return;
    if(!gm_frame_valid(instance->bits)) return;

    // De-dup identical repeats (fob repeats each press many times).
    if(instance->last_raw_valid &&
       memcmp(instance->last_raw_data, instance->bits, GM_WIRE_BYTES) == 0) {
        return;
    }

    memcpy(instance->raw_data, instance->bits, GM_WIRE_BYTES);
    memcpy(instance->last_raw_data, instance->bits, GM_WIRE_BYTES);
    instance->last_raw_valid = true;
    gm_decode_fields(&instance->generic, instance->raw_data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void subghz_protocol_decoder_gm_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    switch(instance->decoder.parser_step) {
    case GMDecoderStepReset:
        // Any plausible fixed-width HIGH pulse starts data collection. The PPM
        // bit value is carried by the FOLLOWING low gap, so we just wait for the
        // first pulse and then read gaps.
        if(level && duration >= GM_PULSE_MIN_US && duration <= GM_PULSE_MAX_US) {
            instance->decoder.parser_step = GMDecoderStepData;
            gm_data_reset(instance);
        }
        break;

    case GMDecoderStepData:
        if(level) {
            // A HIGH pulse: must be a fixed-width bit pulse. If it is grossly out
            // of range, treat as noise and restart.
            if(duration < GM_PULSE_MIN_US || duration > GM_PULSE_MAX_US) {
                gm_report(instance);
                instance->decoder.parser_step = GMDecoderStepReset;
                gm_data_reset(instance);
            }
            // otherwise: ignore the pulse; the next LOW gap yields the bit.
        } else {
            // A LOW gap encodes the bit (short=0, long=1) unless it is the
            // end-of-frame guard.
            if(duration >= GM_RESET_US) {
                gm_report(instance);
                instance->decoder.parser_step = GMDecoderStepReset;
                gm_data_reset(instance);
            } else if(DURATION_DIFF(duration, GM_TE_SHORT) < GM_TE_DELTA) {
                gm_push_bit(instance, false);
                if(instance->bit_count >= GM_WIRE_BITS) gm_report(instance);
            } else if(DURATION_DIFF(duration, GM_TE_LONG) < GM_TE_DELTA) {
                gm_push_bit(instance, true);
                if(instance->bit_count >= GM_WIRE_BITS) gm_report(instance);
            } else {
                // Unexpected gap: end this burst, try to report, then restart.
                gm_report(instance);
                instance->decoder.parser_step = GMDecoderStepReset;
                gm_data_reset(instance);
            }
        }
        break;
    }
}

void* subghz_protocol_decoder_gm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderGM* instance = calloc(1, sizeof(SubGhzProtocolDecoderGM));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_gm;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_gm_reset(instance);
    return instance;
}

void subghz_protocol_decoder_gm_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;
    free(instance);
}

void subghz_protocol_decoder_gm_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;
    instance->decoder.parser_step = GMDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->last_raw_valid = false;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    memset(instance->last_raw_data, 0, sizeof(instance->last_raw_data));
    gm_data_reset(instance);
}

uint8_t subghz_protocol_decoder_gm_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0U; i < GM_WIRE_BYTES; i++) {
        hash ^= instance->raw_data[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_gm_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, GM_DATA_FIELD, instance->raw_data, GM_WIRE_BYTES)) {
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
    subghz_protocol_decoder_gm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_gm_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, GM_DATA_FIELD, instance->raw_data, GM_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!gm_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    gm_decode_fields(&instance->generic, instance->raw_data);
    return SubGhzProtocolStatusOk;
}

static const char* gm_button_name(uint8_t button) {
    switch(button) {
    case 0x1U: return "Unlock";
    case 0x2U: return "Lock";
    case 0x3U: return "Trunk";
    case 0x4U: return "Panic";
    default:   return "?";
    }
}

void subghz_protocol_decoder_gm_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    const uint8_t full_ck = instance->raw_data[13];
    const bool full_ck_ok = ((gm_add_bytes(&instance->raw_data[1], 13U) & 0xFFU) == 0U);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%08lX Btn:%X[%s]\r\n"
        "Seq:%06lX CS:%02X[%s]\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X\r\n"
        "     %02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        GM_WIRE_BITS,
        (unsigned long)instance->generic.serial,
        (unsigned)gm_button(instance->raw_data),
        gm_button_name(gm_button(instance->raw_data)),
        (unsigned long)gm_sequence(instance->raw_data),
        full_ck,
        full_ck_ok ? "OK" : "ERR",
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
// Encoder: rebuild the exact TX wire (PPM), REPLAY only.
//   For each of the 112 payload bits (MSB-first over the 14 bytes):
//       HIGH pulse (te_short) then LOW gap: bit0 -> 300 us, bit1 -> 500 us.
//   + a trailing guard LOW so consecutive repeats are separable.
// The 24-bit rolling field is encrypted (no key), so we cannot forge; OK
// replays the captured 14 bytes byte-identically.
// ---------------------------------------------------------------------------

static bool gm_encoder_build_upload(SubGhzProtocolEncoderGM* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    size_t index = 0U;

    for(uint8_t bit_index = 0U; bit_index < GM_WIRE_BITS; bit_index++) {
        const bool bit =
            ((instance->raw_data[bit_index >> 3U] >> (7U - (bit_index & 7U))) & 1U) != 0U;
        // Fixed-width HIGH pulse.
        upload[index++] = level_duration_make(true, GM_TE_SHORT);
        // LOW gap encodes the bit.
        upload[index++] = level_duration_make(false, bit ? GM_TE_LONG : GM_TE_SHORT);
    }

    // Trailing guard gap (end-of-frame), shorter than the reset so a receiver
    // still segments repeats but our own decoder finalizes on it.
    upload[index++] = level_duration_make(false, GM_RESET_US);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
    return true;
}

void* subghz_protocol_encoder_gm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderGM* instance = calloc(1, sizeof(SubGhzProtocolEncoderGM));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_gm;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = GM_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_gm_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderGM* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_gm_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderGM* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

LevelDuration subghz_protocol_encoder_gm_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderGM* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0 ||
       instance->encoder.size_upload == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0U;
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_gm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderGM* instance = context;

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
       bit_count != GM_WIRE_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, GM_DATA_FIELD, instance->raw_data, GM_WIRE_BYTES) ||
       !gm_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = gm_id(instance->raw_data);
    instance->generic.btn = gm_button(instance->raw_data);
    instance->generic.cnt = (uint16_t)(gm_sequence(instance->raw_data) & 0xFFFFU);

    // [PROTOPIRATE_PORT] REPLAY only: the rolling field is encrypted (no key), so
    // we cannot re-encode a new button/counter into a valid frame. Disable the
    // directional D-pad (set_max 0 -> is_allowed()==false) so the transmitter UI
    // offers only OK, which replays the captured frame byte-identically.
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->raw_data[2] & 0x07U);
    }
    subghz_custom_btn_set_max(0);

    instance->generic.data =
        ((uint64_t)instance->generic.serial << 16U) | instance->generic.cnt;
    instance->generic.data_count_bit = GM_WIRE_BITS;

    uint32_t repeat = GM_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? GM_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(GM_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!gm_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

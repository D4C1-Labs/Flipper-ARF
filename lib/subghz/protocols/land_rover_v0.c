#include "land_rover_v0.h"

// [PROTOPIRATE_PORT] custom_btn support
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

#define TAG "LandRoverV0"

static const SubGhzBlockConst subghz_protocol_land_rover_v0_const = {
    .te_short                = 250,
    .te_long                 = 500,
    .te_delta                = 100,
    .min_count_bit_for_found = 81,
};

#define LAND_ROVER_V0_PREAMBLE_PAIRS     319U
#define LAND_ROVER_V0_MIN_PREAMBLE_PAIRS 64U
#define LAND_ROVER_V0_SYNC_US            750U
#define LAND_ROVER_V0_SYNC_DELTA_US      120U
#define LAND_ROVER_V0_UPLOAD_CAPACITY    1024U
#define LAND_ROVER_V0_GAP_US             50000U

#define LAND_ROVER_V0_BTN_UNKNOWN 0x00U
#define LAND_ROVER_V0_BTN_LOCK    0x02U
#define LAND_ROVER_V0_BTN_UNLOCK  0x04U

#define LAND_ROVER_V0_SIG_UNLOCK 0xA285E3UL
#define LAND_ROVER_V0_SIG_LOCK   0xC20363UL

/* Extra FlipperFormat field names specific to this protocol */
#define LAND_ROVER_V0_FF_BTNSIG    "BtnSig"
#define LAND_ROVER_V0_FF_CHECK     "Check"
#define LAND_ROVER_V0_FF_TAIL      "Tail"
#define LAND_ROVER_V0_FF_EXTRA_BIT "ExtraBit"

/* FlipperFormat field name aliases (replacing the external pp library's FF_* macros) */
#define LR_FF_KEY    "Key"
#define LR_FF_SERIAL "Serial"
#define LR_FF_BTN    "Btn"
#define LR_FF_CNT    "Cnt"

/* ── Decoder struct ──────────────────────────────────────────────────────── */
typedef struct SubGhzProtocolDecoderLandRoverV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder        decoder;
    SubGhzBlockGeneric        generic;

    uint16_t preamble_count;
    uint8_t  raw[10];
    uint8_t  bit_count;
    bool     extra_bit;
    bool     previous_bit;
    bool     boundary_pad_skipped;
    bool     pending_short;

    uint64_t key;
    uint16_t tail;
    uint32_t command_signature;
    uint32_t serial;
    uint32_t count;
    uint8_t  button;
    uint8_t  check;
    bool     check_ok;
    bool     tail_ok;
} SubGhzProtocolDecoderLandRoverV0;

/* ── Encoder struct ──────────────────────────────────────────────────────── */
typedef struct SubGhzProtocolEncoderLandRoverV0 {
    SubGhzProtocolEncoderBase  base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric         generic;

    uint64_t key;
    uint16_t tail;
    uint32_t command_signature;
    uint32_t serial;
    uint32_t count;
    uint8_t  button;
    uint8_t  check;
} SubGhzProtocolEncoderLandRoverV0;

/* ── Decoder state machine steps ─────────────────────────────────────────── */
typedef enum {
    LandRoverV0DecoderStepReset = 0,
    LandRoverV0DecoderStepPreambleLow,
    LandRoverV0DecoderStepPreambleHigh,
    LandRoverV0DecoderStepSyncLow,
    LandRoverV0DecoderStepData,
} LandRoverV0DecoderStep;

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal helpers replacing pp_* functions from the external app library
 * ═════════════════════════════════════════════════════════════════════════*/

/** Write a uint64_t into 8 bytes in big-endian order. */
static inline void lr_u64_to_bytes_be(uint64_t val, uint8_t out[8]) {
    for(int i = 7; i >= 0; i--) {
        out[i] = (uint8_t)(val & 0xFFU);
        val >>= 8;
    }
}

/** Read 8 big-endian bytes and return a uint64_t. */
static inline uint64_t lr_bytes_to_u64_be(const uint8_t in[8]) {
    uint64_t val = 0;
    for(int i = 0; i < 8; i++) {
        val = (val << 8) | in[i];
    }
    return val;
}

/** Returns true when duration matches te_short within te_delta. */
static inline bool lr_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_land_rover_v0_const.te_short) <
           subghz_protocol_land_rover_v0_const.te_delta;
}

/** Returns true when duration matches te_long within te_delta. */
static inline bool lr_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_land_rover_v0_const.te_long) <
           subghz_protocol_land_rover_v0_const.te_delta;
}

/** Insert-or-update a single uint32 field in a FlipperFormat file. */
static void lr_ff_write_u32(FlipperFormat* ff, const char* key, uint32_t val) {
    flipper_format_insert_or_update_uint32(ff, key, &val, 1);
}

/** Read a single uint32 field from a FlipperFormat file. */
static bool lr_ff_read_u32(FlipperFormat* ff, const char* key, uint32_t* out) {
    return flipper_format_read_uint32(ff, key, out, 1);
}

/**
 * Verify that the "Protocol" field in the FlipperFormat file matches the
 * expected protocol name.  Returns SubGhzProtocolStatusOk on match.
 */
static SubGhzProtocolStatus lr_verify_protocol_name(
    FlipperFormat* ff,
    const char*    expected_name) {
    FuriString* name = furi_string_alloc();
    bool        ok   = false;
    if(flipper_format_read_string(ff, "Protocol", name)) {
        ok = furi_string_equal_str(name, expected_name);
    }
    furi_string_free(name);
    return ok ? SubGhzProtocolStatusOk : SubGhzProtocolStatusErrorProtocolNotFound;
}

/**
 * Read the "Repeat" field from a FlipperFormat file.
 * Falls back to default_val when the field is absent.
 */
static uint16_t lr_encoder_read_repeat(FlipperFormat* ff, uint16_t default_val) {
    uint32_t repeat = default_val;
    flipper_format_rewind(ff);
    if(!flipper_format_read_uint32(ff, "Repeat", &repeat, 1)) {
        repeat = default_val;
    }
    return (uint16_t)repeat;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Forward declarations for internal (static) helpers
 * ═════════════════════════════════════════════════════════════════════════*/
static uint8_t     land_rover_v0_button_from_signature(uint32_t signature);
static uint8_t     land_rover_v0_calculate_check(uint32_t count);
static bool        land_rover_v0_calculate_tail_msb(uint32_t count);
static uint16_t    land_rover_v0_calculate_tail(uint32_t count);
static void        land_rover_v0_parse_key_fields(
           uint64_t  key,
           uint32_t* signature,
           uint32_t* serial,
           uint32_t* count,
           uint8_t*  button,
           uint8_t*  check);
static bool land_rover_v0_validate_frame(
    uint64_t key,
    uint16_t tail,
    bool     extra_bit,
    bool*    check_ok,
    bool*    tail_ok);
static bool land_rover_v0_add_decoded_bit(
    SubGhzProtocolDecoderLandRoverV0* instance,
    bool                              bit);
static bool land_rover_v0_process_transition(
    SubGhzProtocolDecoderLandRoverV0* instance,
    bool                              level,
    uint32_t                          duration);
static bool land_rover_v0_finish_frame(SubGhzProtocolDecoderLandRoverV0* instance);

static bool land_rover_v0_encoder_add_level(
    SubGhzProtocolEncoderLandRoverV0* instance,
    size_t*                           index,
    bool                              level,
    uint32_t                          duration);
static bool land_rover_v0_encoder_add_bit(
    SubGhzProtocolEncoderLandRoverV0* instance,
    size_t*                           index,
    bool*                             previous_bit,
    bool                              bit);
static bool land_rover_v0_build_upload(SubGhzProtocolEncoderLandRoverV0* instance);

/* ═══════════════════════════════════════════════════════════════════════════
 * Protocol descriptor tables
 * ═════════════════════════════════════════════════════════════════════════*/
const SubGhzProtocolDecoder subghz_protocol_land_rover_v0_decoder = {
    .alloc         = subghz_protocol_decoder_land_rover_v0_alloc,
    .free          = subghz_protocol_decoder_land_rover_v0_free,
    .feed          = subghz_protocol_decoder_land_rover_v0_feed,
    .reset         = subghz_protocol_decoder_land_rover_v0_reset,
    .get_hash_data = subghz_protocol_decoder_land_rover_v0_get_hash_data,
    .serialize     = subghz_protocol_decoder_land_rover_v0_serialize,
    .deserialize   = subghz_protocol_decoder_land_rover_v0_deserialize,
    .get_string    = subghz_protocol_decoder_land_rover_v0_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_land_rover_v0_encoder = {
    .alloc       = subghz_protocol_encoder_land_rover_v0_alloc,
    .free        = subghz_protocol_encoder_land_rover_v0_free,
    .deserialize = subghz_protocol_encoder_land_rover_v0_deserialize,
    .stop        = subghz_protocol_encoder_land_rover_v0_stop,
    .yield       = subghz_protocol_encoder_land_rover_v0_yield,
};

const SubGhzProtocol subghz_protocol_land_rover_v0 = {
    .name    = LAND_ROVER_PROTOCOL_V0_NAME,
    .type    = SubGhzProtocolTypeDynamic,
    .flag    = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
               SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
               SubGhzProtocolFlag_Save      | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_land_rover_v0_decoder,
    .encoder = &subghz_protocol_land_rover_v0_encoder,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Protocol logic helpers
 * ═════════════════════════════════════════════════════════════════════════*/

static bool land_rover_v0_is_sync(uint32_t duration) {
    return DURATION_DIFF(duration, LAND_ROVER_V0_SYNC_US) < LAND_ROVER_V0_SYNC_DELTA_US;
}

static uint8_t land_rover_v0_button_from_signature(uint32_t signature) {
    if(signature == LAND_ROVER_V0_SIG_UNLOCK) return LAND_ROVER_V0_BTN_UNLOCK;
    if(signature == LAND_ROVER_V0_SIG_LOCK)   return LAND_ROVER_V0_BTN_LOCK;
    return LAND_ROVER_V0_BTN_UNKNOWN;
}

static uint8_t land_rover_v0_calculate_check(uint32_t count) {
    const uint8_t c0 =
        ((count >> 1) ^ (count >> 2) ^ (count >> 3) ^ (count >> 4) ^ (count >> 6)) & 1U;
    const uint8_t c1 =
        ((count >> 0) ^ (count >> 2) ^ (count >> 3) ^ (count >> 4) ^ (count >> 5) ^
         (count >> 6) ^ 1U) & 1U;
    const uint8_t c2 =
        ((count >> 1) ^ (count >> 3) ^ (count >> 4) ^ (count >> 5) ^ (count >> 6)) & 1U;
    return (uint8_t)(c0 | (c1 << 1) | (c2 << 2));
}

static bool land_rover_v0_calculate_tail_msb(uint32_t count) {
    const uint8_t tail =
        ((count >> 0) ^ (count >> 2) ^ (count >> 4) ^ (count >> 5)) & 1U;
    return tail != 0U;
}

static uint16_t land_rover_v0_calculate_tail(uint32_t count) {
    return land_rover_v0_calculate_tail_msb(count) ? 0xFFFFU : 0x7FFFU;
}

static void land_rover_v0_parse_key_fields(
    uint64_t  key,
    uint32_t* signature,
    uint32_t* serial,
    uint32_t* count,
    uint8_t*  button,
    uint8_t*  check) {

    uint8_t key_bytes[8];
    lr_u64_to_bytes_be(key, key_bytes);

    const uint32_t sig =
        ((uint32_t)key_bytes[0] << 16) | ((uint32_t)key_bytes[1] << 8) | key_bytes[2];
    const uint32_t sn =
        ((uint32_t)key_bytes[3] << 16) | ((uint32_t)key_bytes[4] << 8) | key_bytes[5];
    const uint32_t cnt =
        ((uint32_t)key_bytes[6] << 1) | ((key_bytes[7] >> 7) & 1U);

    if(signature) *signature = sig;
    if(serial)    *serial    = sn;
    if(count)     *count     = cnt;
    if(button)    *button    = land_rover_v0_button_from_signature(sig);
    if(check)     *check     = key_bytes[7] & 0x07U;
}

static bool land_rover_v0_validate_frame(
    uint64_t key,
    uint16_t tail,
    bool     extra_bit,
    bool*    check_ok,
    bool*    tail_ok) {

    uint8_t key_bytes[8];
    lr_u64_to_bytes_be(key, key_bytes);

    const uint32_t count          = ((uint32_t)key_bytes[6] << 1) | ((key_bytes[7] >> 7) & 1U);
    const uint8_t  expected_check = land_rover_v0_calculate_check(count);
    const uint16_t expected_tail  = land_rover_v0_calculate_tail(count);

    const bool local_check_ok =
        ((key_bytes[7] & 0x78U) == 0U) && ((key_bytes[7] & 0x07U) == expected_check);
    const bool local_tail_ok = (tail == expected_tail) && extra_bit;

    if(check_ok) *check_ok = local_check_ok;
    if(tail_ok)  *tail_ok  = local_tail_ok;

    return local_check_ok && local_tail_ok;
}

static bool land_rover_v0_add_decoded_bit(
    SubGhzProtocolDecoderLandRoverV0* instance,
    bool                              bit) {

    if(instance->bit_count < 80U) {
        const uint8_t byte_index = instance->bit_count / 8U;
        const uint8_t bit_index  = 7U - (instance->bit_count % 8U);
        if(bit) instance->raw[byte_index] |= (uint8_t)(1U << bit_index);
    } else if(instance->bit_count == 80U) {
        instance->extra_bit = bit;
    } else {
        return false;
    }

    instance->bit_count++;
    return true;
}

static bool land_rover_v0_finish_frame(SubGhzProtocolDecoderLandRoverV0* instance) {
    const uint64_t key  = lr_bytes_to_u64_be(instance->raw);
    const uint16_t tail =
        ((uint16_t)instance->raw[8] << 8) | instance->raw[9];

    if(!land_rover_v0_validate_frame(
           key, tail, instance->extra_bit,
           &instance->check_ok, &instance->tail_ok)) {
        return false;
    }

    instance->key  = key;
    instance->tail = tail;

    land_rover_v0_parse_key_fields(
        key,
        &instance->command_signature,
        &instance->serial,
        &instance->count,
        &instance->button,
        &instance->check);

    instance->generic.data           = instance->key;
    instance->generic.data_count_bit =
        subghz_protocol_land_rover_v0_const.min_count_bit_for_found;
    instance->generic.serial = instance->serial;
    instance->generic.btn    = instance->button;
    instance->generic.cnt    = instance->count;

    return true;
}

static bool land_rover_v0_process_transition(
    SubGhzProtocolDecoderLandRoverV0* instance,
    bool                              level,
    uint32_t                          duration) {

    if(!instance->boundary_pad_skipped) {
        if(level && lr_is_short(duration)) {
            instance->boundary_pad_skipped = true;
            return true;
        }
        instance->boundary_pad_skipped = true;
    }

    if(instance->pending_short) {
        if(!instance->previous_bit && !level && lr_is_short(duration)) {
            instance->pending_short = false;
            return land_rover_v0_add_decoded_bit(instance, false);
        } else if(instance->previous_bit && level && lr_is_short(duration)) {
            instance->pending_short = false;
            return land_rover_v0_add_decoded_bit(instance, true);
        }
        return false;
    }

    if(!instance->previous_bit) {
        if(level && lr_is_long(duration)) {
            instance->previous_bit = true;
            return land_rover_v0_add_decoded_bit(instance, true);
        } else if(level && lr_is_short(duration)) {
            instance->pending_short = true;
            return true;
        }
        return false;
    }

    if(!level && lr_is_long(duration)) {
        instance->previous_bit = false;
        return land_rover_v0_add_decoded_bit(instance, false);
    } else if(!level && lr_is_short(duration)) {
        instance->pending_short = true;
        return true;
    }

    return false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Encoder waveform helpers
 * ═════════════════════════════════════════════════════════════════════════*/

static bool land_rover_v0_encoder_add_level(
    SubGhzProtocolEncoderLandRoverV0* instance,
    size_t*                           index,
    bool                              level,
    uint32_t                          duration) {

    if(*index >= LAND_ROVER_V0_UPLOAD_CAPACITY) return false;
    instance->encoder.upload[(*index)++] = level_duration_make(level, duration);
    return true;
}

static bool land_rover_v0_encoder_add_bit(
    SubGhzProtocolEncoderLandRoverV0* instance,
    size_t*                           index,
    bool*                             previous_bit,
    bool                              bit) {

    const uint32_t te_short = subghz_protocol_land_rover_v0_const.te_short;
    const uint32_t te_long  = subghz_protocol_land_rover_v0_const.te_long;

    if(!*previous_bit && !bit) {
        /* 0→0: two short pulses low-high */
        if(!land_rover_v0_encoder_add_level(instance, index, true,  te_short) ||
           !land_rover_v0_encoder_add_level(instance, index, false, te_short))
            return false;
    } else if(!*previous_bit && bit) {
        /* 0→1: one long high */
        if(!land_rover_v0_encoder_add_level(instance, index, true, te_long))
            return false;
    } else if(*previous_bit && !bit) {
        /* 1→0: one long low */
        if(!land_rover_v0_encoder_add_level(instance, index, false, te_long))
            return false;
    } else {
        /* 1→1: two short pulses high-low */
        if(!land_rover_v0_encoder_add_level(instance, index, false, te_short) ||
           !land_rover_v0_encoder_add_level(instance, index, true,  te_short))
            return false;
    }

    *previous_bit = bit;
    return true;
}

static bool land_rover_v0_build_upload(SubGhzProtocolEncoderLandRoverV0* instance) {
    furi_check(instance);

    size_t         index    = 0;
    const uint32_t te_short = subghz_protocol_land_rover_v0_const.te_short;

    uint8_t key_bytes[8];
    lr_u64_to_bytes_be(instance->key, key_bytes);

    /* Preamble: alternating short high/low pairs */
    for(uint16_t i = 0; i < LAND_ROVER_V0_PREAMBLE_PAIRS; i++) {
        if(!land_rover_v0_encoder_add_level(instance, &index, true,  te_short) ||
           !land_rover_v0_encoder_add_level(instance, &index, false, te_short))
            return false;
    }

    /* Sync: long high, long low, short high (boundary pulse) */
    if(!land_rover_v0_encoder_add_level(instance, &index, true,  LAND_ROVER_V0_SYNC_US) ||
       !land_rover_v0_encoder_add_level(instance, &index, false, LAND_ROVER_V0_SYNC_US) ||
       !land_rover_v0_encoder_add_level(instance, &index, true,  te_short))
        return false;

    /* First encoded bit: always 0, previous state is 1 (the boundary pulse) */
    bool previous_bit = true;
    if(!land_rover_v0_encoder_add_bit(instance, &index, &previous_bit, false))
        return false;

    /* Data bits 2..63 from the 64-bit key */
    for(uint8_t bit_index = 2; bit_index < 64; bit_index++) {
        const uint8_t byte_index  = bit_index / 8U;
        const uint8_t bit_in_byte = 7U - (bit_index % 8U);
        const bool    bit         = (key_bytes[byte_index] >> bit_in_byte) & 1U;
        if(!land_rover_v0_encoder_add_bit(instance, &index, &previous_bit, bit))
            return false;
    }

    /* 16-bit tail */
    instance->tail = land_rover_v0_calculate_tail(instance->count);
    for(uint8_t bit_index = 0; bit_index < 16; bit_index++) {
        const bool bit = (instance->tail >> (15U - bit_index)) & 1U;
        if(!land_rover_v0_encoder_add_bit(instance, &index, &previous_bit, bit))
            return false;
    }

    /* Extra bit (always 1) */
    if(!land_rover_v0_encoder_add_bit(instance, &index, &previous_bit, true))
        return false;

    /* Inter-frame gap */
    if(!land_rover_v0_encoder_add_level(instance, &index, false, LAND_ROVER_V0_GAP_US))
        return false;

    instance->encoder.front       = 0;
    instance->encoder.size_upload = index;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Decoder – public API
 * ═════════════════════════════════════════════════════════════════════════*/

void* subghz_protocol_decoder_land_rover_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderLandRoverV0* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderLandRoverV0));
    furi_check(instance);
    instance->base.protocol         = &subghz_protocol_land_rover_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_land_rover_v0_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_land_rover_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    instance->decoder.parser_step     = LandRoverV0DecoderStepReset;
    instance->decoder.te_last         = 0;
    instance->preamble_count          = 0;
    memset(instance->raw, 0, sizeof(instance->raw));
    instance->bit_count               = 0;
    instance->extra_bit               = false;
    instance->previous_bit            = true;
    instance->boundary_pad_skipped    = false;
    instance->pending_short           = false;
}

void subghz_protocol_decoder_land_rover_v0_feed(
    void*    context,
    bool     level,
    uint32_t duration) {

    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    switch(instance->decoder.parser_step) {
    case LandRoverV0DecoderStepReset:
        if(level && lr_is_short(duration)) {
            instance->preamble_count      = 0;
            instance->decoder.parser_step = LandRoverV0DecoderStepPreambleLow;
        }
        break;

    case LandRoverV0DecoderStepPreambleLow:
        if(!level && lr_is_short(duration)) {
            instance->preamble_count++;
            instance->decoder.parser_step = LandRoverV0DecoderStepPreambleHigh;
        } else {
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
        }
        break;

    case LandRoverV0DecoderStepPreambleHigh:
        if(level && lr_is_short(duration)) {
            instance->decoder.parser_step = LandRoverV0DecoderStepPreambleLow;
        } else if(level && land_rover_v0_is_sync(duration) &&
                  instance->preamble_count >= LAND_ROVER_V0_MIN_PREAMBLE_PAIRS) {
            instance->decoder.parser_step = LandRoverV0DecoderStepSyncLow;
        } else {
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
        }
        break;

    case LandRoverV0DecoderStepSyncLow:
        if(!level && land_rover_v0_is_sync(duration)) {
            memset(instance->raw, 0, sizeof(instance->raw));
            instance->bit_count            = 0;
            instance->extra_bit            = false;
            instance->previous_bit         = true;
            instance->boundary_pad_skipped = false;
            instance->pending_short        = false;
            land_rover_v0_add_decoded_bit(instance, true);
            instance->decoder.parser_step  = LandRoverV0DecoderStepData;
        } else {
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
        }
        break;

    case LandRoverV0DecoderStepData:
        if(!land_rover_v0_process_transition(instance, level, duration)) {
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
            break;
        }
        if(instance->bit_count ==
           subghz_protocol_land_rover_v0_const.min_count_bit_for_found) {
            if(land_rover_v0_finish_frame(instance) && instance->base.callback) {
                instance->base.callback(&instance->base, instance->base.context);
            }
            instance->decoder.parser_step = LandRoverV0DecoderStepReset;
        }
        break;
    }

    instance->decoder.te_last = duration;
}

uint8_t subghz_protocol_decoder_land_rover_v0_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    SubGhzBlockDecoder decoder = {
        .decode_data      = instance->key,
        .decode_count_bit = 64,
    };
    uint8_t hash = subghz_protocol_blocks_get_hash_data(&decoder, 9);
    hash ^= (uint8_t)(instance->tail >> 8);
    hash ^= (uint8_t)instance->tail;
    hash ^= instance->extra_bit ? 1U : 0U;
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_land_rover_v0_serialize(
    void*              context,
    FlipperFormat*     flipper_format,
    SubGhzRadioPreset* preset) {

    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint8_t key_bytes[8];
        lr_u64_to_bytes_be(instance->key, key_bytes);

        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_hex(
            flipper_format, LR_FF_KEY, key_bytes, sizeof(key_bytes));
        lr_ff_write_u32(flipper_format, LR_FF_SERIAL,             instance->serial);
        lr_ff_write_u32(flipper_format, LR_FF_BTN,                instance->button);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_BTNSIG,  instance->command_signature);
        lr_ff_write_u32(flipper_format, LR_FF_CNT,                instance->count);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_CHECK,   instance->check);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_TAIL,    instance->tail);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_EXTRA_BIT,
                        instance->extra_bit ? 1U : 0U);
    }

    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_land_rover_v0_deserialize(
    void*          context,
    FlipperFormat* flipper_format) {

    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_land_rover_v0_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        uint8_t key_bytes[8] = {0};
        bool    have_key     = false;

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(
               flipper_format, LR_FF_KEY, key_bytes, sizeof(key_bytes))) {
            instance->key = lr_bytes_to_u64_be(key_bytes);
            have_key      = true;
        }

        if(!have_key) {
            instance->key = instance->generic.data;
            lr_u64_to_bytes_be(instance->key, key_bytes);
        }

        uint32_t temp = 0;

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LAND_ROVER_V0_FF_TAIL, &temp)) {
            instance->tail = (uint16_t)(temp & 0xFFFFU);
        } else {
            const uint32_t count =
                ((uint32_t)key_bytes[6] << 1) | ((key_bytes[7] >> 7) & 1U);
            instance->tail = land_rover_v0_calculate_tail(count);
        }

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LAND_ROVER_V0_FF_EXTRA_BIT, &temp)) {
            instance->extra_bit = (temp & 1U) != 0;
        } else {
            instance->extra_bit = true;
        }

        land_rover_v0_validate_frame(
            instance->key, instance->tail, instance->extra_bit,
            &instance->check_ok, &instance->tail_ok);

        land_rover_v0_parse_key_fields(
            instance->key,
            &instance->command_signature,
            &instance->serial,
            &instance->count,
            &instance->button,
            &instance->check);

        instance->generic.data           = instance->key;
        instance->generic.data_count_bit =
            subghz_protocol_land_rover_v0_const.min_count_bit_for_found;
        instance->generic.serial = instance->serial;
        instance->generic.btn    = instance->button;
        instance->generic.cnt    = instance->count;

        // [PROTOPIRATE_PORT] custom_btn support
        // Land Rover V0 mapping: Up=0x02 (Lock), OK=0x04 (Unlock). 2 buttons.
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->generic.btn);
        }
        subghz_custom_btn_set_max(2);
    }

    return ret;
}

void subghz_protocol_decoder_land_rover_v0_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderLandRoverV0* instance = context;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%llX\r\n"
        "SN:0x%06lX Btn:%X\r\n"
        "CRC:%02X Cnt:%05lX [%s]",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)instance->key,
        (unsigned long)instance->serial,
        instance->button,
        instance->check,
        (unsigned long)instance->count,
        instance->check_ok ? "OK" : "BAD");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Encoder – helpers
 * ═════════════════════════════════════════════════════════════════════════*/

static uint32_t land_rover_v0_signature_from_button(uint8_t button) {
    switch(button) {
    case LAND_ROVER_V0_BTN_LOCK:   return LAND_ROVER_V0_SIG_LOCK;
    case LAND_ROVER_V0_BTN_UNLOCK: return LAND_ROVER_V0_SIG_UNLOCK;
    default:                        return 0;
    }
}

static uint64_t land_rover_v0_build_key(
    uint32_t signature,
    uint32_t serial,
    uint32_t count) {

    uint8_t key_bytes[8] = {0};
    key_bytes[0] = (uint8_t)((signature >> 16) & 0xFFU);
    key_bytes[1] = (uint8_t)((signature >>  8) & 0xFFU);
    key_bytes[2] = (uint8_t)( signature        & 0xFFU);
    key_bytes[3] = (uint8_t)((serial    >> 16) & 0xFFU);
    key_bytes[4] = (uint8_t)((serial    >>  8) & 0xFFU);
    key_bytes[5] = (uint8_t)( serial           & 0xFFU);
    key_bytes[6] = (uint8_t)((count     >>  1) & 0xFFU);

    const bool    counter_lsb = (count & 1U) != 0;
    const uint8_t check       = land_rover_v0_calculate_check(count);
    key_bytes[7]              = (counter_lsb ? 0x80U : 0x00U) | check;

    return lr_bytes_to_u64_be(key_bytes);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Encoder – public API
 * ═════════════════════════════════════════════════════════════════════════*/

void* subghz_protocol_encoder_land_rover_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderLandRoverV0* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderLandRoverV0));
    furi_check(instance);

    instance->base.protocol         = &subghz_protocol_land_rover_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    /* Allocate the waveform upload buffer */
    instance->encoder.upload =
        malloc(LAND_ROVER_V0_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);

    instance->encoder.repeat      = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.front       = 0;
    instance->encoder.is_running  = false;

    return instance;
}

void subghz_protocol_encoder_land_rover_v0_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderLandRoverV0* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus subghz_protocol_encoder_land_rover_v0_deserialize(
    void*          context,
    FlipperFormat* flipper_format) {

    furi_check(context);
    SubGhzProtocolEncoderLandRoverV0* instance = context;
    SubGhzProtocolStatus              ret      = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front      = 0;
    instance->encoder.repeat     = 10;

    do {
        if(lr_verify_protocol_name(flipper_format, instance->base.protocol->name) !=
           SubGhzProtocolStatusOk)
            break;

        SubGhzProtocolStatus load_status =
            subghz_block_generic_deserialize_check_count_bit(
                &instance->generic,
                flipper_format,
                subghz_protocol_land_rover_v0_const.min_count_bit_for_found);
        if(load_status != SubGhzProtocolStatusOk) break;

        instance->serial = instance->generic.serial & 0xFFFFFFU;
        instance->button = instance->generic.btn;
        instance->count  = instance->generic.cnt    & 0x1FFU;

        uint8_t key_bytes[8] = {0};
        bool    have_key     = false;

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(
               flipper_format, LR_FF_KEY, key_bytes, sizeof(key_bytes))) {
            instance->key = lr_bytes_to_u64_be(key_bytes);
            have_key      = true;
        }

        if(have_key) {
            land_rover_v0_parse_key_fields(
                instance->key,
                &instance->command_signature,
                &instance->serial,
                &instance->count,
                &instance->button,
                &instance->check);
        }

        uint32_t u32         = 0;
        bool     have_button = false;

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LR_FF_SERIAL, &u32))
            instance->serial = u32 & 0xFFFFFFU;

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LR_FF_CNT, &u32))
            instance->count = u32 & 0x1FFU;

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LR_FF_BTN, &u32)) {
            instance->button = (uint8_t)u32;
            have_button      = true;
        }

        flipper_format_rewind(flipper_format);
        if(lr_ff_read_u32(flipper_format, LAND_ROVER_V0_FF_BTNSIG, &u32))
            instance->command_signature = u32 & 0xFFFFFFU;

        // [PROTOPIRATE_PORT] custom_btn support
        // Land Rover V0 mapping: Up=0x02 (Lock), OK=0x04 (Unlock).
        {
            const uint8_t original_btn = instance->button;
            if(subghz_custom_btn_get_original() == 0) {
                subghz_custom_btn_set_original(original_btn);
            }
            subghz_custom_btn_set_max(2);
            uint8_t custom_btn_id = subghz_custom_btn_get();
            uint8_t new_btn = original_btn;
            switch(custom_btn_id) {
            case SUBGHZ_CUSTOM_BTN_UP: new_btn = LAND_ROVER_V0_BTN_LOCK;   break;
            // [BUGFIX] OK = default post-load; replay captured button (do
            // not overwrite to UNLOCK unconditionally).
            case SUBGHZ_CUSTOM_BTN_OK: new_btn = original_btn;             break;
            default:                   new_btn = original_btn;             break;
            }
            if(new_btn != original_btn && new_btn != 0U) {
                instance->button = new_btn;
                have_button = true;
            }
        }

        if(have_button) {
            const uint32_t sig = land_rover_v0_signature_from_button(instance->button);
            if(sig != 0U) instance->command_signature = sig;
        }

        if(instance->command_signature == 0U) break;

        instance->key = land_rover_v0_build_key(
            instance->command_signature, instance->serial, instance->count);

        lr_u64_to_bytes_be(instance->key, key_bytes);
        instance->tail = land_rover_v0_calculate_tail(instance->count);

        land_rover_v0_parse_key_fields(
            instance->key,
            &instance->command_signature,
            &instance->serial,
            &instance->count,
            &instance->button,
            &instance->check);

        instance->generic.data           = instance->key;
        instance->generic.data_count_bit =
            subghz_protocol_land_rover_v0_const.min_count_bit_for_found;
        instance->generic.serial = instance->serial;
        instance->generic.btn    = instance->button;
        instance->generic.cnt    = instance->count;

        flipper_format_rewind(flipper_format);
        instance->encoder.repeat = lr_encoder_read_repeat(flipper_format, 10);

        if(!land_rover_v0_build_upload(instance) ||
           instance->encoder.size_upload == 0U)
            break;

        /* Update the file with all recalculated fields */
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_hex(
            flipper_format, LR_FF_KEY, key_bytes, sizeof(key_bytes));
        lr_ff_write_u32(flipper_format, LR_FF_SERIAL,             instance->serial);
        lr_ff_write_u32(flipper_format, LR_FF_BTN,                instance->button);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_BTNSIG,  instance->command_signature);
        lr_ff_write_u32(flipper_format, LR_FF_CNT,                instance->count);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_CHECK,   instance->check);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_TAIL,    instance->tail);
        lr_ff_write_u32(flipper_format, LAND_ROVER_V0_FF_EXTRA_BIT, 1U);

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_land_rover_v0_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderLandRoverV0* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_land_rover_v0_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderLandRoverV0* instance = context;

    if(instance->encoder.front >= instance->encoder.size_upload) {
        /* One full repetition done; count it down */
        if(instance->encoder.repeat > 0 && !subghz_block_generic_global.endless_tx) {
            instance->encoder.repeat--;
        }
        instance->encoder.front = 0;

        if(instance->encoder.repeat == 0) {
            instance->encoder.is_running = false;
            return level_duration_reset();
        }
    }

    return instance->encoder.upload[instance->encoder.front++];
}

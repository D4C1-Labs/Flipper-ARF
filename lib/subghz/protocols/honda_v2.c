#include "honda_v2.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <string.h>

#define TAG "HondaV2"

static const SubGhzBlockConst subghz_protocol_honda_v2_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 81,
};

#define HONDA_V2_PREAMBLE_PAIRS     319U
#define HONDA_V2_MIN_PREAMBLE_PAIRS 64U
#define HONDA_V2_SYNC_US            750U
#define HONDA_V2_SYNC_DELTA_US      120U
#define HONDA_V2_UPLOAD_CAPACITY    1024U
#define HONDA_V2_GAP_US             50000U

#define HONDA_V2_BTN_UNKNOWN 0x00U
#define HONDA_V2_BTN_LOCK    0x02U
#define HONDA_V2_BTN_UNLOCK  0x04U

#define HONDA_V2_SIG_UNLOCK 0xA285E3UL
#define HONDA_V2_SIG_LOCK   0xC20363UL

#define HONDA_V2_FF_BTNSIG    "BtnSig"
#define HONDA_V2_FF_CHECK     "Check"
#define HONDA_V2_FF_TAIL      "Tail"
#define HONDA_V2_FF_EXTRA_BIT "ExtraBit"

typedef struct SubGhzProtocolDecoderHondaV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    uint8_t raw[10];
    uint8_t bit_count;
    bool extra_bit;
    bool previous_bit;
    bool boundary_pad_skipped;
    bool pending_short;

    uint64_t key;
    uint16_t tail;
    uint32_t command_signature;
    uint32_t serial;
    uint32_t count;
    uint8_t button;
    uint8_t check;
    bool check_ok;
    bool tail_ok;
} SubGhzProtocolDecoderHondaV2;

typedef struct SubGhzProtocolEncoderHondaV2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t key;
    uint16_t tail;
    uint32_t command_signature;
    uint32_t serial;
    uint32_t count;
    uint8_t button;
    uint8_t check;
} SubGhzProtocolEncoderHondaV2;

typedef enum {
    HondaV2DecoderStepReset = 0,
    HondaV2DecoderStepPreambleLow,
    HondaV2DecoderStepPreambleHigh,
    HondaV2DecoderStepSyncLow,
    HondaV2DecoderStepData,
} HondaV2DecoderStep;

static void honda_v2_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[i] = (data >> ((7U - i) * 8U)) & 0xFFU;
    }
}

static uint64_t honda_v2_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t data = 0;
    for(size_t i = 0; i < 8; i++) {
        data = (data << 8U) | bytes[i];
    }
    return data;
}

static uint8_t honda_v2_button_from_signature(uint32_t signature);
static uint8_t honda_v2_calculate_check(uint32_t count);
static bool honda_v2_calculate_tail_msb(uint32_t count);
static uint16_t honda_v2_calculate_tail(uint32_t count);
static void honda_v2_parse_key_fields(
    uint64_t key,
    uint32_t* signature,
    uint32_t* serial,
    uint32_t* count,
    uint8_t* button,
    uint8_t* check);
static bool honda_v2_validate_frame(
    uint64_t key,
    uint16_t tail,
    bool extra_bit,
    bool* check_ok,
    bool* tail_ok);
static bool honda_v2_add_decoded_bit(SubGhzProtocolDecoderHondaV2* instance, bool bit);
static bool honda_v2_process_transition(
    SubGhzProtocolDecoderHondaV2* instance,
    bool level,
    uint32_t duration);
static bool honda_v2_finish_frame(SubGhzProtocolDecoderHondaV2* instance);

static bool honda_v2_encoder_add_level(
    SubGhzProtocolEncoderHondaV2* instance,
    size_t* index,
    bool level,
    uint32_t duration);
static bool honda_v2_encoder_add_bit(
    SubGhzProtocolEncoderHondaV2* instance,
    size_t* index,
    bool* previous_bit,
    bool bit);
static bool honda_v2_build_upload(SubGhzProtocolEncoderHondaV2* instance);

static bool honda_v2_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_honda_v2_const.te_short) <
           subghz_protocol_honda_v2_const.te_delta;
}

static bool honda_v2_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_honda_v2_const.te_long) <
           subghz_protocol_honda_v2_const.te_delta;
}

static bool honda_v2_is_sync(uint32_t duration) {
    return DURATION_DIFF(duration, HONDA_V2_SYNC_US) < HONDA_V2_SYNC_DELTA_US;
}

static uint8_t honda_v2_button_from_signature(uint32_t signature) {
    if(signature == HONDA_V2_SIG_UNLOCK) {
        return HONDA_V2_BTN_UNLOCK;
    } else if(signature == HONDA_V2_SIG_LOCK) {
        return HONDA_V2_BTN_LOCK;
    }
    return HONDA_V2_BTN_UNKNOWN;
}

static uint8_t honda_v2_calculate_check(uint32_t count) {
    const uint8_t c0 = ((count >> 1) ^ (count >> 2) ^ (count >> 3) ^ (count >> 4) ^ (count >> 6)) &
                       1U;
    const uint8_t c1 = ((count >> 0) ^ (count >> 2) ^ (count >> 3) ^ (count >> 4) ^ (count >> 5) ^
                        (count >> 6) ^ 1U) &
                       1U;
    const uint8_t c2 = ((count >> 1) ^ (count >> 3) ^ (count >> 4) ^ (count >> 5) ^ (count >> 6)) &
                       1U;
    return (uint8_t)(c0 | (c1 << 1) | (c2 << 2));
}

static bool honda_v2_calculate_tail_msb(uint32_t count) {
    const uint8_t tail = ((count >> 0) ^ (count >> 2) ^ (count >> 4) ^ (count >> 5)) & 1U;
    return tail != 0U;
}

static uint16_t honda_v2_calculate_tail(uint32_t count) {
    return honda_v2_calculate_tail_msb(count) ? 0xFFFFU : 0x7FFFU;
}

static void honda_v2_parse_key_fields(
    uint64_t key,
    uint32_t* signature,
    uint32_t* serial,
    uint32_t* count,
    uint8_t* button,
    uint8_t* check) {
    uint8_t key_bytes[8];
    honda_v2_u64_to_bytes_be(key, key_bytes);

    const uint32_t sig = ((uint32_t)key_bytes[0] << 16) | ((uint32_t)key_bytes[1] << 8) |
                         key_bytes[2];
    const uint32_t sn = ((uint32_t)key_bytes[3] << 16) | ((uint32_t)key_bytes[4] << 8) |
                        key_bytes[5];
    const uint32_t cnt = ((uint32_t)key_bytes[6] << 1) | ((key_bytes[7] >> 7) & 1U);

    if(signature) *signature = sig;
    if(serial) *serial = sn;
    if(count) *count = cnt;
    if(button) *button = honda_v2_button_from_signature(sig);
    if(check) *check = key_bytes[7] & 0x07U;
}

static bool honda_v2_validate_frame(
    uint64_t key,
    uint16_t tail,
    bool extra_bit,
    bool* check_ok,
    bool* tail_ok) {
    uint8_t key_bytes[8];
    honda_v2_u64_to_bytes_be(key, key_bytes);

    const uint32_t count = ((uint32_t)key_bytes[6] << 1) | ((key_bytes[7] >> 7) & 1U);
    const uint8_t expected_check = honda_v2_calculate_check(count);
    const uint16_t expected_tail = honda_v2_calculate_tail(count);

    const bool local_check_ok = ((key_bytes[7] & 0x78U) == 0U) &&
                                ((key_bytes[7] & 0x07U) == expected_check);
    const bool local_tail_ok = (tail == expected_tail) && extra_bit;

    if(check_ok) *check_ok = local_check_ok;
    if(tail_ok) *tail_ok = local_tail_ok;

    return local_check_ok && local_tail_ok;
}

static bool honda_v2_add_decoded_bit(SubGhzProtocolDecoderHondaV2* instance, bool bit) {
    if(instance->bit_count < 80U) {
        const uint8_t byte_index = instance->bit_count / 8U;
        const uint8_t bit_index = 7U - (instance->bit_count % 8U);
        if(bit) {
            instance->raw[byte_index] |= (uint8_t)(1U << bit_index);
        }
    } else if(instance->bit_count == 80U) {
        instance->extra_bit = bit;
    } else {
        return false;
    }
    instance->bit_count++;
    return true;
}

static bool honda_v2_finish_frame(SubGhzProtocolDecoderHondaV2* instance) {
    const uint64_t key = honda_v2_bytes_to_u64_be(instance->raw);
    const uint16_t tail = ((uint16_t)instance->raw[8] << 8) | instance->raw[9];

    if(!honda_v2_validate_frame(
           key, tail, instance->extra_bit, &instance->check_ok, &instance->tail_ok)) {
        return false;
    }

    instance->key = key;
    instance->tail = tail;

    honda_v2_parse_key_fields(
        key,
        &instance->command_signature,
        &instance->serial,
        &instance->count,
        &instance->button,
        &instance->check);

    instance->generic.data = instance->key;
    instance->generic.data_count_bit = subghz_protocol_honda_v2_const.min_count_bit_for_found;
    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->button;
    instance->generic.cnt = instance->count;

    return true;
}

static bool honda_v2_process_transition(
    SubGhzProtocolDecoderHondaV2* instance,
    bool level,
    uint32_t duration) {
    if(!instance->boundary_pad_skipped) {
        if(level && honda_v2_is_short(duration)) {
            instance->boundary_pad_skipped = true;
            return true;
        }
        instance->boundary_pad_skipped = true;
    }

    if(instance->pending_short) {
        if(!instance->previous_bit && !level && honda_v2_is_short(duration)) {
            instance->pending_short = false;
            return honda_v2_add_decoded_bit(instance, false);
        } else if(instance->previous_bit && level && honda_v2_is_short(duration)) {
            instance->pending_short = false;
            return honda_v2_add_decoded_bit(instance, true);
        }
        return false;
    }

    if(!instance->previous_bit) {
        if(level && honda_v2_is_long(duration)) {
            instance->previous_bit = true;
            return honda_v2_add_decoded_bit(instance, true);
        } else if(level && honda_v2_is_short(duration)) {
            instance->pending_short = true;
            return true;
        }
        return false;
    }

    if(!level && honda_v2_is_long(duration)) {
        instance->previous_bit = false;
        return honda_v2_add_decoded_bit(instance, false);
    } else if(!level && honda_v2_is_short(duration)) {
        instance->pending_short = true;
        return true;
    }

    return false;
}

static bool honda_v2_encoder_add_level(
    SubGhzProtocolEncoderHondaV2* instance,
    size_t* index,
    bool level,
    uint32_t duration) {
    if(*index >= HONDA_V2_UPLOAD_CAPACITY) {
        return false;
    }
    instance->encoder.upload[(*index)++] = level_duration_make(level, duration);
    return true;
}

static bool honda_v2_encoder_add_bit(
    SubGhzProtocolEncoderHondaV2* instance,
    size_t* index,
    bool* previous_bit,
    bool bit) {
    const uint32_t te_short = subghz_protocol_honda_v2_const.te_short;
    const uint32_t te_long = subghz_protocol_honda_v2_const.te_long;

    if(!*previous_bit && !bit) {
        if(!honda_v2_encoder_add_level(instance, index, true, te_short) ||
           !honda_v2_encoder_add_level(instance, index, false, te_short)) {
            return false;
        }
    } else if(!*previous_bit && bit) {
        if(!honda_v2_encoder_add_level(instance, index, true, te_long)) {
            return false;
        }
    } else if(*previous_bit && !bit) {
        if(!honda_v2_encoder_add_level(instance, index, false, te_long)) {
            return false;
        }
    } else {
        if(!honda_v2_encoder_add_level(instance, index, false, te_short) ||
           !honda_v2_encoder_add_level(instance, index, true, te_short)) {
            return false;
        }
    }

    *previous_bit = bit;
    return true;
}

static bool honda_v2_build_upload(SubGhzProtocolEncoderHondaV2* instance) {
    furi_check(instance);

    size_t index = 0;
    const uint32_t te_short = subghz_protocol_honda_v2_const.te_short;

    uint8_t key_bytes[8];
    honda_v2_u64_to_bytes_be(instance->key, key_bytes);

    for(uint16_t i = 0; i < HONDA_V2_PREAMBLE_PAIRS; i++) {
        if(!honda_v2_encoder_add_level(instance, &index, true, te_short) ||
           !honda_v2_encoder_add_level(instance, &index, false, te_short)) {
            return false;
        }
    }

    if(!honda_v2_encoder_add_level(instance, &index, true, HONDA_V2_SYNC_US) ||
       !honda_v2_encoder_add_level(instance, &index, false, HONDA_V2_SYNC_US) ||
       !honda_v2_encoder_add_level(instance, &index, true, te_short)) {
        return false;
    }

    bool previous_bit = true;
    if(!honda_v2_encoder_add_bit(instance, &index, &previous_bit, false)) {
        return false;
    }

    for(uint8_t bit_index = 2; bit_index < 64; bit_index++) {
        const uint8_t byte_index = bit_index / 8U;
        const uint8_t bit_in_byte = 7U - (bit_index % 8U);
        const bool bit = (key_bytes[byte_index] >> bit_in_byte) & 1U;
        if(!honda_v2_encoder_add_bit(instance, &index, &previous_bit, bit)) {
            return false;
        }
    }

    instance->tail = honda_v2_calculate_tail(instance->count);
    for(uint8_t bit_index = 0; bit_index < 16; bit_index++) {
        const bool bit = (instance->tail >> (15U - bit_index)) & 1U;
        if(!honda_v2_encoder_add_bit(instance, &index, &previous_bit, bit)) {
            return false;
        }
    }

    if(!honda_v2_encoder_add_bit(instance, &index, &previous_bit, true)) {
        return false;
    }

    if(!honda_v2_encoder_add_level(instance, &index, false, HONDA_V2_GAP_US)) {
        return false;
    }

    instance->encoder.front = 0;
    instance->encoder.size_upload = index;
    return true;
}

static uint32_t honda_v2_signature_from_button(uint8_t button) {
    switch(button) {
    case HONDA_V2_BTN_LOCK:
        return HONDA_V2_SIG_LOCK;
    case HONDA_V2_BTN_UNLOCK:
        return HONDA_V2_SIG_UNLOCK;
    default:
        return 0;
    }
}

static uint64_t honda_v2_build_key(uint32_t signature, uint32_t serial, uint32_t count) {
    uint8_t key_bytes[8] = {0};
    key_bytes[0] = (uint8_t)((signature >> 16) & 0xFFU);
    key_bytes[1] = (uint8_t)((signature >> 8) & 0xFFU);
    key_bytes[2] = (uint8_t)(signature & 0xFFU);
    key_bytes[3] = (uint8_t)((serial >> 16) & 0xFFU);
    key_bytes[4] = (uint8_t)((serial >> 8) & 0xFFU);
    key_bytes[5] = (uint8_t)(serial & 0xFFU);
    key_bytes[6] = (uint8_t)((count >> 1) & 0xFFU);

    const bool counter_lsb = (count & 1U) != 0;
    const uint8_t check = honda_v2_calculate_check(count);
    key_bytes[7] = (counter_lsb ? 0x80U : 0x00U) | check;

    return honda_v2_bytes_to_u64_be(key_bytes);
}

const SubGhzProtocolDecoder subghz_protocol_honda_v2_decoder = {
    .alloc = subghz_protocol_decoder_honda_v2_alloc,
    .free = subghz_protocol_decoder_honda_v2_free,
    .feed = subghz_protocol_decoder_honda_v2_feed,
    .reset = subghz_protocol_decoder_honda_v2_reset,
    .get_hash_data = subghz_protocol_decoder_honda_v2_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_v2_serialize,
    .deserialize = subghz_protocol_decoder_honda_v2_deserialize,
    .get_string = subghz_protocol_decoder_honda_v2_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_honda_v2_encoder = {
    .alloc = subghz_protocol_encoder_honda_v2_alloc,
    .free = subghz_protocol_encoder_honda_v2_free,
    .deserialize = subghz_protocol_encoder_honda_v2_deserialize,
    .stop = subghz_protocol_encoder_honda_v2_stop,
    .yield = subghz_protocol_encoder_honda_v2_yield,
};

const SubGhzProtocol honda_v2_protocol = {
    .name = HONDA_V2_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_honda_v2_decoder,
    .encoder = &subghz_protocol_honda_v2_encoder,
};

void* subghz_protocol_decoder_honda_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHondaV2* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderHondaV2));
    furi_check(instance);

    instance->base.protocol = &honda_v2_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;

    return instance;
}

void subghz_protocol_decoder_honda_v2_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV2* instance = context;
    free(instance);
}

void subghz_protocol_decoder_honda_v2_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV2* instance = context;

    instance->decoder.parser_step = HondaV2DecoderStepReset;
    instance->decoder.te_last = 0;
    instance->preamble_count = 0;
    memset(instance->raw, 0, sizeof(instance->raw));
    instance->bit_count = 0;
    instance->extra_bit = false;
    instance->previous_bit = true;
    instance->boundary_pad_skipped = false;
    instance->pending_short = false;
}

void subghz_protocol_decoder_honda_v2_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV2* instance = context;

    switch(instance->decoder.parser_step) {
    case HondaV2DecoderStepReset:
        if(level && honda_v2_is_short(duration)) {
            instance->preamble_count = 0;
            instance->decoder.parser_step = HondaV2DecoderStepPreambleLow;
        }
        break;

    case HondaV2DecoderStepPreambleLow:
        if(!level && honda_v2_is_short(duration)) {
            instance->preamble_count++;
            instance->decoder.parser_step = HondaV2DecoderStepPreambleHigh;
        } else {
            instance->decoder.parser_step = HondaV2DecoderStepReset;
        }
        break;

    case HondaV2DecoderStepPreambleHigh:
        if(level && honda_v2_is_short(duration)) {
            instance->decoder.parser_step = HondaV2DecoderStepPreambleLow;
        } else if(
            level && honda_v2_is_sync(duration) &&
            instance->preamble_count >= HONDA_V2_MIN_PREAMBLE_PAIRS) {
            instance->decoder.parser_step = HondaV2DecoderStepSyncLow;
        } else {
            instance->decoder.parser_step = HondaV2DecoderStepReset;
        }
        break;

    case HondaV2DecoderStepSyncLow:
        if(!level && honda_v2_is_sync(duration)) {
            memset(instance->raw, 0, sizeof(instance->raw));
            instance->bit_count = 0;
            instance->extra_bit = false;
            instance->previous_bit = true;
            instance->boundary_pad_skipped = false;
            instance->pending_short = false;
            honda_v2_add_decoded_bit(instance, true);
            instance->decoder.parser_step = HondaV2DecoderStepData;
        } else {
            instance->decoder.parser_step = HondaV2DecoderStepReset;
        }
        break;

    case HondaV2DecoderStepData:
        if(!honda_v2_process_transition(instance, level, duration)) {
            instance->decoder.parser_step = HondaV2DecoderStepReset;
            break;
        }

        if(instance->bit_count == subghz_protocol_honda_v2_const.min_count_bit_for_found) {
            if(honda_v2_finish_frame(instance) && instance->base.callback) {
                instance->base.callback(&instance->base, instance->base.context);
            }
            instance->decoder.parser_step = HondaV2DecoderStepReset;
        }
        break;
    }

    instance->decoder.te_last = duration;
}

uint8_t subghz_protocol_decoder_honda_v2_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV2* instance = context;

    SubGhzBlockDecoder decoder = {
        .decode_data = instance->key,
        .decode_count_bit = 64,
    };
    uint8_t hash = subghz_protocol_blocks_get_hash_data(&decoder, 9);
    hash ^= (uint8_t)(instance->tail >> 8);
    hash ^= (uint8_t)instance->tail;
    hash ^= instance->extra_bit ? 1U : 0U;
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_honda_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV2* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint8_t key_bytes[8];
        honda_v2_u64_to_bytes_be(instance->key, key_bytes);
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_hex(flipper_format, "Key", key_bytes, sizeof(key_bytes));

        uint32_t tmp;
        flipper_format_rewind(flipper_format);
        tmp = instance->serial;
        if(!flipper_format_update_uint32(flipper_format, "Serial", &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Serial", &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->button;
        if(!flipper_format_update_uint32(flipper_format, "Btn", &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Btn", &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->command_signature;
        if(!flipper_format_update_uint32(flipper_format, HONDA_V2_FF_BTNSIG, &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, HONDA_V2_FF_BTNSIG, &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->count;
        if(!flipper_format_update_uint32(flipper_format, "Cnt", &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->check;
        if(!flipper_format_update_uint32(flipper_format, HONDA_V2_FF_CHECK, &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, HONDA_V2_FF_CHECK, &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->tail;
        if(!flipper_format_update_uint32(flipper_format, HONDA_V2_FF_TAIL, &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, HONDA_V2_FF_TAIL, &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->extra_bit ? 1U : 0U;
        if(!flipper_format_update_uint32(flipper_format, HONDA_V2_FF_EXTRA_BIT, &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, HONDA_V2_FF_EXTRA_BIT, &tmp, 1);
        }
    }

    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_honda_v2_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV2* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_honda_v2_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        uint8_t key_bytes[8] = {0};
        bool have_key = false;

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(flipper_format, "Key", key_bytes, sizeof(key_bytes))) {
            instance->key = honda_v2_bytes_to_u64_be(key_bytes);
            have_key = true;
        }

        if(!have_key) {
            instance->key = instance->generic.data;
            honda_v2_u64_to_bytes_be(instance->key, key_bytes);
        }

        uint32_t temp = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, HONDA_V2_FF_TAIL, &temp, 1)) {
            instance->tail = temp & 0xFFFFU;
        } else {
            const uint32_t count = ((uint32_t)key_bytes[6] << 1) | ((key_bytes[7] >> 7) & 1U);
            instance->tail = honda_v2_calculate_tail(count);
        }

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, HONDA_V2_FF_EXTRA_BIT, &temp, 1)) {
            instance->extra_bit = (temp & 1U) != 0;
        } else {
            instance->extra_bit = true;
        }

        honda_v2_validate_frame(
            instance->key,
            instance->tail,
            instance->extra_bit,
            &instance->check_ok,
            &instance->tail_ok);
        honda_v2_parse_key_fields(
            instance->key,
            &instance->command_signature,
            &instance->serial,
            &instance->count,
            &instance->button,
            &instance->check);

        instance->generic.data = instance->key;
        instance->generic.data_count_bit =
            subghz_protocol_honda_v2_const.min_count_bit_for_found;
        instance->generic.serial = instance->serial;
        instance->generic.btn = instance->button;
        instance->generic.cnt = instance->count;
    }

    return ret;
}

static const char* honda_v2_button_name(uint8_t button) {
    switch(button) {
    case HONDA_V2_BTN_LOCK:
        return "Lock";
    case HONDA_V2_BTN_UNLOCK:
        return "Unlock";
    default:
        return "Unknown";
    }
}

void subghz_protocol_decoder_honda_v2_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV2* instance = context;

    // [BUGFIX UI] Re-derive the displayed button from the current D-pad selection
    // so the transmitter UI reflects subghz_custom_btn_get() (like psa.c/star_line.c),
    // mirroring the encoder remap (see encoder deserialize): Up=Lock, Down=Unlock,
    // OK=captured. Honda V2 has 2 real buttons. Rebuild the key with the selected
    // button's signature so the shown key matches the TX frame. The check bits
    // depend only on the counter, so they stay valid.
    subghz_custom_btn_set_max(4);
    uint8_t display_btn = instance->button;
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP:
        display_btn = HONDA_V2_BTN_LOCK;
        break;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        display_btn = HONDA_V2_BTN_UNLOCK;
        break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        break;
    }

    uint64_t display_key = instance->key;
    if(display_btn != instance->button) {
        uint32_t sig = honda_v2_signature_from_button(display_btn);
        if(sig != 0U) {
            display_key = honda_v2_build_key(sig, instance->serial, instance->count);
        } else {
            display_btn = instance->button; // unknown signature: keep captured
        }
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "SN:%06lX Btn:[%s]\r\n"
        "CRC:%02X [%s] Cnt:%05lX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long long)display_key,
        (unsigned long)instance->serial,
        honda_v2_button_name(display_btn),
        instance->check,
        instance->check_ok ? "OK" : "BAD",
        (unsigned long)instance->count);
}

void* subghz_protocol_encoder_honda_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHondaV2* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderHondaV2));
    furi_check(instance);

    instance->base.protocol = &honda_v2_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 10;
    instance->encoder.size_upload = HONDA_V2_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(HONDA_V2_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.front = 0;
    instance->encoder.is_running = false;

    return instance;
}

void subghz_protocol_encoder_honda_v2_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderHondaV2* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus subghz_protocol_encoder_honda_v2_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderHondaV2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = 10;

    do {
        FuriString* temp_str = furi_string_alloc();
        if(!temp_str) break;

        flipper_format_rewind(flipper_format);
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            furi_string_free(temp_str);
            break;
        }
        if(!furi_string_equal(temp_str, instance->base.protocol->name)) {
            furi_string_free(temp_str);
            break;
        }
        furi_string_free(temp_str);

        SubGhzProtocolStatus load_status = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_honda_v2_const.min_count_bit_for_found);
        if(load_status != SubGhzProtocolStatusOk) {
            break;
        }

        instance->serial = instance->generic.serial & 0xFFFFFFU;
        instance->button = instance->generic.btn;
        instance->count = instance->generic.cnt & 0x1FFU;

        uint8_t key_bytes[8] = {0};
        bool have_key = false;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_hex(flipper_format, "Key", key_bytes, sizeof(key_bytes))) {
            instance->key = honda_v2_bytes_to_u64_be(key_bytes);
            have_key = true;
        }

        if(have_key) {
            honda_v2_parse_key_fields(
                instance->key,
                &instance->command_signature,
                &instance->serial,
                &instance->count,
                &instance->button,
                &instance->check);
        }

        uint32_t u32 = 0;
        bool have_button = false;

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Serial", &u32, 1)) {
            instance->serial = u32 & 0xFFFFFFU;
        }

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Cnt", &u32, 1)) {
            instance->count = u32 & 0x1FFU;
        }

        // [ROLLING_CNT] Forward-encode the NEXT counter (like VAG/PSA) so the
        // transmitter UI shows an incrementing counter on each OK/D-pad press. The
        // Key (and check/tail, which derive from count) is rebuilt from this count
        // below and both Key + Cnt are persisted, so the decoder re-derives the
        // advanced counter on the UI refresh.
        {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            if(mult == 0U) mult = 1U;
            instance->count = (instance->count + mult) & 0x1FFU;
        }

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Btn", &u32, 1)) {
            instance->button = (uint8_t)u32;
            have_button = true;
        }

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, HONDA_V2_FF_BTNSIG, &u32, 1)) {
            instance->command_signature = u32 & 0xFFFFFFU;
        }

        // [PROTOPIRATE_PORT] custom_btn support
        // Honda V2 has only two real buttons:
        //   Up   = 0x02 (Lock)
        //   Down = 0x04 (Unlock)
        //   OK   = original captured button (byte-identical replay)
        //   Left/Right unsupported -> fall through to original.
        {
            const uint8_t original_btn = instance->button;
            if(subghz_custom_btn_get_original() == 0) {
                subghz_custom_btn_set_original(original_btn);
            }
            subghz_custom_btn_set_max(4);
            uint8_t custom_btn_id = subghz_custom_btn_get();
            uint8_t remapped = original_btn;
            switch(custom_btn_id) {
            case SUBGHZ_CUSTOM_BTN_UP:   remapped = HONDA_V2_BTN_LOCK;   break;
            case SUBGHZ_CUSTOM_BTN_OK:   remapped = original_btn;        break;
            case SUBGHZ_CUSTOM_BTN_DOWN: remapped = HONDA_V2_BTN_UNLOCK; break;
            default:                     remapped = original_btn;        break;
            }
            // Only accept the remap if it maps to a known signature; otherwise
            // keep the original captured button so replay stays valid.
            if(honda_v2_signature_from_button(remapped) != 0U) {
                instance->button = remapped;
                have_button = true;
            }
        }

        if(have_button) {
            const uint32_t signature = honda_v2_signature_from_button(instance->button);
            if(signature != 0U) {
                instance->command_signature = signature;
            }
        }

        if(instance->command_signature == 0U) {
            break;
        }

        instance->key = honda_v2_build_key(
            instance->command_signature, instance->serial, instance->count);

        honda_v2_u64_to_bytes_be(instance->key, key_bytes);
        instance->tail = honda_v2_calculate_tail(instance->count);

        honda_v2_parse_key_fields(
            instance->key,
            &instance->command_signature,
            &instance->serial,
            &instance->count,
            &instance->button,
            &instance->check);

        instance->generic.data = instance->key;
        instance->generic.data_count_bit =
            subghz_protocol_honda_v2_const.min_count_bit_for_found;
        instance->generic.serial = instance->serial;
        instance->generic.btn = instance->button;
        instance->generic.cnt = instance->count;

        flipper_format_rewind(flipper_format);
        uint32_t repeat = 10;
        flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
        instance->encoder.repeat = (size_t)repeat;

        if(!honda_v2_build_upload(instance) || instance->encoder.size_upload == 0U) {
            break;
        }

        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_hex(flipper_format, "Key", key_bytes, sizeof(key_bytes));

        uint32_t tmp;
        flipper_format_rewind(flipper_format);
        tmp = instance->serial;
        if(!flipper_format_update_uint32(flipper_format, "Serial", &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Serial", &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->button;
        if(!flipper_format_update_uint32(flipper_format, "Btn", &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Btn", &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->command_signature;
        if(!flipper_format_update_uint32(flipper_format, HONDA_V2_FF_BTNSIG, &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, HONDA_V2_FF_BTNSIG, &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->count;
        if(!flipper_format_update_uint32(flipper_format, "Cnt", &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->check;
        if(!flipper_format_update_uint32(flipper_format, HONDA_V2_FF_CHECK, &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, HONDA_V2_FF_CHECK, &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = instance->tail;
        if(!flipper_format_update_uint32(flipper_format, HONDA_V2_FF_TAIL, &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, HONDA_V2_FF_TAIL, &tmp, 1);
        }
        flipper_format_rewind(flipper_format);
        tmp = 1U;
        if(!flipper_format_update_uint32(flipper_format, HONDA_V2_FF_EXTRA_BIT, &tmp, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, HONDA_V2_FF_EXTRA_BIT, &tmp, 1);
        }

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_honda_v2_stop(void* context) {
    SubGhzProtocolEncoderHondaV2* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_honda_v2_yield(void* context) {
    SubGhzProtocolEncoderHondaV2* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration duration = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        // Endless/breakless TX: while OK is held (endless_tx set by the transmit
        // scene) do not consume repeats, so the signal loops until release.
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }

    return duration;
}

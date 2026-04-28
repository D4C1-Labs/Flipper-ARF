#include "honda_v1.h"
#include <string.h>

#define HONDA_V1_BIT_COUNT          68U
#define HONDA_V1_VALID_BIT_COUNT_MAX 75U
#define HONDA_V1_TE_SHORT           1000U
#define HONDA_V1_TE_LONG            2000U
#define HONDA_V1_TE_DELTA           400U
#define HONDA_V1_TE_SHORT_MIN       600U
#define HONDA_V1_TE_END             3500U

typedef enum {
    HondaV1DecoderStepReset = 0,
    HondaV1DecoderStepPreamble = 1,
    HondaV1DecoderStepData = 2,
} HondaV1DecoderStep;

typedef struct SubGhzProtocolDecoderHondaV1 {
    SubGhzProtocolDecoderBase base;
    uint32_t _reserved_0c;
    uint32_t parser_step;
    uint8_t _reserved_14[0x10];
    SubGhzBlockGeneric generic;
    uint32_t key_2;
    uint16_t packet_bit_count;
    uint8_t _reserved_5a;
    uint8_t _reserved_5b;
    uint32_t pending_duration;
    bool pending_duration_valid;
    uint8_t preamble_count;
    bool data_pending;
    bool last_level;
    uint8_t bits[0x0C];
    uint8_t bit_count;
} SubGhzProtocolDecoderHondaV1;

static const char* const honda_v1_button_names[11] = {
    "Unlock",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Unknown",
    "Lock",
    "Trunk",
    "Panic",
};

static bool honda_v1_button_valid(uint8_t button) {
    return ((0x701U >> button) & 1U) != 0U;
}

static const char* honda_v1_button_name(uint8_t button) {
    if(button < COUNT_OF(honda_v1_button_names)) {
        return honda_v1_button_names[button];
    }

    return "Unknown";
}

static uint64_t honda_v1_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t value = 0;
    for(size_t i = 0; i < 8; i++) {
        value = (value << 8U) | bytes[i];
    }
    return value;
}

static void honda_v1_parse_generic_fields(SubGhzBlockGeneric* generic) {
    const uint32_t low = (uint32_t)(generic->data & 0xFFFFFFFFULL);
    const uint32_t high = (uint32_t)(generic->data >> 32U);

    generic->cnt = high >> 4U;
    generic->btn = (uint8_t)((low >> 28U) & 0x0FU);
    generic->serial = low & 0xFFFFU;
    generic->data_count_bit = HONDA_V1_BIT_COUNT;
}

static void honda_v1_crc_candidates(uint16_t serial, uint8_t* first, uint8_t* second) {
    uint8_t base = (uint8_t)((((~(serial >> 6U)) << 2U) & 0x04U) | ((serial >> 3U) & 0x01U));
    uint8_t value = (uint8_t)((((~(serial >> 4U)) & 0x01U) | (((serial >> 5U) & 0x01U) << 1U)) ^
                              (serial & 0x07U));

    *first = (uint8_t)((value + base) & 0x07U);
    *second = (uint8_t)(((value + (base ^ 0x01U)) & 0x07U) | 0x08U);
}

static void honda_v1_add_bit(SubGhzProtocolDecoderHondaV1* instance, bool bit) {
    if(instance->bit_count > HONDA_V1_VALID_BIT_COUNT_MAX) {
        return;
    }

    if(bit) {
        const uint8_t byte_index = instance->bit_count >> 3U;
        const uint8_t shift = ((uint8_t)~instance->bit_count) & 0x07U;
        instance->bits[byte_index] |= (uint8_t)(1U << shift);
    }

    instance->bit_count++;
}

static void honda_v1_reset_state_(SubGhzProtocolDecoderHondaV1* instance) {
    instance->parser_step = HondaV1DecoderStepReset;
    instance->preamble_count = 0;
    instance->data_pending = 0;
    instance->bit_count = 0;
    memset(instance->bits, 0, sizeof(instance->bits));
}

static bool honda_v1_duration_is(uint32_t duration, uint32_t target) {
    return (duration >= target) ? ((duration - target) <= HONDA_V1_TE_DELTA) :
                                  ((target - duration) <= HONDA_V1_TE_DELTA);
}

static bool honda_v1_decoder_commit(SubGhzProtocolDecoderHondaV1* instance) {
    if(instance->bit_count <= 0x43U) {
        return false;
    }

    uint8_t shift_count = instance->bit_count - HONDA_V1_BIT_COUNT;
    if(shift_count < 1U) {
        shift_count = 1U;
    }

    for(uint8_t shift = 0; shift < shift_count; shift++) {
        for(size_t i = 0; i < (sizeof(instance->bits) - 1U); i++) {
            instance->bits[i] = (uint8_t)((instance->bits[i] << 1U) | (instance->bits[i + 1U] >> 7U));
        }
        instance->bits[sizeof(instance->bits) - 1U] <<= 1U;
    }

    const uint8_t button = instance->bits[4] >> 4U;
    if(!honda_v1_button_valid(button)) {
        return false;
    }

    instance->generic.data = honda_v1_bytes_to_u64_be(instance->bits);
    instance->generic.data_2 = (uint8_t)(instance->bits[8] >> 4U);
    instance->packet_bit_count = HONDA_V1_BIT_COUNT;
    honda_v1_parse_generic_fields(&instance->generic);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }

    return true;
}

static void honda_v1_decoder_process_symbol(
    SubGhzProtocolDecoderHondaV1* instance,
    bool level,
    uint32_t duration) {
    const bool short_symbol = honda_v1_duration_is(duration, HONDA_V1_TE_SHORT);
    const bool long_symbol = honda_v1_duration_is(duration, HONDA_V1_TE_LONG);

    if(!short_symbol && !long_symbol) {
        if(!level && (duration > HONDA_V1_TE_END) &&
           (instance->parser_step == HondaV1DecoderStepData)) {
            honda_v1_decoder_commit(instance);
        }
        honda_v1_reset_state_(instance);
        return;
    }

    if(instance->parser_step == HondaV1DecoderStepReset) {
        if(level) {
            instance->parser_step = HondaV1DecoderStepPreamble;
            instance->preamble_count = 1U;
            instance->last_level = level;
        }
        return;
    }

    if(instance->parser_step == HondaV1DecoderStepPreamble) {
        if(long_symbol) {
            instance->preamble_count++;
            instance->last_level = level;
            return;
        }

        if(short_symbol && (instance->preamble_count > 5U)) {
            instance->parser_step = HondaV1DecoderStepData;
            instance->bit_count = 0U;
            memset(instance->bits, 0, sizeof(instance->bits));
            instance->data_pending = true;
            instance->last_level = level;
            return;
        }

        honda_v1_reset_state_(instance);
        return;
    }

    if(short_symbol) {
        if(instance->data_pending) {
            honda_v1_add_bit(instance, level);
            instance->data_pending = false;
            instance->last_level = level;
            return;
        }

        instance->data_pending = true;
        instance->last_level = level;
    } else {
        if(instance->data_pending) {
            honda_v1_add_bit(instance, level);
        } else {
            honda_v1_add_bit(instance, instance->last_level);
        }

        instance->last_level = level;
    }
}

const SubGhzProtocolDecoder subghz_protocol_honda_v1_decoder = {
    .alloc = subghz_protocol_decoder_honda_v1_alloc,
    .free = subghz_protocol_decoder_honda_v1_free,
    .feed = subghz_protocol_decoder_honda_v1_feed,
    .reset = subghz_protocol_decoder_honda_v1_reset,
    .get_hash_data = subghz_protocol_decoder_honda_v1_get_hash_data,
    .get_string = subghz_protocol_decoder_honda_v1_get_string,
    .serialize = subghz_protocol_decoder_honda_v1_serialize,
    .deserialize = subghz_protocol_decoder_honda_v1_deserialize,
};

const SubGhzProtocolEncoder subghz_protocol_honda_v1_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_honda_v1 = {
    .name = HONDA_V1_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load,
    .encoder = &subghz_protocol_honda_v1_encoder,
    .decoder = &subghz_protocol_honda_v1_decoder,
};

void* subghz_protocol_decoder_honda_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderHondaV1* instance = calloc(1, sizeof(SubGhzProtocolDecoderHondaV1));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_honda_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_honda_v1_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_honda_v1_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV1* instance = context;
    instance->pending_duration = 0;
    instance->pending_duration_valid = false;
    
    honda_v1_reset_state_(instance);
    instance->last_level = false;
}

void subghz_protocol_decoder_honda_v1_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV1* instance = context;

    if(duration < HONDA_V1_TE_DELTA) {
        instance->pending_duration += duration;
        instance->pending_duration_valid = true;
        return;
    }

    if(instance->pending_duration_valid) {
        const uint32_t pending = instance->pending_duration;

        if(level) {
            instance->pending_duration = pending + duration;
            instance->pending_duration_valid = true;
            return;
        }

        if(pending >= HONDA_V1_TE_SHORT_MIN) {
            honda_v1_decoder_process_symbol(instance, true, pending);
        }

        instance->pending_duration = 0;
        instance->pending_duration_valid = false;
    }

    if(level) {
        instance->pending_duration = duration;
        instance->pending_duration_valid = true;
        return;
    }

    honda_v1_decoder_process_symbol(instance, false, duration);
}

uint8_t subghz_protocol_decoder_honda_v1_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV1* instance = context;
    return (uint8_t)(instance->generic.data >> 40U);
}

void subghz_protocol_decoder_honda_v1_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV1* instance = context;
    honda_v1_parse_generic_fields(&instance->generic);

    uint8_t candidate_a = 0;
    uint8_t candidate_b = 0;
    honda_v1_crc_candidates((uint16_t)instance->generic.serial, &candidate_a, &candidate_b);

    const uint8_t key_2 = instance->generic.data_2 & 0x0FU;
    const bool crc_ok = (key_2 == candidate_a) || (key_2 == candidate_b);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Sn:%07lX Btn:%02X [%s]\r\n"
        "Cnt:%04lX CRC:%01X [%s]",
        instance->generic.protocol_name,
        instance->packet_bit_count ? instance->packet_bit_count : HONDA_V1_BIT_COUNT,
        instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        honda_v1_button_name(instance->generic.btn),
        instance->generic.cnt,
        key_2,
        crc_ok ? "OK" : "ERR");
}

SubGhzProtocolStatus subghz_protocol_decoder_honda_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV1* instance = context;
    honda_v1_parse_generic_fields(&instance->generic);

    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t key_2 = instance->generic.data_2 & 0x0FU;
    if(!flipper_format_update_uint32(flipper_format, "Key_2", &key_2, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    return status;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderHondaV1* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t key_2 = 0;
    if(!flipper_format_read_uint32(flipper_format, "Key_2", &key_2, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.data_2 = key_2 & 0x0FU;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->packet_bit_count = instance->generic.data_count_bit;
    honda_v1_parse_generic_fields(&instance->generic);

    return status;
}

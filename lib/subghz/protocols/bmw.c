#include "bmw.h"

#define TAG "SubGhzProtocolBMW_868"

static const SubGhzBlockConst subghz_protocol_bmw_const = {
    .te_short = 350, // BMW 868 MHz
    .te_long = 700, // ~2 × te_short
    .te_delta = 120,
    .min_count_bit_for_found = 61,
};

typedef struct SubGhzProtocolDecoderBMW {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    uint8_t crc_type; // 0 = unknown, 8 = CRC8, 16 = CRC16
} SubGhzProtocolDecoderBMW;

typedef struct SubGhzProtocolEncoderBMW {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolEncoderBMW;

typedef enum {
    BMWDecoderStepReset = 0,
    BMWDecoderStepCheckPreambula,
    BMWDecoderStepSaveDuration,
    BMWDecoderStepCheckDuration,
} BMWDecoderStep;

static void subghz_protocol_decoder_bmw_reset_internal(SubGhzProtocolDecoderBMW* instance) {
    memset(&instance->decoder, 0, sizeof(instance->decoder));
    memset(&instance->generic, 0, sizeof(instance->generic));
    instance->decoder.parser_step = BMWDecoderStepReset;
    instance->header_count = 0;
    instance->crc_type = 0;
}

const SubGhzProtocolDecoder subghz_protocol_bmw_decoder = {
    .alloc = subghz_protocol_decoder_bmw_alloc,
    .free = subghz_protocol_decoder_bmw_free,

    .feed = subghz_protocol_decoder_bmw_feed,
    .reset = subghz_protocol_decoder_bmw_reset,

    .get_hash_data = subghz_protocol_decoder_bmw_get_hash_data,
    .serialize = subghz_protocol_decoder_bmw_serialize,
    .deserialize = subghz_protocol_decoder_bmw_deserialize,
    .get_string = subghz_protocol_decoder_bmw_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_bmw_encoder = {
    .alloc = NULL,
    .free = NULL,

    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_bmw = {
    .name = BMW_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_868 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable,

    .decoder = &subghz_protocol_bmw_decoder,
    .encoder = &subghz_protocol_bmw_encoder,
};

// ----------------- Allocation / Reset / Free -------------------

void* subghz_protocol_decoder_bmw_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderBMW* instance = calloc(1, sizeof(SubGhzProtocolDecoderBMW));
    instance->base.protocol = &subghz_protocol_bmw;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_bmw_reset(instance);
    return instance;
}

void subghz_protocol_decoder_bmw_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_bmw_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderBMW* instance = context;
    subghz_protocol_decoder_bmw_reset_internal(instance);
}

// ----------------- CRC -------------------
// BMW utilise CRC-8 (poly 0x31, init 0x00)

uint8_t subghz_protocol_bmw_crc8(uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(uint8_t j = 0; j < 8; j++) {
            if(crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x31);
            else
                crc <<= 1;
        }
    }
    return crc;
}

// BMW utilise aussi CRC-16 (poly 0x1021, init 0xFFFF)
uint16_t subghz_protocol_bmw_crc16(uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for(uint8_t j = 0; j < 8; j++) {
            if(crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// ----------------- Decoder Feed -------------------

void subghz_protocol_decoder_bmw_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderBMW* instance = context;

    switch(instance->decoder.parser_step) {
    case BMWDecoderStepReset:
        if(level && (DURATION_DIFF(duration, subghz_protocol_bmw_const.te_short) <
                     subghz_protocol_bmw_const.te_delta)) {
            instance->decoder.parser_step = BMWDecoderStepCheckPreambula;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case BMWDecoderStepCheckPreambula:
        if(level) {
            if((DURATION_DIFF(duration, subghz_protocol_bmw_const.te_short) <
                subghz_protocol_bmw_const.te_delta) ||
               (DURATION_DIFF(duration, subghz_protocol_bmw_const.te_long) <
                subghz_protocol_bmw_const.te_delta)) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = BMWDecoderStepReset;
            }
        } else if(
            (DURATION_DIFF(duration, subghz_protocol_bmw_const.te_short) <
             subghz_protocol_bmw_const.te_delta) &&
            (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_bmw_const.te_short) <
             subghz_protocol_bmw_const.te_delta)) {
            instance->header_count++;
        } else if(
            (DURATION_DIFF(duration, subghz_protocol_bmw_const.te_long) <
             subghz_protocol_bmw_const.te_delta) &&
            (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_bmw_const.te_long) <
             subghz_protocol_bmw_const.te_delta)) {
            if(instance->header_count > 15) {
                instance->decoder.parser_step = BMWDecoderStepSaveDuration;
                instance->decoder.decode_data = 0ULL;
                instance->decoder.decode_count_bit = 0;
            } else {
                instance->decoder.parser_step = BMWDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = BMWDecoderStepReset;
        }
        break;

    case BMWDecoderStepSaveDuration:
        if(level) {
            if(duration >=
               (subghz_protocol_bmw_const.te_long + subghz_protocol_bmw_const.te_delta * 2UL)) {
                if(instance->decoder.decode_count_bit >=
                   subghz_protocol_bmw_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    // Perform CRC check with both CRC8 and CRC16
                    uint8_t* raw_bytes = (uint8_t*)&instance->generic.data;
                    size_t raw_len = (instance->generic.data_count_bit + 7) / 8;
                    uint8_t crc8 = subghz_protocol_bmw_crc8(raw_bytes, raw_len - 1);
                    if(crc8 == raw_bytes[raw_len - 1]) {
                        instance->crc_type = 8;
                    } else {
                        uint16_t crc16 = subghz_protocol_bmw_crc16(raw_bytes, raw_len - 2);
                        uint16_t rx_crc16 = (raw_bytes[raw_len - 2] << 8) | raw_bytes[raw_len - 1];
                        if(crc16 == rx_crc16) {
                            instance->crc_type = 16;
                        } else {
                            instance->crc_type = 0; // invalid
                        }
                    }

                    if(instance->crc_type != 0 && instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                subghz_protocol_decoder_bmw_reset_internal(instance);
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = BMWDecoderStepCheckDuration;
            }
        } else {
            instance->decoder.parser_step = BMWDecoderStepReset;
        }
        break;

    case BMWDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_bmw_const.te_short) <
                subghz_protocol_bmw_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_bmw_const.te_short) <
                subghz_protocol_bmw_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = BMWDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_bmw_const.te_long) <
                 subghz_protocol_bmw_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_bmw_const.te_long) <
                 subghz_protocol_bmw_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = BMWDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = BMWDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = BMWDecoderStepReset;
        }
        break;
    }
}

// ----------------- Utils -------------------

static void subghz_protocol_bmw_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (uint32_t)((instance->data >> 12) & 0x0FFFFFFF);
    instance->btn = (instance->data >> 8) & 0x0F;
    instance->cnt = (instance->data >> 40) & 0xFFFF;
}

// ----------------- API -------------------

uint8_t subghz_protocol_decoder_bmw_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderBMW* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_bmw_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderBMW* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderBMW* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_bmw_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_bmw_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderBMW* instance = context;

    subghz_protocol_bmw_check_remote_controller(&instance->generic);
    uint32_t hi = instance->generic.data >> 32;
    uint32_t lo = instance->generic.data & 0xFFFFFFFF;

    furi_string_cat_printf(
        output,
        "%s %dbit (CRC:%d)\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%07lX Btn:%X Cnt:%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->crc_type,
        hi,
        lo,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

#include "keeloq.h"
#include "protocols_common.h"

#define TAG "SubGhzProtocolKeeloq"

static const SubGhzBlockConst subghz_protocol_keeloq_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 180,
    .min_count_bit_for_found = 64,
};

struct SubGhzProtocolDecoderKeeloq {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
};

struct SubGhzProtocolEncoderKeeloq {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    KeeloqDecoderStepReset = 0,
    KeeloqDecoderStepCheckPreambula,
    KeeloqDecoderStepSaveDuration,
    KeeloqDecoderStepCheckDuration,
} KeeloqDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_keeloq_decoder = {
    .alloc = subghz_protocol_decoder_keeloq_alloc,
    .free = pp_decoder_free_default,
    .feed = subghz_protocol_decoder_keeloq_feed,
    .reset = subghz_protocol_decoder_keeloq_reset,
    .get_hash_data = pp_decoder_hash_blocks,
    .serialize = subghz_protocol_decoder_keeloq_serialize,
    .deserialize = subghz_protocol_decoder_keeloq_deserialize,
    .get_string = subghz_protocol_decoder_keeloq_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_keeloq_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_keeloq = {
    .name = SUBGHZ_PROTOCOL_KEELOQ_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,

    .decoder = &subghz_protocol_keeloq_decoder,
    .encoder = &subghz_protocol_keeloq_encoder,
};

static void subghz_protocol_keeloq_parse_key(SubGhzBlockGeneric* instance) {
    uint64_t key =
        subghz_protocol_blocks_reverse_key(instance->data, instance->data_count_bit);
    uint32_t key_fix = (uint32_t)(key >> 32);

    instance->serial = key_fix & 0x0FFFFFFF;
    instance->btn = (uint8_t)(key_fix >> 28);
    instance->cnt = 0;
}

void* subghz_protocol_decoder_keeloq_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKeeloq* instance = malloc(sizeof(SubGhzProtocolDecoderKeeloq));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_keeloq;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_keeloq_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderKeeloq* instance = context;
    instance->decoder.parser_step = KeeloqDecoderStepReset;
    instance->header_count = 0;
}

void subghz_protocol_decoder_keeloq_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderKeeloq* instance = context;

    switch(instance->decoder.parser_step) {
    case KeeloqDecoderStepReset:
        if((level) && DURATION_DIFF(duration, subghz_protocol_keeloq_const.te_short) <
                          subghz_protocol_keeloq_const.te_delta) {
            instance->decoder.parser_step = KeeloqDecoderStepCheckPreambula;
            instance->header_count++;
        }
        break;
    case KeeloqDecoderStepCheckPreambula:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_keeloq_const.te_short) <
                        subghz_protocol_keeloq_const.te_delta)) {
            instance->decoder.parser_step = KeeloqDecoderStepReset;
            break;
        }
        if((instance->header_count > 2) &&
           (DURATION_DIFF(duration, subghz_protocol_keeloq_const.te_short * 10) <
            subghz_protocol_keeloq_const.te_delta * 10)) {
            instance->decoder.parser_step = KeeloqDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            instance->decoder.parser_step = KeeloqDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    case KeeloqDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = KeeloqDecoderStepCheckDuration;
        }
        break;
    case KeeloqDecoderStepCheckDuration:
        if(!level) {
            if(duration >= ((uint32_t)subghz_protocol_keeloq_const.te_short * 2 +
                            subghz_protocol_keeloq_const.te_delta)) {
                instance->decoder.parser_step = KeeloqDecoderStepReset;
                if((instance->decoder.decode_count_bit >=
                    subghz_protocol_keeloq_const.min_count_bit_for_found) &&
                   (instance->decoder.decode_count_bit <=
                    subghz_protocol_keeloq_const.min_count_bit_for_found + 2)) {
                    if(instance->generic.data != instance->decoder.decode_data) {
                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit =
                            subghz_protocol_keeloq_const.min_count_bit_for_found;
                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                    instance->header_count = 0;
                }
                break;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_keeloq_const.te_short) <
                 subghz_protocol_keeloq_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_keeloq_const.te_long) <
                 subghz_protocol_keeloq_const.te_delta * 2)) {
                if(instance->decoder.decode_count_bit <
                   subghz_protocol_keeloq_const.min_count_bit_for_found) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                } else {
                    instance->decoder.decode_count_bit++;
                }
                instance->decoder.parser_step = KeeloqDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_keeloq_const.te_long) <
                 subghz_protocol_keeloq_const.te_delta * 2) &&
                (DURATION_DIFF(duration, subghz_protocol_keeloq_const.te_short) <
                 subghz_protocol_keeloq_const.te_delta)) {
                if(instance->decoder.decode_count_bit <
                   subghz_protocol_keeloq_const.min_count_bit_for_found) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                } else {
                    instance->decoder.decode_count_bit++;
                }
                instance->decoder.parser_step = KeeloqDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = KeeloqDecoderStepReset;
                instance->header_count = 0;
            }
        } else {
            instance->decoder.parser_step = KeeloqDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_keeloq_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderKeeloq* instance = context;
    subghz_protocol_keeloq_parse_key(&instance->generic);

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        if(preset != NULL) {
            if(!flipper_format_insert_or_update_uint32(
                   flipper_format, FF_FREQUENCY, &preset->frequency, 1)) {
                break;
            }

            const char* preset_name = furi_string_get_cstr(preset->name);
            const char* short_preset = pp_get_short_preset_name(preset_name);
            if(!flipper_format_insert_or_update_string_cstr(
                   flipper_format, FF_PRESET, short_preset)) {
                break;
            }
        }

        if(!flipper_format_insert_or_update_string_cstr(
               flipper_format, FF_PROTOCOL, instance->generic.protocol_name)) {
            break;
        }

        uint32_t bits = instance->generic.data_count_bit;
        if(!flipper_format_insert_or_update_uint32(flipper_format, FF_BIT, &bits, 1)) {
            break;
        }

        char key_str[20];
        snprintf(key_str, sizeof(key_str), "%016llX", instance->generic.data);
        if(!flipper_format_insert_or_update_string_cstr(flipper_format, FF_KEY, key_str)) {
            break;
        }

        if(!flipper_format_insert_or_update_uint32(
               flipper_format, FF_SERIAL, &instance->generic.serial, 1)) {
            break;
        }

        uint32_t temp = instance->generic.btn;
        if(!flipper_format_insert_or_update_uint32(flipper_format, FF_BTN, &temp, 1)) {
            break;
        }

        if(!flipper_format_insert_or_update_string_cstr(
               flipper_format, FF_MANUFACTURE, KEELOQ_MF_UNKNOWN)) {
            break;
        }

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_keeloq_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderKeeloq* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);

    do {
        if(pp_verify_protocol_name(flipper_format, instance->base.protocol->name) !=
           SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Missing or wrong Protocol");
            break;
        }

        uint32_t bit_count_temp;
        if(!flipper_format_read_uint32(flipper_format, FF_BIT, &bit_count_temp, 1)) {
            FURI_LOG_E(TAG, "Missing Bit");
            break;
        }

        instance->generic.data_count_bit = subghz_protocol_keeloq_const.min_count_bit_for_found;

        uint64_t key = 0;
        if(!pp_flipper_read_hex_u64(flipper_format, FF_KEY, &key)) {
            FURI_LOG_E(TAG, "Missing Key");
            break;
        }

        instance->generic.data = key;
        if(instance->generic.data == 0) {
            FURI_LOG_E(TAG, "Key is zero after parsing!");
            break;
        }

        if(!flipper_format_read_uint32(flipper_format, FF_SERIAL, &instance->generic.serial, 1)) {
            subghz_protocol_keeloq_parse_key(&instance->generic);
        }

        uint32_t btn_temp;
        if(!flipper_format_read_uint32(flipper_format, FF_BTN, &btn_temp, 1)) {
            subghz_protocol_keeloq_parse_key(&instance->generic);
        } else {
            instance->generic.btn = (uint8_t)btn_temp;
        }

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_decoder_keeloq_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderKeeloq* instance = context;

    subghz_protocol_keeloq_parse_key(&instance->generic);

    uint32_t code_found_hi = (uint32_t)(instance->generic.data >> 32);
    uint32_t code_found_lo = (uint32_t)(instance->generic.data & 0x00000000ffffffff);

    uint64_t code_found_reverse = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);
    uint32_t code_found_reverse_hi = (uint32_t)(code_found_reverse >> 32);
    uint32_t code_found_reverse_lo =
        (uint32_t)(code_found_reverse & 0x00000000ffffffff);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Fix:0x%08lX    Cnt:????\r\n"
        "Hop:0x%08lX    Btn:%01X\r\n"
        "MF:%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        code_found_reverse_hi,
        code_found_reverse_lo,
        instance->generic.btn,
        KEELOQ_MF_UNKNOWN);
}

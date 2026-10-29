#include "jarolift.h"
#include "protocols_common.h"
#include "core/log.h"
#include "keeloq_common.h"

#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include "../helpers/subghz_generic_data2_seed_compat.h"
#include <lib/subghz/blocks/math.h>

#include "../helpers/subghz_custom_btn_i_compat.h"
#include "../helpers/subghz_lib_ext_compat.h"

#define TAG "SubGhzProtocolJarolift"

static const SubGhzBlockConst subghz_protocol_jarolift_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 167,
    .min_count_bit_for_found = 72,
};

struct SubGhzProtocolDecoderJarolift {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzGenericCompat generic;

    uint16_t header_count;
    SubGhzKeystore* keystore;
};

struct SubGhzProtocolEncoderJarolift {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzGenericCompat generic;
    SubGhzKeystore* keystore;
};

typedef enum {
    JaroliftDecoderStepReset = 0,
    JaroliftDecoderStepCheckPreambula,
    JaroliftDecoderStepSaveDuration,
    JaroliftDecoderStepCheckDuration,
} JaroliftDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_jarolift_decoder = {
    .alloc = subghz_protocol_decoder_jarolift_alloc,
    .free = subghz_protocol_decoder_jarolift_free,

    .feed = subghz_protocol_decoder_jarolift_feed,
    .reset = subghz_protocol_decoder_jarolift_reset,

    .get_hash_data = subghz_protocol_decoder_jarolift_get_hash_data,
    .serialize = subghz_protocol_decoder_jarolift_serialize,
    .deserialize = subghz_protocol_decoder_jarolift_deserialize,
    .get_string = subghz_protocol_decoder_jarolift_get_string,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
const SubGhzProtocolEncoder subghz_protocol_jarolift_encoder = {
    .alloc = subghz_protocol_encoder_jarolift_alloc,
    .free = subghz_protocol_encoder_jarolift_free,

    .deserialize = subghz_protocol_encoder_jarolift_deserialize,
    .stop = subghz_protocol_encoder_jarolift_stop,
    .yield = subghz_protocol_encoder_jarolift_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_jarolift_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol subghz_protocol_jarolift = {
    .name = SUBGHZ_PROTOCOL_JAROLIFT_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_jarolift_decoder,
    .encoder = &subghz_protocol_jarolift_encoder,
};

static void subghz_protocol_jarolift_remote_controller(
    SubGhzGenericCompat* instance,
    SubGhzKeystore* keystore);

#if SUBGHZ_GARAGE_WITH_ENCODER
static uint8_t subghz_protocol_jarolift_get_btn_code(void);

void* subghz_protocol_encoder_jarolift_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolEncoderJarolift* instance = malloc(sizeof(SubGhzProtocolEncoderJarolift));

    instance->base.protocol = &subghz_protocol_jarolift;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 256;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;

    return instance;
}

void subghz_protocol_encoder_jarolift_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderJarolift* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_jarolift_stop(void* context) {
    SubGhzProtocolEncoderJarolift* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_jarolift_yield(void* context) {
    SubGhzProtocolEncoderJarolift* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
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

static bool
    subghz_protocol_jarolift_gen_data(SubGhzProtocolEncoderJarolift* instance, uint8_t btn) {
    if(subghz_custom_btn_get_original() == 0) {
    }

    btn = subghz_protocol_jarolift_get_btn_code();

    if(subghz_garage_button_override_get(&btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);

    if(subghz_garage_get_rolling_counter_mult() != -0x7FFFFFFF) {
        if(!subghz_garage_counter_override_get(&instance->generic.cnt)) {
            if((instance->generic.cnt + subghz_garage_get_rolling_counter_mult()) > 0xFFFF) {
                instance->generic.cnt = 0;
            } else {
                instance->generic.cnt += subghz_garage_get_rolling_counter_mult();
            }
        }
    } else {
        if((instance->generic.cnt + 0x1) > 0xFFFF) {
            instance->generic.cnt = 0;
        } else if(instance->generic.cnt >= 0x1 && instance->generic.cnt != 0xFFFE) {
            instance->generic.cnt = 0xFFFE;
        } else {
            instance->generic.cnt++;
        }
    }

    uint32_t hop_decrypted = (uint64_t)((instance->generic.seed >> 8) & 0xFF) << 24 |
                             ((instance->generic.serial & 0xFF) << 16) |
                             (instance->generic.cnt & 0xFFFF);

    uint64_t hop_encrypted = 0;
    for
        M_EACH(manufacture_code, *subghz_keystore_get_data(instance->keystore), SubGhzKeyArray_t) {
            if(manufacture_code->type == KEELOQ_LEARNING_NORMAL_JAROLIFT) {
                uint64_t man = subghz_protocol_keeloq_common_normal_learning(
                    instance->generic.serial, manufacture_code->key);
                hop_encrypted = subghz_protocol_keeloq_common_encrypt(hop_decrypted, man);
                break;
            }
        }

    if(hop_encrypted == 0) {
        return false;
    }
    uint64_t fix = (uint64_t)btn << 60 | ((uint64_t)(instance->generic.serial & 0xFFFFFFF) << 32) |
                   hop_encrypted;

    instance->generic.data = subghz_protocol_blocks_reverse_key(fix, 64);

    instance->generic.data_2 =
        subghz_protocol_blocks_reverse_key((instance->generic.seed & 0xFF), 8);

    return true;
}

bool subghz_protocol_jarolift_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint16_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolEncoderJarolift* instance = context;
    instance->generic.serial = (serial & 0xFFFFF00);
    instance->generic.cnt = cnt;
    instance->generic.btn = btn;
    instance->generic.seed = 0x0100;
    instance->generic.data_count_bit = 72;

    uint32_t hop_decrypted = (uint64_t)((instance->generic.seed >> 8) & 0xFF) << 24 |
                             ((instance->generic.serial & 0xFF) << 16) |
                             (instance->generic.cnt & 0xFFFF);

    uint64_t hop_encrypted = 0;
    for
        M_EACH(manufacture_code, *subghz_keystore_get_data(instance->keystore), SubGhzKeyArray_t) {
            if(manufacture_code->type == KEELOQ_LEARNING_NORMAL_JAROLIFT) {
                uint64_t man = subghz_protocol_keeloq_common_normal_learning(
                    instance->generic.serial, manufacture_code->key);
                hop_encrypted = subghz_protocol_keeloq_common_encrypt(hop_decrypted, man);
                break;
            }
        }

    uint64_t fix = (uint64_t)instance->generic.btn << 60 |
                   ((uint64_t)(instance->generic.serial & 0xFFFFFFF) << 32) | hop_encrypted;

    instance->generic.data = subghz_protocol_blocks_reverse_key(fix, 64);

    instance->generic.data_2 =
        subghz_protocol_blocks_reverse_key((instance->generic.seed & 0xFF), 8);

    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(
            (SubGhzBlockGeneric*)&instance->generic, flipper_format, preset);

    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> (i * 8)) & 0xFF;
    }

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        res = SubGhzProtocolStatusErrorParserOthers;
    }

    if((res == SubGhzProtocolStatusOk) &&
       !flipper_format_insert_or_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Unable to add Data2");
        res = SubGhzProtocolStatusErrorParserOthers;
    }

    return res == SubGhzProtocolStatusOk;
}

static bool subghz_protocol_encoder_jarolift_get_upload(
    SubGhzProtocolEncoderJarolift* instance,
    uint8_t btn) {
    furi_assert(instance);

    if(!subghz_protocol_jarolift_gen_data(instance, btn)) {
        return false;
    }

    size_t index = 0;

    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_jarolift_const.te_long * 18);

    instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)1500);
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_jarolift_const.te_short);

    for(uint8_t i = 8; i > 0; i--) {
        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_jarolift_const.te_short);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_jarolift_const.te_short);
    }

    instance->encoder.upload[index - 1].duration = (uint32_t)3800;

    for(uint8_t i = 64; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_jarolift_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_jarolift_const.te_long);
        } else {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_jarolift_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_jarolift_const.te_short);
        }
    }

    for(uint8_t i = 8; i > 0; i--) {
        if(bit_read(instance->generic.data_2, i - 1)) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_jarolift_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_jarolift_const.te_long);
        } else {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_jarolift_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_jarolift_const.te_short);
        }
    }

    instance->encoder.size_upload = index;

    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_jarolift_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderJarolift* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(
               (SubGhzBlockGeneric*)&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Data");
            break;
        }

        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = instance->generic.data_2 << 8 | key_data[i];
        }

        subghz_protocol_jarolift_remote_controller(&instance->generic, instance->keystore);

        subghz_protocol_encoder_jarolift_get_upload(instance, instance->generic.btn);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Key");
            break;
        }

        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Data");
            break;
        }

        instance->encoder.is_running = true;

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}
#endif

void* subghz_protocol_decoder_jarolift_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderJarolift* instance = malloc(sizeof(SubGhzProtocolDecoderJarolift));
    instance->base.protocol = &subghz_protocol_jarolift;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    return instance;
}

void subghz_protocol_decoder_jarolift_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    free(instance);
}

void subghz_protocol_decoder_jarolift_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    instance->decoder.parser_step = JaroliftDecoderStepReset;
}

void subghz_protocol_decoder_jarolift_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;

    switch(instance->decoder.parser_step) {
    case JaroliftDecoderStepReset:
        if((level) && DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_short) <
                          subghz_protocol_jarolift_const.te_delta) {
            instance->decoder.parser_step = JaroliftDecoderStepCheckPreambula;
            instance->header_count++;
        }
        break;
    case JaroliftDecoderStepCheckPreambula:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_short) <
                        subghz_protocol_jarolift_const.te_delta)) {
            instance->decoder.parser_step = JaroliftDecoderStepReset;
            break;
        }
        if((!level) && (instance->header_count == 8) &&
           (DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_long * 5) <
            subghz_protocol_jarolift_const.te_delta * 6)) {
            instance->decoder.parser_step = JaroliftDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->header_count = 0;
            break;
        } else {
            instance->decoder.parser_step = JaroliftDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    case JaroliftDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = JaroliftDecoderStepCheckDuration;
        } else {
            instance->header_count = 0;
            instance->decoder.parser_step = JaroliftDecoderStepReset;
        }
        break;
    case JaroliftDecoderStepCheckDuration:
        if(!level) {
            if(instance->decoder.decode_count_bit == 64) {
                instance->generic.data = instance->decoder.decode_data;
                instance->decoder.decode_data = 0;
            }
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_jarolift_const.te_short) <
                subghz_protocol_jarolift_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_long) <
                subghz_protocol_jarolift_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = JaroliftDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_jarolift_const.te_long) <
                 subghz_protocol_jarolift_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_jarolift_const.te_short) <
                 subghz_protocol_jarolift_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = JaroliftDecoderStepSaveDuration;
            } else {
                if(duration >= ((uint32_t)subghz_protocol_jarolift_const.te_long * 3)) {
                    if((DURATION_DIFF(
                            instance->decoder.te_last, subghz_protocol_jarolift_const.te_long) <
                        subghz_protocol_jarolift_const.te_delta)) {
                        subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                    } else if((DURATION_DIFF(
                                   instance->decoder.te_last,
                                   subghz_protocol_jarolift_const.te_short) <
                               subghz_protocol_jarolift_const.te_delta)) {
                        subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                    }
                    if(instance->decoder.decode_count_bit ==
                       subghz_protocol_jarolift_const.min_count_bit_for_found) {
                        instance->generic.data_2 = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }

                    instance->decoder.parser_step = JaroliftDecoderStepReset;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                    instance->header_count = 0;
                    break;
                }
                instance->decoder.parser_step = JaroliftDecoderStepReset;
                instance->header_count = 0;
            }
        } else {
            instance->decoder.parser_step = JaroliftDecoderStepReset;
            instance->header_count = 0;
        }
        break;
    }
}

static const char* subghz_protocol_jarolift_get_button_name(uint8_t btn) {
    const char* btn_name;
    switch(btn) {
    case 0x1:
        btn_name = "Learn";
        break;
    case 0x2:
        btn_name = "Down";
        break;
    case 0x4:
        btn_name = "Stop";
        break;
    case 0x8:
        btn_name = "Up";
        break;
    default:
        btn_name = "Unkn";
        break;
    }
    return btn_name;
}

static void subghz_protocol_jarolift_remote_controller(
    SubGhzGenericCompat* instance,
    SubGhzKeystore* keystore) {
    uint32_t group = subghz_protocol_blocks_reverse_key(instance->data_2, 8);
    uint64_t key = subghz_protocol_blocks_reverse_key(instance->data, 64);
    bool ret = false;
    uint32_t decrypt = 0;
    instance->serial = (key >> 32) & 0xFFFFFFF;
    uint32_t hop = key & 0xFFFFFFFF;

    for
        M_EACH(manufacture_code, *subghz_keystore_get_data(keystore), SubGhzKeyArray_t) {
            if(manufacture_code->type == KEELOQ_LEARNING_NORMAL_JAROLIFT) {
                uint64_t man = subghz_protocol_keeloq_common_normal_learning(
                    instance->serial, manufacture_code->key);
                decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                if(((decrypt >> 16) & 0xFF) == (instance->serial & 0xFF)) {
                    ret = true;
                }
                break;
            }
        }
    if(ret) {
        instance->btn = (key >> 60) & 0xF;
        instance->seed = ((decrypt >> 24) << 8) | (group >> 8);
        instance->cnt = decrypt & 0xFFFF;

        if(subghz_custom_btn_get_original() == 0) {
        }
    } else {
        instance->btn = 0;
        instance->serial = 0;
        instance->cnt = 0;
        instance->seed = 0;
    }
}

uint8_t subghz_protocol_decoder_jarolift_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    uint8_t hash = 0;
    uint8_t* p = (uint8_t*)&instance->generic.data;
    for(size_t i = 0; i < 16; i++) {
        hash ^= p[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_jarolift_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(
            (SubGhzBlockGeneric*)&instance->generic, flipper_format, preset);

    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> (i * 8)) & 0xFF;
    }

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }

    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_insert_or_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Unable to add Data");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_jarolift_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            (SubGhzBlockGeneric*)&instance->generic,
            flipper_format,
            subghz_protocol_jarolift_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Data");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = instance->generic.data_2 << 8 | key_data[i];
        }
    } while(false);
    return ret;
}

#if SUBGHZ_GARAGE_WITH_ENCODER
static uint8_t subghz_protocol_jarolift_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {
        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x2;
            break;
        case 0x2:
            btn = 0x1;
            break;
        case 0x4:
            btn = 0x1;
            break;
        case 0x8:
            btn = 0x1;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x4;
            break;
        case 0x2:
            btn = 0x4;
            break;
        case 0x4:
            btn = 0x2;
            break;
        case 0x8:
            btn = 0x4;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0x8;
            break;
        case 0x2:
            btn = 0x8;
            break;
        case 0x4:
            btn = 0x8;
            break;
        case 0x8:
            btn = 0x2;
            break;

        default:
            break;
        }
    }

    return btn;
}
#endif

void subghz_protocol_decoder_jarolift_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderJarolift* instance = context;
    subghz_protocol_jarolift_remote_controller(&instance->generic, instance->keystore);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 16;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%0llX\r\n"
        "Sn:%07lX  Btn:%01X - %s\r\n"
        "Cnt:%04lX Group:%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        subghz_protocol_jarolift_get_button_name(instance->generic.btn),
        instance->generic.cnt,
        instance->generic.seed);
}

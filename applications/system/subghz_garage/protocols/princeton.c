#include "princeton.h"
#include "protocols_common.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include <lib/subghz/blocks/math.h>

#include "../helpers/subghz_custom_btn_i_compat.h"

#define TAG "SubGhzProtocolPrinceton"

#define PRINCETON_GUARD_TIME_DEFALUT 30

static const SubGhzBlockConst subghz_protocol_princeton_const = {
    .te_short = 390,
    .te_long = 1170,
    .te_delta = 300,
    .min_count_bit_for_found = 24,
};

struct SubGhzProtocolDecoderPrinceton {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t last_data;
    uint32_t guard_time;
};

struct SubGhzProtocolEncoderPrinceton {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t te;
    uint32_t guard_time;
};

typedef enum {
    PrincetonDecoderStepReset = 0,
    PrincetonDecoderStepSaveDuration,
    PrincetonDecoderStepCheckDuration,
} PrincetonDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_princeton_decoder = {
    .alloc = subghz_protocol_decoder_princeton_alloc,
    .free = subghz_protocol_decoder_princeton_free,

    .feed = subghz_protocol_decoder_princeton_feed,
    .reset = subghz_protocol_decoder_princeton_reset,

    .get_hash_data = subghz_protocol_decoder_princeton_get_hash_data,
    .serialize = subghz_protocol_decoder_princeton_serialize,
    .deserialize = subghz_protocol_decoder_princeton_deserialize,
    .get_string = subghz_protocol_decoder_princeton_get_string,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
const SubGhzProtocolEncoder subghz_protocol_princeton_encoder = {
    .alloc = subghz_protocol_encoder_princeton_alloc,
    .free = subghz_protocol_encoder_princeton_free,

    .deserialize = subghz_protocol_encoder_princeton_deserialize,
    .stop = subghz_protocol_encoder_princeton_stop,
    .yield = subghz_protocol_encoder_princeton_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_princeton_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol subghz_protocol_princeton = {
    .name = SUBGHZ_PROTOCOL_PRINCETON_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_315 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_princeton_decoder,
    .encoder = &subghz_protocol_princeton_encoder,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
void* subghz_protocol_encoder_princeton_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderPrinceton* instance = malloc(sizeof(SubGhzProtocolEncoderPrinceton));

    instance->base.protocol = &subghz_protocol_princeton;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 52;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_princeton_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPrinceton* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static uint8_t subghz_protocol_princeton_get_btn_code(void) {
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
            btn = 0x2;
            break;
        case 0x8:
            btn = 0x2;
            break;
        case 0xF:
            btn = 0x2;
            break;

        case 0x30:
            btn = 0xC0;
            break;
        case 0xC0:
            btn = 0x30;
            break;
        case 0xF3:
            btn = 0xC0;
            break;
        case 0xFC:
            btn = 0xC0;
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
            btn = 0x1;
            break;
        case 0x8:
            btn = 0x1;
            break;
        case 0xF:
            btn = 0x1;
            break;

        case 0x30:
            btn = 0xF3;
            break;
        case 0xC0:
            btn = 0xF3;
            break;
        case 0xF3:
            btn = 0x30;
            break;
        case 0xFC:
            btn = 0xF3;
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
            btn = 0x4;
            break;
        case 0xF:
            btn = 0x4;
            break;

        case 0x30:
            btn = 0xFC;
            break;
        case 0xC0:
            btn = 0xFC;
            break;
        case 0xF3:
            btn = 0xFC;
            break;
        case 0xFC:
            btn = 0x30;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_RIGHT) {
        switch(original_btn_code) {
        case 0x1:
            btn = 0xF;
            break;
        case 0x2:
            btn = 0xF;
            break;
        case 0x4:
            btn = 0xF;
            break;
        case 0x8:
            btn = 0xF;
            break;
        case 0xF:
            btn = 0x8;
            break;

        default:
            break;
        }
    }

    return btn;
}

static bool
    subghz_protocol_encoder_princeton_get_upload(SubGhzProtocolEncoderPrinceton* instance) {
    furi_assert(instance);

    instance->generic.btn = subghz_protocol_princeton_get_btn_code();

    if(subghz_garage_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    if(instance->generic.btn == 0x30 || instance->generic.btn == 0xC0) {
        instance->generic.data =
            ((uint64_t)instance->generic.serial << 8 | (uint64_t)instance->generic.btn);
    } else if(instance->generic.btn == 0xF3 || instance->generic.btn == 0xFC) {
        instance->generic.data =
            ((uint64_t)instance->generic.serial << 8 | (uint64_t)(instance->generic.btn & 0xF));
    } else {
        instance->generic.data =
            ((uint64_t)instance->generic.serial << 4 | (uint64_t)instance->generic.btn);
    }

    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2) + 2;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)instance->te * 3);
            instance->encoder.upload[index++] = level_duration_make(false, (uint32_t)instance->te);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)instance->te);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)instance->te * 3);
        }
    }

    instance->encoder.upload[index++] = level_duration_make(true, (uint32_t)instance->te);

    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)instance->te * instance->guard_time);

    return true;
}

#endif

static void subghz_protocol_princeton_check_remote_controller(SubGhzBlockGeneric* instance) {
    if((instance->data & 0xFF) == 0x30 || (instance->data & 0xFF) == 0xC0) {
        instance->serial = instance->data >> 8;
        instance->btn = instance->data & 0xFF;
    } else if((instance->data & 0xFF) == 0x03 || (instance->data & 0xFF) == 0x0C) {
        instance->serial = instance->data >> 8;
        instance->btn = (instance->data & 0xFF) | 0xF0;
    } else {
        instance->serial = instance->data >> 4;
        instance->btn = instance->data & 0xF;
    }

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(4);
}

#if SUBGHZ_GARAGE_WITH_ENCODER
SubGhzProtocolStatus
    subghz_protocol_encoder_princeton_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderPrinceton* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_princeton_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "TE", (uint32_t*)&instance->te, 1)) {
            FURI_LOG_E(TAG, "Missing TE");
            ret = SubGhzProtocolStatusErrorParserTe;
            break;
        }

        if(!flipper_format_read_uint32(
               flipper_format, "Guard_time", (uint32_t*)&instance->guard_time, 1)) {
            instance->guard_time = PRINCETON_GUARD_TIME_DEFALUT;
        } else {
            if((instance->guard_time < 15) || (instance->guard_time > 72)) {
                instance->guard_time = PRINCETON_GUARD_TIME_DEFALUT;
            }
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_princeton_check_remote_controller(&instance->generic);

        if(!subghz_protocol_encoder_princeton_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_princeton_stop(void* context) {
    SubGhzProtocolEncoderPrinceton* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_princeton_yield(void* context) {
    SubGhzProtocolEncoderPrinceton* instance = context;

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
#endif

void* subghz_protocol_decoder_princeton_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPrinceton* instance = malloc(sizeof(SubGhzProtocolDecoderPrinceton));
    instance->base.protocol = &subghz_protocol_princeton;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_princeton_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPrinceton* instance = context;
    free(instance);
}

void subghz_protocol_decoder_princeton_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPrinceton* instance = context;
    instance->decoder.parser_step = PrincetonDecoderStepReset;
    instance->last_data = 0;
}

void subghz_protocol_decoder_princeton_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPrinceton* instance = context;

    switch(instance->decoder.parser_step) {
    case PrincetonDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_princeton_const.te_short * 36) <
                        subghz_protocol_princeton_const.te_delta * 36)) {
            instance->decoder.parser_step = PrincetonDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->te = 0;
            instance->guard_time = PRINCETON_GUARD_TIME_DEFALUT;
        }
        break;
    case PrincetonDecoderStepSaveDuration:

        if(level) {
            instance->decoder.te_last = duration;
            instance->te += duration;
            instance->decoder.parser_step = PrincetonDecoderStepCheckDuration;
        }
        break;
    case PrincetonDecoderStepCheckDuration:
        if(!level) {
            if(duration >= ((uint32_t)subghz_protocol_princeton_const.te_long * 2)) {
                instance->decoder.parser_step = PrincetonDecoderStepSaveDuration;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_princeton_const.min_count_bit_for_found) {
                    if((instance->last_data == instance->decoder.decode_data) &&
                       instance->last_data) {
                        instance->te /= (instance->decoder.decode_count_bit * 4 + 1);

                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                        instance->guard_time = roundf((float)duration / instance->te);

                        if((instance->guard_time < 15) || (instance->guard_time > 72)) {
                            instance->guard_time = PRINCETON_GUARD_TIME_DEFALUT;
                        }

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                    instance->last_data = instance->decoder.decode_data;
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->te = 0;
                break;
            }

            instance->te += duration;

            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_princeton_const.te_short) <
                subghz_protocol_princeton_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_princeton_const.te_long) <
                subghz_protocol_princeton_const.te_delta * 3)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = PrincetonDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_princeton_const.te_long) <
                 subghz_protocol_princeton_const.te_delta * 3) &&
                (DURATION_DIFF(duration, subghz_protocol_princeton_const.te_short) <
                 subghz_protocol_princeton_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = PrincetonDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = PrincetonDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = PrincetonDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_princeton_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPrinceton* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_princeton_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderPrinceton* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_write_uint32(flipper_format, "TE", &instance->te, 1)) {
        FURI_LOG_E(TAG, "Unable to add TE");
        ret = SubGhzProtocolStatusErrorParserTe;
    }
    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_write_uint32(flipper_format, "Guard_time", &instance->guard_time, 1)) {
        FURI_LOG_E(TAG, "Unable to add Guard_time");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_princeton_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderPrinceton* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_princeton_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!flipper_format_read_uint32(flipper_format, "TE", (uint32_t*)&instance->te, 1)) {
            FURI_LOG_E(TAG, "Missing TE");
            ret = SubGhzProtocolStatusErrorParserTe;
            break;
        }
        if(!flipper_format_read_uint32(
               flipper_format, "Guard_time", (uint32_t*)&instance->guard_time, 1)) {
            instance->guard_time = PRINCETON_GUARD_TIME_DEFALUT;
        } else {
            if((instance->guard_time < 15) || (instance->guard_time > 72)) {
                instance->guard_time = PRINCETON_GUARD_TIME_DEFALUT;
            }
        }
    } while(false);

    return ret;
}

void subghz_protocol_decoder_princeton_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderPrinceton* instance = context;
    subghz_protocol_princeton_check_remote_controller(&instance->generic);
    uint32_t data_rev = subghz_protocol_blocks_reverse_key(
        instance->generic.data, instance->generic.data_count_bit);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;

    if(instance->generic.btn == 0x30 || instance->generic.btn == 0xC0 ||
       instance->generic.btn == 0xF3 || instance->generic.btn == 0xFC) {
        subghz_block_generic_global.btn_length_bit = 8;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:0x%08lX\r\n"
            "Yek:0x%08lX\r\n"
            "Sn:0x%05lX Btn:%02X (8b)\r\n"
            "Te:%luus  GT:Te*%lu\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            (uint32_t)(instance->generic.data & 0xFFFFFF),
            data_rev,
            instance->generic.serial,
            (instance->generic.btn == 0xF3 || instance->generic.btn == 0xFC) ?
                instance->generic.btn & 0xF :
                instance->generic.btn,
            instance->te,
            instance->guard_time);
    } else {
        subghz_block_generic_global.btn_length_bit = 4;
        furi_string_cat_printf(
            output,
            "%s %dbit\r\n"
            "Key:0x%08lX\r\n"
            "Yek:0x%08lX\r\n"
            "Sn:0x%05lX Btn:%01X (4b)\r\n"
            "Te:%luus  GT:Te*%lu\r\n",
            instance->generic.protocol_name,
            instance->generic.data_count_bit,
            (uint32_t)(instance->generic.data & 0xFFFFFF),
            data_rev,
            instance->generic.serial,
            instance->generic.btn,
            instance->te,
            instance->guard_time);
    }
}

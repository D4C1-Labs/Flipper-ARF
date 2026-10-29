#include "roger.h"
#include "protocols_common.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include <lib/subghz/blocks/math.h>

#include "../helpers/subghz_custom_btn_i_compat.h"

#define TAG "SubGhzProtocolRoger"

static const SubGhzBlockConst subghz_protocol_roger_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 270,
    .min_count_bit_for_found = 28,
};

struct SubGhzProtocolDecoderRoger {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderRoger {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    RogerDecoderStepReset = 0,
    RogerDecoderStepSaveDuration,
    RogerDecoderStepCheckDuration,
} RogerDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_roger_decoder = {
    .alloc = subghz_protocol_decoder_roger_alloc,
    .free = subghz_protocol_decoder_roger_free,

    .feed = subghz_protocol_decoder_roger_feed,
    .reset = subghz_protocol_decoder_roger_reset,

    .get_hash_data = subghz_protocol_decoder_roger_get_hash_data,
    .serialize = subghz_protocol_decoder_roger_serialize,
    .deserialize = subghz_protocol_decoder_roger_deserialize,
    .get_string = subghz_protocol_decoder_roger_get_string,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
const SubGhzProtocolEncoder subghz_protocol_roger_encoder = {
    .alloc = subghz_protocol_encoder_roger_alloc,
    .free = subghz_protocol_encoder_roger_free,

    .deserialize = subghz_protocol_encoder_roger_deserialize,
    .stop = subghz_protocol_encoder_roger_stop,
    .yield = subghz_protocol_encoder_roger_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_roger_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol subghz_protocol_roger = {
    .name = SUBGHZ_PROTOCOL_ROGER_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_roger_decoder,
    .encoder = &subghz_protocol_roger_encoder,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
void* subghz_protocol_encoder_roger_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderRoger* instance = malloc(sizeof(SubGhzProtocolEncoderRoger));

    instance->base.protocol = &subghz_protocol_roger;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 128;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_roger_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderRoger* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static uint8_t subghz_protocol_roger_get_btn_code(void) {
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

static void subghz_protocol_encoder_roger_get_upload(SubGhzProtocolEncoderRoger* instance) {
    furi_assert(instance);
    size_t index = 0;

    uint8_t btn = instance->generic.btn;

    btn = subghz_protocol_roger_get_btn_code();

    if(subghz_garage_button_override_get(&btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", btn);

    if((instance->generic.data & 0xFF) == instance->generic.btn) {
        instance->generic.data = (uint64_t)instance->generic.serial << 12 | ((uint64_t)btn << 8) |
                                 btn;
    } else if(((instance->generic.data & 0xFF) == 0x23) && btn == 0x1) {
        instance->generic.data = (uint64_t)instance->generic.serial << 12 | ((uint64_t)btn << 8) |
                                 0x20;
    } else if(((instance->generic.data & 0xFF) == 0x20) && btn == 0x2) {
        instance->generic.data = (uint64_t)instance->generic.serial << 12 | ((uint64_t)btn << 8) |
                                 0x23;
    }

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_roger_const.te_long);
            if(i == 1) {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_roger_const.te_short * 19);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_roger_const.te_short);
            }
        } else {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_roger_const.te_short);
            if(i == 1) {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_roger_const.te_short * 19);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_roger_const.te_long);
            }
        }
    }

    instance->encoder.size_upload = index;
    return;
}
#endif

static void subghz_protocol_roger_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = instance->data >> 12;
    instance->btn = (instance->data >> 8) & 0xF;

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->btn);
    }
    subghz_custom_btn_set_max(3);
}

#if SUBGHZ_GARAGE_WITH_ENCODER
SubGhzProtocolStatus
    subghz_protocol_encoder_roger_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderRoger* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_roger_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_roger_check_remote_controller(&instance->generic);
        subghz_protocol_encoder_roger_get_upload(instance);

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

void subghz_protocol_encoder_roger_stop(void* context) {
    SubGhzProtocolEncoderRoger* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_roger_yield(void* context) {
    SubGhzProtocolEncoderRoger* instance = context;

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

void* subghz_protocol_decoder_roger_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderRoger* instance = malloc(sizeof(SubGhzProtocolDecoderRoger));
    instance->base.protocol = &subghz_protocol_roger;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_roger_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRoger* instance = context;
    free(instance);
}

void subghz_protocol_decoder_roger_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRoger* instance = context;
    instance->decoder.parser_step = RogerDecoderStepReset;
}

void subghz_protocol_decoder_roger_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderRoger* instance = context;

    switch(instance->decoder.parser_step) {
    case RogerDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_roger_const.te_short * 19) <
                        subghz_protocol_roger_const.te_delta * 5)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = RogerDecoderStepSaveDuration;
        }
        break;
    case RogerDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = RogerDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = RogerDecoderStepReset;
        }
        break;
    case RogerDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_roger_const.te_long) <
                subghz_protocol_roger_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_roger_const.te_short) <
                subghz_protocol_roger_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = RogerDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_roger_const.te_short) <
                 subghz_protocol_roger_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_roger_const.te_long) <
                 subghz_protocol_roger_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = RogerDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_roger_const.te_short * 19) <
                subghz_protocol_roger_const.te_delta * 5) {
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_roger_const.te_long) <
                    subghz_protocol_roger_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_roger_const.te_short) <
                    subghz_protocol_roger_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_roger_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = RogerDecoderStepReset;
            } else {
                instance->decoder.parser_step = RogerDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = RogerDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_roger_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRoger* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_roger_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderRoger* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_roger_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderRoger* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_roger_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_roger_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderRoger* instance = context;

    subghz_protocol_roger_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%07lX\r\n"
        "Serial: 0x%04lX\r\n"
        "End: 0x%02lX\r\n"
        "Btn: %01X",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFFF),
        instance->generic.serial,
        (uint32_t)(instance->generic.data & 0xFF),
        instance->generic.btn);
}

#include "hollarm.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include <lib/subghz/blocks/math.h>

#include "../helpers/subghz_custom_btn_i_compat.h"

#define TAG "SubGhzProtocolHollarm"

static const SubGhzBlockConst subghz_protocol_hollarm_const = {
    .te_short = 200,
    .te_long = 1000,
    .te_delta = 200,
    .min_count_bit_for_found = 42,
};

struct SubGhzProtocolDecoderHollarm {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderHollarm {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    HollarmDecoderStepReset = 0,
    HollarmDecoderStepSaveDuration,
    HollarmDecoderStepCheckDuration,
} HollarmDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_hollarm_decoder = {
    .alloc = subghz_protocol_decoder_hollarm_alloc,
    .free = subghz_protocol_decoder_hollarm_free,

    .feed = subghz_protocol_decoder_hollarm_feed,
    .reset = subghz_protocol_decoder_hollarm_reset,

    .get_hash_data = subghz_protocol_decoder_hollarm_get_hash_data,
    .serialize = subghz_protocol_decoder_hollarm_serialize,
    .deserialize = subghz_protocol_decoder_hollarm_deserialize,
    .get_string = subghz_protocol_decoder_hollarm_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_hollarm_encoder = {
    .alloc = subghz_protocol_encoder_hollarm_alloc,
    .free = subghz_protocol_encoder_hollarm_free,

    .deserialize = subghz_protocol_encoder_hollarm_deserialize,
    .stop = subghz_protocol_encoder_hollarm_stop,
    .yield = subghz_protocol_encoder_hollarm_yield,
};

const SubGhzProtocol subghz_protocol_hollarm = {
    .name = SUBGHZ_PROTOCOL_HOLLARM_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_hollarm_decoder,
    .encoder = &subghz_protocol_hollarm_encoder,
};

void* subghz_protocol_encoder_hollarm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHollarm* instance = malloc(sizeof(SubGhzProtocolEncoderHollarm));

    instance->base.protocol = &subghz_protocol_hollarm;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 128;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_hollarm_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderHollarm* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static uint8_t subghz_protocol_hollarm_get_btn_code(void) {
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

static void subghz_protocol_encoder_hollarm_get_upload(SubGhzProtocolEncoderHollarm* instance) {
    furi_assert(instance);

    instance->generic.btn = subghz_protocol_hollarm_get_btn_code();

    if(subghz_garage_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    uint64_t new_key = (instance->generic.data >> 12) << 12 | (instance->generic.btn << 8);

    uint8_t bytesum = ((new_key >> 32) & 0xFF) + ((new_key >> 24) & 0xFF) +
                      ((new_key >> 16) & 0xFF) + ((new_key >> 8) & 0xFF);

    instance->generic.data = (new_key | bytesum);

    size_t index = 0;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read((instance->generic.data << 2), i - 1)) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_hollarm_const.te_short);
            if(i == 1) {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_hollarm_const.te_short * 12);
            } else {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_hollarm_const.te_short * 8);
            }
        } else {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_hollarm_const.te_short);
            if(i == 1) {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_hollarm_const.te_short * 12);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_hollarm_const.te_long);
            }
        }
    }

    instance->encoder.size_upload = index;
    return;
}

static void subghz_protocol_hollarm_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = (instance->data >> 8) & 0xF;
    instance->serial = (instance->data & 0xFFFFFFF0000) >> 16;

    if(subghz_custom_btn_get_original() == 0) {
    }
}

SubGhzProtocolStatus
    subghz_protocol_encoder_hollarm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHollarm* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_hollarm_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_hollarm_remote_controller(&instance->generic);
        subghz_protocol_encoder_hollarm_get_upload(instance);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            break;
        }

        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_hollarm_stop(void* context) {
    SubGhzProtocolEncoderHollarm* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_hollarm_yield(void* context) {
    SubGhzProtocolEncoderHollarm* instance = context;

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

void* subghz_protocol_decoder_hollarm_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHollarm* instance = malloc(sizeof(SubGhzProtocolDecoderHollarm));
    instance->base.protocol = &subghz_protocol_hollarm;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_hollarm_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHollarm* instance = context;
    free(instance);
}

void subghz_protocol_decoder_hollarm_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHollarm* instance = context;
    instance->decoder.parser_step = HollarmDecoderStepReset;
}

void subghz_protocol_decoder_hollarm_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHollarm* instance = context;

    switch(instance->decoder.parser_step) {
    case HollarmDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_hollarm_const.te_short * 12) <
                        subghz_protocol_hollarm_const.te_delta * 2)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = HollarmDecoderStepSaveDuration;
        }
        break;
    case HollarmDecoderStepSaveDuration:

        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = HollarmDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = HollarmDecoderStepReset;
        }
        break;
    case HollarmDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hollarm_const.te_short) <
                subghz_protocol_hollarm_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_hollarm_const.te_long) <
                subghz_protocol_hollarm_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = HollarmDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_hollarm_const.te_short) <
                 subghz_protocol_hollarm_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_hollarm_const.te_short * 8) <
                 subghz_protocol_hollarm_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = HollarmDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_hollarm_const.te_short * 12) <
                subghz_protocol_hollarm_const.te_delta) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_hollarm_const.min_count_bit_for_found) {
                    instance->generic.data = (instance->decoder.decode_data >> 2);
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    uint8_t bytesum = ((instance->generic.data >> 32) & 0xFF) +
                                      ((instance->generic.data >> 24) & 0xFF) +
                                      ((instance->generic.data >> 16) & 0xFF) +
                                      ((instance->generic.data >> 8) & 0xFF);

                    if(bytesum != (instance->generic.data & 0xFF)) {
                        instance->generic.data = 0;
                        instance->generic.data_count_bit = 0;
                        instance->decoder.decode_data = 0;
                        instance->decoder.decode_count_bit = 0;
                        instance->decoder.parser_step = HollarmDecoderStepReset;
                        break;
                    }
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = HollarmDecoderStepReset;
            } else {
                instance->decoder.parser_step = HollarmDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = HollarmDecoderStepReset;
        }
        break;
    }
}

static const char* subghz_protocol_hollarm_get_button_name(uint8_t btn) {
    const char* name_btn[16] = {
        "Unknown",
        "Disarm",
        "Arm",
        "0x3",
        "Ringtone/Alarm",
        "0x5",
        "0x6",
        "0x7",
        "Ring",
        "Settings mode",
        "Exit settings",
        "Vibro sens. setting",
        "Not used\n(in settings)",
        "Volume setting",
        "0xE",
        "0xF"};
    return btn <= 0xf ? name_btn[btn] : name_btn[0];
}

uint8_t subghz_protocol_decoder_hollarm_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHollarm* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_hollarm_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHollarm* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hollarm_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHollarm* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_hollarm_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_hollarm_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderHollarm* instance = context;

    subghz_protocol_hollarm_remote_controller(&instance->generic);

    uint8_t bytesum =
        ((instance->generic.data >> 32) & 0xFF) + ((instance->generic.data >> 24) & 0xFF) +
        ((instance->generic.data >> 16) & 0xFF) + ((instance->generic.data >> 8) & 0xFF);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%02lX%08lX\r\n"
        "Serial: 0x%06lX  Sum: %02X\r\n"
        "Btn: 0x%01X - %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        bytesum,
        instance->generic.btn,
        subghz_protocol_hollarm_get_button_name(instance->generic.btn));
}

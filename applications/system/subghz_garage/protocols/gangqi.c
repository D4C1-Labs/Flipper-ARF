#include "gangqi.h"
#include "protocols_common.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include <lib/subghz/blocks/math.h>

#include "../helpers/subghz_custom_btn_i_compat.h"

#define TAG "SubGhzProtocolGangQi"

static const SubGhzBlockConst subghz_protocol_gangqi_const = {
    .te_short = 500,
    .te_long = 1200,
    .te_delta = 200,
    .min_count_bit_for_found = 34,
};

struct SubGhzProtocolDecoderGangQi {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderGangQi {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    GangQiDecoderStepReset = 0,
    GangQiDecoderStepSaveDuration,
    GangQiDecoderStepCheckDuration,
} GangQiDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_gangqi_decoder = {
    .alloc = subghz_protocol_decoder_gangqi_alloc,
    .free = subghz_protocol_decoder_gangqi_free,

    .feed = subghz_protocol_decoder_gangqi_feed,
    .reset = subghz_protocol_decoder_gangqi_reset,

    .get_hash_data = subghz_protocol_decoder_gangqi_get_hash_data,
    .serialize = subghz_protocol_decoder_gangqi_serialize,
    .deserialize = subghz_protocol_decoder_gangqi_deserialize,
    .get_string = subghz_protocol_decoder_gangqi_get_string,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
const SubGhzProtocolEncoder subghz_protocol_gangqi_encoder = {
    .alloc = subghz_protocol_encoder_gangqi_alloc,
    .free = subghz_protocol_encoder_gangqi_free,

    .deserialize = subghz_protocol_encoder_gangqi_deserialize,
    .stop = subghz_protocol_encoder_gangqi_stop,
    .yield = subghz_protocol_encoder_gangqi_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_gangqi_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol subghz_protocol_gangqi = {
    .name = SUBGHZ_PROTOCOL_GANGQI_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_gangqi_decoder,
    .encoder = &subghz_protocol_gangqi_encoder,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
void* subghz_protocol_encoder_gangqi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderGangQi* instance = malloc(sizeof(SubGhzProtocolEncoderGangQi));

    instance->base.protocol = &subghz_protocol_gangqi;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 256;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_gangqi_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderGangQi* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static uint8_t subghz_protocol_gangqi_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {
        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0xD:
            btn = 0xE;
            break;
        case 0xE:
            btn = 0xD;
            break;
        case 0xB:
            btn = 0xD;
            break;
        case 0x7:
            btn = 0xD;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0xD:
            btn = 0xB;
            break;
        case 0xE:
            btn = 0xB;
            break;
        case 0xB:
            btn = 0xE;
            break;
        case 0x7:
            btn = 0xE;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        switch(original_btn_code) {
        case 0xD:
            btn = 0x7;
            break;
        case 0xE:
            btn = 0x7;
            break;
        case 0xB:
            btn = 0x7;
            break;
        case 0x7:
            btn = 0xB;
            break;

        default:
            break;
        }
    }

    return btn;
}

static void subghz_protocol_encoder_gangqi_get_upload(SubGhzProtocolEncoderGangQi* instance) {
    furi_assert(instance);

    instance->generic.btn = subghz_protocol_gangqi_get_btn_code();

    if(subghz_garage_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    uint16_t serial = (uint16_t)((instance->generic.data >> 18) & 0xFFFF);
    uint8_t const_and_button = (uint8_t)(0xD0 | instance->generic.btn);
    uint8_t serial_high = (uint8_t)(serial >> 8);
    uint8_t serial_low = (uint8_t)(serial & 0xFF);
    uint8_t bytesum = (uint8_t)(0xC8 - serial_high - serial_low - const_and_button);

    instance->generic.data = (instance->generic.data >> 14) << 14 | (instance->generic.btn << 10) |
                             (bytesum << 2);

    size_t index = 0;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_gangqi_const.te_long);
            if(i == 1) {
                instance->encoder.upload[index++] = level_duration_make(
                    false,
                    (uint32_t)subghz_protocol_gangqi_const.te_short * 4 +
                        subghz_protocol_gangqi_const.te_delta);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_gangqi_const.te_short);
            }
        } else {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_gangqi_const.te_short);
            if(i == 1) {
                instance->encoder.upload[index++] = level_duration_make(
                    false,
                    (uint32_t)subghz_protocol_gangqi_const.te_short * 4 +
                        subghz_protocol_gangqi_const.te_delta);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_gangqi_const.te_long);
            }
        }
    }

    instance->encoder.size_upload = index;
    return;
}

#endif

static void subghz_protocol_gangqi_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = (instance->data >> 10) & 0xF;
    instance->serial = (instance->data & 0xFFFFF0000) >> 16;
}

#if SUBGHZ_GARAGE_WITH_ENCODER
SubGhzProtocolStatus
    subghz_protocol_encoder_gangqi_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderGangQi* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_gangqi_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_gangqi_remote_controller(&instance->generic);
        subghz_protocol_encoder_gangqi_get_upload(instance);

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

void subghz_protocol_encoder_gangqi_stop(void* context) {
    SubGhzProtocolEncoderGangQi* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_gangqi_yield(void* context) {
    SubGhzProtocolEncoderGangQi* instance = context;

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

void* subghz_protocol_decoder_gangqi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderGangQi* instance = malloc(sizeof(SubGhzProtocolDecoderGangQi));
    instance->base.protocol = &subghz_protocol_gangqi;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_gangqi_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderGangQi* instance = context;
    free(instance);
}

void subghz_protocol_decoder_gangqi_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderGangQi* instance = context;
    instance->decoder.parser_step = GangQiDecoderStepReset;
}

void subghz_protocol_decoder_gangqi_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderGangQi* instance = context;

    switch(instance->decoder.parser_step) {
    case GangQiDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_gangqi_const.te_long * 2) <
                        subghz_protocol_gangqi_const.te_delta * 3)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = GangQiDecoderStepSaveDuration;
        }
        break;
    case GangQiDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = GangQiDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = GangQiDecoderStepReset;
        }
        break;
    case GangQiDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_gangqi_const.te_short) <
                subghz_protocol_gangqi_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_gangqi_const.te_long) <
                subghz_protocol_gangqi_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = GangQiDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_gangqi_const.te_long) <
                 subghz_protocol_gangqi_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_gangqi_const.te_short) <
                 subghz_protocol_gangqi_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = GangQiDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(duration, subghz_protocol_gangqi_const.te_long * 2) <
                 subghz_protocol_gangqi_const.te_delta * 3)) {
                if((DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_gangqi_const.te_short) <
                    subghz_protocol_gangqi_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_gangqi_const.te_long) <
                    subghz_protocol_gangqi_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_gangqi_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = GangQiDecoderStepReset;
            } else {
                instance->decoder.parser_step = GangQiDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = GangQiDecoderStepReset;
        }
        break;
    }
}

static const char* subghz_protocol_gangqi_get_button_name(uint8_t btn) {
    const char* name_btn[16] = {
        "Unknown",
        "Exit settings",
        "Volume setting",
        "0x3",
        "Vibro sens. setting",
        "Settings mode",
        "Ringtone setting",
        "Ring",
        "0x8",
        "0x9",
        "0xA",
        "Alarm",
        "0xC",
        "Arm",
        "Disarm",
        "0xF"};
    return btn <= 0xf ? name_btn[btn] : name_btn[0];
}

uint8_t subghz_protocol_decoder_gangqi_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderGangQi* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_gangqi_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderGangQi* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_gangqi_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderGangQi* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_gangqi_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_gangqi_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderGangQi* instance = context;

    subghz_protocol_gangqi_remote_controller(&instance->generic);

    uint16_t serial = (uint16_t)((instance->generic.data >> 18) & 0xFFFF);
    uint8_t const_and_button = (uint8_t)(0xD0 | instance->generic.btn);
    uint8_t serial_high = (uint8_t)(serial >> 8);
    uint8_t serial_low = (uint8_t)(serial & 0xFF);

    uint8_t sum_type1 = (uint8_t)(0xC8 - serial_high - serial_low - const_and_button);
    uint8_t sum_type2 = (uint8_t)(0x02 + serial_high + serial_low + const_and_button);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%X%08lX\r\n"
        "Serial: 0x%05lX\r\n"
        "Sum: 0x%02X   Sum2: 0x%02X\r\n"
        "Btn: 0x%01X - %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint8_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        sum_type1,
        sum_type2,
        instance->generic.btn,
        subghz_protocol_gangqi_get_button_name(instance->generic.btn));
}

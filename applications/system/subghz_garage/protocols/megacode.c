#include "megacode.h"
#include "protocols_common.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include <lib/subghz/blocks/math.h>

#define TAG "SubGhzProtocolMegaCode"

static const SubGhzBlockConst subghz_protocol_megacode_const = {
    .te_short = 1000,
    .te_long = 1000,
    .te_delta = 200,
    .min_count_bit_for_found = 24,
};

struct SubGhzProtocolDecoderMegaCode {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t last_bit;
};

struct SubGhzProtocolEncoderMegaCode {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    MegaCodeDecoderStepReset = 0,
    MegaCodeDecoderStepFoundStartBit,
    MegaCodeDecoderStepSaveDuration,
    MegaCodeDecoderStepCheckDuration,
} MegaCodeDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_megacode_decoder = {
    .alloc = subghz_protocol_decoder_megacode_alloc,
    .free = subghz_protocol_decoder_megacode_free,

    .feed = subghz_protocol_decoder_megacode_feed,
    .reset = subghz_protocol_decoder_megacode_reset,

    .get_hash_data = subghz_protocol_decoder_megacode_get_hash_data,
    .serialize = subghz_protocol_decoder_megacode_serialize,
    .deserialize = subghz_protocol_decoder_megacode_deserialize,
    .get_string = subghz_protocol_decoder_megacode_get_string,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
const SubGhzProtocolEncoder subghz_protocol_megacode_encoder = {
    .alloc = subghz_protocol_encoder_megacode_alloc,
    .free = subghz_protocol_encoder_megacode_free,

    .deserialize = subghz_protocol_encoder_megacode_deserialize,
    .stop = subghz_protocol_encoder_megacode_stop,
    .yield = subghz_protocol_encoder_megacode_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_megacode_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol subghz_protocol_megacode = {
    .name = SUBGHZ_PROTOCOL_MEGACODE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_megacode_decoder,
    .encoder = &subghz_protocol_megacode_encoder,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
void* subghz_protocol_encoder_megacode_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMegaCode* instance = malloc(sizeof(SubGhzProtocolEncoderMegaCode));

    instance->base.protocol = &subghz_protocol_megacode;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 52;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_megacode_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderMegaCode* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static bool subghz_protocol_encoder_megacode_get_upload(SubGhzProtocolEncoderMegaCode* instance) {
    furi_assert(instance);
    uint8_t last_bit = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2);
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    size_t index = size_upload - 1;

    instance->encoder.upload[index--] =
        level_duration_make(true, (uint32_t)subghz_protocol_megacode_const.te_short);
    if(bit_read(instance->generic.data, 0)) {
        last_bit = 1;
    } else {
        last_bit = 0;
    }

    for(uint8_t i = 1; i < instance->generic.data_count_bit; i++) {
        if(bit_read(instance->generic.data, i)) {
            instance->encoder.upload[index--] = level_duration_make(
                false,
                last_bit ? (uint32_t)subghz_protocol_megacode_const.te_short * 5 :
                           (uint32_t)subghz_protocol_megacode_const.te_short * 2);
            last_bit = 1;
        } else {
            instance->encoder.upload[index--] = level_duration_make(
                false,
                last_bit ? (uint32_t)subghz_protocol_megacode_const.te_short * 8 :
                           (uint32_t)subghz_protocol_megacode_const.te_short * 5);
            last_bit = 0;
        }
        instance->encoder.upload[index--] =
            level_duration_make(true, (uint32_t)subghz_protocol_megacode_const.te_short);
    }

    if(bit_read(instance->generic.data, 0)) {
        instance->encoder.upload[index] =
            level_duration_make(false, (uint32_t)subghz_protocol_megacode_const.te_short * 11);
    } else {
        instance->encoder.upload[index] =
            level_duration_make(false, (uint32_t)subghz_protocol_megacode_const.te_short * 14);
    }

    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_megacode_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderMegaCode* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_megacode_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_megacode_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_megacode_stop(void* context) {
    SubGhzProtocolEncoderMegaCode* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_megacode_yield(void* context) {
    SubGhzProtocolEncoderMegaCode* instance = context;

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

void* subghz_protocol_decoder_megacode_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMegaCode* instance = malloc(sizeof(SubGhzProtocolDecoderMegaCode));
    instance->base.protocol = &subghz_protocol_megacode;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_megacode_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMegaCode* instance = context;
    free(instance);
}

void subghz_protocol_decoder_megacode_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMegaCode* instance = context;
    instance->decoder.parser_step = MegaCodeDecoderStepReset;
}

void subghz_protocol_decoder_megacode_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderMegaCode* instance = context;
    switch(instance->decoder.parser_step) {
    case MegaCodeDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_megacode_const.te_short * 13) <
                        subghz_protocol_megacode_const.te_delta * 17)) {
            instance->decoder.parser_step = MegaCodeDecoderStepFoundStartBit;
        }
        break;
    case MegaCodeDecoderStepFoundStartBit:
        if(level && (DURATION_DIFF(duration, subghz_protocol_megacode_const.te_short) <
                     subghz_protocol_megacode_const.te_delta)) {
            instance->decoder.parser_step = MegaCodeDecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            subghz_protocol_blocks_add_bit(&instance->decoder, 1);
            instance->last_bit = 1;
        } else {
            instance->decoder.parser_step = MegaCodeDecoderStepReset;
        }
        break;
    case MegaCodeDecoderStepSaveDuration:
        if(!level) {
            if(duration >= (subghz_protocol_megacode_const.te_short * 10)) {
                instance->decoder.parser_step = MegaCodeDecoderStepReset;
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_megacode_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                break;
            }

            if(!instance->last_bit) {
                instance->decoder.te_last = duration - subghz_protocol_megacode_const.te_short * 3;
            } else {
                instance->decoder.te_last = duration;
            }
            instance->decoder.parser_step = MegaCodeDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = MegaCodeDecoderStepReset;
        }
        break;
    case MegaCodeDecoderStepCheckDuration:
        if(level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_megacode_const.te_short * 5) <
                subghz_protocol_megacode_const.te_delta * 5) &&
               (DURATION_DIFF(duration, subghz_protocol_megacode_const.te_short) <
                subghz_protocol_megacode_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->last_bit = 1;
                instance->decoder.parser_step = MegaCodeDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_megacode_const.te_short * 2) <
                 subghz_protocol_megacode_const.te_delta * 2) &&
                (DURATION_DIFF(duration, subghz_protocol_megacode_const.te_short) <
                 subghz_protocol_megacode_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->last_bit = 0;
                instance->decoder.parser_step = MegaCodeDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = MegaCodeDecoderStepReset;
        } else {
            instance->decoder.parser_step = MegaCodeDecoderStepReset;
        }
        break;
    }
}

static void subghz_protocol_megacode_check_remote_controller(SubGhzBlockGeneric* instance) {
    if((instance->data >> 23) == 1) {
        instance->serial = (instance->data >> 3) & 0xFFFF;
        instance->btn = instance->data & 0b111;
        instance->cnt = (instance->data >> 19) & 0b1111;
    } else {
        instance->serial = 0;
        instance->btn = 0;
        instance->cnt = 0;
    }
}

uint8_t subghz_protocol_decoder_megacode_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMegaCode* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_megacode_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderMegaCode* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_megacode_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMegaCode* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_megacode_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_megacode_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderMegaCode* instance = context;
    subghz_protocol_megacode_check_remote_controller(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 3;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%06lX\r\n"
        "Sn:0x%04lX - %lu\r\n"
        "Facility:%lX Btn:%X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn);
}

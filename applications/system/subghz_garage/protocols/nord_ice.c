#include "nord_ice.h"
#include "protocols_common.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include <lib/subghz/blocks/math.h>

#define TAG "SubGhzProtocolNordIce"

#define NORD_ICE_UPLOAD_SIZE 138

static const SubGhzBlockConst subghz_protocol_nord_ice_const = {
    .te_short = 300,
    .te_long = 800,
    .te_delta = 150,
    .min_count_bit_for_found = 33,
};

struct SubGhzProtocolDecoderNordIce {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderNordIce {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    NordIceDecoderStepReset = 0,
    NordIceDecoderStepSaveDuration,
    NordIceDecoderStepCheckDuration,
} NordIceDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_nord_ice_decoder = {
    .alloc = subghz_protocol_decoder_nord_ice_alloc,
    .free = subghz_protocol_decoder_nord_ice_free,

    .feed = subghz_protocol_decoder_nord_ice_feed,
    .reset = subghz_protocol_decoder_nord_ice_reset,

    .get_hash_data = subghz_protocol_decoder_nord_ice_get_hash_data,
    .serialize = subghz_protocol_decoder_nord_ice_serialize,
    .deserialize = subghz_protocol_decoder_nord_ice_deserialize,
    .get_string = subghz_protocol_decoder_nord_ice_get_string,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
const SubGhzProtocolEncoder subghz_protocol_nord_ice_encoder = {
    .alloc = subghz_protocol_encoder_nord_ice_alloc,
    .free = subghz_protocol_encoder_nord_ice_free,

    .deserialize = subghz_protocol_encoder_nord_ice_deserialize,
    .stop = subghz_protocol_encoder_nord_ice_stop,
    .yield = subghz_protocol_encoder_nord_ice_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_nord_ice_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol subghz_protocol_nord_ice = {
    .name = SUBGHZ_PROTOCOL_NORD_ICE_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_nord_ice_decoder,
    .encoder = &subghz_protocol_nord_ice_encoder,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
void* subghz_protocol_encoder_nord_ice_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderNordIce* instance = malloc(sizeof(SubGhzProtocolEncoderNordIce));

    instance->base.protocol = &subghz_protocol_nord_ice;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = NORD_ICE_UPLOAD_SIZE;
    instance->encoder.upload = malloc(NORD_ICE_UPLOAD_SIZE * sizeof(LevelDuration));
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_nord_ice_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderNordIce* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static bool subghz_protocol_encoder_nord_ice_get_upload(SubGhzProtocolEncoderNordIce* instance) {
    furi_assert(instance);
    size_t index = 0;
    uint32_t te_short = subghz_protocol_nord_ice_const.te_short;
    uint32_t te_long = subghz_protocol_nord_ice_const.te_long;
    uint32_t gap = te_short * 25;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        bool bit = bit_read(instance->generic.data, i - 1);
        if(bit) {
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (i == 1) ? gap : te_short);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (i == 1) ? gap : te_long);
        }
    }

    instance->encoder.size_upload = index;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_nord_ice_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderNordIce* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_nord_ice_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) break;

        if(!subghz_protocol_encoder_nord_ice_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.front = 0;
        instance->encoder.is_running = true;
    } while(false);
    return ret;
}

void subghz_protocol_encoder_nord_ice_stop(void* context) {
    SubGhzProtocolEncoderNordIce* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_nord_ice_yield(void* context) {
    SubGhzProtocolEncoderNordIce* instance = context;

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
#endif // SUBGHZ_GARAGE_WITH_ENCODER

void* subghz_protocol_decoder_nord_ice_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderNordIce* instance = malloc(sizeof(SubGhzProtocolDecoderNordIce));
    instance->base.protocol = &subghz_protocol_nord_ice;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_nord_ice_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;
    free(instance);
}

void subghz_protocol_decoder_nord_ice_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;
    instance->decoder.parser_step = NordIceDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

static void subghz_protocol_nord_ice_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = ((instance->data >> 15) << 9) | (instance->data & 0x1FF);
    instance->btn = (instance->data >> 9) & 0x3F;
}

void subghz_protocol_decoder_nord_ice_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;

    switch(instance->decoder.parser_step) {
    case NordIceDecoderStepReset:
        if(!level &&
           DURATION_DIFF(duration, subghz_protocol_nord_ice_const.te_short * 25) <
               subghz_protocol_nord_ice_const.te_delta * 11) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = NordIceDecoderStepSaveDuration;
        }
        break;
    case NordIceDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = NordIceDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = NordIceDecoderStepReset;
        }
        break;
    case NordIceDecoderStepCheckDuration:
        if(!level) {
            if(DURATION_DIFF(
                   instance->decoder.te_last, subghz_protocol_nord_ice_const.te_short) <
                   subghz_protocol_nord_ice_const.te_delta &&
               DURATION_DIFF(duration, subghz_protocol_nord_ice_const.te_long) <
                   subghz_protocol_nord_ice_const.te_delta) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = NordIceDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(instance->decoder.te_last, subghz_protocol_nord_ice_const.te_long) <
                    subghz_protocol_nord_ice_const.te_delta &&
                DURATION_DIFF(duration, subghz_protocol_nord_ice_const.te_short) <
                    subghz_protocol_nord_ice_const.te_delta) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = NordIceDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_nord_ice_const.te_short * 25) <
                subghz_protocol_nord_ice_const.te_delta * 11) {

                if(DURATION_DIFF(
                       instance->decoder.te_last, subghz_protocol_nord_ice_const.te_short) <
                   subghz_protocol_nord_ice_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                } else if(
                    DURATION_DIFF(
                        instance->decoder.te_last, subghz_protocol_nord_ice_const.te_long) <
                    subghz_protocol_nord_ice_const.te_delta) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_nord_ice_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = NordIceDecoderStepReset;
            } else {
                instance->decoder.parser_step = NordIceDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = NordIceDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_nord_ice_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_nord_ice_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_nord_ice_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_nord_ice_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_nord_ice_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderNordIce* instance = context;

    subghz_protocol_nord_ice_check_remote_controller(&instance->generic);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key:0x%08lX\r\n"
        "Serial:0x%07lX\r\n"
        "Btn:%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(instance->generic.data & 0xFFFFFFFF),
        (unsigned long)instance->generic.serial,
        instance->generic.btn);
}

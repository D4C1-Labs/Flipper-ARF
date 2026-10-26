#include "x10.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolX10"

static const SubGhzBlockConst subghz_protocol_x10_const = {
    .te_short = 600,
    .te_long = 1800,
    .te_delta = 100,
    .min_count_bit_for_found = 32,
};

struct SubGhzProtocolDecoderX10 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderX10 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    X10DecoderStepReset = 0,
    X10DecoderStepFoundPreambula,
    X10DecoderStepSaveDuration,
    X10DecoderStepCheckDuration,
} X10DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_x10_decoder = {
    .alloc = subghz_protocol_decoder_x10_alloc,
    .free = subghz_protocol_decoder_x10_free,
    .feed = subghz_protocol_decoder_x10_feed,
    .reset = subghz_protocol_decoder_x10_reset,
    .get_hash_data = subghz_protocol_decoder_x10_get_hash_data,
    .serialize = subghz_protocol_decoder_x10_serialize,
    .deserialize = subghz_protocol_decoder_x10_deserialize,
    .get_string = subghz_protocol_decoder_x10_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_x10_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_x10 = {
    .name = SUBGHZ_PROTOCOL_X10_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315  | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable,
    .decoder = &subghz_protocol_x10_decoder,
    .encoder = &subghz_protocol_x10_encoder,
};

void* subghz_protocol_decoder_x10_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderX10* instance = malloc(sizeof(SubGhzProtocolDecoderX10));
    instance->base.protocol = &subghz_protocol_x10;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_x10_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderX10* instance = context;
    free(instance);
}

void subghz_protocol_decoder_x10_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderX10* instance = context;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->decoder.parser_step = X10DecoderStepReset;
}

bool subghz_protocol_x10_validate(void* context) {
    furi_assert(context);

    SubGhzProtocolDecoderX10* instance = context;
    SubGhzBlockDecoder* decoder = &instance->decoder;
    uint64_t data = decoder->decode_data;

    return decoder->decode_count_bit >= subghz_protocol_x10_const.min_count_bit_for_found &&
           ((((data >> 24) ^ (data >> 16)) & 0xFF) == 0xFF) &&
           ((((data >> 8) ^ (data )) & 0xFF) == 0xFF);
}

void subghz_protocol_decoder_x10_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderX10* instance = context;

    switch(instance->decoder.parser_step) {
    case X10DecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_x10_const.te_short * 16) <
                       subghz_protocol_x10_const.te_delta * 7)) {
            instance->decoder.parser_step = X10DecoderStepFoundPreambula;
        }
        break;
    case X10DecoderStepFoundPreambula:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_x10_const.te_short * 8) <
                        subghz_protocol_x10_const.te_delta * 5)) {
            instance->decoder.parser_step = X10DecoderStepSaveDuration;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        } else {
            subghz_protocol_decoder_x10_reset(context);
        }
        break;
    case X10DecoderStepSaveDuration:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_x10_const.te_short) <
               subghz_protocol_x10_const.te_delta) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_x10_const.min_count_bit_for_found) {
                    instance->decoder.parser_step = X10DecoderStepReset;
                    if (subghz_protocol_x10_validate(context)) {
                        FURI_LOG_E(TAG, "Decoded a signal!");
                        instance->generic.data = instance->decoder.decode_data;
                        instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    }
                    subghz_protocol_decoder_x10_reset(context);
                } else {
                    instance->decoder.te_last = duration;
                    instance->decoder.parser_step = X10DecoderStepCheckDuration;
                }
            } else {
                subghz_protocol_decoder_x10_reset(context);
            }
        } else {
            subghz_protocol_decoder_x10_reset(context);
        }
        break;
    case X10DecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_x10_const.te_short) <
                subghz_protocol_x10_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_x10_const.te_short) <
                subghz_protocol_x10_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = X10DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_x10_const.te_short) <
                 subghz_protocol_x10_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_x10_const.te_long) <
                 subghz_protocol_x10_const.te_delta * 2)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = X10DecoderStepSaveDuration;
            } else {
                subghz_protocol_decoder_x10_reset(context);
            }
        } else {
            subghz_protocol_decoder_x10_reset(context);
        }
        break;
    }
}

static void subghz_protocol_x10_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = (instance->data & 0xF0000000) >> (24+4);
    instance->btn = (((instance->data & 0x07000000) >> 24) | ((instance->data & 0xF800) >> 8));
}

uint8_t subghz_protocol_decoder_x10_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderX10* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_x10_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderX10* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_x10_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderX10* instance = context;
    bool ret = false;
    do {
        if(!subghz_block_generic_deserialize(&instance->generic, flipper_format)) {
            break;
        }
        if(instance->generic.data_count_bit !=
            subghz_protocol_x10_const.min_count_bit_for_found) {
            FURI_LOG_E(TAG, "Wrong number of bits in key");
            break;
        }
        ret = true;
    } while(false);
    return ret;
}

const char* CHANNEL_LETTERS = "MNOPCDABEFGHKLIJ";
void subghz_protocol_decoder_x10_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderX10* instance = context;
    subghz_protocol_x10_check_remote_controller(&instance->generic);

    char code_channel = CHANNEL_LETTERS[(instance->generic.serial & 0x0F)];

    uint32_t code_button = 1 + (
        ((instance->generic.btn&0x10) >> 4) |
        ((instance->generic.btn&0x8) >> 2) |
        ((instance->generic.btn&0x40)>>4) |
        ((instance->generic.btn&4)<<1));

    char* code_action = (instance->generic.btn & 0x20) == 0x20 ? "Off" : "On";
    if (instance->generic.btn == 0x98) {
        code_button = 0;
        code_action = "Dim";
    } else if (instance->generic.btn == 0x88) {
        code_button = 0;
        code_action = "Bright";
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Channel:%c \r\n"
        "Button:%ld %s\r\n\r\n"
        "Key:%lX%08lX\r\n"
        "Sn:%07lX Btn:%X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_channel,
        code_button,
        code_action,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        instance->generic.btn);
}

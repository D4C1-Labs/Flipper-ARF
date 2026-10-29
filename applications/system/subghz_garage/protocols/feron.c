#include "feron.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include <lib/subghz/blocks/math.h>

#define TAG "SubGhzProtocolFeron"

static const SubGhzBlockConst subghz_protocol_feron_const = {
    .te_short = 350,
    .te_long = 750,
    .te_delta = 150,
    .min_count_bit_for_found = 32,
};

struct SubGhzProtocolDecoderFeron {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderFeron {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    FeronDecoderStepReset = 0,
    FeronDecoderStepSaveDuration,
    FeronDecoderStepCheckDuration,
} FeronDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_feron_decoder = {
    .alloc = subghz_protocol_decoder_feron_alloc,
    .free = subghz_protocol_decoder_feron_free,

    .feed = subghz_protocol_decoder_feron_feed,
    .reset = subghz_protocol_decoder_feron_reset,

    .get_hash_data = subghz_protocol_decoder_feron_get_hash_data,
    .serialize = subghz_protocol_decoder_feron_serialize,
    .deserialize = subghz_protocol_decoder_feron_deserialize,
    .get_string = subghz_protocol_decoder_feron_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_feron_encoder = {
    .alloc = subghz_protocol_encoder_feron_alloc,
    .free = subghz_protocol_encoder_feron_free,

    .deserialize = subghz_protocol_encoder_feron_deserialize,
    .stop = subghz_protocol_encoder_feron_stop,
    .yield = subghz_protocol_encoder_feron_yield,
};

const SubGhzProtocol subghz_protocol_feron = {
    .name = SUBGHZ_PROTOCOL_FERON_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_feron_decoder,
    .encoder = &subghz_protocol_feron_encoder,
};

void* subghz_protocol_encoder_feron_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFeron* instance = malloc(sizeof(SubGhzProtocolEncoderFeron));

    instance->base.protocol = &subghz_protocol_feron;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 256;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_feron_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFeron* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static void subghz_protocol_encoder_feron_get_upload(SubGhzProtocolEncoderFeron* instance) {
    furi_assert(instance);
    size_t index = 0;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_feron_const.te_long);
            if(i == 1) {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_feron_const.te_short + 150);
                instance->encoder.upload[index++] = level_duration_make(
                    true, (uint32_t)subghz_protocol_feron_const.te_short + 150);

                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_feron_const.te_long * 6);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_feron_const.te_short);
            }
        } else {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_feron_const.te_short);
            if(i == 1) {
                instance->encoder.upload[index++] = level_duration_make(
                    false, (uint32_t)subghz_protocol_feron_const.te_short + 150);
                instance->encoder.upload[index++] = level_duration_make(
                    true, (uint32_t)subghz_protocol_feron_const.te_short + 150);

                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_feron_const.te_long * 6);
            } else {
                instance->encoder.upload[index++] =
                    level_duration_make(false, (uint32_t)subghz_protocol_feron_const.te_long);
            }
        }
    }

    instance->encoder.size_upload = index;
    return;
}

static void subghz_protocol_feron_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->serial = instance->data >> 16;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_feron_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderFeron* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_feron_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_feron_check_remote_controller(&instance->generic);
        subghz_protocol_encoder_feron_get_upload(instance);
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_feron_stop(void* context) {
    SubGhzProtocolEncoderFeron* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_feron_yield(void* context) {
    SubGhzProtocolEncoderFeron* instance = context;

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

void* subghz_protocol_decoder_feron_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderFeron* instance = malloc(sizeof(SubGhzProtocolDecoderFeron));
    instance->base.protocol = &subghz_protocol_feron;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_feron_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFeron* instance = context;
    free(instance);
}

void subghz_protocol_decoder_feron_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFeron* instance = context;
    instance->decoder.parser_step = FeronDecoderStepReset;
}

void subghz_protocol_decoder_feron_feed(void* context, bool level, volatile uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderFeron* instance = context;

    switch(instance->decoder.parser_step) {
    case FeronDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_feron_const.te_long * 6) <
                        subghz_protocol_feron_const.te_delta * 4)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = FeronDecoderStepSaveDuration;
        }
        break;
    case FeronDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = FeronDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = FeronDecoderStepReset;
        }
        break;
    case FeronDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_feron_const.te_short) <
                subghz_protocol_feron_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_feron_const.te_long) <
                subghz_protocol_feron_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = FeronDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_feron_const.te_long) <
                 subghz_protocol_feron_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_feron_const.te_short) <
                 subghz_protocol_feron_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = FeronDecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(
                    duration, (uint16_t)(subghz_protocol_feron_const.te_short + (uint16_t)150)) <
                subghz_protocol_feron_const.te_delta) {
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_feron_const.te_short) <
                    subghz_protocol_feron_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                }
                if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_feron_const.te_long) <
                    subghz_protocol_feron_const.te_delta)) {
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                }

                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_feron_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->decoder.parser_step = FeronDecoderStepReset;
            } else {
                instance->decoder.parser_step = FeronDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = FeronDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_feron_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderFeron* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_feron_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderFeron* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_feron_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderFeron* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_feron_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_feron_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderFeron* instance = context;

    subghz_protocol_feron_check_remote_controller(&instance->generic);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%08lX\r\n"
        "Serial: 0x%04lX\r\n"
        "Command: 0x%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        (uint32_t)(instance->generic.data & 0xFFFF));
}

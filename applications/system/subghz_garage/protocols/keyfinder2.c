#include "keyfinder2.h"
#include "protocols_common.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include <lib/subghz/blocks/math.h>

#define TAG "SubGhzProtocolKeyFinder2"

#define KEYFINDER2_TE_WIDE_SHORT 1500
#define KEYFINDER2_TE_WIDE_LONG  2500
#define KEYFINDER2_TE_WIDE_DELTA 350
#define KEYFINDER2_HEADER_BITS   3

static const SubGhzBlockConst subghz_protocol_keyfinder2_const = {
    .te_short = 350,
    .te_long = 1020,
    .te_delta = 150,
    .min_count_bit_for_found = 11,
};

struct SubGhzProtocolDecoderKeyFinder2 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
};

struct SubGhzProtocolEncoderKeyFinder2 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    KeyFinder2DecoderStepReset = 0,
    KeyFinder2DecoderStepSaveWide,
    KeyFinder2DecoderStepCheckWide,
    KeyFinder2DecoderStepSaveDuration,
    KeyFinder2DecoderStepCheckDuration,
} KeyFinder2DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_keyfinder2_decoder = {
    .alloc = subghz_protocol_decoder_keyfinder2_alloc,
    .free = subghz_protocol_decoder_keyfinder2_free,

    .feed = subghz_protocol_decoder_keyfinder2_feed,
    .reset = subghz_protocol_decoder_keyfinder2_reset,

    .get_hash_data = subghz_protocol_decoder_keyfinder2_get_hash_data,
    .serialize = subghz_protocol_decoder_keyfinder2_serialize,
    .deserialize = subghz_protocol_decoder_keyfinder2_deserialize,
    .get_string = subghz_protocol_decoder_keyfinder2_get_string,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
const SubGhzProtocolEncoder subghz_protocol_keyfinder2_encoder = {
    .alloc = subghz_protocol_encoder_keyfinder2_alloc,
    .free = subghz_protocol_encoder_keyfinder2_free,

    .deserialize = subghz_protocol_encoder_keyfinder2_deserialize,
    .stop = subghz_protocol_encoder_keyfinder2_stop,
    .yield = subghz_protocol_encoder_keyfinder2_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_keyfinder2_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol subghz_protocol_keyfinder2 = {
    .name = SUBGHZ_PROTOCOL_KEYFINDER2_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_keyfinder2_decoder,
    .encoder = &subghz_protocol_keyfinder2_encoder,
};

static const char* subghz_protocol_keyfinder2_color(uint8_t btn) {
    switch(btn) {
    case 3:
        return "Purple";
    case 4:
        return "Blue";
    case 5:
        return "Green";
    case 6:
        return "Red";
    default:
        return "Unknown";
    }
}

static void subghz_protocol_keyfinder2_check_remote_controller(SubGhzBlockGeneric* instance) {
    instance->btn = instance->data & 0x7;
}

#if SUBGHZ_GARAGE_WITH_ENCODER
void* subghz_protocol_encoder_keyfinder2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderKeyFinder2* instance = malloc(sizeof(SubGhzProtocolEncoderKeyFinder2));

    instance->base.protocol = &subghz_protocol_keyfinder2;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 5;
    instance->encoder.size_upload = 32;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_keyfinder2_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKeyFinder2* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static void
    subghz_protocol_encoder_keyfinder2_get_upload(SubGhzProtocolEncoderKeyFinder2* instance) {
    furi_assert(instance);
    size_t index = 0;

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        bool wide = (i > instance->generic.data_count_bit - KEYFINDER2_HEADER_BITS);
        uint32_t te_short =
            wide ? KEYFINDER2_TE_WIDE_SHORT : (uint32_t)subghz_protocol_keyfinder2_const.te_short;
        uint32_t te_long =
            wide ? KEYFINDER2_TE_WIDE_LONG : (uint32_t)subghz_protocol_keyfinder2_const.te_long;

        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] = level_duration_make(true, te_short);
            instance->encoder.upload[index++] = level_duration_make(false, te_long);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, te_long);
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }
    }

    instance->encoder.size_upload = index;
    return;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_keyfinder2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKeyFinder2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_keyfinder2_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        subghz_protocol_keyfinder2_check_remote_controller(&instance->generic);
        subghz_protocol_encoder_keyfinder2_get_upload(instance);
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_keyfinder2_stop(void* context) {
    SubGhzProtocolEncoderKeyFinder2* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_keyfinder2_yield(void* context) {
    SubGhzProtocolEncoderKeyFinder2* instance = context;

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

void* subghz_protocol_decoder_keyfinder2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKeyFinder2* instance = malloc(sizeof(SubGhzProtocolDecoderKeyFinder2));
    instance->base.protocol = &subghz_protocol_keyfinder2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_keyfinder2_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder2* instance = context;
    free(instance);
}

void subghz_protocol_decoder_keyfinder2_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder2* instance = context;
    instance->decoder.parser_step = KeyFinder2DecoderStepReset;
}

void subghz_protocol_decoder_keyfinder2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder2* instance = context;

    switch(instance->decoder.parser_step) {
    case KeyFinder2DecoderStepReset:
        if((level) && ((DURATION_DIFF(duration, KEYFINDER2_TE_WIDE_SHORT) <
                        KEYFINDER2_TE_WIDE_DELTA) ||
                       (DURATION_DIFF(duration, KEYFINDER2_TE_WIDE_LONG) <
                        KEYFINDER2_TE_WIDE_DELTA))) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = KeyFinder2DecoderStepCheckWide;
        }
        break;
    case KeyFinder2DecoderStepSaveWide:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = KeyFinder2DecoderStepCheckWide;
        } else {
            instance->decoder.parser_step = KeyFinder2DecoderStepReset;
        }
        break;
    case KeyFinder2DecoderStepCheckWide:
        if(!level) {
            if((DURATION_DIFF(instance->decoder.te_last, KEYFINDER2_TE_WIDE_SHORT) <
                KEYFINDER2_TE_WIDE_DELTA) &&
               (DURATION_DIFF(duration, KEYFINDER2_TE_WIDE_LONG) < KEYFINDER2_TE_WIDE_DELTA)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, KEYFINDER2_TE_WIDE_LONG) <
                 KEYFINDER2_TE_WIDE_DELTA) &&
                (DURATION_DIFF(duration, KEYFINDER2_TE_WIDE_SHORT) < KEYFINDER2_TE_WIDE_DELTA)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
            } else {
                instance->decoder.parser_step = KeyFinder2DecoderStepReset;
                break;
            }
            instance->decoder.parser_step =
                (instance->decoder.decode_count_bit == KEYFINDER2_HEADER_BITS) ?
                    KeyFinder2DecoderStepSaveDuration :
                    KeyFinder2DecoderStepSaveWide;
        } else {
            instance->decoder.parser_step = KeyFinder2DecoderStepReset;
        }
        break;
    case KeyFinder2DecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = KeyFinder2DecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = KeyFinder2DecoderStepReset;
        }
        break;
    case KeyFinder2DecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_keyfinder2_const.te_short) <
                subghz_protocol_keyfinder2_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_keyfinder2_const.te_long) <
                subghz_protocol_keyfinder2_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_keyfinder2_const.te_long) <
                 subghz_protocol_keyfinder2_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_keyfinder2_const.te_short) <
                 subghz_protocol_keyfinder2_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
            } else {
                instance->decoder.parser_step = KeyFinder2DecoderStepReset;
                break;
            }

            if(instance->decoder.decode_count_bit ==
               subghz_protocol_keyfinder2_const.min_count_bit_for_found) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
                instance->decoder.parser_step = KeyFinder2DecoderStepReset;
            } else {
                instance->decoder.parser_step = KeyFinder2DecoderStepSaveDuration;
            }
        } else {
            instance->decoder.parser_step = KeyFinder2DecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_keyfinder2_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder2* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_keyfinder2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder2* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_keyfinder2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder2* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_keyfinder2_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_keyfinder2_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderKeyFinder2* instance = context;

    subghz_protocol_keyfinder2_check_remote_controller(&instance->generic);

    furi_string_cat_printf(
        output,
        "%s %db\r\n"
        "Key: 0x%03lX\r\n"
        "Btn: %01X  %s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data & 0x7FF),
        instance->generic.btn,
        subghz_protocol_keyfinder2_color(instance->generic.btn));
}

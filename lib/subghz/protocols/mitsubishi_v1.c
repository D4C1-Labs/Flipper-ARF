#include "mitsubishi_v1.h"

#define TAG "SubGhzProtocolMitsubishi"

static const SubGhzBlockConst subghz_protocol_mitsubishi_const = {
    .te_short = 320,  // Similar to KIA timing
    .te_long = 640,   // ~2× te_short
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

typedef struct SubGhzProtocolDecoderMitsubishi {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
} SubGhzProtocolDecoderMitsubishi;

typedef struct SubGhzProtocolEncoderMitsubishi {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolEncoderMitsubishi;

typedef enum {
    MitsubishiDecoderStepReset = 0,
    MitsubishiDecoderStepCheckPreamble,
    MitsubishiDecoderStepSaveDuration,
    MitsubishiDecoderStepCheckDuration,
} MitsubishiDecoderStep;

static void subghz_protocol_decoder_mitsubishi_reset_internal(SubGhzProtocolDecoderMitsubishi* instance) {
    memset(&instance->decoder, 0, sizeof(instance->decoder));
    memset(&instance->generic, 0, sizeof(instance->generic));
    instance->decoder.parser_step = MitsubishiDecoderStepReset;
    instance->header_count = 0;
}

const SubGhzProtocolDecoder subghz_protocol_mitsubishi_decoder = {
    .alloc = subghz_protocol_decoder_mitsubishi_alloc,
    .free = subghz_protocol_decoder_mitsubishi_free,

    .feed = subghz_protocol_decoder_mitsubishi_feed,
    .reset = subghz_protocol_decoder_mitsubishi_reset,

    .get_hash_data = subghz_protocol_decoder_mitsubishi_get_hash_data,
    .serialize = subghz_protocol_decoder_mitsubishi_serialize,
    .deserialize = subghz_protocol_decoder_mitsubishi_deserialize,
    .get_string = subghz_protocol_decoder_mitsubishi_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_mitsubishi_encoder = {
    .alloc = NULL,
    .free = NULL,

    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_mitsubishi_v1 = {
    .name = MITSUBISHI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable,

    .decoder = &subghz_protocol_mitsubishi_decoder,
    .encoder = &subghz_protocol_mitsubishi_encoder,
};

// ----------------- Allocation / Reset / Free -------------------

void* subghz_protocol_decoder_mitsubishi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMitsubishi* instance = calloc(1, sizeof(SubGhzProtocolDecoderMitsubishi));
    instance->base.protocol = &subghz_protocol_mitsubishi_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_mitsubishi_reset(instance);
    return instance;
}

void subghz_protocol_decoder_mitsubishi_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_mitsubishi_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;
    subghz_protocol_decoder_mitsubishi_reset_internal(instance);
}

// ----------------- Helper Functions -------------------

// Parse Mitsubishi/KIA-Hyundai data structure
static void subghz_protocol_mitsubishi_parse_data(SubGhzProtocolDecoderMitsubishi* instance) {
    // Structure similar to KIA/Hyundai protocol
    // Serial number in upper bits
    // Button code in middle bits
    // Counter in lower bits
    
    instance->generic.serial = (uint32_t)((instance->generic.data >> 32) & 0xFFFFFFFF);
    instance->generic.btn = (instance->generic.data >> 24) & 0xFF;
    instance->generic.cnt = (instance->generic.data >> 8) & 0xFFFF;
}

// ----------------- Decoder Feed -------------------

void subghz_protocol_decoder_mitsubishi_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;

    switch(instance->decoder.parser_step) {
    case MitsubishiDecoderStepReset:
        if(level && (DURATION_DIFF(duration, subghz_protocol_mitsubishi_const.te_short) <
                     subghz_protocol_mitsubishi_const.te_delta)) {
            instance->decoder.parser_step = MitsubishiDecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case MitsubishiDecoderStepCheckPreamble:
        if(level) {
            if((DURATION_DIFF(duration, subghz_protocol_mitsubishi_const.te_short) <
                subghz_protocol_mitsubishi_const.te_delta) ||
               (DURATION_DIFF(duration, subghz_protocol_mitsubishi_const.te_long) <
                subghz_protocol_mitsubishi_const.te_delta)) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = MitsubishiDecoderStepReset;
            }
        } else {
            if((DURATION_DIFF(duration, subghz_protocol_mitsubishi_const.te_short) <
                subghz_protocol_mitsubishi_const.te_delta) &&
               (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_mitsubishi_const.te_short) <
                subghz_protocol_mitsubishi_const.te_delta)) {
                instance->header_count++;
            } else if(
                (DURATION_DIFF(duration, subghz_protocol_mitsubishi_const.te_long) <
                 subghz_protocol_mitsubishi_const.te_delta) &&
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_mitsubishi_const.te_long) <
                 subghz_protocol_mitsubishi_const.te_delta)) {
                if(instance->header_count > 10) {
                    instance->decoder.parser_step = MitsubishiDecoderStepSaveDuration;
                    instance->decoder.decode_data = 0ULL;
                    instance->decoder.decode_count_bit = 0;
                } else {
                    instance->decoder.parser_step = MitsubishiDecoderStepReset;
                }
            } else {
                instance->decoder.parser_step = MitsubishiDecoderStepReset;
            }
        }
        break;

    case MitsubishiDecoderStepSaveDuration:
        if(level) {
            if(duration >= (subghz_protocol_mitsubishi_const.te_long * 3)) {
                if(instance->decoder.decode_count_bit >=
                   subghz_protocol_mitsubishi_const.min_count_bit_for_found) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    // Parse Mitsubishi data
                    subghz_protocol_mitsubishi_parse_data(instance);

                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                subghz_protocol_decoder_mitsubishi_reset_internal(instance);
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = MitsubishiDecoderStepCheckDuration;
            }
        } else {
            instance->decoder.parser_step = MitsubishiDecoderStepReset;
        }
        break;

    case MitsubishiDecoderStepCheckDuration:
        if(!level) {
            // Manchester-like decoding (KIA/Hyundai style)
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_mitsubishi_const.te_short) <
                subghz_protocol_mitsubishi_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_mitsubishi_const.te_short) <
                subghz_protocol_mitsubishi_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = MitsubishiDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_mitsubishi_const.te_long) <
                 subghz_protocol_mitsubishi_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_mitsubishi_const.te_long) <
                 subghz_protocol_mitsubishi_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = MitsubishiDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = MitsubishiDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = MitsubishiDecoderStepReset;
        }
        break;
    }
}

// ----------------- API -------------------

uint8_t subghz_protocol_decoder_mitsubishi_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mitsubishi_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_mitsubishi_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;

    uint32_t hi = instance->generic.data >> 32;
    uint32_t lo = instance->generic.data & 0xFFFFFFFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%08lX Btn:%02X Cnt:%04lX\r\n"
        "Type:KIA/Hyundai based\r\n"
        "Models:L200,Pajero,ASX+\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        hi,
        lo,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

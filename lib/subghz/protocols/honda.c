#include "honda.h"

#define TAG "SubGhzProtocolHonda"

static const SubGhzBlockConst subghz_protocol_honda_const = {
    .te_short = 432,  // Short pulse ~432µs
    .te_long = 864,   // Long pulse ~864µs (2x short)
    .te_delta = 150,  // Tolerance
    .min_count_bit_for_found = 64,
};

typedef struct SubGhzProtocolDecoderHonda {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
} SubGhzProtocolDecoderHonda;

typedef struct SubGhzProtocolEncoderHonda {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolEncoderHonda;

typedef enum {
    HondaDecoderStepReset = 0,
    HondaDecoderStepCheckPreamble,
    HondaDecoderStepSaveDuration,
    HondaDecoderStepCheckDuration,
} HondaDecoderStep;

static void subghz_protocol_decoder_honda_reset_internal(SubGhzProtocolDecoderHonda* instance) {
    memset(&instance->decoder, 0, sizeof(instance->decoder));
    memset(&instance->generic, 0, sizeof(instance->generic));
    instance->decoder.parser_step = HondaDecoderStepReset;
    instance->header_count = 0;
}

const SubGhzProtocolDecoder subghz_protocol_honda_decoder = {
    .alloc = subghz_protocol_decoder_honda_alloc,
    .free = subghz_protocol_decoder_honda_free,

    .feed = subghz_protocol_decoder_honda_feed,
    .reset = subghz_protocol_decoder_honda_reset,

    .get_hash_data = subghz_protocol_decoder_honda_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_serialize,
    .deserialize = subghz_protocol_decoder_honda_deserialize,
    .get_string = subghz_protocol_decoder_honda_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_honda_encoder = {
    .alloc = NULL,
    .free = NULL,

    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_honda = {
    .name = HONDA_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,  // Rolling code (vulnerable)
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable,

    .decoder = &subghz_protocol_honda_decoder,
    .encoder = &subghz_protocol_honda_encoder,
};

// ----------------- Allocation / Reset / Free -------------------

void* subghz_protocol_decoder_honda_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHonda* instance = calloc(1, sizeof(SubGhzProtocolDecoderHonda));
    instance->base.protocol = &subghz_protocol_honda;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_honda_reset(instance);
    return instance;
}

void subghz_protocol_decoder_honda_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_honda_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHonda* instance = context;
    subghz_protocol_decoder_honda_reset_internal(instance);
}

// ----------------- Honda Protocol Parsing -------------------

static bool subghz_protocol_honda_parse_data(SubGhzProtocolDecoderHonda* instance) {
    uint8_t* b = (uint8_t*)&instance->generic.data;
    
    // Honda protocol structure (from rtl_433):
    // Bits 0-7: Preamble/sync
    // Bits 8-39: Device ID (32 bits)
    // Bits 40-55: Rolling counter (16 bits)
    // Bits 56-63: Function code (8 bits) - which button was pressed
    
    // Extract device ID (bytes 1-4)
    uint32_t device_id = ((uint32_t)b[1] << 24) | 
                         (b[2] << 16) | 
                         (b[3] << 8) | 
                         b[4];
    
    // Extract rolling counter (bytes 5-6)
    uint16_t rolling_counter = (b[5] << 8) | b[6];
    
    // Extract function code (byte 7)
    uint8_t function = b[7];
    
    // Store parsed data
    instance->generic.serial = device_id;
    instance->generic.cnt = rolling_counter;
    instance->generic.btn = function;
    
    return true;
}

// ----------------- Decoder Feed -------------------

void subghz_protocol_decoder_honda_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHonda* instance = context;

    switch(instance->decoder.parser_step) {
    case HondaDecoderStepReset:
        if(level && (DURATION_DIFF(duration, subghz_protocol_honda_const.te_short) <
                     subghz_protocol_honda_const.te_delta)) {
            instance->decoder.parser_step = HondaDecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case HondaDecoderStepCheckPreamble:
        if(level) {
            if((DURATION_DIFF(duration, subghz_protocol_honda_const.te_short) <
                subghz_protocol_honda_const.te_delta)) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = HondaDecoderStepReset;
            }
        } else {
            // Looking for preamble pattern
            if((DURATION_DIFF(duration, subghz_protocol_honda_const.te_short) <
                subghz_protocol_honda_const.te_delta) &&
               (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_honda_const.te_short) <
                subghz_protocol_honda_const.te_delta)) {
                instance->header_count++;
            } else if((DURATION_DIFF(duration, subghz_protocol_honda_const.te_long) <
                       subghz_protocol_honda_const.te_delta * 2) && 
                      instance->header_count >= 10) {
                // Long gap after preamble - start of data
                instance->decoder.parser_step = HondaDecoderStepSaveDuration;
                instance->decoder.decode_data = 0ULL;
                instance->decoder.decode_count_bit = 0;
            } else {
                instance->decoder.parser_step = HondaDecoderStepReset;
            }
        }
        break;

    case HondaDecoderStepSaveDuration:
        if(level) {
            if(duration >= (subghz_protocol_honda_const.te_long * 3)) {
                // End of transmission
                if(instance->decoder.decode_count_bit >= 
                   subghz_protocol_honda_const.min_count_bit_for_found) {
                    
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    // Parse Honda protocol structure
                    if(subghz_protocol_honda_parse_data(instance)) {
                        if(instance->base.callback) {
                            instance->base.callback(&instance->base, instance->base.context);
                        }
                    }
                }
                subghz_protocol_decoder_honda_reset_internal(instance);
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = HondaDecoderStepCheckDuration;
            }
        } else {
            instance->decoder.parser_step = HondaDecoderStepReset;
        }
        break;

    case HondaDecoderStepCheckDuration:
        if(!level) {
            // Manchester decoding (differential)
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_honda_const.te_short) <
                subghz_protocol_honda_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_honda_const.te_long) <
                subghz_protocol_honda_const.te_delta)) {
                // Short-Long = 0
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = HondaDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_honda_const.te_long) <
                 subghz_protocol_honda_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_honda_const.te_short) <
                 subghz_protocol_honda_const.te_delta)) {
                // Long-Short = 1
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = HondaDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = HondaDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = HondaDecoderStepReset;
        }
        break;
    }
}

// ----------------- API -------------------

uint8_t subghz_protocol_decoder_honda_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHonda* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_honda_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHonda* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus subghz_protocol_decoder_honda_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHonda* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, 
        flipper_format, 
        subghz_protocol_honda_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_honda_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderHonda* instance = context;

    uint32_t hi = instance->generic.data >> 32;
    uint32_t lo = instance->generic.data & 0xFFFFFFFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "ID:%08lX Btn:%02X Cnt:%04X\r\n"
        "CVE:CVE-2022-27254\r\n"
        "Note:Rolling code vulnerable\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        hi,
        lo,
        instance->generic.serial,
        instance->generic.btn,
        (uint16_t)instance->generic.cnt);
}

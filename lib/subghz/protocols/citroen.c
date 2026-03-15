#include "citroen.h"

#define TAG "SubGhzProtocolCitroen"

static const SubGhzBlockConst subghz_protocol_citroen_const = {
    .te_short = 370,  // Short pulse duration
    .te_long = 772,   // Long pulse duration
    .te_delta = 152,  // Tolerance
    .min_count_bit_for_found = 66,
};

typedef struct SubGhzProtocolDecoderCitroen {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    uint8_t packet_count;
} SubGhzProtocolDecoderCitroen;

typedef struct SubGhzProtocolEncoderCitroen {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolEncoderCitroen;

typedef enum {
    CitroenDecoderStepReset = 0,
    CitroenDecoderStepCheckPreamble,
    CitroenDecoderStepSaveDuration,
    CitroenDecoderStepCheckDuration,
} CitroenDecoderStep;

static void subghz_protocol_decoder_citroen_reset_internal(SubGhzProtocolDecoderCitroen* instance) {
    memset(&instance->decoder, 0, sizeof(instance->decoder));
    memset(&instance->generic, 0, sizeof(instance->generic));
    instance->decoder.parser_step = CitroenDecoderStepReset;
    instance->header_count = 0;
    instance->packet_count = 0;
}

const SubGhzProtocolDecoder subghz_protocol_citroen_decoder = {
    .alloc = subghz_protocol_decoder_citroen_alloc,
    .free = subghz_protocol_decoder_citroen_free,

    .feed = subghz_protocol_decoder_citroen_feed,
    .reset = subghz_protocol_decoder_citroen_reset,

    .get_hash_data = subghz_protocol_decoder_citroen_get_hash_data,
    .serialize = subghz_protocol_decoder_citroen_serialize,
    .deserialize = subghz_protocol_decoder_citroen_deserialize,
    .get_string = subghz_protocol_decoder_citroen_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_citroen_encoder = {
    .alloc = NULL,
    .free = NULL,

    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_citroen = {
    .name = CITROEN_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable,

    .decoder = &subghz_protocol_citroen_decoder,
    .encoder = &subghz_protocol_citroen_encoder,
};

// ----------------- Allocation / Reset / Free -------------------

void* subghz_protocol_decoder_citroen_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderCitroen* instance = calloc(1, sizeof(SubGhzProtocolDecoderCitroen));
    instance->base.protocol = &subghz_protocol_citroen;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_citroen_reset(instance);
    return instance;
}

void subghz_protocol_decoder_citroen_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_citroen_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCitroen* instance = context;
    subghz_protocol_decoder_citroen_reset_internal(instance);
}

// ----------------- Helper Functions -------------------

static uint8_t reverse8(uint8_t byte) {
    byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
    byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
    byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
    return byte;
}

// Parse Citroën/PSA data structure
static bool subghz_protocol_citroen_parse_data(SubGhzProtocolDecoderCitroen* instance) {
    uint8_t* b = (uint8_t*)&instance->generic.data;
    
    // PSA structure (similar to Peugeot Keeloq)
    // Check preamble
    if(b[0] != 0xFF || (b[1] & 0xF0) != 0xF0) {
        return false;
    }

    // Extract encrypted part (32 bits) - reversed
    uint32_t encrypted = ((uint32_t)reverse8(b[3]) << 24) | 
                        (reverse8(b[2]) << 16) | 
                        (reverse8(b[1] & 0x0F) << 8) | 
                        reverse8(b[0]);

    // Extract serial number (28 bits) - reversed
    uint32_t serial = ((uint32_t)reverse8(b[7] & 0xF0) << 20) | 
                     (reverse8(b[6]) << 12) | 
                     (reverse8(b[5]) << 4) | 
                     (reverse8(b[4]) >> 4);

    // Extract button bits (4 bits)
    uint8_t button_bits = (encrypted >> 28) & 0x0F;

    // Store parsed data
    instance->generic.serial = serial;
    instance->generic.btn = button_bits;
    instance->generic.cnt = (encrypted >> 16) & 0xFFFF; // Counter
    
    return true;
}

// ----------------- Decoder Feed -------------------

void subghz_protocol_decoder_citroen_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderCitroen* instance = context;

    switch(instance->decoder.parser_step) {
    case CitroenDecoderStepReset:
        if(level && (DURATION_DIFF(duration, subghz_protocol_citroen_const.te_short) <
                     subghz_protocol_citroen_const.te_delta)) {
            instance->decoder.parser_step = CitroenDecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case CitroenDecoderStepCheckPreamble:
        if(level) {
            if((DURATION_DIFF(duration, subghz_protocol_citroen_const.te_short) <
                subghz_protocol_citroen_const.te_delta)) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = CitroenDecoderStepReset;
            }
        } else {
            if((DURATION_DIFF(duration, subghz_protocol_citroen_const.te_short) <
                subghz_protocol_citroen_const.te_delta) &&
               (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_citroen_const.te_short) <
                subghz_protocol_citroen_const.te_delta)) {
                instance->header_count++;
            } else if((DURATION_DIFF(duration, 4400) < 500) && instance->header_count >= 10) {
                instance->decoder.parser_step = CitroenDecoderStepSaveDuration;
                instance->decoder.decode_data = 0ULL;
                instance->decoder.decode_count_bit = 0;
            } else {
                instance->decoder.parser_step = CitroenDecoderStepReset;
            }
        }
        break;

    case CitroenDecoderStepSaveDuration:
        if(level) {
            if(duration >= (subghz_protocol_citroen_const.te_long * 3)) {
                if(instance->decoder.decode_count_bit >= 
                   subghz_protocol_citroen_const.min_count_bit_for_found) {
                    
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(subghz_protocol_citroen_parse_data(instance)) {
                        instance->packet_count++;
                        
                        if(instance->base.callback) {
                            instance->base.callback(&instance->base, instance->base.context);
                        }
                    }
                }
                subghz_protocol_decoder_citroen_reset_internal(instance);
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = CitroenDecoderStepCheckDuration;
            }
        } else {
            instance->decoder.parser_step = CitroenDecoderStepReset;
        }
        break;

    case CitroenDecoderStepCheckDuration:
        if(!level) {
            // PWM decoding
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_citroen_const.te_short) <
                subghz_protocol_citroen_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_citroen_const.te_long) <
                subghz_protocol_citroen_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = CitroenDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_citroen_const.te_long) <
                 subghz_protocol_citroen_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_citroen_const.te_short) <
                 subghz_protocol_citroen_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = CitroenDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = CitroenDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = CitroenDecoderStepReset;
        }
        break;
    }
}

// ----------------- API -------------------

uint8_t subghz_protocol_decoder_citroen_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderCitroen* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_citroen_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderCitroen* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus subghz_protocol_decoder_citroen_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderCitroen* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, 
        flipper_format, 
        subghz_protocol_citroen_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_citroen_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderCitroen* instance = context;

    uint32_t hi = instance->generic.data >> 32;
    uint32_t lo = instance->generic.data & 0xFFFFFFFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%07lX Btn:%X Cnt:%04lX\r\n"
        "Type:PSA/Keeloq\r\n"
        "Models:2005-2018\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        hi,
        lo,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

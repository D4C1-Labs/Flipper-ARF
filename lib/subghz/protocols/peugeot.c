#include "peugeot.h"

#define TAG "SubGhzProtocolPeugeot"

static const SubGhzBlockConst subghz_protocol_peugeot_const = {
    .te_short = 370,  // Short pulse duration
    .te_long = 772,   // Long pulse duration (~2x short)
    .te_delta = 152,  // Tolerance
    .min_count_bit_for_found = 66,
};

typedef struct SubGhzProtocolDecoderPeugeot {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    uint8_t packet_count;
} SubGhzProtocolDecoderPeugeot;

typedef struct SubGhzProtocolEncoderPeugeot {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolEncoderPeugeot;

typedef enum {
    PeugeotDecoderStepReset = 0,
    PeugeotDecoderStepCheckPreamble,
    PeugeotDecoderStepSaveDuration,
    PeugeotDecoderStepCheckDuration,
} PeugeotDecoderStep;

static void subghz_protocol_decoder_peugeot_reset_internal(SubGhzProtocolDecoderPeugeot* instance) {
    memset(&instance->decoder, 0, sizeof(instance->decoder));
    memset(&instance->generic, 0, sizeof(instance->generic));
    instance->decoder.parser_step = PeugeotDecoderStepReset;
    instance->header_count = 0;
    instance->packet_count = 0;
}

const SubGhzProtocolDecoder subghz_protocol_peugeot_decoder = {
    .alloc = subghz_protocol_decoder_peugeot_alloc,
    .free = subghz_protocol_decoder_peugeot_free,

    .feed = subghz_protocol_decoder_peugeot_feed,
    .reset = subghz_protocol_decoder_peugeot_reset,

    .get_hash_data = subghz_protocol_decoder_peugeot_get_hash_data,
    .serialize = subghz_protocol_decoder_peugeot_serialize,
    .deserialize = subghz_protocol_decoder_peugeot_deserialize,
    .get_string = subghz_protocol_decoder_peugeot_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_peugeot_encoder = {
    .alloc = NULL,
    .free = NULL,

    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_peugeot = {
    .name = PEUGEOT_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable,

    .decoder = &subghz_protocol_peugeot_decoder,
    .encoder = &subghz_protocol_peugeot_encoder,
};

// ----------------- Allocation / Reset / Free -------------------

void* subghz_protocol_decoder_peugeot_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPeugeot* instance = calloc(1, sizeof(SubGhzProtocolDecoderPeugeot));
    instance->base.protocol = &subghz_protocol_peugeot;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_peugeot_reset(instance);
    return instance;
}

void subghz_protocol_decoder_peugeot_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_peugeot_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugeot* instance = context;
    subghz_protocol_decoder_peugeot_reset_internal(instance);
}

// ----------------- Helper Functions -------------------

// Reverse 8 bits (LSB to MSB)
static uint8_t reverse8(uint8_t byte) {
    byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
    byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
    byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
    return byte;
}

// Parse Keeloq data structure
static bool subghz_protocol_peugeot_parse_data(SubGhzProtocolDecoderPeugeot* instance) {
    uint8_t* b = (uint8_t*)&instance->generic.data;
    
    // Check preamble (first 12 bits should be 0xFFF)
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

    // Extract button bits (4 bits from encrypted part)
    // Note: Button bits are (MSB/first sent to LSB) S3, S0, S1, S2
    uint8_t button_bits = (encrypted >> 28) & 0x0F;

    // Store parsed data
    instance->generic.serial = serial;
    instance->generic.btn = button_bits;
    instance->generic.cnt = (encrypted >> 16) & 0xFFFF; // Counter from encrypted part
    
    return true;
}

// ----------------- Decoder Feed -------------------

void subghz_protocol_decoder_peugeot_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugeot* instance = context;

    switch(instance->decoder.parser_step) {
    case PeugeotDecoderStepReset:
        if(level && (DURATION_DIFF(duration, subghz_protocol_peugeot_const.te_short) <
                     subghz_protocol_peugeot_const.te_delta)) {
            instance->decoder.parser_step = PeugeotDecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
        }
        break;

    case PeugeotDecoderStepCheckPreamble:
        if(level) {
            // High level - save duration
            if((DURATION_DIFF(duration, subghz_protocol_peugeot_const.te_short) <
                subghz_protocol_peugeot_const.te_delta)) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = PeugeotDecoderStepReset;
            }
        } else {
            // Low level - check for warm-up pulses
            if((DURATION_DIFF(duration, subghz_protocol_peugeot_const.te_short) <
                subghz_protocol_peugeot_const.te_delta) &&
               (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_peugeot_const.te_short) <
                subghz_protocol_peugeot_const.te_delta)) {
                // Short pulse pair - part of warm-up
                instance->header_count++;
            } else if((DURATION_DIFF(duration, 4400) < 500) && instance->header_count >= 10) {
                // Long gap after warm-up pulses (~4400µs)
                instance->decoder.parser_step = PeugeotDecoderStepSaveDuration;
                instance->decoder.decode_data = 0ULL;
                instance->decoder.decode_count_bit = 0;
            } else {
                instance->decoder.parser_step = PeugeotDecoderStepReset;
            }
        }
        break;

    case PeugeotDecoderStepSaveDuration:
        if(level) {
            // High level - save duration
            if(duration >= (subghz_protocol_peugeot_const.te_long * 3)) {
                // Very long pulse - end of packet
                if(instance->decoder.decode_count_bit >= 
                   subghz_protocol_peugeot_const.min_count_bit_for_found) {
                    
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    // Parse the Keeloq structure
                    if(subghz_protocol_peugeot_parse_data(instance)) {
                        instance->packet_count++;
                        
                        // Call callback after receiving at least one packet
                        if(instance->base.callback) {
                            instance->base.callback(&instance->base, instance->base.context);
                        }
                    }
                }
                subghz_protocol_decoder_peugeot_reset_internal(instance);
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = PeugeotDecoderStepCheckDuration;
            }
        } else {
            instance->decoder.parser_step = PeugeotDecoderStepReset;
        }
        break;

    case PeugeotDecoderStepCheckDuration:
        if(!level) {
            // PWM decoding: short-long = 0, long-short = 1
            if((DURATION_DIFF(instance->decoder.te_last, subghz_protocol_peugeot_const.te_short) <
                subghz_protocol_peugeot_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_peugeot_const.te_long) <
                subghz_protocol_peugeot_const.te_delta)) {
                // Short high, long low = 0
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = PeugeotDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_peugeot_const.te_long) <
                 subghz_protocol_peugeot_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_peugeot_const.te_short) <
                 subghz_protocol_peugeot_const.te_delta)) {
                // Long high, short low = 1
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = PeugeotDecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = PeugeotDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = PeugeotDecoderStepReset;
        }
        break;
    }
}

// ----------------- API -------------------

uint8_t subghz_protocol_decoder_peugeot_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugeot* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_peugeot_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugeot* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus subghz_protocol_decoder_peugeot_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugeot* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, 
        flipper_format, 
        subghz_protocol_peugeot_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_peugeot_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderPeugeot* instance = context;

    uint32_t hi = instance->generic.data >> 32;
    uint32_t lo = instance->generic.data & 0xFFFFFFFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Sn:%07lX Btn:%X Cnt:%04lX\r\n"
        "Type:Keeloq/HCS\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        hi,
        lo,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

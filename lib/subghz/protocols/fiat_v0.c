#include "fiat_v0.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>

#define TAG                     "FiatProtocolV0"
#define FIAT_V0_PREAMBLE_PAIRS  150
#define FIAT_V0_GAP_US          800
#define FIAT_V0_TOTAL_BURSTS    3
#define FIAT_V0_INTER_BURST_GAP 25000
#define FIAT_V0_UPLOAD_CAPACITY 1328U

static const SubGhzBlockConst subghz_protocol_fiat_v0_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

struct SubGhzProtocolDecoderFiatV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_state;
    uint16_t preamble_count;
    uint32_t data_low;
    uint32_t data_high;
    uint8_t bit_count;
    uint32_t hop;
    uint32_t fix;
    uint8_t endbyte;
};

typedef enum {
    FiatV0DecoderStepReset = 0,
    FiatV0DecoderStepPreamble = 1,
    FiatV0DecoderStepData = 2,
} FiatV0DecoderStep;

static void fiat_v0_finish_packet(struct SubGhzProtocolDecoderFiatV0* instance) {
    instance->generic.data = ((uint64_t)instance->hop << 32) | instance->fix;
    instance->generic.data_count_bit = 71;
    instance->generic.serial = instance->fix;
    instance->generic.btn = instance->endbyte;
    instance->generic.cnt = instance->hop;
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;
    if(instance->base.callback) instance->base.callback(&instance->base, instance->base.context);
    instance->data_low = 0;
    instance->data_high = 0;
    instance->bit_count = 0;
    instance->decoder.parser_step = FiatV0DecoderStepReset;
}

struct SubGhzProtocolEncoderFiatV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t hop;
    uint32_t fix;
    uint8_t endbyte;
};

static void subghz_protocol_decoder_fiat_v0_free(void* context) {
    furi_assert(context);
    free(context);
}

const SubGhzProtocolDecoder subghz_protocol_fiat_v0_decoder = {
    .alloc = subghz_protocol_decoder_fiat_v0_alloc,
    .free = subghz_protocol_decoder_fiat_v0_free,
    .feed = subghz_protocol_decoder_fiat_v0_feed,
    .reset = subghz_protocol_decoder_fiat_v0_reset,
    .get_hash_data = subghz_protocol_decoder_fiat_v0_get_hash_data,
    .serialize = subghz_protocol_decoder_fiat_v0_serialize,
    .deserialize = subghz_protocol_decoder_fiat_v0_deserialize,
    .get_string = subghz_protocol_decoder_fiat_v0_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_fiat_v0_encoder = {
    .alloc = subghz_protocol_encoder_fiat_v0_alloc,
    .free = subghz_protocol_encoder_fiat_v0_free,
    .deserialize = subghz_protocol_encoder_fiat_v0_deserialize,
    .stop = subghz_protocol_encoder_fiat_v0_stop,
    .yield = subghz_protocol_encoder_fiat_v0_yield,
};

const SubGhzProtocol fiat_protocol_v0 = {
    .name = FIAT_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_fiat_v0_decoder,
    .encoder = &subghz_protocol_fiat_v0_encoder,
};

// ============================================================================
// ENCODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_encoder_fiat_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFiatV0* instance = calloc(1, sizeof(SubGhzProtocolEncoderFiatV0));
    furi_check(instance);

    instance->base.protocol = &fiat_protocol_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;

    return instance;
}

void subghz_protocol_encoder_fiat_v0_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatV0* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static void subghz_protocol_encoder_fiat_v0_get_upload(SubGhzProtocolEncoderFiatV0* instance) {
    furi_check(instance);
    LevelDuration* up = instance->encoder.upload;
    if(up == NULL) return;

    size_t index = 0;
    uint32_t te_short = subghz_protocol_fiat_v0_const.te_short;
    uint32_t te_long = subghz_protocol_fiat_v0_const.te_long;

    uint64_t data = ((uint64_t)instance->hop << 32) | instance->fix;
    uint8_t endbyte_to_send = instance->endbyte >> 1;

    for(uint8_t burst = 0; burst < FIAT_V0_TOTAL_BURSTS; burst++) {
        if(burst > 0) {
            up[index++] = level_duration_make(false, FIAT_V0_INTER_BURST_GAP);
        }

        for(int i = 0; i < FIAT_V0_PREAMBLE_PAIRS; i++) {
            up[index++] = level_duration_make(true, te_short);
            up[index++] = level_duration_make(false, te_short);
        }
        if(index > 0) up[index - 1] = level_duration_make(false, FIAT_V0_GAP_US);

        bool first_bit = (data >> 63) & 1;
        if(first_bit) {
            up[index++] = level_duration_make(true, te_long);
        } else {
            up[index++] = level_duration_make(true, te_short);
            up[index++] = level_duration_make(false, te_long);
        }
        bool prev_bit = first_bit;

        for(int bit = 62; bit >= 0; bit--) {
            bool curr_bit = (data >> bit) & 1;
            if(!prev_bit && !curr_bit) {
                up[index++] = level_duration_make(true, te_short);
                up[index++] = level_duration_make(false, te_short);
            } else if(!prev_bit && curr_bit) {
                up[index++] = level_duration_make(true, te_long);
            } else if(prev_bit && !curr_bit) {
                up[index++] = level_duration_make(false, te_long);
            } else {
                up[index++] = level_duration_make(false, te_short);
                up[index++] = level_duration_make(true, te_short);
            }
            prev_bit = curr_bit;
        }

        for(int bit = 5; bit >= 0; bit--) {
            bool curr_bit = (endbyte_to_send >> bit) & 1;
            if(!prev_bit && !curr_bit) {
                up[index++] = level_duration_make(true, te_short);
                up[index++] = level_duration_make(false, te_short);
            } else if(!prev_bit && curr_bit) {
                up[index++] = level_duration_make(true, te_long);
            } else if(prev_bit && !curr_bit) {
                up[index++] = level_duration_make(false, te_long);
            } else {
                up[index++] = level_duration_make(false, te_short);
                up[index++] = level_duration_make(true, te_short);
            }
            prev_bit = curr_bit;
        }

        if(prev_bit) {
            up[index++] = level_duration_make(false, te_short);
        }
        up[index++] = level_duration_make(false, te_short * 8);
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderFiatV0* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    do {
        flipper_format_rewind(flipper_format);
        uint32_t temp_data = 0;
        if(!flipper_format_read_uint32(flipper_format, "Bit", &temp_data, 1)) {
            break;
        }
        if(temp_data != 64 && temp_data != 71) {
            break;
        }
        instance->generic.data_count_bit = temp_data;

        flipper_format_rewind(flipper_format);
        uint8_t key_bytes[8];
        if(!flipper_format_read_hex(flipper_format, "Key", key_bytes, 8)) {
            break;
        }
        uint64_t key = 0;
        for(int i = 0; i < 8; i++) {
            key = (key << 8) | key_bytes[i];
        }
        instance->generic.data = key;
        instance->hop = (uint32_t)(key >> 32);
        instance->fix = (uint32_t)(key & 0xFFFFFFFFU);

        uint32_t eb_read = 0;
        flipper_format_rewind(flipper_format);
        bool have_endbyte = flipper_format_read_uint32(flipper_format, "EndByte", &eb_read, 1);

        flipper_format_rewind(flipper_format);
        uint32_t btn_u32 = 0;
        flipper_format_read_uint32(flipper_format, "Btn", &btn_u32, 1);

        if(have_endbyte) {
            instance->endbyte = (uint8_t)(eb_read & 0x7FU);
        } else {
            instance->endbyte = (uint8_t)(btn_u32 & 0x7FU);
        }

        instance->generic.btn = instance->endbyte;
        instance->generic.cnt = instance->hop;
        instance->generic.serial = instance->fix;

        flipper_format_rewind(flipper_format);
        uint32_t repeat = 10;
        flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
        instance->encoder.repeat = repeat;

        instance->encoder.upload = malloc(FIAT_V0_UPLOAD_CAPACITY * sizeof(LevelDuration));
        subghz_protocol_encoder_fiat_v0_get_upload(instance);
        instance->encoder.is_running = true;

        return SubGhzProtocolStatusOk;
    } while(false);

    return SubGhzProtocolStatusError;
}

void subghz_protocol_encoder_fiat_v0_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatV0* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

LevelDuration subghz_protocol_encoder_fiat_v0_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderFiatV0* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0 ||
       instance->encoder.size_upload == 0) {
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
// ============================================================================
// DECODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_decoder_fiat_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderFiatV0* instance = calloc(1, sizeof(SubGhzProtocolDecoderFiatV0));
    furi_check(instance);
    instance->base.protocol = &fiat_protocol_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_fiat_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;
    instance->decoder.parser_step = FiatV0DecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->preamble_count = 0;
    instance->data_low = 0;
    instance->data_high = 0;
    instance->bit_count = 0;
    instance->hop = 0;
    instance->fix = 0;
    instance->endbyte = 0;
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_fiat_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;

    uint32_t te_short = (uint32_t)subghz_protocol_fiat_v0_const.te_short;
    uint32_t te_long = (uint32_t)subghz_protocol_fiat_v0_const.te_long;
    uint32_t te_delta = (uint32_t)subghz_protocol_fiat_v0_const.te_delta;
    uint32_t gap_threshold = FIAT_V0_GAP_US;
    uint32_t diff;

    switch(instance->decoder.parser_step) {
    case FiatV0DecoderStepReset:
        if(!level) return;
        if(duration < te_short) {
            diff = te_short - duration;
        } else {
            diff = duration - te_short;
        }
        if(diff < te_delta) {
            instance->data_low = 0;
            instance->data_high = 0;
            instance->decoder.parser_step = FiatV0DecoderStepPreamble;
            instance->preamble_count = 0;
            instance->bit_count = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        }
        break;

    case FiatV0DecoderStepPreamble:
        if(level) {
            if(duration < te_short) {
                diff = te_short - duration;
            } else {
                diff = duration - te_short;
            }
            if(diff < te_delta) {
                instance->preamble_count++;
            } else {
                instance->decoder.parser_step = FiatV0DecoderStepReset;
            }
            return;
        }

        if(duration < te_short) {
            diff = te_short - duration;
        } else {
            diff = duration - te_short;
        }

        if(diff < te_delta) {
            instance->preamble_count++;
        } else {
            if(instance->preamble_count >= FIAT_V0_PREAMBLE_PAIRS) {
                if(duration < gap_threshold) {
                    diff = gap_threshold - duration;
                } else {
                    diff = duration - gap_threshold;
                }
                if(diff < te_delta) {
                    instance->decoder.parser_step = FiatV0DecoderStepData;
                    instance->preamble_count = 0;
                    instance->data_low = 0;
                    instance->data_high = 0;
                    instance->bit_count = 0;
                    manchester_advance(
                        instance->manchester_state,
                        ManchesterEventReset,
                        &instance->manchester_state,
                        NULL);
                    return;
                }
            }
            instance->decoder.parser_step = FiatV0DecoderStepReset;
        }

        if(instance->preamble_count >= FIAT_V0_PREAMBLE_PAIRS &&
           instance->decoder.parser_step == FiatV0DecoderStepPreamble) {
            if(duration < gap_threshold) {
                diff = gap_threshold - duration;
            } else {
                diff = duration - gap_threshold;
            }
            if(diff < te_delta) {
                instance->decoder.parser_step = FiatV0DecoderStepData;
                instance->preamble_count = 0;
                instance->data_low = 0;
                instance->data_high = 0;
                instance->bit_count = 0;
                manchester_advance(
                    instance->manchester_state,
                    ManchesterEventReset,
                    &instance->manchester_state,
                    NULL);
                return;
            }
        }
        break;

    case FiatV0DecoderStepData: {
        ManchesterEvent event = ManchesterEventReset;
        if(duration < te_short) {
            diff = te_short - duration;
            if(diff < te_delta) {
                event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            }
        } else {
            diff = duration - te_short;
            if(diff < te_delta) {
                event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
            } else {
                if(duration < te_long) {
                    diff = te_long - duration;
                } else {
                    diff = duration - te_long;
                }
                if(diff < te_delta) {
                    event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
                }
            }
        }

        if(event != ManchesterEventReset) {
            bool data_bit_bool;
            if(manchester_advance(
                   instance->manchester_state,
                   event,
                   &instance->manchester_state,
                   &data_bit_bool)) {
                uint32_t new_bit = data_bit_bool ? 1 : 0;
                uint32_t carry = (instance->data_low >> 31) & 1;
                instance->data_low = (instance->data_low << 1) | new_bit;
                instance->data_high = (instance->data_high << 1) | carry;
                instance->bit_count++;

                if(instance->bit_count == 64) {
                    instance->fix = instance->data_low;
                    instance->hop = instance->data_high;
                    instance->data_low = 0;
                    instance->data_high = 0;
                }
                if(instance->bit_count == 0x47) {
                    instance->endbyte = (uint8_t)(instance->data_low & 0x3F);
                    fiat_v0_finish_packet(instance);
                }
            }
        } else {
            if(instance->bit_count == 0x47) {
                instance->endbyte = (uint8_t)(instance->data_low & 0x3F);
                fiat_v0_finish_packet(instance);
            } else if(instance->bit_count < 64) {
                instance->decoder.parser_step = FiatV0DecoderStepReset;
            }
        }
        break;
    }
    default:
        break;
    }
}

uint8_t subghz_protocol_decoder_fiat_v0_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_fiat_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    uint32_t serial = instance->fix;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &serial, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t btn = instance->endbyte;
    if(!flipper_format_write_uint32(flipper_format, "Btn", &btn, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t cnt = instance->hop;
    if(!flipper_format_write_uint32(flipper_format, "Cnt", &cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t endbyte_ff = instance->endbyte;
    if(!flipper_format_write_uint32(flipper_format, "EndByte", &endbyte_ff, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_fiat_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;

    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_fiat_v0_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    instance->hop = (uint32_t)(instance->generic.data >> 32U);
    instance->fix = (uint32_t)(instance->generic.data & 0xFFFFFFFFU);
    instance->decoder.decode_data = instance->generic.data;
    instance->decoder.decode_count_bit = instance->generic.data_count_bit;

    uint32_t endbyte_u32 = 0U;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "EndByte", &endbyte_u32, 1)) {
        instance->endbyte = (uint8_t)(endbyte_u32 & 0x7FU);
    } else {
        flipper_format_rewind(flipper_format);
        uint32_t btn = 0;
        flipper_format_read_uint32(flipper_format, "Btn", &btn, 1);
        instance->endbyte = (uint8_t)(btn & 0x7FU);
    }

    instance->generic.serial = instance->fix;
    instance->generic.btn = instance->endbyte;
    instance->generic.cnt = instance->hop;

    return status;
}

void subghz_protocol_decoder_fiat_v0_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderFiatV0* instance = context;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Hop:%08lX\r\n"
        "Sn:%08lX\r\n"
        "EndByte:%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->hop,
        instance->fix,
        instance->hop,
        instance->fix,
        instance->endbyte & 0x3F);
}

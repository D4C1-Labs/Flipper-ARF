#include "secplus_v1.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include "../blocks/custom_btn_i.h"

/*
* Help
* https://github.com/argilo/secplus
* https://github.com/merbanan/rtl_433/blob/master/src/devices/secplus_v1.c
*/

#define TAG "SubGhzProtocoSecPlusV1"

#define SECPLUS_V1_BIT_ERR -1 //0b0000
#define SECPLUS_V1_BIT_0   0 //0b0001
#define SECPLUS_V1_BIT_1   1 //0b0011
#define SECPLUS_V1_BIT_2   2 //0b0111

#define SECPLUS_V1_PACKET_1_HEADER     0x00
#define SECPLUS_V1_PACKET_2_HEADER     0x02
#define SECPLUS_V1_PACKET_1_INDEX_BASE 0
#define SECPLUS_V1_PACKET_2_INDEX_BASE 21
#define SECPLUS_V1_PACKET_1_ACCEPTED   (1 << 0)
#define SECPLUS_V1_PACKET_2_ACCEPTED   (1 << 1)

static const SubGhzBlockConst subghz_protocol_secplus_v1_const = {
    .te_short = 500,
    .te_long = 1500,
    .te_delta = 100,
    .min_count_bit_for_found = 21,
};

struct SubGhzProtocolDecoderSecPlus_v1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t packet_accepted;
    uint8_t base_packet_index;
    uint8_t data_array[44];
};

struct SubGhzProtocolEncoderSecPlus_v1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t data_array[44];
};

typedef enum {
    SecPlus_v1DecoderStepReset = 0,
    SecPlus_v1DecoderStepSearchStartBit,
    SecPlus_v1DecoderStepSaveDuration,
    SecPlus_v1DecoderStepDecoderData,
} SecPlus_v1DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_secplus_v1_decoder = {
    .alloc = subghz_protocol_decoder_secplus_v1_alloc,
    .free = subghz_protocol_decoder_secplus_v1_free,

    .feed = subghz_protocol_decoder_secplus_v1_feed,
    .reset = subghz_protocol_decoder_secplus_v1_reset,

    .get_hash_data = subghz_protocol_decoder_secplus_v1_get_hash_data,
    .serialize = subghz_protocol_decoder_secplus_v1_serialize,
    .deserialize = subghz_protocol_decoder_secplus_v1_deserialize,
    .get_string = subghz_protocol_decoder_secplus_v1_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_secplus_v1_encoder = {
    .alloc = subghz_protocol_encoder_secplus_v1_alloc,
    .free = subghz_protocol_encoder_secplus_v1_free,

    .deserialize = subghz_protocol_encoder_secplus_v1_deserialize,
    .stop = subghz_protocol_encoder_secplus_v1_stop,
    .yield = subghz_protocol_encoder_secplus_v1_yield,
};

const SubGhzProtocol subghz_protocol_secplus_v1 = {
    .name = SUBGHZ_PROTOCOL_SECPLUS_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Send | SubGhzProtocolFlag_Save,

    .decoder = &subghz_protocol_secplus_v1_decoder,
    .encoder = &subghz_protocol_secplus_v1_encoder,
};

void* subghz_protocol_encoder_secplus_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderSecPlus_v1* instance = malloc(sizeof(SubGhzProtocolEncoderSecPlus_v1));

    instance->base.protocol = &subghz_protocol_secplus_v1;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 128;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_secplus_v1_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderSecPlus_v1* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

/**
 * Generating an upload from data.
 * @param instance Pointer to a SubGhzProtocolEncoderSecPlus_v1 instance
 * @return true On success
 */
static bool
    subghz_protocol_encoder_secplus_v1_get_upload(SubGhzProtocolEncoderSecPlus_v1* instance) {
    furi_assert(instance);
    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2);
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Encoder size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    //Send header packet 1
    instance->encoder.upload[index++] = level_duration_make(
        false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * (116 + 3));
    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short);

    //Send data packet 1
    for(uint8_t i = SECPLUS_V1_PACKET_1_INDEX_BASE + 1; i < SECPLUS_V1_PACKET_1_INDEX_BASE + 21;
        i++) {
        switch(instance->data_array[i]) {
        case SECPLUS_V1_BIT_0:
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short);
            break;
        case SECPLUS_V1_BIT_1:
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 2);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 2);
            break;
        case SECPLUS_V1_BIT_2:
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_secplus_v1_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);
            break;

        default:
            FURI_LOG_E(TAG, "Encoder error, wrong bit type");
            return false;
            break;
        }
    }

    //Send header packet 2
    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * (116));
    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);

    //Send data packet 2
    for(uint8_t i = SECPLUS_V1_PACKET_2_INDEX_BASE + 1; i < SECPLUS_V1_PACKET_2_INDEX_BASE + 21;
        i++) {
        switch(instance->data_array[i]) {
        case SECPLUS_V1_BIT_0:
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short);
            break;
        case SECPLUS_V1_BIT_1:
            instance->encoder.upload[index++] = level_duration_make(
                false, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 2);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 2);
            break;
        case SECPLUS_V1_BIT_2:
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_secplus_v1_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_secplus_v1_const.te_short * 3);
            break;

        default:
            FURI_LOG_E(TAG, "Encoder error, wrong bit type.");
            return false;
            break;
        }
    }

    return true;
}

/** 
 * Security+ 1.0 message encoding
 * @param instance SubGhzProtocolEncoderSecPlus_v1* 
 */

static bool subghz_protocol_secplus_v1_encode(SubGhzProtocolEncoderSecPlus_v1* instance) {
    uint32_t fixed = (instance->generic.data >> 32) & 0xFFFFFFFF;
    uint32_t rolling = instance->generic.data & 0xFFFFFFFF;

    uint8_t rolling_array[20] = {0};
    uint8_t fixed_array[20] = {0};
    uint32_t acc = 0;

    //increment the counter
    //rolling += 2; - old way
    // Experemental case - we dont know counter size exactly, so just will be think that it is in range of 0xE6000000 - 0xFFFFFFFF

    // Check for OFEX (overflow experimental) mode
    if(furi_hal_subghz_get_rolling_counter_mult() != -0x7FFFFFFF) {
        // standart counter mode. PULL data from subghz_block_generic_global variables
        if(!subghz_block_generic_global_counter_override_get(&rolling)) {
            // if counter_override_get return FALSE then counter was not changed and we increase counter by standart mult value
            if((rolling + furi_hal_subghz_get_rolling_counter_mult()) > 0xFFFFFFFF) {
                rolling = 0xE6000000;
            } else {
                rolling += furi_hal_subghz_get_rolling_counter_mult();
            }
        }
        if(rolling < 0xE6000000) rolling = 0xE6000000;
    } else {
        // OFEX (overflow experimental) mode
        if((rolling + 0x1) > 0xFFFFFFFF) {
            rolling = 0xE6000000;
        } else if(rolling >= 0xE6000000 && rolling != 0xFFFFFFFE) {
            rolling = 0xFFFFFFFE;
        } else {
            rolling++;
        }
    }

    //update data
    instance->generic.data &= 0xFFFFFFFF00000000;
    instance->generic.data |= rolling;

    if(fixed > 0xCFD41B90) {
        FURI_LOG_E(TAG, "Encode wrong fixed data");
        return false;
    }

    rolling = subghz_protocol_blocks_reverse_key(rolling, 32);

    for(int i = 19; i > -1; i--) {
        rolling_array[i] = rolling % 3;
        rolling /= 3;
        fixed_array[i] = fixed % 3;
        fixed /= 3;
    }

    instance->data_array[SECPLUS_V1_PACKET_1_INDEX_BASE] = SECPLUS_V1_PACKET_1_HEADER;
    instance->data_array[SECPLUS_V1_PACKET_2_INDEX_BASE] = SECPLUS_V1_PACKET_2_HEADER;

    //encode packet 1
    for(uint8_t i = 1; i < 11; i++) {
        acc += rolling_array[i - 1];
        instance->data_array[i * 2 - 1] = rolling_array[i - 1];
        acc += fixed_array[i - 1];
        instance->data_array[i * 2] = acc % 3;
    }

    acc = 0;
    //encode packet 2
    for(uint8_t i = 11; i < 21; i++) {
        acc += rolling_array[i - 1];
        instance->data_array[i * 2] = rolling_array[i - 1];
        acc += fixed_array[i - 1];
        instance->data_array[i * 2 + 1] = acc % 3;
    }

    return true;
}

// Full D-pad support. Security+ 1.0 stores a real button in the fixed part of the
// frame as the least-significant base-3 digit: Btn = fixed % 3, with three valid
// values 0, 1 and 2 (see get_string / check_fixed). fixed is the high 32 bits of
// generic.data. The encoder does not populate generic.btn via
// subghz_block_generic_deserialize, so we derive the button from the fixed word,
// enable the D-pad and, for each direction, replace only that base-3 digit with a
// different real button value while leaving the rest of fixed intact. OK re-sends
// the originally captured button.
static uint8_t subghz_protocol_secplus_v1_get_btn_code(uint8_t original_btn) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t btn = original_btn;

    if(custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) {
        btn = original_btn;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        btn = 0x0;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        btn = 0x1;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_LEFT) {
        btn = 0x2;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_RIGHT) {
        btn = original_btn;
    }

    // Only 0, 1 and 2 are valid Security+ 1.0 button digits.
    return btn % 3;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_secplus_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderSecPlus_v1* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            2 * subghz_protocol_secplus_v1_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        // Optional value
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        // Full D-pad: derive the original button from the fixed word (btn = fixed % 3)
        // and re-encode it based on the current custom button selection, rewriting
        // only the least-significant base-3 digit of fixed.
        {
            uint32_t fixed = (uint32_t)(instance->generic.data >> 32);
            uint8_t original_btn = (uint8_t)(fixed % 3);
            if(subghz_custom_btn_get_original() == 0) {
                subghz_custom_btn_set_original(original_btn);
            }
            subghz_custom_btn_set_max(4);
            uint8_t new_btn = subghz_protocol_secplus_v1_get_btn_code(original_btn);
            fixed = (fixed - original_btn) + new_btn;
            instance->generic.data =
                ((uint64_t)fixed << 32) | (instance->generic.data & 0xFFFFFFFF);
        }

        if(!subghz_protocol_secplus_v1_encode(instance)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        if(!subghz_protocol_encoder_secplus_v1_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            ;
            break;
        }

        uint8_t key_data[sizeof(uint64_t)] = {0};
        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> (i * 8)) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to add Key");
            ret = SubGhzProtocolStatusErrorParserKey;
            break;
        }

        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_secplus_v1_stop(void* context) {
    SubGhzProtocolEncoderSecPlus_v1* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_secplus_v1_yield(void* context) {
    SubGhzProtocolEncoderSecPlus_v1* instance = context;

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

void* subghz_protocol_decoder_secplus_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderSecPlus_v1* instance = malloc(sizeof(SubGhzProtocolDecoderSecPlus_v1));
    instance->base.protocol = &subghz_protocol_secplus_v1;
    instance->generic.protocol_name = instance->base.protocol->name;

    return instance;
}

void subghz_protocol_decoder_secplus_v1_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v1* instance = context;
    free(instance);
}

void subghz_protocol_decoder_secplus_v1_reset(void* context) {
    furi_assert(context);
    // SubGhzProtocolDecoderSecPlus_v1* instance = context;
    // does not reset the decoder because you need to get 2 parts of the package
}

/** 
 * Security+ 1.0 message decoding
 * @param instance SubGhzProtocolDecoderSecPlus_v1* 
 */

static void subghz_protocol_secplus_v1_decode(SubGhzProtocolDecoderSecPlus_v1* instance) {
    uint32_t rolling = 0;
    uint32_t fixed = 0;
    uint32_t acc = 0;
    uint8_t digit = 0;

    //decode packet 1
    for(uint8_t i = 1; i < 21; i += 2) {
        digit = instance->data_array[i];
        rolling = (rolling * 3) + digit;
        acc += digit;

        digit = (60 + instance->data_array[i + 1] - acc) % 3;
        fixed = (fixed * 3) + digit;
        acc += digit;
    }

    acc = 0;
    //decode packet 2
    for(uint8_t i = 22; i < 42; i += 2) {
        digit = instance->data_array[i];
        rolling = (rolling * 3) + digit;
        acc += digit;

        digit = (60 + instance->data_array[i + 1] - acc) % 3;
        fixed = (fixed * 3) + digit;
        acc += digit;
    }

    rolling = subghz_protocol_blocks_reverse_key(rolling, 32);
    instance->generic.data = (uint64_t)fixed << 32 | rolling;

    instance->generic.data_count_bit =
        subghz_protocol_secplus_v1_const.min_count_bit_for_found * 2;
}

void subghz_protocol_decoder_secplus_v1_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v1* instance = context;

    switch(instance->decoder.parser_step) {
    case SecPlus_v1DecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short * 120) <
                        subghz_protocol_secplus_v1_const.te_delta * 120)) {
            //Found header Security+ 1.0
            instance->decoder.parser_step = SecPlus_v1DecoderStepSearchStartBit;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->packet_accepted = 0;
            memset(instance->data_array, 0, sizeof(instance->data_array));
        }
        break;
    case SecPlus_v1DecoderStepSearchStartBit:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short) <
               subghz_protocol_secplus_v1_const.te_delta) {
                instance->base_packet_index = SECPLUS_V1_PACKET_1_INDEX_BASE;
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_0;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_long) <
                subghz_protocol_secplus_v1_const.te_delta) {
                instance->base_packet_index = SECPLUS_V1_PACKET_2_INDEX_BASE;
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_2;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
        }
        break;
    case SecPlus_v1DecoderStepSaveDuration:
        if(!level) { //save interval
            if(DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short * 120) <
               subghz_protocol_secplus_v1_const.te_delta * 120) {
                if(instance->decoder.decode_count_bit ==
                   subghz_protocol_secplus_v1_const.min_count_bit_for_found) {
                    if(instance->base_packet_index == SECPLUS_V1_PACKET_1_INDEX_BASE)
                        instance->packet_accepted |= SECPLUS_V1_PACKET_1_ACCEPTED;
                    if(instance->base_packet_index == SECPLUS_V1_PACKET_2_INDEX_BASE)
                        instance->packet_accepted |= SECPLUS_V1_PACKET_2_ACCEPTED;

                    if(instance->packet_accepted ==
                       (SECPLUS_V1_PACKET_1_ACCEPTED | SECPLUS_V1_PACKET_2_ACCEPTED)) {
                        subghz_protocol_secplus_v1_decode(instance);

                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                        instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
                    }
                }
                instance->decoder.parser_step = SecPlus_v1DecoderStepSearchStartBit;
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
            } else {
                instance->decoder.te_last = duration;
                instance->decoder.parser_step = SecPlus_v1DecoderStepDecoderData;
            }
        } else {
            instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
        }
        break;
    case SecPlus_v1DecoderStepDecoderData:
        if(level && (instance->decoder.decode_count_bit <=
                     subghz_protocol_secplus_v1_const.min_count_bit_for_found)) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_secplus_v1_const.te_short * 3) <
                subghz_protocol_secplus_v1_const.te_delta * 3) &&
               (DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short) <
                subghz_protocol_secplus_v1_const.te_delta)) {
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_0;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_secplus_v1_const.te_short * 2) <
                 subghz_protocol_secplus_v1_const.te_delta * 2) &&
                (DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short * 2) <
                 subghz_protocol_secplus_v1_const.te_delta * 2)) {
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_1;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_secplus_v1_const.te_short) <
                 subghz_protocol_secplus_v1_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_secplus_v1_const.te_short * 3) <
                 subghz_protocol_secplus_v1_const.te_delta * 3)) {
                instance
                    ->data_array[instance->decoder.decode_count_bit + instance->base_packet_index] =
                    SECPLUS_V1_BIT_2;
                instance->decoder.decode_count_bit++;
                instance->decoder.parser_step = SecPlus_v1DecoderStepSaveDuration;
            } else {
                instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = SecPlus_v1DecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_secplus_v1_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v1* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_secplus_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v1* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_secplus_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v1* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        2 * subghz_protocol_secplus_v1_const.min_count_bit_for_found);
}

bool subghz_protocol_secplus_v1_check_fixed(uint32_t fixed) {
    //uint8_t id0 = (fixed / 3) % 3;
    uint8_t id1 = (fixed / 9) % 3;
    uint8_t btn = fixed % 3;

    do {
        if(id1 == 0) return false;
        if(!(btn == 0 || btn == 1 || btn == 2)) return false; //-V560
    } while(false);
    return true;
}

void subghz_protocol_decoder_secplus_v1_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderSecPlus_v1* instance = context;

    uint32_t fixed = (instance->generic.data >> 32) & 0xFFFFFFFF;
    instance->generic.cnt = instance->generic.data & 0xFFFFFFFF;

    instance->generic.btn = fixed % 3;
    uint8_t id1 = (fixed / 9) % 3;

    // push protocol data to global variable
    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 32;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 2;
    //

    if(id1 == 0) {
        // (fixed // 3**3) % (3**7)    3^3=27  3^73=72187
        instance->generic.serial = (fixed / 27) % 2187;
    } else {
        //id = fixed / 27;
        instance->generic.serial = fixed / 27;
    }

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "SN:0x%lX Btn:%X\r\n"
        "Cnt:%08lX",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

#include "landrover_rke.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#include <string.h>

#define TAG "SubGhzProtocolLandRoverRke"

/*
 * Land Rover / Jaguar-family RKE framing (Freelander 2, Discovery 3-4,
 * Range Rover Sport, about 2004-2013). 433.92 MHz EU or 315 MHz NA.
 * OOK fixed-width PWM, 66 bits, MSB first.
 *
 *   [65:34] 32-bit hopping word (KeeLoq ciphertext; stored opaque)
 *   [33:10] 24-bit serial
 *   [9:6]   4-bit button
 *   [5:2]   4-bit function / repeat flags
 *   [1:0]   2-bit status
 *
 * The hopping word is not decrypted here. A lab replay sends the captured
 * ciphertext plus the clear button nibble. Changing the button without a
 * matching hop is the desync the bench is measuring.
 */

#define LR_SYNC_HIGH_US 400U
#define LR_SYNC_LOW_US 9600U
#define LR_BIT1_HIGH_US 700U
#define LR_BIT1_LOW_US 300U
#define LR_BIT0_HIGH_US 300U
#define LR_BIT0_LOW_US 700U
#define LR_TE_DELTA 160U
#define LR_SYNC_DELTA 1200U
#define LR_FRAME_BITS 66U
#define LR_CLEAR_BITS 64U
#define LR_REPEAT 4U

typedef enum {
    LandRoverRkeDecoderStepReset = 0,
    LandRoverRkeDecoderStepSync,
    LandRoverRkeDecoderStepSaveDuration,
    LandRoverRkeDecoderStepCheckDuration,
} LandRoverRkeDecoderStep;

struct SubGhzProtocolDecoderLandRoverRke {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t status;
};

struct SubGhzProtocolEncoderLandRoverRke {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint8_t status;
};

static const SubGhzBlockConst subghz_protocol_land_rover_rke_const = {
    .te_short = LR_BIT0_HIGH_US,
    .te_long = LR_BIT1_HIGH_US,
    .te_delta = LR_TE_DELTA,
    .min_count_bit_for_found = LR_FRAME_BITS,
};

const SubGhzProtocolDecoder subghz_protocol_land_rover_rke_decoder = {
    .alloc = subghz_protocol_decoder_land_rover_rke_alloc,
    .free = subghz_protocol_decoder_land_rover_rke_free,
    .feed = subghz_protocol_decoder_land_rover_rke_feed,
    .reset = subghz_protocol_decoder_land_rover_rke_reset,
    .get_hash_data = subghz_protocol_decoder_land_rover_rke_get_hash_data,
    .serialize = subghz_protocol_decoder_land_rover_rke_serialize,
    .deserialize = subghz_protocol_decoder_land_rover_rke_deserialize,
    .get_string = subghz_protocol_decoder_land_rover_rke_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_land_rover_rke_encoder = {
    .alloc = subghz_protocol_encoder_land_rover_rke_alloc,
    .free = subghz_protocol_encoder_land_rover_rke_free,
    .deserialize = subghz_protocol_encoder_land_rover_rke_deserialize,
    .stop = subghz_protocol_encoder_land_rover_rke_stop,
    .yield = subghz_protocol_encoder_land_rover_rke_yield,
};

const SubGhzProtocol subghz_protocol_land_rover_rke = {
    .name = LAND_ROVER_RKE_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_land_rover_rke_decoder,
    .encoder = &subghz_protocol_land_rover_rke_encoder,
};

static bool lr_near(uint32_t duration, uint32_t ref, uint32_t delta) {
    return DURATION_DIFF(duration, ref) < delta;
}

static void lr_unpack(SubGhzBlockGeneric* generic, uint8_t status) {
    const uint64_t clear = generic->data;
    generic->serial = (uint32_t)((clear >> 8) & 0xFFFFFFULL);
    generic->btn = (uint8_t)((clear >> 4) & 0x0FU);
    generic->cnt = (uint32_t)(clear >> 32);
    generic->extra = status & 0x03U;
}

static uint64_t lr_pack_clear(uint32_t hop, uint32_t serial, uint8_t button, uint8_t func) {
    return ((uint64_t)hop << 32) | ((uint64_t)(serial & 0xFFFFFFU) << 8) |
           ((uint64_t)(button & 0x0FU) << 4) | (uint64_t)(func & 0x0FU);
}

static uint8_t lr_read_status(FlipperFormat* flipper_format);

static const char* lr_button_name(uint8_t button) {
    switch(button) {
    case 0x1:
        return "Lock";
    case 0x2:
        return "Unlock";
    case 0x4:
        return "Boot";
    case 0x8:
        return "Panic";
    default:
        return "Unknown";
    }
}

void* subghz_protocol_encoder_land_rover_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderLandRoverRke* instance = malloc(sizeof(SubGhzProtocolEncoderLandRoverRke));
    instance->base.protocol = &subghz_protocol_land_rover_rke;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = 220;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.repeat = LR_REPEAT;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_land_rover_rke_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderLandRoverRke* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_land_rover_rke_stop(void* context) {
    SubGhzProtocolEncoderLandRoverRke* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_land_rover_rke_yield(void* context) {
    SubGhzProtocolEncoderLandRoverRke* instance = context;
    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = instance->encoder.upload[instance->encoder.front];
    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }
    return ret;
}

static bool lr_encoder_get_upload(SubGhzProtocolEncoderLandRoverRke* instance) {
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    subghz_custom_btn_set_max(4);
    if(subghz_custom_btn_get() != SUBGHZ_CUSTOM_BTN_OK) {
        instance->generic.btn = subghz_custom_btn_get();
    }

    const uint32_t hop = (uint32_t)(instance->generic.data >> 32);
    const uint8_t func = (uint8_t)(instance->generic.data & 0x0FU);
    instance->generic.data =
        lr_pack_clear(hop, instance->generic.serial, instance->generic.btn, func);

    size_t idx = 0;
    for(uint32_t p = 0; p < 20; p++) {
        instance->encoder.upload[idx++] = level_duration_make(true, LR_SYNC_HIGH_US);
        instance->encoder.upload[idx++] = level_duration_make(false, 600);
    }
    instance->encoder.upload[idx++] = level_duration_make(true, LR_SYNC_HIGH_US);
    instance->encoder.upload[idx++] = level_duration_make(false, LR_SYNC_LOW_US);

    for(int bit = 63; bit >= 0; bit--) {
        const bool one = ((instance->generic.data >> bit) & 1ULL) != 0;
        instance->encoder.upload[idx++] =
            level_duration_make(true, one ? LR_BIT1_HIGH_US : LR_BIT0_HIGH_US);
        instance->encoder.upload[idx++] =
            level_duration_make(false, one ? LR_BIT1_LOW_US : LR_BIT0_LOW_US);
    }
    for(int bit = 1; bit >= 0; bit--) {
        const bool one = ((instance->status >> bit) & 1U) != 0;
        instance->encoder.upload[idx++] =
            level_duration_make(true, one ? LR_BIT1_HIGH_US : LR_BIT0_HIGH_US);
        instance->encoder.upload[idx++] =
            level_duration_make(false, one ? LR_BIT1_LOW_US : LR_BIT0_LOW_US);
    }
    instance->encoder.upload[idx++] = level_duration_make(false, 12000);
    instance->encoder.size_upload = idx;
    return idx <= 220;
}

SubGhzProtocolStatus subghz_protocol_encoder_land_rover_rke_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderLandRoverRke* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(instance->generic.data_count_bit != LR_FRAME_BITS &&
       instance->generic.data_count_bit != LR_CLEAR_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->status = lr_read_status(flipper_format);
    lr_unpack(&instance->generic, instance->status);
    if(!lr_encoder_get_upload(instance)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.front = 0;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

void* subghz_protocol_decoder_land_rover_rke_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderLandRoverRke* instance = malloc(sizeof(SubGhzProtocolDecoderLandRoverRke));
    instance->base.protocol = &subghz_protocol_land_rover_rke;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_land_rover_rke_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_land_rover_rke_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderLandRoverRke* instance = context;
    instance->decoder.parser_step = LandRoverRkeDecoderStepReset;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->status = 0;
}

void subghz_protocol_decoder_land_rover_rke_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderLandRoverRke* instance = context;

    switch(instance->decoder.parser_step) {
    case LandRoverRkeDecoderStepReset:
        if(level &&
           lr_near(duration, LR_SYNC_HIGH_US, subghz_protocol_land_rover_rke_const.te_delta)) {
            instance->decoder.parser_step = LandRoverRkeDecoderStepSync;
        }
        break;
    case LandRoverRkeDecoderStepSync:
        if(!level && lr_near(duration, LR_SYNC_LOW_US, LR_SYNC_DELTA)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->status = 0;
            instance->decoder.parser_step = LandRoverRkeDecoderStepSaveDuration;
        } else {
            instance->decoder.parser_step = LandRoverRkeDecoderStepReset;
        }
        break;
    case LandRoverRkeDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = LandRoverRkeDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = LandRoverRkeDecoderStepReset;
        }
        break;
    case LandRoverRkeDecoderStepCheckDuration:
        if(level) {
            instance->decoder.parser_step = LandRoverRkeDecoderStepReset;
            break;
        }
        {
            bool one = false;
            bool bit_ok = false;
            if(lr_near(instance->decoder.te_last, LR_BIT1_HIGH_US, LR_TE_DELTA) &&
               lr_near(duration, LR_BIT1_LOW_US, LR_TE_DELTA)) {
                one = true;
                bit_ok = true;
            } else if(
                lr_near(instance->decoder.te_last, LR_BIT0_HIGH_US, LR_TE_DELTA) &&
                lr_near(duration, LR_BIT0_LOW_US, LR_TE_DELTA)) {
                one = false;
                bit_ok = true;
            }
            if(!bit_ok) {
                instance->decoder.parser_step = LandRoverRkeDecoderStepReset;
                break;
            }
            if(instance->decoder.decode_count_bit < LR_CLEAR_BITS) {
                instance->decoder.decode_data = (instance->decoder.decode_data << 1) | (one ? 1ULL : 0ULL);
            } else {
                instance->status = (uint8_t)((instance->status << 1) | (one ? 1U : 0U));
            }
            instance->decoder.decode_count_bit++;
            if(instance->decoder.decode_count_bit == LR_FRAME_BITS) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = LR_FRAME_BITS;
                instance->status &= 0x03U;
                lr_unpack(&instance->generic, instance->status);
                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.parser_step = LandRoverRkeDecoderStepReset;
            } else {
                instance->decoder.parser_step = LandRoverRkeDecoderStepSaveDuration;
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_land_rover_rke_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderLandRoverRke* instance = context;
    return subghz_protocol_blocks_get_hash_data(&instance->decoder, 8);
}

static void lr_write_status(FlipperFormat* flipper_format, uint8_t status) {
    uint32_t value = status & 0x03U;
    flipper_format_write_uint32(flipper_format, "LrStatus", &value, 1);
}

static uint8_t lr_read_status(FlipperFormat* flipper_format) {
    uint32_t value = 0;
    if(!flipper_format_read_uint32(flipper_format, "LrStatus", &value, 1)) {
        return 0;
    }
    return (uint8_t)(value & 0x03U);
}

SubGhzProtocolStatus subghz_protocol_decoder_land_rover_rke_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderLandRoverRke* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        lr_write_status(flipper_format, instance->status);
    }
    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_land_rover_rke_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderLandRoverRke* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret == SubGhzProtocolStatusOk) {
        instance->status = lr_read_status(flipper_format);
        lr_unpack(&instance->generic, instance->status);
    }
    return ret;
}

void subghz_protocol_decoder_land_rover_rke_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderLandRoverRke* instance = context;
    furi_string_cat_printf(
        output,
        "Land Rover RKE\r\n"
        "Hop:%08lX Sn:%06lX\r\n"
        "Btn:%X [%s] Fn:%X St:%X\r\n"
        "%ubit",
        (unsigned long)instance->generic.cnt,
        (unsigned long)instance->generic.serial,
        instance->generic.btn,
        lr_button_name(instance->generic.btn),
        (unsigned)(instance->generic.data & 0x0FU),
        instance->status,
        instance->generic.data_count_bit);
}

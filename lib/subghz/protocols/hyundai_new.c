#include "hyundai_new.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolHyundaiNew"

/*
 * Hyundai New RKE — 433.92 MHz
 * PWM encoding, 64-bit frame MSB-first, 2× repeated transmission.
 *
 * Frame layout (MSB-first):
 *   [32b serial number]
 *   [16b rolling counter]
 *   [8b  button code]
 *   [8b  CRC-8 poly 0x31]
 *
 * Button codes:
 *   0x01 = Lock, 0x02 = Unlock, 0x04 = Trunk, 0x08 = Panic, 0x10 = RemoteStart
 *
 * Covers: Hyundai (2017+), Genesis (all models), FCCID TQ8-RKE-4F14 family.
 */

static const SubGhzBlockConst subghz_protocol_hyundai_new_const = {
    .te_short = 350,
    .te_long = 700,
    .te_delta = 120,
    .min_count_bit_for_found = 64,
};

#define HYUNDAI_NEW_PREAMBLE_MIN 18u
#define HYUNDAI_NEW_DATA_BITS    64u

#define HYUNDAI_NEW_BTN_LOCK         0x01u
#define HYUNDAI_NEW_BTN_UNLOCK       0x02u
#define HYUNDAI_NEW_BTN_TRUNK        0x04u
#define HYUNDAI_NEW_BTN_PANIC        0x08u
#define HYUNDAI_NEW_BTN_REMOTE_START 0x10u

struct SubGhzProtocolDecoderHyundaiNew {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    uint8_t crc_received;
    bool crc_valid;
};

struct SubGhzProtocolEncoderHyundaiNew {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    HyundaiNewDecoderStepReset = 0,
    HyundaiNewDecoderStepCheckPreamble,
    HyundaiNewDecoderStepSaveDuration,
    HyundaiNewDecoderStepCheckDuration,
} HyundaiNewDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_hyundai_new_decoder = {
    .alloc = subghz_protocol_decoder_hyundai_new_alloc,
    .free = subghz_protocol_decoder_hyundai_new_free,
    .feed = subghz_protocol_decoder_hyundai_new_feed,
    .reset = subghz_protocol_decoder_hyundai_new_reset,
    .get_hash_data = subghz_protocol_decoder_hyundai_new_get_hash_data,
    .serialize = subghz_protocol_decoder_hyundai_new_serialize,
    .deserialize = subghz_protocol_decoder_hyundai_new_deserialize,
    .get_string = subghz_protocol_decoder_hyundai_new_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_hyundai_new_encoder = {
    .alloc = subghz_protocol_encoder_hyundai_new_alloc,
    .free = subghz_protocol_encoder_hyundai_new_free,
    .deserialize = subghz_protocol_encoder_hyundai_new_deserialize,
    .stop = subghz_protocol_encoder_hyundai_new_stop,
    .yield = subghz_protocol_encoder_hyundai_new_yield,
};

const SubGhzProtocol subghz_protocol_hyundai_new = {
    .name = HYUNDAI_NEW_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_hyundai_new_decoder,
    .encoder = &subghz_protocol_hyundai_new_encoder,
};

// ─── CRC ────────────────────────────────────────────────────────────────────

static uint8_t hyundai_new_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int j = 0; j < 8; j++) {
            if(crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x31);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static uint8_t hyundai_new_calculate_crc(uint64_t data) {
    uint8_t buf[7];
    for(int i = 0; i < 7; i++) buf[i] = (uint8_t)(data >> (56 - i * 8));
    return hyundai_new_crc8(buf, 7);
}

static void hyundai_new_extract_fields(SubGhzProtocolDecoderHyundaiNew* instance) {
    uint64_t d = instance->generic.data;
    instance->generic.serial = (uint32_t)(d >> 32);
    instance->generic.cnt = (uint16_t)((d >> 16) & 0xFFFF);
    instance->generic.btn = (uint8_t)((d >> 8) & 0xFF);
    instance->crc_received = (uint8_t)(d & 0xFF);
    instance->crc_valid = (instance->crc_received == hyundai_new_calculate_crc(d));
}

// ─── Encoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_encoder_hyundai_new_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHyundaiNew* instance = malloc(sizeof(SubGhzProtocolEncoderHyundaiNew));
    instance->base.protocol = &subghz_protocol_hyundai_new;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = 600;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.repeat = 2;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_hyundai_new_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderHyundaiNew* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_hyundai_new_stop(void* context) {
    SubGhzProtocolEncoderHyundaiNew* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_hyundai_new_yield(void* context) {
    SubGhzProtocolEncoderHyundaiNew* instance = context;
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

static bool hyundai_new_encoder_get_upload(SubGhzProtocolEncoderHyundaiNew* instance) {
    furi_assert(instance);

    if(subghz_custom_btn_get_original() == 0)
        subghz_custom_btn_set_original(instance->generic.btn);
    subghz_custom_btn_set_max(5);

    uint8_t btn = (subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK) ?
                      subghz_custom_btn_get_original() :
                      subghz_custom_btn_get();
    instance->generic.btn = btn;

    if(instance->generic.cnt < 0xFFFF)
        instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
    else
        instance->generic.cnt = 0;

    uint8_t frame[8];
    frame[0] = (instance->generic.serial >> 24) & 0xFF;
    frame[1] = (instance->generic.serial >> 16) & 0xFF;
    frame[2] = (instance->generic.serial >> 8) & 0xFF;
    frame[3] = instance->generic.serial & 0xFF;
    frame[4] = (instance->generic.cnt >> 8) & 0xFF;
    frame[5] = instance->generic.cnt & 0xFF;
    frame[6] = btn;
    frame[7] = hyundai_new_crc8(frame, 7);

    size_t idx = 0;
    for(size_t i = 0; i < HYUNDAI_NEW_PREAMBLE_MIN; i++) {
        instance->encoder.upload[idx++] =
            level_duration_make(true, subghz_protocol_hyundai_new_const.te_short);
        instance->encoder.upload[idx++] =
            level_duration_make(false, subghz_protocol_hyundai_new_const.te_short);
    }
    for(int byte = 0; byte < 8; byte++) {
        for(int bit = 7; bit >= 0; bit--) {
            bool b = (frame[byte] >> bit) & 1;
            uint32_t w = b ? (uint32_t)subghz_protocol_hyundai_new_const.te_long :
                             (uint32_t)subghz_protocol_hyundai_new_const.te_short;
            instance->encoder.upload[idx++] = level_duration_make(true, w);
            instance->encoder.upload[idx++] = level_duration_make(false, w);
        }
    }
    instance->encoder.upload[idx++] =
        level_duration_make(false, subghz_protocol_hyundai_new_const.te_long * 8);
    instance->encoder.size_upload = idx;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_hyundai_new_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHyundaiNew* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_hyundai_new_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(!hyundai_new_encoder_get_upload(instance)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

// ─── Decoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_decoder_hyundai_new_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHyundaiNew* instance = malloc(sizeof(SubGhzProtocolDecoderHyundaiNew));
    instance->base.protocol = &subghz_protocol_hyundai_new;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_hyundai_new_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_hyundai_new_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHyundaiNew* instance = context;
    instance->decoder.parser_step = HyundaiNewDecoderStepReset;
    instance->header_count = 0;
}

void subghz_protocol_decoder_hyundai_new_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHyundaiNew* instance = context;

    switch(instance->decoder.parser_step) {
    case HyundaiNewDecoderStepReset:
        if(level &&
           DURATION_DIFF(duration, subghz_protocol_hyundai_new_const.te_short) <
               subghz_protocol_hyundai_new_const.te_delta) {
            instance->decoder.parser_step = HyundaiNewDecoderStepCheckPreamble;
            instance->header_count = 1;
            instance->decoder.te_last = duration;
        }
        break;

    case HyundaiNewDecoderStepCheckPreamble:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_hyundai_new_const.te_short) <
               subghz_protocol_hyundai_new_const.te_delta) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = HyundaiNewDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_hyundai_new_const.te_short) <
               subghz_protocol_hyundai_new_const.te_delta) {
                instance->header_count++;
                if(instance->header_count >= HYUNDAI_NEW_PREAMBLE_MIN) {
                    instance->decoder.parser_step = HyundaiNewDecoderStepSaveDuration;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                }
            } else {
                instance->decoder.parser_step = HyundaiNewDecoderStepReset;
            }
        }
        break;

    case HyundaiNewDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = HyundaiNewDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = HyundaiNewDecoderStepReset;
        }
        break;

    case HyundaiNewDecoderStepCheckDuration:
        if(!level) {
            bool bit;
            if(DURATION_DIFF(
                   instance->decoder.te_last, subghz_protocol_hyundai_new_const.te_long) <
               subghz_protocol_hyundai_new_const.te_delta) {
                bit = true;
            } else if(
                DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_hyundai_new_const.te_short) <
                subghz_protocol_hyundai_new_const.te_delta) {
                bit = false;
            } else {
                instance->decoder.parser_step = HyundaiNewDecoderStepReset;
                break;
            }
            subghz_protocol_blocks_add_bit(&instance->decoder, bit);
            instance->decoder.parser_step = HyundaiNewDecoderStepSaveDuration;

            if(instance->decoder.decode_count_bit == HYUNDAI_NEW_DATA_BITS) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = HYUNDAI_NEW_DATA_BITS;
                hyundai_new_extract_fields(instance);
                if(instance->crc_valid) {
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                } else {
                    FURI_LOG_W(TAG, "CRC mismatch, frame dropped");
                }
                instance->decoder.parser_step = HyundaiNewDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = HyundaiNewDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_hyundai_new_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHyundaiNew* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_hyundai_new_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHyundaiNew* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_hyundai_new_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHyundaiNew* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret == SubGhzProtocolStatusOk) hyundai_new_extract_fields(instance);
    return ret;
}

// ─── Display ────────────────────────────────────────────────────────────────

static const char* hyundai_new_button_name(uint8_t btn) {
    switch(btn) {
    case HYUNDAI_NEW_BTN_LOCK: return "Lock";
    case HYUNDAI_NEW_BTN_UNLOCK: return "Unlock";
    case HYUNDAI_NEW_BTN_TRUNK: return "Trunk";
    case HYUNDAI_NEW_BTN_PANIC: return "Panic";
    case HYUNDAI_NEW_BTN_REMOTE_START: return "RemoteStart";
    default: return "Unknown";
    }
}

void subghz_protocol_decoder_hyundai_new_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderHyundaiNew* instance = context;

    furi_string_cat_printf(
        output,
        "Hyundai New  433MHz\r\n"
        "Sn:%08lX  Cnt:%04lX\r\n"
        "Btn:%02X [%s]\r\n"
        "CRC:%02X %s  %dbit",
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn,
        hyundai_new_button_name(instance->generic.btn),
        instance->crc_received,
        instance->crc_valid ? "(OK)" : "(FAIL)",
        instance->generic.data_count_bit);
}

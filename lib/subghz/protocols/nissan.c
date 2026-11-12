#include "nissan.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolNissan"

/*
 * Nissan/Infiniti RKE — 315 MHz (NA) / 433.92 MHz (EU)
 * PWM encoding, 64-bit frame, 2× repeated transmission.
 *
 * Frame layout (MSB-first):
 *   [32b serial number]
 *   [16b rolling counter]
 *   [8b  button code]
 *   [8b  CRC-8 poly 0x97]
 *
 * Button physical→code mapping is inverted vs most brands:
 *   Physical 1 → code 0x02, Physical 2 → code 0x01, etc.
 *   (documented in keeloq.c from Pandora firmware analysis)
 *
 * Covers: Nissan, Infiniti (2003–2018).
 */

static const SubGhzBlockConst subghz_protocol_nissan_const = {
    .te_short = 300,
    .te_long = 600,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

#define NISSAN_PREAMBLE_MIN 16u
#define NISSAN_DATA_BITS    64u

#define NISSAN_BTN_LOCK   0x02u
#define NISSAN_BTN_UNLOCK 0x01u
#define NISSAN_BTN_TRUNK  0x04u
#define NISSAN_BTN_PANIC  0x08u

struct SubGhzProtocolDecoderNissan {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    uint8_t crc_received;
    bool crc_valid;
};

struct SubGhzProtocolEncoderNissan {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    NissanDecoderStepReset = 0,
    NissanDecoderStepCheckPreamble,
    NissanDecoderStepSaveDuration,
    NissanDecoderStepCheckDuration,
} NissanDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_nissan_decoder = {
    .alloc = subghz_protocol_decoder_nissan_alloc,
    .free = subghz_protocol_decoder_nissan_free,
    .feed = subghz_protocol_decoder_nissan_feed,
    .reset = subghz_protocol_decoder_nissan_reset,
    .get_hash_data = subghz_protocol_decoder_nissan_get_hash_data,
    .serialize = subghz_protocol_decoder_nissan_serialize,
    .deserialize = subghz_protocol_decoder_nissan_deserialize,
    .get_string = subghz_protocol_decoder_nissan_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_nissan_encoder = {
    .alloc = subghz_protocol_encoder_nissan_alloc,
    .free = subghz_protocol_encoder_nissan_free,
    .deserialize = subghz_protocol_encoder_nissan_deserialize,
    .stop = subghz_protocol_encoder_nissan_stop,
    .yield = subghz_protocol_encoder_nissan_yield,
};

const SubGhzProtocol subghz_protocol_nissan = {
    .name = NISSAN_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_nissan_decoder,
    .encoder = &subghz_protocol_nissan_encoder,
};

// ─── CRC ────────────────────────────────────────────────────────────────────

static uint8_t nissan_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int j = 0; j < 8; j++) {
            if(crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x97);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static uint8_t nissan_calculate_crc(uint64_t data) {
    uint8_t buf[7];
    for(int i = 0; i < 7; i++) buf[i] = (uint8_t)(data >> (56 - i * 8));
    return nissan_crc8(buf, 7);
}

static void nissan_extract_fields(SubGhzProtocolDecoderNissan* instance) {
    uint64_t d = instance->generic.data;
    instance->generic.serial = (uint32_t)(d >> 32);
    instance->generic.cnt = (uint16_t)((d >> 16) & 0xFFFF);
    instance->generic.btn = (uint8_t)((d >> 8) & 0xFF);
    instance->crc_received = (uint8_t)(d & 0xFF);
    instance->crc_valid = (instance->crc_received == nissan_calculate_crc(d));
}

// ─── Encoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_encoder_nissan_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderNissan* instance = malloc(sizeof(SubGhzProtocolEncoderNissan));
    instance->base.protocol = &subghz_protocol_nissan;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = 600;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.repeat = 2;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_nissan_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderNissan* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_nissan_stop(void* context) {
    SubGhzProtocolEncoderNissan* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_nissan_yield(void* context) {
    SubGhzProtocolEncoderNissan* instance = context;
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

static bool nissan_encoder_get_upload(SubGhzProtocolEncoderNissan* instance) {
    furi_assert(instance);

    if(subghz_custom_btn_get_original() == 0)
        subghz_custom_btn_set_original(instance->generic.btn);
    subghz_custom_btn_set_max(4);

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
    frame[7] = nissan_crc8(frame, 7);

    size_t idx = 0;
    for(size_t i = 0; i < NISSAN_PREAMBLE_MIN; i++) {
        instance->encoder.upload[idx++] =
            level_duration_make(true, subghz_protocol_nissan_const.te_short);
        instance->encoder.upload[idx++] =
            level_duration_make(false, subghz_protocol_nissan_const.te_short);
    }
    for(int byte = 0; byte < 8; byte++) {
        for(int bit = 7; bit >= 0; bit--) {
            bool b = (frame[byte] >> bit) & 1;
            uint32_t w = b ? (uint32_t)subghz_protocol_nissan_const.te_long :
                             (uint32_t)subghz_protocol_nissan_const.te_short;
            instance->encoder.upload[idx++] = level_duration_make(true, w);
            instance->encoder.upload[idx++] = level_duration_make(false, w);
        }
    }
    instance->encoder.upload[idx++] =
        level_duration_make(false, subghz_protocol_nissan_const.te_long * 8);
    instance->encoder.size_upload = idx;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_nissan_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderNissan* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_nissan_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(!nissan_encoder_get_upload(instance)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

// ─── Decoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_decoder_nissan_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderNissan* instance = malloc(sizeof(SubGhzProtocolDecoderNissan));
    instance->base.protocol = &subghz_protocol_nissan;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_nissan_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_nissan_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNissan* instance = context;
    instance->decoder.parser_step = NissanDecoderStepReset;
    instance->header_count = 0;
}

void subghz_protocol_decoder_nissan_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderNissan* instance = context;

    switch(instance->decoder.parser_step) {
    case NissanDecoderStepReset:
        if(level &&
           DURATION_DIFF(duration, subghz_protocol_nissan_const.te_short) <
               subghz_protocol_nissan_const.te_delta) {
            instance->decoder.parser_step = NissanDecoderStepCheckPreamble;
            instance->header_count = 1;
            instance->decoder.te_last = duration;
        }
        break;

    case NissanDecoderStepCheckPreamble:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_nissan_const.te_short) <
               subghz_protocol_nissan_const.te_delta) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = NissanDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_nissan_const.te_short) <
               subghz_protocol_nissan_const.te_delta) {
                instance->header_count++;
                if(instance->header_count >= NISSAN_PREAMBLE_MIN) {
                    instance->decoder.parser_step = NissanDecoderStepSaveDuration;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                }
            } else {
                instance->decoder.parser_step = NissanDecoderStepReset;
            }
        }
        break;

    case NissanDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = NissanDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = NissanDecoderStepReset;
        }
        break;

    case NissanDecoderStepCheckDuration:
        if(!level) {
            bool bit;
            if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_nissan_const.te_long) <
               subghz_protocol_nissan_const.te_delta) {
                bit = true;
            } else if(
                DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_nissan_const.te_short) <
                subghz_protocol_nissan_const.te_delta) {
                bit = false;
            } else {
                instance->decoder.parser_step = NissanDecoderStepReset;
                break;
            }
            subghz_protocol_blocks_add_bit(&instance->decoder, bit);
            instance->decoder.parser_step = NissanDecoderStepSaveDuration;

            if(instance->decoder.decode_count_bit == NISSAN_DATA_BITS) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = NISSAN_DATA_BITS;
                nissan_extract_fields(instance);
                if(instance->crc_valid) {
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                } else {
                    FURI_LOG_W(TAG, "CRC mismatch, frame dropped");
                }
                instance->decoder.parser_step = NissanDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = NissanDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_nissan_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderNissan* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_nissan_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderNissan* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_nissan_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderNissan* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret == SubGhzProtocolStatusOk) nissan_extract_fields(instance);
    return ret;
}

// ─── Display ────────────────────────────────────────────────────────────────

static const char* nissan_button_name(uint8_t btn) {
    switch(btn) {
    case NISSAN_BTN_LOCK: return "Lock";
    case NISSAN_BTN_UNLOCK: return "Unlock";
    case NISSAN_BTN_TRUNK: return "Trunk";
    case NISSAN_BTN_PANIC: return "Panic";
    default: return "Unknown";
    }
}

void subghz_protocol_decoder_nissan_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderNissan* instance = context;

    furi_string_cat_printf(
        output,
        "Nissan/Infiniti  315/433MHz\r\n"
        "Sn:%08lX  Cnt:%04lX\r\n"
        "Btn:%02X [%s]\r\n"
        "CRC:%02X %s  %dbit",
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn,
        nissan_button_name(instance->generic.btn),
        instance->crc_received,
        instance->crc_valid ? "(OK)" : "(FAIL)",
        instance->generic.data_count_bit);
}

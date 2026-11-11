#include "toyota_lexus.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolToyota"

/*
 * Toyota/Lexus RKE — 315 MHz (NA) / 433.92 MHz (EU/JPN)
 * PWM encoding, 72-bit frame, 3× repeated transmission.
 *
 * Frame layout (MSB-first):
 *   [8b  fixed preamble 0xCB]
 *   [16b rolling counter]
 *   [32b serial number]
 *   [8b  button code]
 *   [8b  CRC-8 poly 0xEA over bytes 1-7]
 *
 * Covers: Corolla, Camry, RAV4, Hilux, Land Cruiser, Lexus IS/RX/GS (2003-2020).
 */

static const SubGhzBlockConst subghz_protocol_toyota_const = {
    .te_short = 430,
    .te_long = 1290,
    .te_delta = 150,
    .min_count_bit_for_found = 72,
};

#define TOYOTA_PREAMBLE_COUNT_MIN 10u
#define TOYOTA_DATA_BITS          72u
#define TOYOTA_PREAMBLE_MARK      0xCBu

#define TOYOTA_BTN_LOCK         0x01u
#define TOYOTA_BTN_UNLOCK       0x02u
#define TOYOTA_BTN_TRUNK        0x04u
#define TOYOTA_BTN_PANIC        0x08u
#define TOYOTA_BTN_REMOTE_START 0x10u

struct SubGhzProtocolDecoderToyotaLexus {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    uint8_t crc_received;
    bool crc_valid;
};

struct SubGhzProtocolEncoderToyotaLexus {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    ToyotaDecoderStepReset = 0,
    ToyotaDecoderStepCheckPreamble,
    ToyotaDecoderStepSaveDuration,
    ToyotaDecoderStepCheckDuration,
} ToyotaDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_toyota_lexus_decoder = {
    .alloc = subghz_protocol_decoder_toyota_lexus_alloc,
    .free = subghz_protocol_decoder_toyota_lexus_free,
    .feed = subghz_protocol_decoder_toyota_lexus_feed,
    .reset = subghz_protocol_decoder_toyota_lexus_reset,
    .get_hash_data = subghz_protocol_decoder_toyota_lexus_get_hash_data,
    .serialize = subghz_protocol_decoder_toyota_lexus_serialize,
    .deserialize = subghz_protocol_decoder_toyota_lexus_deserialize,
    .get_string = subghz_protocol_decoder_toyota_lexus_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_toyota_lexus_encoder = {
    .alloc = subghz_protocol_encoder_toyota_lexus_alloc,
    .free = subghz_protocol_encoder_toyota_lexus_free,
    .deserialize = subghz_protocol_encoder_toyota_lexus_deserialize,
    .stop = subghz_protocol_encoder_toyota_lexus_stop,
    .yield = subghz_protocol_encoder_toyota_lexus_yield,
};

const SubGhzProtocol subghz_protocol_toyota_lexus = {
    .name = TOYOTA_LEXUS_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_toyota_lexus_decoder,
    .encoder = &subghz_protocol_toyota_lexus_encoder,
};

// ─── CRC ────────────────────────────────────────────────────────────────────

static uint8_t toyota_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for(size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int j = 0; j < 8; j++) {
            if(crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0xEA);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static uint8_t toyota_calculate_crc(uint64_t hi) {
    /* hi = d >> 8, i.e. [counter(2)][serial(4)][button(1)] packed in the low
       56 bits. The preamble byte never survives reception (see comment in
       toyota_extract_fields) and CRC is defined over bytes 1-7 (counter,
       serial, button) to match what the encoder actually transmits. */
    uint8_t buf[7];
    buf[0] = (uint8_t)(hi >> 48);
    buf[1] = (uint8_t)(hi >> 40);
    buf[2] = (uint8_t)(hi >> 32);
    buf[3] = (uint8_t)(hi >> 24);
    buf[4] = (uint8_t)(hi >> 16);
    buf[5] = (uint8_t)(hi >> 8);
    buf[6] = (uint8_t)(hi & 0xFF);
    return toyota_crc8(buf, 7);
}

// ─── Protocol data extraction ────────────────────────────────────────────────

static void toyota_extract_fields(SubGhzProtocolDecoderToyotaLexus* instance) {
    /* generic.data holds the upper 64 bits; low 8 bits are CRC carried separately */
    uint64_t d = instance->generic.data;
    instance->generic.serial = (uint32_t)((d >> 16) & 0xFFFFFFFF);
    instance->generic.cnt = (uint16_t)((d >> 48) & 0xFFFF);
    instance->generic.btn = (uint8_t)((d >> 8) & 0xFF);
    instance->crc_received = (uint8_t)(d & 0xFF);
    instance->crc_valid = (instance->crc_received == toyota_calculate_crc(d >> 8));
}

// ─── Encoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_encoder_toyota_lexus_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderToyotaLexus* instance = malloc(sizeof(SubGhzProtocolEncoderToyotaLexus));
    instance->base.protocol = &subghz_protocol_toyota_lexus;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = 800;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.repeat = 3;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_toyota_lexus_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderToyotaLexus* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_toyota_lexus_stop(void* context) {
    SubGhzProtocolEncoderToyotaLexus* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_toyota_lexus_yield(void* context) {
    SubGhzProtocolEncoderToyotaLexus* instance = context;
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

static bool toyota_encoder_get_upload(SubGhzProtocolEncoderToyotaLexus* instance) {
    furi_assert(instance);

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    subghz_custom_btn_set_max(5);

    uint8_t btn = (subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK) ?
                      subghz_custom_btn_get_original() :
                      subghz_custom_btn_get();
    instance->generic.btn = btn;

    if(instance->generic.cnt < 0xFFFF)
        instance->generic.cnt += furi_hal_subghz_get_rolling_counter_mult();
    else
        instance->generic.cnt = 0;

    /* Build 72-bit frame as 9 bytes */
    uint8_t frame[9];
    frame[0] = TOYOTA_PREAMBLE_MARK;
    frame[1] = (instance->generic.cnt >> 8) & 0xFF;
    frame[2] = instance->generic.cnt & 0xFF;
    frame[3] = (instance->generic.serial >> 24) & 0xFF;
    frame[4] = (instance->generic.serial >> 16) & 0xFF;
    frame[5] = (instance->generic.serial >> 8) & 0xFF;
    frame[6] = instance->generic.serial & 0xFF;
    frame[7] = btn;
    /* CRC covers bytes 1-7 (counter, serial, button) — matches
       toyota_calculate_crc, which never sees the preamble byte since it
       doesn't survive the 72-bit-into-64-bit-accumulator reception path. */
    frame[8] = toyota_crc8(&frame[1], 7);

    size_t idx = 0;
    /* Preamble: 12 short pairs */
    for(int i = 0; i < 12; i++) {
        instance->encoder.upload[idx++] =
            level_duration_make(true, subghz_protocol_toyota_const.te_short);
        instance->encoder.upload[idx++] =
            level_duration_make(false, subghz_protocol_toyota_const.te_short);
    }

    /* Data bits PWM: 1 → long pulse, 0 → short pulse */
    for(int byte = 0; byte < 9; byte++) {
        for(int bit = 7; bit >= 0; bit--) {
            bool b = (frame[byte] >> bit) & 1;
            uint32_t width = b ? (uint32_t)subghz_protocol_toyota_const.te_long :
                                 (uint32_t)subghz_protocol_toyota_const.te_short;
            instance->encoder.upload[idx++] = level_duration_make(true, width);
            instance->encoder.upload[idx++] = level_duration_make(false, width);
        }
    }

    /* Inter-frame gap */
    instance->encoder.upload[idx++] =
        level_duration_make(false, subghz_protocol_toyota_const.te_long * 10);

    instance->encoder.size_upload = idx;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_toyota_lexus_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderToyotaLexus* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_toyota_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;

    if(!toyota_encoder_get_upload(instance)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

// ─── Decoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_decoder_toyota_lexus_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderToyotaLexus* instance = malloc(sizeof(SubGhzProtocolDecoderToyotaLexus));
    instance->base.protocol = &subghz_protocol_toyota_lexus;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_toyota_lexus_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_toyota_lexus_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaLexus* instance = context;
    instance->decoder.parser_step = ToyotaDecoderStepReset;
    instance->header_count = 0;
}

void subghz_protocol_decoder_toyota_lexus_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaLexus* instance = context;

    switch(instance->decoder.parser_step) {
    case ToyotaDecoderStepReset:
        if(level &&
           DURATION_DIFF(duration, subghz_protocol_toyota_const.te_short) <
               subghz_protocol_toyota_const.te_delta) {
            instance->decoder.parser_step = ToyotaDecoderStepCheckPreamble;
            instance->header_count = 1;
            instance->decoder.te_last = duration;
        }
        break;

    case ToyotaDecoderStepCheckPreamble:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_toyota_const.te_short) <
               subghz_protocol_toyota_const.te_delta) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = ToyotaDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_toyota_const.te_short) <
               subghz_protocol_toyota_const.te_delta) {
                if(DURATION_DIFF(
                       instance->decoder.te_last, subghz_protocol_toyota_const.te_short) <
                   subghz_protocol_toyota_const.te_delta) {
                    instance->header_count++;
                    if(instance->header_count >= TOYOTA_PREAMBLE_COUNT_MIN) {
                        instance->decoder.parser_step = ToyotaDecoderStepSaveDuration;
                        instance->decoder.decode_data = 0;
                        instance->decoder.decode_count_bit = 0;
                    }
                }
            } else {
                instance->decoder.parser_step = ToyotaDecoderStepReset;
            }
        }
        break;

    case ToyotaDecoderStepSaveDuration:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_toyota_const.te_long * 8) <
               subghz_protocol_toyota_const.te_delta * 3) {
                /* long gap = inter-frame silence, ignore */
                instance->decoder.parser_step = ToyotaDecoderStepReset;
                break;
            }
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = ToyotaDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = ToyotaDecoderStepReset;
        }
        break;

    case ToyotaDecoderStepCheckDuration:
        if(!level) {
            bool bit;
            if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_toyota_const.te_long) <
               subghz_protocol_toyota_const.te_delta) {
                bit = true;
            } else if(
                DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_toyota_const.te_short) <
                subghz_protocol_toyota_const.te_delta) {
                bit = false;
            } else {
                instance->decoder.parser_step = ToyotaDecoderStepReset;
                break;
            }

            subghz_protocol_blocks_add_bit(&instance->decoder, bit);
            instance->decoder.parser_step = ToyotaDecoderStepSaveDuration;

            if(instance->decoder.decode_count_bit == TOYOTA_DATA_BITS) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = TOYOTA_DATA_BITS;
                toyota_extract_fields(instance);
                if(instance->crc_valid) {
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                } else {
                    FURI_LOG_W(TAG, "CRC mismatch, frame dropped");
                }
                instance->decoder.parser_step = ToyotaDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = ToyotaDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_toyota_lexus_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaLexus* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_toyota_lexus_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaLexus* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_toyota_lexus_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaLexus* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret == SubGhzProtocolStatusOk) toyota_extract_fields(instance);
    return ret;
}

// ─── Display ────────────────────────────────────────────────────────────────

static const char* toyota_button_name(uint8_t btn) {
    switch(btn) {
    case TOYOTA_BTN_LOCK: return "Lock";
    case TOYOTA_BTN_UNLOCK: return "Unlock";
    case TOYOTA_BTN_TRUNK: return "Trunk";
    case TOYOTA_BTN_PANIC: return "Panic";
    case TOYOTA_BTN_REMOTE_START: return "RmtStart";
    default: return "Unknown";
    }
}

void subghz_protocol_decoder_toyota_lexus_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderToyotaLexus* instance = context;

    furi_string_cat_printf(
        output,
        "Toyota/Lexus  315/433MHz\r\n"
        "Sn:%08lX  Cnt:%04lX\r\n"
        "Btn:%02X [%s]\r\n"
        "CRC:%02X %s  %dbit",
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn,
        toyota_button_name(instance->generic.btn),
        instance->crc_received,
        instance->crc_valid ? "(OK)" : "(FAIL)",
        instance->generic.data_count_bit);
}

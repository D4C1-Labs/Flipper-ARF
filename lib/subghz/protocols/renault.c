#include "renault.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolRenault"

/*
 * Renault/Dacia RKE — 433.92 MHz
 * PCM (biphase) encoding, 64-bit frame, 2× repeated transmission.
 *
 * Frame layout (MSB-first):
 *   [32b serial number]
 *   [16b rolling counter]
 *   [8b  button code]
 *   [8b  XOR checksum over bytes 0-6]
 *
 * Covers: Clio III/IV, Megane III, Duster, Sandero, Symbol (2005-2020).
 */

static const SubGhzBlockConst subghz_protocol_renault_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 120,
    .min_count_bit_for_found = 64,
};

#define RENAULT_PREAMBLE_MIN 18u
#define RENAULT_DATA_BITS    64u

#define RENAULT_BTN_LOCK   0x01u
#define RENAULT_BTN_UNLOCK 0x02u
#define RENAULT_BTN_TRUNK  0x04u

struct SubGhzProtocolDecoderRenault {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    uint8_t checksum_received;
    bool checksum_valid;
};

struct SubGhzProtocolEncoderRenault {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    RenaultDecoderStepReset = 0,
    RenaultDecoderStepCheckPreamble,
    RenaultDecoderStepSaveDuration,
    RenaultDecoderStepCheckDuration,
} RenaultDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_renault_decoder = {
    .alloc = subghz_protocol_decoder_renault_alloc,
    .free = subghz_protocol_decoder_renault_free,
    .feed = subghz_protocol_decoder_renault_feed,
    .reset = subghz_protocol_decoder_renault_reset,
    .get_hash_data = subghz_protocol_decoder_renault_get_hash_data,
    .serialize = subghz_protocol_decoder_renault_serialize,
    .deserialize = subghz_protocol_decoder_renault_deserialize,
    .get_string = subghz_protocol_decoder_renault_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_renault_encoder = {
    .alloc = subghz_protocol_encoder_renault_alloc,
    .free = subghz_protocol_encoder_renault_free,
    .deserialize = subghz_protocol_encoder_renault_deserialize,
    .stop = subghz_protocol_encoder_renault_stop,
    .yield = subghz_protocol_encoder_renault_yield,
};

const SubGhzProtocol subghz_protocol_renault = {
    .name = RENAULT_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_renault_decoder,
    .encoder = &subghz_protocol_renault_encoder,
};

// ─── Checksum ───────────────────────────────────────────────────────────────

static uint8_t renault_checksum(const uint8_t* data, size_t len) {
    uint8_t xor = 0;
    for(size_t i = 0; i < len; i++) xor ^= data[i];
    return xor;
}

static void renault_extract_fields(SubGhzProtocolDecoderRenault* instance) {
    uint64_t d = instance->generic.data;
    instance->generic.serial = (uint32_t)(d >> 32);
    instance->generic.cnt = (uint16_t)((d >> 16) & 0xFFFF);
    instance->generic.btn = (uint8_t)((d >> 8) & 0xFF);
    instance->checksum_received = (uint8_t)(d & 0xFF);

    uint8_t buf[7];
    for(int i = 0; i < 7; i++) buf[i] = (uint8_t)(d >> (56 - i * 8));
    instance->checksum_valid = (instance->checksum_received == renault_checksum(buf, 7));
}

// ─── Encoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_encoder_renault_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderRenault* instance = malloc(sizeof(SubGhzProtocolEncoderRenault));
    instance->base.protocol = &subghz_protocol_renault;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = 600;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.repeat = 2;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_renault_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderRenault* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_renault_stop(void* context) {
    SubGhzProtocolEncoderRenault* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_renault_yield(void* context) {
    SubGhzProtocolEncoderRenault* instance = context;
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

static bool renault_encoder_get_upload(SubGhzProtocolEncoderRenault* instance) {
    furi_assert(instance);

    if(subghz_custom_btn_get_original() == 0)
        subghz_custom_btn_set_original(instance->generic.btn);
    subghz_custom_btn_set_max(3);

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
    frame[7] = renault_checksum(frame, 7);

    size_t idx = 0;
    /* Preamble */
    for(int i = 0; i < (int)RENAULT_PREAMBLE_MIN; i++) {
        instance->encoder.upload[idx++] =
            level_duration_make(true, subghz_protocol_renault_const.te_short);
        instance->encoder.upload[idx++] =
            level_duration_make(false, subghz_protocol_renault_const.te_short);
    }
    /* Data — biphase/PCM: bit 1 = long pulse, bit 0 = short pulse */
    for(int byte = 0; byte < 8; byte++) {
        for(int bit = 7; bit >= 0; bit--) {
            bool b = (frame[byte] >> bit) & 1;
            uint32_t w = b ? (uint32_t)subghz_protocol_renault_const.te_long :
                             (uint32_t)subghz_protocol_renault_const.te_short;
            instance->encoder.upload[idx++] = level_duration_make(true, w);
            instance->encoder.upload[idx++] = level_duration_make(false, w);
        }
    }
    instance->encoder.upload[idx++] =
        level_duration_make(false, subghz_protocol_renault_const.te_long * 10);
    instance->encoder.size_upload = idx;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_renault_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderRenault* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_renault_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(!renault_encoder_get_upload(instance)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

// ─── Decoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_decoder_renault_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderRenault* instance = malloc(sizeof(SubGhzProtocolDecoderRenault));
    instance->base.protocol = &subghz_protocol_renault;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_renault_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_renault_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRenault* instance = context;
    instance->decoder.parser_step = RenaultDecoderStepReset;
    instance->header_count = 0;
}

void subghz_protocol_decoder_renault_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderRenault* instance = context;

    switch(instance->decoder.parser_step) {
    case RenaultDecoderStepReset:
        if(level &&
           DURATION_DIFF(duration, subghz_protocol_renault_const.te_short) <
               subghz_protocol_renault_const.te_delta) {
            instance->decoder.parser_step = RenaultDecoderStepCheckPreamble;
            instance->header_count = 1;
            instance->decoder.te_last = duration;
        }
        break;

    case RenaultDecoderStepCheckPreamble:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_renault_const.te_short) <
               subghz_protocol_renault_const.te_delta) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = RenaultDecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_renault_const.te_short) <
               subghz_protocol_renault_const.te_delta) {
                instance->header_count++;
                if(instance->header_count >= RENAULT_PREAMBLE_MIN) {
                    instance->decoder.parser_step = RenaultDecoderStepSaveDuration;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 0;
                }
            } else {
                instance->decoder.parser_step = RenaultDecoderStepReset;
            }
        }
        break;

    case RenaultDecoderStepSaveDuration:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = RenaultDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = RenaultDecoderStepReset;
        }
        break;

    case RenaultDecoderStepCheckDuration:
        if(!level) {
            bool bit;
            if(DURATION_DIFF(instance->decoder.te_last, subghz_protocol_renault_const.te_long) <
               subghz_protocol_renault_const.te_delta) {
                bit = true;
            } else if(
                DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_renault_const.te_short) <
                subghz_protocol_renault_const.te_delta) {
                bit = false;
            } else {
                instance->decoder.parser_step = RenaultDecoderStepReset;
                break;
            }
            subghz_protocol_blocks_add_bit(&instance->decoder, bit);
            instance->decoder.parser_step = RenaultDecoderStepSaveDuration;

            if(instance->decoder.decode_count_bit == RENAULT_DATA_BITS) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = RENAULT_DATA_BITS;
                renault_extract_fields(instance);
                if(instance->checksum_valid) {
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                } else {
                    FURI_LOG_W(TAG, "Checksum mismatch, frame dropped");
                }
                instance->decoder.parser_step = RenaultDecoderStepReset;
            }
        } else {
            instance->decoder.parser_step = RenaultDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_renault_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderRenault* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_renault_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderRenault* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_renault_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderRenault* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret == SubGhzProtocolStatusOk) renault_extract_fields(instance);
    return ret;
}

// ─── Display ────────────────────────────────────────────────────────────────

static const char* renault_button_name(uint8_t btn) {
    switch(btn) {
    case RENAULT_BTN_LOCK: return "Lock";
    case RENAULT_BTN_UNLOCK: return "Unlock";
    case RENAULT_BTN_TRUNK: return "Trunk";
    default: return "Unknown";
    }
}

void subghz_protocol_decoder_renault_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderRenault* instance = context;

    furi_string_cat_printf(
        output,
        "Renault/Dacia  433MHz\r\n"
        "Sn:%08lX  Cnt:%04lX\r\n"
        "Btn:%02X [%s]\r\n"
        "CHK:%02X %s  %dbit",
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn,
        renault_button_name(instance->generic.btn),
        instance->checksum_received,
        instance->checksum_valid ? "(OK)" : "(FAIL)",
        instance->generic.data_count_bit);
}

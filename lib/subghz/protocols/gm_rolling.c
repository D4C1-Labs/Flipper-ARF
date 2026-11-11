#include "gm_rolling.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>

#define TAG "SubGhzProtocolGmRolling"

/*
 * GM/Chevrolet/GMC/Buick/Cadillac RKE — 315 MHz
 * Manchester encoding, 64-bit frame, 3× repeated transmission.
 *
 * Frame layout (MSB-first):
 *   [32b serial number]
 *   [16b rolling counter]
 *   [8b  button code]
 *   [8b  XOR checksum: serial_bytes XOR counter_bytes XOR button]
 *
 * Covers: Chevrolet, GMC, Buick, Cadillac (2000–2015).
 */

static const SubGhzBlockConst subghz_protocol_gm_rolling_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = 64,
};

#define GM_PREAMBLE_COUNT_MIN 10u
#define GM_DATA_BITS          64u

#define GM_BTN_LOCK         0x01u
#define GM_BTN_UNLOCK       0x02u
#define GM_BTN_TRUNK        0x04u
#define GM_BTN_PANIC        0x08u
#define GM_BTN_REMOTE_START 0x10u

struct SubGhzProtocolDecoderGmRolling {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_state;
    uint8_t decoder_state;
    uint16_t preamble_count;
    uint32_t te_last;
    uint8_t checksum_received;
    bool checksum_valid;
};

struct SubGhzProtocolEncoderGmRolling {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    GmDecoderStepReset = 0,
    GmDecoderStepPreamble,
    GmDecoderStepData,
} GmDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_gm_rolling_decoder = {
    .alloc = subghz_protocol_decoder_gm_rolling_alloc,
    .free = subghz_protocol_decoder_gm_rolling_free,
    .feed = subghz_protocol_decoder_gm_rolling_feed,
    .reset = subghz_protocol_decoder_gm_rolling_reset,
    .get_hash_data = subghz_protocol_decoder_gm_rolling_get_hash_data,
    .serialize = subghz_protocol_decoder_gm_rolling_serialize,
    .deserialize = subghz_protocol_decoder_gm_rolling_deserialize,
    .get_string = subghz_protocol_decoder_gm_rolling_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_gm_rolling_encoder = {
    .alloc = subghz_protocol_encoder_gm_rolling_alloc,
    .free = subghz_protocol_encoder_gm_rolling_free,
    .deserialize = subghz_protocol_encoder_gm_rolling_deserialize,
    .stop = subghz_protocol_encoder_gm_rolling_stop,
    .yield = subghz_protocol_encoder_gm_rolling_yield,
};

const SubGhzProtocol subghz_protocol_gm_rolling = {
    .name = GM_ROLLING_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_gm_rolling_decoder,
    .encoder = &subghz_protocol_gm_rolling_encoder,
};

// ─── Checksum ───────────────────────────────────────────────────────────────

static uint8_t gm_checksum(uint32_t serial, uint16_t cnt, uint8_t btn) {
    return (uint8_t)(
        ((serial >> 24) & 0xFF) ^ ((serial >> 16) & 0xFF) ^ ((serial >> 8) & 0xFF) ^
        (serial & 0xFF) ^ ((cnt >> 8) & 0xFF) ^ (cnt & 0xFF) ^ btn);
}

static void gm_extract_fields(SubGhzProtocolDecoderGmRolling* instance) {
    uint64_t d = instance->generic.data;
    instance->generic.serial = (uint32_t)(d >> 32);
    instance->generic.cnt = (uint16_t)((d >> 16) & 0xFFFF);
    instance->generic.btn = (uint8_t)((d >> 8) & 0xFF);
    instance->checksum_received = (uint8_t)(d & 0xFF);
    instance->checksum_valid = (instance->checksum_received ==
                                gm_checksum(
                                    instance->generic.serial,
                                    instance->generic.cnt,
                                    instance->generic.btn));
}

// ─── Encoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_encoder_gm_rolling_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderGmRolling* instance = malloc(sizeof(SubGhzProtocolEncoderGmRolling));
    instance->base.protocol = &subghz_protocol_gm_rolling;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = 700;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.repeat = 3;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_gm_rolling_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderGmRolling* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_gm_rolling_stop(void* context) {
    SubGhzProtocolEncoderGmRolling* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_gm_rolling_yield(void* context) {
    SubGhzProtocolEncoderGmRolling* instance = context;
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

static bool gm_encoder_get_upload(SubGhzProtocolEncoderGmRolling* instance) {
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

    uint8_t chk = gm_checksum(instance->generic.serial, instance->generic.cnt, btn);

    uint64_t data = ((uint64_t)instance->generic.serial << 32) |
                    ((uint64_t)instance->generic.cnt << 16) | ((uint64_t)btn << 8) | chk;

    size_t idx = 0;
    /* Preamble: 12 Manchester half-periods */
    for(int i = 0; i < (int)GM_PREAMBLE_COUNT_MIN * 2; i++) {
        bool lvl = (i % 2 == 0);
        instance->encoder.upload[idx++] =
            level_duration_make(lvl, subghz_protocol_gm_rolling_const.te_short);
    }

    /* Manchester encoding: 1 = high-then-low, 0 = low-then-high (IEEE 802.3) */
    for(int bit = GM_DATA_BITS - 1; bit >= 0; bit--) {
        bool b = (data >> bit) & 1;
        instance->encoder.upload[idx++] =
            level_duration_make(b, subghz_protocol_gm_rolling_const.te_short);
        instance->encoder.upload[idx++] =
            level_duration_make(!b, subghz_protocol_gm_rolling_const.te_short);
    }

    /* Inter-frame gap */
    instance->encoder.upload[idx++] =
        level_duration_make(false, subghz_protocol_gm_rolling_const.te_long * 12);

    instance->encoder.size_upload = idx;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_gm_rolling_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderGmRolling* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_gm_rolling_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;
    if(!gm_encoder_get_upload(instance)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

// ─── Decoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_decoder_gm_rolling_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderGmRolling* instance = malloc(sizeof(SubGhzProtocolDecoderGmRolling));
    instance->base.protocol = &subghz_protocol_gm_rolling;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_gm_rolling_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_gm_rolling_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRolling* instance = context;
    instance->decoder_state = GmDecoderStepReset;
    instance->preamble_count = 0;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_gm_rolling_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRolling* instance = context;

    uint32_t te_short = subghz_protocol_gm_rolling_const.te_short;
    uint32_t te_long = subghz_protocol_gm_rolling_const.te_long;
    uint32_t te_delta = subghz_protocol_gm_rolling_const.te_delta;

    switch(instance->decoder_state) {
    case GmDecoderStepReset:
        if(level && DURATION_DIFF(duration, te_short) < te_delta) {
            instance->decoder_state = GmDecoderStepPreamble;
            instance->preamble_count = 1;
            instance->te_last = duration;
        }
        break;

    case GmDecoderStepPreamble:
        if(DURATION_DIFF(duration, te_short) < te_delta) {
            instance->preamble_count++;
            if(instance->preamble_count >= GM_PREAMBLE_COUNT_MIN * 2) {
                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                manchester_advance(
                    instance->manchester_state,
                    ManchesterEventReset,
                    &instance->manchester_state,
                    NULL);
                instance->decoder_state = GmDecoderStepData;
            }
        } else {
            instance->decoder_state = GmDecoderStepReset;
        }
        break;

    case GmDecoderStepData: {
        if(instance->decoder.decode_count_bit >= GM_DATA_BITS) {
            instance->decoder_state = GmDecoderStepReset;
            break;
        }

        ManchesterEvent event = ManchesterEventReset;
        uint32_t diff = DURATION_DIFF(duration, te_short);
        if(diff < te_delta) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        } else {
            diff = DURATION_DIFF(duration, te_long);
            if(diff < te_delta) {
                event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
            }
        }

        if(event != ManchesterEventReset) {
            bool data_bit;
            if(manchester_advance(
                   instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, data_bit);

                if(instance->decoder.decode_count_bit == GM_DATA_BITS) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = GM_DATA_BITS;
                    gm_extract_fields(instance);
                    if(instance->checksum_valid) {
                        if(instance->base.callback)
                            instance->base.callback(&instance->base, instance->base.context);
                    } else {
                        FURI_LOG_W(TAG, "Checksum mismatch, frame dropped");
                    }
                    instance->decoder_state = GmDecoderStepReset;
                }
            }
        } else {
            instance->decoder_state = GmDecoderStepReset;
        }
        break;
    }
    }
}

uint8_t subghz_protocol_decoder_gm_rolling_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRolling* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_gm_rolling_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRolling* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_gm_rolling_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRolling* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret == SubGhzProtocolStatusOk) gm_extract_fields(instance);
    return ret;
}

// ─── Display ────────────────────────────────────────────────────────────────

static const char* gm_button_name(uint8_t btn) {
    switch(btn) {
    case GM_BTN_LOCK: return "Lock";
    case GM_BTN_UNLOCK: return "Unlock";
    case GM_BTN_TRUNK: return "Trunk";
    case GM_BTN_PANIC: return "Panic";
    case GM_BTN_REMOTE_START: return "RmtStart";
    default: return "Unknown";
    }
}

void subghz_protocol_decoder_gm_rolling_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderGmRolling* instance = context;

    furi_string_cat_printf(
        output,
        "GM/Chevrolet/Buick  315MHz\r\n"
        "Sn:%08lX  Cnt:%04lX\r\n"
        "Btn:%02X [%s]\r\n"
        "CHK:%02X %s  %dbit",
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn,
        gm_button_name(instance->generic.btn),
        instance->checksum_received,
        instance->checksum_valid ? "(OK)" : "(FAIL)",
        instance->generic.data_count_bit);
}

#include "renault_marelli.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>
#include <furi_hal_subghz.h>

#define TAG "RenaultMarelli"

//   Magneti Marelli BSI keyfob protocol (PCF7946) — Renault variant
//   Found on: Renault Clio III, Modus, Kangoo II, and some Dacia models
//   sharing the Fiat/Renault Marelli platform (~2004-2014)
//
//   RF: 433.92 MHz, Manchester encoding (FSK modulation)
//   Two timing variants with identical frame structure:
//     Type A (standard):          te_short ~260us, te_long ~520us
//     Type B (fast/compact):      te_short ~100us, te_long ~200us
//   TE is auto-detected from preamble pulse averaging.
//
//   Frame layout (103-104 bits = 13 bytes):
//     Bytes 0-1:  0xFFFF/0xFFFC preamble residue
//     Bytes 2-5:  Serial (32 bits)
//     Byte 6:     [Button:4 | Epoch:4]
//     Byte 7:     [Counter:5 | Scramble:2 | Fixed:1]
//     Bytes 8-12: Encrypted payload (40 bits)

#define REN_MARELLI_PREAMBLE_PULSE_MIN 50
#define REN_MARELLI_PREAMBLE_PULSE_MAX 350
#define REN_MARELLI_PREAMBLE_MIN       80
#define REN_MARELLI_MAX_DATA_BITS      104
#define REN_MARELLI_MIN_DATA_BITS      80
#define REN_MARELLI_GAP_TE_MULT        4
#define REN_MARELLI_SYNC_TE_MIN_MULT   4
#define REN_MARELLI_SYNC_TE_MAX_MULT   12
#define REN_MARELLI_RETX_GAP_MIN       5000
#define REN_MARELLI_RETX_SYNC_MIN      400
#define REN_MARELLI_RETX_SYNC_MAX      2800
#define REN_MARELLI_TE_TYPE_AB_BOUNDARY 180

static const SubGhzBlockConst subghz_protocol_renault_marelli_const = {
    .te_short = 260,
    .te_long = 520,
    .te_delta = 80,
    .min_count_bit_for_found = 80,
};

struct SubGhzProtocolDecoderRenaultMarelli {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    ManchesterState manchester_state;
    uint8_t decoder_state;
    uint16_t preamble_count;
    uint8_t raw_data[13];
    uint8_t bit_count;
    uint32_t extra_data;
    uint32_t te_last;
    uint32_t te_sum;
    uint16_t te_count;
    uint32_t te_detected;
};

struct SubGhzProtocolEncoderRenaultMarelli {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint8_t raw_data[13];
    uint32_t extra_data;
    uint8_t bit_count;
    uint32_t te_detected;
};

typedef enum {
    RenMarelliDecoderStepReset = 0,
    RenMarelliDecoderStepPreamble = 1,
    RenMarelliDecoderStepSync = 2,
    RenMarelliDecoderStepData = 3,
    RenMarelliDecoderStepRetxSync = 4,
} RenMarelliDecoderStep;

static bool renault_marelli_button_is_renault(uint8_t btn) {
    // Renault Marelli: button codes usually seen as 0x1/0x2/0x4.
    // Fiat Marelli commonly uses 0x7/0xB/0xD.
    return (btn == 0x1) || (btn == 0x2) || (btn == 0x4);
}

static bool renault_marelli_frame_is_renault(const SubGhzProtocolDecoderRenaultMarelli* instance) {
    const uint8_t btn = (instance->raw_data[6] >> 4) & 0xF;
    const uint8_t fixed = instance->raw_data[7] & 0x1;
    const uint8_t scramble = (instance->raw_data[7] >> 1) & 0x3;

    if(!renault_marelli_button_is_renault(btn)) {
        return false;
    }

    // Keep structural guards to reduce false positives from noise.
    if(fixed > 1U || scramble > 3U) {
        return false;
    }

    return true;
}

const SubGhzProtocolDecoder subghz_protocol_renault_marelli_decoder = {
    .alloc = subghz_protocol_decoder_renault_marelli_alloc,
    .free = subghz_protocol_decoder_renault_marelli_free,
    .feed = subghz_protocol_decoder_renault_marelli_feed,
    .reset = subghz_protocol_decoder_renault_marelli_reset,
    .get_hash_data = subghz_protocol_decoder_renault_marelli_get_hash_data,
    .serialize = subghz_protocol_decoder_renault_marelli_serialize,
    .deserialize = subghz_protocol_decoder_renault_marelli_deserialize,
    .get_string = subghz_protocol_decoder_renault_marelli_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_renault_marelli_encoder = {
    .alloc = subghz_protocol_encoder_renault_marelli_alloc,
    .free = subghz_protocol_encoder_renault_marelli_free,
    .deserialize = subghz_protocol_encoder_renault_marelli_deserialize,
    .stop = subghz_protocol_encoder_renault_marelli_stop,
    .yield = subghz_protocol_encoder_renault_marelli_yield,
};

const SubGhzProtocol subghz_protocol_renault_marelli = {
    .name = RENAULT_MARELLI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_renault_marelli_decoder,
    .encoder = &subghz_protocol_renault_marelli_encoder,
};

// ============================================================================
// Encoder
// ============================================================================

#define REN_MARELLI_ENCODER_UPLOAD_MAX 1500
#define REN_MARELLI_ENCODER_REPEAT     3
#define REN_MARELLI_PREAMBLE_PAIRS     100

void* subghz_protocol_encoder_renault_marelli_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderRenaultMarelli* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderRenaultMarelli));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_renault_marelli;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = REN_MARELLI_ENCODER_REPEAT;
    instance->encoder.size_upload = REN_MARELLI_ENCODER_UPLOAD_MAX;
    instance->encoder.upload =
        malloc(REN_MARELLI_ENCODER_UPLOAD_MAX * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_renault_marelli_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderRenaultMarelli* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static bool renault_marelli_encoder_get_upload(SubGhzProtocolEncoderRenaultMarelli* instance) {
    uint32_t te = instance->te_detected;
    if(te == 0) te = subghz_protocol_renault_marelli_const.te_short;

    uint32_t te_short = te;
    uint32_t te_long = te * 2;
    uint32_t gap_duration = te * 12;
    uint32_t sync_duration = te * 8;

    size_t index = 0;
    size_t max_upload = REN_MARELLI_ENCODER_UPLOAD_MAX;
    uint8_t data_bits = instance->bit_count;
    if(data_bits == 0) data_bits = instance->generic.data_count_bit;
    if(data_bits < REN_MARELLI_MIN_DATA_BITS || data_bits > REN_MARELLI_MAX_DATA_BITS) {
        return false;
    }

    // Preamble: alternating short HIGH/LOW
    for(uint8_t i = 0; i < REN_MARELLI_PREAMBLE_PAIRS && (index + 1) < max_upload; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, te_short);
        if(i < REN_MARELLI_PREAMBLE_PAIRS - 1) {
            instance->encoder.upload[index++] = level_duration_make(false, te_short);
        }
    }

    // Gap after preamble
    if(index < max_upload) {
        instance->encoder.upload[index++] =
            level_duration_make(false, te_short + gap_duration);
    }

    // Sync pulse
    if(index < max_upload) {
        instance->encoder.upload[index++] = level_duration_make(true, sync_duration);
    }

    // Manchester-encode data bits
    bool in_mid1 = true;

    for(uint8_t bit_i = 0; bit_i < data_bits && (index + 1) < max_upload; bit_i++) {
        uint8_t byte_idx = bit_i / 8;
        uint8_t bit_pos = 7 - (bit_i % 8);
        bool data_bit = (instance->raw_data[byte_idx] >> bit_pos) & 1;

        if(in_mid1) {
            if(data_bit) {
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
            } else {
                instance->encoder.upload[index++] = level_duration_make(false, te_long);
                in_mid1 = false;
            }
        } else {
            if(data_bit) {
                instance->encoder.upload[index++] = level_duration_make(true, te_long);
                in_mid1 = true;
            } else {
                instance->encoder.upload[index++] = level_duration_make(true, te_short);
                instance->encoder.upload[index++] = level_duration_make(false, te_short);
            }
        }
    }

    // Trailing gap
    if(in_mid1) {
        if(index < max_upload) {
            instance->encoder.upload[index++] =
                level_duration_make(false, te_short + gap_duration * 3);
        }
    } else {
        if(index > 0) {
            instance->encoder.upload[index - 1] =
                level_duration_make(false, te_short + gap_duration * 3);
        }
    }

    instance->encoder.size_upload = index;
    return index > 0;
}

static void renault_marelli_encoder_rebuild_raw_data(
    SubGhzProtocolEncoderRenaultMarelli* instance) {
    memset(instance->raw_data, 0, sizeof(instance->raw_data));

    uint64_t key = instance->generic.data;
    for(int i = 0; i < 8; i++) {
        instance->raw_data[i] = (uint8_t)(key >> (56 - i * 8));
    }

    uint8_t extra_bits =
        instance->generic.data_count_bit > 64 ? (instance->generic.data_count_bit - 64) : 0;
    for(uint8_t i = 0; i < extra_bits && i < 32; i++) {
        uint8_t byte_idx = 8 + (i / 8);
        uint8_t bit_pos = 7 - (i % 8);
        if(instance->extra_data & (1UL << (extra_bits - 1 - i))) {
            instance->raw_data[byte_idx] |= (1 << bit_pos);
        }
    }

    instance->bit_count = instance->generic.data_count_bit;
}

SubGhzProtocolStatus subghz_protocol_encoder_renault_marelli_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderRenaultMarelli* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;

        uint32_t extra = 0;
        if(flipper_format_read_uint32(flipper_format, "Extra", &extra, 1)) {
            instance->extra_data = extra;
        }

        uint32_t te = 0;
        if(flipper_format_read_uint32(flipper_format, "TE", &te, 1)) {
            instance->te_detected = te;
        }

        renault_marelli_encoder_rebuild_raw_data(instance);

        if(!renault_marelli_encoder_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        instance->encoder.repeat = REN_MARELLI_ENCODER_REPEAT;
        instance->encoder.front = 0;
        instance->encoder.is_running = true;

    } while(false);

    return ret;
}

void subghz_protocol_encoder_renault_marelli_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderRenaultMarelli* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_renault_marelli_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderRenaultMarelli* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) {
            instance->encoder.repeat--;
        }
        instance->encoder.front = 0;
    }

    return ret;
}

// ============================================================================
// Decoder
// ============================================================================

static void renault_marelli_rebuild_raw_data(SubGhzProtocolDecoderRenaultMarelli* instance) {
    memset(instance->raw_data, 0, sizeof(instance->raw_data));

    uint64_t key = instance->generic.data;
    for(int i = 0; i < 8; i++) {
        instance->raw_data[i] = (uint8_t)(key >> (56 - i * 8));
    }

    uint8_t extra_bits =
        instance->generic.data_count_bit > 64 ? (instance->generic.data_count_bit - 64) : 0;
    for(uint8_t i = 0; i < extra_bits && i < 32; i++) {
        uint8_t byte_idx = 8 + (i / 8);
        uint8_t bit_pos = 7 - (i % 8);
        if(instance->extra_data & (1UL << (extra_bits - 1 - i))) {
            instance->raw_data[byte_idx] |= (1 << bit_pos);
        }
    }

    instance->bit_count = instance->generic.data_count_bit;

    if(instance->bit_count >= 56) {
        instance->generic.serial =
            ((uint32_t)instance->raw_data[2] << 24) |
            ((uint32_t)instance->raw_data[3] << 16) |
            ((uint32_t)instance->raw_data[4] << 8) |
            ((uint32_t)instance->raw_data[5]);
        instance->generic.btn = (instance->raw_data[6] >> 4) & 0xF;
        instance->generic.cnt = (instance->raw_data[7] >> 3) & 0x1F;
    }
}

static void renault_marelli_prepare_data(SubGhzProtocolDecoderRenaultMarelli* instance) {
    instance->bit_count = 0;
    instance->extra_data = 0;
    instance->generic.data = 0;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    manchester_advance(
        instance->manchester_state,
        ManchesterEventReset,
        &instance->manchester_state,
        NULL);
    instance->decoder_state = RenMarelliDecoderStepData;
}

void* subghz_protocol_decoder_renault_marelli_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderRenaultMarelli* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderRenaultMarelli));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_renault_marelli;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_renault_marelli_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultMarelli* instance = context;
    free(instance);
}

void subghz_protocol_decoder_renault_marelli_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultMarelli* instance = context;
    instance->decoder_state = RenMarelliDecoderStepReset;
    instance->preamble_count = 0;
    instance->bit_count = 0;
    instance->extra_data = 0;
    instance->te_last = 0;
    instance->te_sum = 0;
    instance->te_count = 0;
    instance->te_detected = 0;
    instance->generic.data = 0;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_renault_marelli_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultMarelli* instance = context;

    uint32_t te_short = instance->te_detected
                            ? instance->te_detected
                            : (uint32_t)subghz_protocol_renault_marelli_const.te_short;
    uint32_t te_long = te_short * 2;
    uint32_t te_delta = te_short / 2;
    if(te_delta < 30) te_delta = 30;
    uint32_t diff;

    switch(instance->decoder_state) {
    case RenMarelliDecoderStepReset:
        if(level) {
            if(duration >= REN_MARELLI_PREAMBLE_PULSE_MIN &&
               duration <= REN_MARELLI_PREAMBLE_PULSE_MAX) {
                instance->decoder_state = RenMarelliDecoderStepPreamble;
                instance->preamble_count = 1;
                instance->te_sum = duration;
                instance->te_count = 1;
                instance->te_last = duration;
            }
        } else {
            if(duration > REN_MARELLI_RETX_GAP_MIN) {
                instance->decoder_state = RenMarelliDecoderStepRetxSync;
                instance->te_last = duration;
            }
        }
        break;

    case RenMarelliDecoderStepPreamble:
        if(duration >= REN_MARELLI_PREAMBLE_PULSE_MIN &&
           duration <= REN_MARELLI_PREAMBLE_PULSE_MAX) {
            instance->preamble_count++;
            instance->te_sum += duration;
            instance->te_count++;
            instance->te_last = duration;
        } else if(!level) {
            if(instance->preamble_count >= REN_MARELLI_PREAMBLE_MIN &&
               instance->te_count > 0) {
                instance->te_detected = instance->te_sum / instance->te_count;
                uint32_t gap_threshold =
                    instance->te_detected * REN_MARELLI_GAP_TE_MULT;

                if(duration > gap_threshold) {
                    instance->decoder_state = RenMarelliDecoderStepSync;
                    instance->te_last = duration;
                } else {
                    instance->decoder_state = RenMarelliDecoderStepReset;
                }
            } else {
                instance->decoder_state = RenMarelliDecoderStepReset;
            }
        } else {
            instance->decoder_state = RenMarelliDecoderStepReset;
        }
        break;

    case RenMarelliDecoderStepSync: {
        uint32_t sync_min = instance->te_detected * REN_MARELLI_SYNC_TE_MIN_MULT;
        uint32_t sync_max = instance->te_detected * REN_MARELLI_SYNC_TE_MAX_MULT;

        if(level && duration >= sync_min && duration <= sync_max) {
            renault_marelli_prepare_data(instance);
            instance->te_last = duration;
        } else {
            instance->decoder_state = RenMarelliDecoderStepReset;
        }
        break;
    }

    case RenMarelliDecoderStepRetxSync:
        if(level && duration >= REN_MARELLI_RETX_SYNC_MIN &&
           duration <= REN_MARELLI_RETX_SYNC_MAX) {
            if(!instance->te_detected) {
                instance->te_detected = duration / 8;
                if(instance->te_detected < 70) instance->te_detected = 100;
                if(instance->te_detected > 350) instance->te_detected = 260;
            }
            renault_marelli_prepare_data(instance);
            instance->te_last = duration;
        } else {
            instance->decoder_state = RenMarelliDecoderStepReset;
        }
        break;

    case RenMarelliDecoderStepData: {
        ManchesterEvent event = ManchesterEventReset;
        bool frame_complete = false;

        diff = (duration > te_short) ? (duration - te_short) : (te_short - duration);
        if(diff < te_delta) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        } else {
            diff = (duration > te_long) ? (duration - te_long) : (te_long - duration);
            if(diff < te_delta) {
                event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
            }
        }

        if(event != ManchesterEventReset) {
            bool data_bit;
            if(manchester_advance(
                   instance->manchester_state,
                   event,
                   &instance->manchester_state,
                   &data_bit)) {
                uint32_t new_bit = data_bit ? 1 : 0;

                if(instance->bit_count < REN_MARELLI_MAX_DATA_BITS) {
                    uint8_t byte_idx = instance->bit_count / 8;
                    uint8_t bit_pos = 7 - (instance->bit_count % 8);
                    if(new_bit) {
                        instance->raw_data[byte_idx] |= (1 << bit_pos);
                    }
                }

                if(instance->bit_count < 64) {
                    instance->generic.data = (instance->generic.data << 1) | new_bit;
                } else {
                    instance->extra_data = (instance->extra_data << 1) | new_bit;
                }

                instance->bit_count++;

                if(instance->bit_count >= REN_MARELLI_MAX_DATA_BITS) {
                    frame_complete = true;
                }
            }
        } else {
            if(instance->bit_count >= REN_MARELLI_MIN_DATA_BITS) {
                frame_complete = true;
            } else {
                instance->decoder_state = RenMarelliDecoderStepReset;
            }
        }

        if(frame_complete) {
            instance->generic.data_count_bit = instance->bit_count;

            instance->generic.serial =
                ((uint32_t)instance->raw_data[2] << 24) |
                ((uint32_t)instance->raw_data[3] << 16) |
                ((uint32_t)instance->raw_data[4] << 8) |
                ((uint32_t)instance->raw_data[5]);
            instance->generic.btn = (instance->raw_data[6] >> 4) & 0xF;
            instance->generic.cnt = (instance->raw_data[7] >> 3) & 0x1F;

            if(renault_marelli_frame_is_renault(instance) && instance->base.callback) {
                instance->base.callback(&instance->base, instance->base.context);
            }

            instance->decoder_state = RenMarelliDecoderStepReset;
        }

        instance->te_last = duration;
        break;
    }
    }
}

uint8_t subghz_protocol_decoder_renault_marelli_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultMarelli* instance = context;
    SubGhzBlockDecoder decoder = {
        .decode_data = instance->generic.data,
        .decode_count_bit =
            instance->generic.data_count_bit > 64 ? 64 : instance->generic.data_count_bit,
    };
    return subghz_protocol_blocks_get_hash_data(
        &decoder, (decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_renault_marelli_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultMarelli* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_write_uint32(flipper_format, "Extra", &instance->extra_data, 1);

        uint32_t extra_bits = instance->generic.data_count_bit > 64
                                  ? (instance->generic.data_count_bit - 64)
                                  : 0;
        flipper_format_write_uint32(flipper_format, "Extra_bits", &extra_bits, 1);

        uint32_t te = instance->te_detected;
        flipper_format_write_uint32(flipper_format, "TE", &te, 1);
    }

    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_renault_marelli_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultMarelli* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t extra = 0;
        if(flipper_format_read_uint32(flipper_format, "Extra", &extra, 1)) {
            instance->extra_data = extra;
        }

        uint32_t te = 0;
        if(flipper_format_read_uint32(flipper_format, "TE", &te, 1)) {
            instance->te_detected = te;
        }

        renault_marelli_rebuild_raw_data(instance);
    }

    return ret;
}

static const char* renault_marelli_button_name(uint8_t btn) {
    switch(btn) {
    case 0x1:
        return "Lock";
    case 0x2:
        return "Unlock";
    case 0x4:
        return "Trunk";
    case 0x7:
        return "Lock";
    case 0xB:
        return "Unlock";
    case 0xD:
        return "Trunk";
    default:
        return "Unknown";
    }
}

void subghz_protocol_decoder_renault_marelli_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderRenaultMarelli* instance = context;

    uint8_t epoch = instance->raw_data[6] & 0xF;
    uint8_t counter = (instance->raw_data[7] >> 3) & 0x1F;
    const char* variant = (instance->te_detected &&
                           instance->te_detected < REN_MARELLI_TE_TYPE_AB_BOUNDARY)
                              ? "B"
                              : "A";
    uint8_t scramble = (instance->raw_data[7] >> 1) & 0x3;
    uint8_t fixed = instance->raw_data[7] & 0x1;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Enc:%02X%02X%02X%02X%02X Scr:%02X\r\n"
        "Raw:%02X%02X Fixed:%X\r\n"
        "Sn:%08X Cnt:%02X\r\n"
        "Btn:%02X:[%s] Ep:%02X\r\n"
        "Tp:%s\r\n",
        instance->generic.protocol_name,
        (int)instance->bit_count,
        instance->raw_data[8],
        instance->raw_data[9],
        instance->raw_data[10],
        instance->raw_data[11],
        instance->raw_data[12],
        (unsigned)scramble,
        instance->raw_data[6],
        instance->raw_data[7],
        (unsigned)fixed,
        (unsigned int)instance->generic.serial,
        (unsigned)counter,
        (unsigned)instance->generic.btn,
        renault_marelli_button_name(instance->generic.btn),
        (unsigned)epoch,
        variant);
}

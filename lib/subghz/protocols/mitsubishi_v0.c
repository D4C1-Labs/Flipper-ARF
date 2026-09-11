#include "mitsubishi_v0.h"
#include <inttypes.h>

#include "../blocks/custom_btn_i.h"

#define TAG                          "MitsubishiProtocolV0"
#define MITSUBISHI_V0_PREAMBLE_COUNT 100
#define MITSUBISHI_V0_BIT_TE         250
#define MITSUBISHI_V0_BIT_TE_GAP     500
#define MITSUBISHI_V0_BIT_COUNT      96 // 12 bytes * 8 bits
#define MITSUBISHI_V0_TOTAL_BURSTS   3
#define MITSUBISHI_V0_INTER_BURST_GAP 25000

static const SubGhzBlockConst subghz_protocol_mitsubishi_v0_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 80,
};

// [PROTOPIRATE_PORT] Added decoder_state field for FSM-based decoding.
// te_last removed in favor of decoder.te_last (SubGhzBlockDecoder field), matching PP layout.
struct SubGhzProtocolDecoderMitsubishiV0 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t decoder_state; // [PROTOPIRATE_PORT]
    uint16_t bit_count;
    uint8_t decode_data[12];
};

// [PROTOPIRATE_PORT] FSM states for atomic pulse-pair validation.
typedef enum {
    MitsubishiV0DecoderStepReset = 0,
    MitsubishiV0DecoderStepDataSave,
    MitsubishiV0DecoderStepDataCheck,
} MitsubishiV0DecoderStep;

struct SubGhzProtocolEncoderMitsubishiV0 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    size_t upload_capacity;
};

// ============================================================================
// HELPER FUNCTIONS (Scrambling & Logic)
// ============================================================================

static void mitsubishi_v0_scramble(uint8_t* payload, uint16_t counter) {
    uint8_t hi = (counter >> 8) & 0xFF;
    uint8_t lo = counter & 0xFF;
    uint8_t mask1 = (hi & 0xAA) | (lo & 0x55);
    uint8_t mask2 = (lo & 0xAA) | (hi & 0x55);
    uint8_t mask3 = mask1 ^ mask2;

    // Apply scrambling to first 5 bytes (as per sub_ROM_148BE @ 0x148BE)
    for(int i = 0; i < 5; i++) {
        payload[i] ^= mask3;
    }

    // Apply inversion (first 8 bytes) — firmware XORs bytes 1..8 with 0xFF in sub_ROM_151E8
    for(int i = 0; i < 8; i++) {
        payload[i] = ~payload[i];
    }
}

// [PROTOPIRATE_PORT] Decoder helpers — atomic pulse-pair validation, ported from
// ProtoPirate's mitsubishi_v0 FSM. Provides robustness against glitches by resetting
// payload on any invalid pair rather than silently drifting on te_last.

static void mitsubishi_v0_reset_payload(SubGhzProtocolDecoderMitsubishiV0* instance) {
    instance->bit_count = 0;
    memset(instance->decode_data, 0, sizeof(instance->decode_data));
}

// [PROTOPIRATE_PORT] Validate a (high, low) pulse pair and, on success, append the
// decoded bit MSB-first into decode_data. Returns false on invalid pair so the
// caller can reset the FSM.
static bool mitsubishi_v0_collect_pair(
    SubGhzProtocolDecoderMitsubishiV0* instance,
    uint32_t high,
    uint32_t low) {
    bool bit_value;

    if(DURATION_DIFF(high, subghz_protocol_mitsubishi_v0_const.te_short) <
           subghz_protocol_mitsubishi_v0_const.te_delta &&
       DURATION_DIFF(low, subghz_protocol_mitsubishi_v0_const.te_long) <
           subghz_protocol_mitsubishi_v0_const.te_delta) {
        bit_value = true;
    } else if(
        DURATION_DIFF(high, subghz_protocol_mitsubishi_v0_const.te_long) <
            subghz_protocol_mitsubishi_v0_const.te_delta &&
        DURATION_DIFF(low, subghz_protocol_mitsubishi_v0_const.te_short) <
            subghz_protocol_mitsubishi_v0_const.te_delta) {
        bit_value = false;
    } else {
        return false;
    }

    uint16_t bit_index = instance->bit_count;
    if(bit_index < MITSUBISHI_V0_BIT_COUNT) {
        if(bit_value) {
            uint8_t byte_index = bit_index >> 3;
            uint8_t bit_position = 7 - (bit_index & 0x07);
            instance->decode_data[byte_index] |= (1U << bit_position);
        }
        instance->bit_count++;
    }

    return true;
}

// [PROTOPIRATE_PORT] Unscramble decoded bytes (mitsubishi_v0_scramble is involutive
// in its operations: ~~x = x, x^m^m = x) and publish decoded frame via callback.
// NOTE: The counter used to derive mask3 is read from bytes 4,5 AFTER inversion but
// BEFORE XOR-unscrambling. This matches the ARF encoder's behaviour and the previous
// ARF decoder — see the TODO at bottom of file regarding cnt roundtripping.
static void mitsubishi_v0_publish_frame(SubGhzProtocolDecoderMitsubishiV0* instance) {
    uint8_t payload[12];
    memcpy(payload, instance->decode_data, sizeof(payload));

    // Undo inversion
    for(uint8_t i = 0; i < 8; i++) {
        payload[i] = (uint8_t)~payload[i];
    }

    uint16_t counter = ((uint16_t)payload[4] << 8) | payload[5];
    uint8_t hi = (counter >> 8) & 0xFF;
    uint8_t lo = counter & 0xFF;
    uint8_t mask1 = (hi & 0xAAU) | (lo & 0x55U);
    uint8_t mask2 = (lo & 0xAAU) | (hi & 0x55U);
    uint8_t mask3 = mask1 ^ mask2;

    // Undo XOR scrambling on first 5 bytes
    for(uint8_t i = 0; i < 5; i++) {
        payload[i] ^= mask3;
    }

    instance->generic.data_count_bit = instance->bit_count;
    instance->generic.serial = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                               ((uint32_t)payload[2] << 8) | payload[3];
    instance->generic.cnt = ((uint16_t)payload[4] << 8) | payload[5];
    instance->generic.btn = payload[6];

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

// ============================================================================
// PROTOCOL INTERFACE DEFINITIONS
// ============================================================================

const SubGhzProtocolDecoder subghz_protocol_mitsubishi_v0_decoder = {
    .alloc = subghz_protocol_decoder_mitsubishi_v0_alloc,
    .free = subghz_protocol_decoder_mitsubishi_v0_free,
    .feed = subghz_protocol_decoder_mitsubishi_v0_feed,
    .reset = subghz_protocol_decoder_mitsubishi_v0_reset,
    .get_hash_data = subghz_protocol_decoder_mitsubishi_v0_get_hash_data,
    .serialize = subghz_protocol_decoder_mitsubishi_v0_serialize,
    .deserialize = subghz_protocol_decoder_mitsubishi_v0_deserialize,
    .get_string = subghz_protocol_decoder_mitsubishi_v0_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_mitsubishi_v0_encoder = {
    .alloc = subghz_protocol_encoder_mitsubishi_v0_alloc,
    .free = subghz_protocol_encoder_mitsubishi_v0_free,
    .deserialize = subghz_protocol_encoder_mitsubishi_v0_deserialize,
    .stop = subghz_protocol_encoder_mitsubishi_v0_stop,
    .yield = subghz_protocol_encoder_mitsubishi_v0_yield,
};

const SubGhzProtocol subghz_protocol_mitsubishi_v0 = {
    .name = MITSUBISHI_PROTOCOL_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_868 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_mitsubishi_v0_decoder,
    .encoder = &subghz_protocol_mitsubishi_v0_encoder,
};

// ============================================================================
// ENCODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_encoder_mitsubishi_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMitsubishiV0* instance = calloc(1, sizeof(SubGhzProtocolEncoderMitsubishiV0));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_mitsubishi_v0;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 5;
    instance->encoder.size_upload = 0;
    // Preamble + Sync + (12 bytes * 8 bits * 2 elements) + Gap
    instance->upload_capacity = (MITSUBISHI_V0_PREAMBLE_COUNT * 2) + 20 + (MITSUBISHI_V0_BIT_COUNT * 2) + 2;
    instance->encoder.upload = calloc(instance->upload_capacity, sizeof(LevelDuration));

    return instance;
}

void subghz_protocol_encoder_mitsubishi_v0_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderMitsubishiV0* instance = context;
    if(instance->encoder.upload) free(instance->encoder.upload);
    free(instance);
}

static void subghz_protocol_encoder_mitsubishi_v0_get_upload(SubGhzProtocolEncoderMitsubishiV0* instance) {
    size_t index = 0;
    uint8_t payload[12] = {0};

    // Pack data
    payload[0] = (instance->generic.serial >> 24) & 0xFF;
    payload[1] = (instance->generic.serial >> 16) & 0xFF;
    payload[2] = (instance->generic.serial >> 8) & 0xFF;
    payload[3] = instance->generic.serial & 0xFF;
    payload[4] = (instance->generic.cnt >> 8) & 0xFF;
    payload[5] = instance->generic.cnt & 0xFF;
    payload[6] = instance->generic.btn;
    payload[9] = 0x5A; // ID byte (firmware: byte_RAM_59 = 0x5A in sub_ROM_151E8 @ 0x15258)
    payload[10] = 0xFF;
    payload[11] = 0xFF;

    mitsubishi_v0_scramble(payload, (uint16_t)instance->generic.cnt);

    // Preamble
    for(int i = 0; i < MITSUBISHI_V0_PREAMBLE_COUNT; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0_BIT_TE);
        instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0_BIT_TE);
    }

    // Sync pulses (firmware: 96-iteration loop in sub_ROM_1526C @ 0x152A0)
    for(int i = 0; i < 95; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0_BIT_TE);
        instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0_BIT_TE);
    }

    // Data bits
    for(int i = 0; i < 12; i++) {
        for(int bit = 7; bit >= 0; bit--) {
            bool curr = (payload[i] >> bit) & 1;
            if(curr) {
                instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0_BIT_TE);
                instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0_BIT_TE_GAP);
            } else {
                instance->encoder.upload[index++] = level_duration_make(true, MITSUBISHI_V0_BIT_TE_GAP);
                instance->encoder.upload[index++] = level_duration_make(false, MITSUBISHI_V0_BIT_TE);
            }
        }
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_mitsubishi_v0_deserialize(void* context, FlipperFormat* flipper_format) {
    SubGhzProtocolEncoderMitsubishiV0* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;
        if(!flipper_format_rewind(flipper_format)) break;
        flipper_format_read_uint32(flipper_format, "Serial", &instance->generic.serial, 1);
        flipper_format_read_uint32(flipper_format, "Cnt", &instance->generic.cnt, 1);
        uint32_t btn_temp = 0;
        flipper_format_read_uint32(flipper_format, "Btn", &btn_temp, 1);
        instance->generic.btn = (uint8_t)btn_temp;

        // Full D-pad: generic.btn is a genuine (full byte) button that is loaded
        // above, so it is safe to read here. Mitsubishi V0 has no documented set of
        // alternate button codes, so we only enable the D-pad and re-send the
        // originally captured button for every direction.
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original(instance->generic.btn);
        }
        subghz_custom_btn_set_max(4);

        subghz_protocol_encoder_mitsubishi_v0_get_upload(instance);
        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);
    return ret;
}

void subghz_protocol_encoder_mitsubishi_v0_stop(void* context) {
    ((SubGhzProtocolEncoderMitsubishiV0*)context)->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_mitsubishi_v0_yield(void* context) {
    SubGhzProtocolEncoderMitsubishiV0* instance = context;
    if(!instance->encoder.is_running || instance->encoder.repeat == 0) return level_duration_reset();
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

void* subghz_protocol_decoder_mitsubishi_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMitsubishiV0* instance = calloc(1, sizeof(SubGhzProtocolDecoderMitsubishiV0));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mitsubishi_v0;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_mitsubishi_v0_free(void* context) {
    free(context);
}

// [PROTOPIRATE_PORT] Reset now clears FSM state as well as payload buffer.
void subghz_protocol_decoder_mitsubishi_v0_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishiV0* instance = context;
    instance->decoder_state = MitsubishiV0DecoderStepReset;
    instance->decoder.te_last = 0;
    instance->generic.data_count_bit = 0;
    mitsubishi_v0_reset_payload(instance);
}

// [PROTOPIRATE_PORT] FSM-based feed ported from ProtoPirate's mitsubishi_v0.
// Three explicit states (Reset / DataSave / DataCheck) ensure atomic validation of
// each (high, low) pulse pair. On any invalid transition the payload is discarded
// and the FSM returns to Reset — more robust against glitches than the previous
// stateless implementation which silently updated te_last on every edge.
void subghz_protocol_decoder_mitsubishi_v0_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishiV0* instance = context;

    switch(instance->decoder_state) {
    case MitsubishiV0DecoderStepReset:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder_state = MitsubishiV0DecoderStepDataCheck;
        }
        break;

    case MitsubishiV0DecoderStepDataSave:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder_state = MitsubishiV0DecoderStepDataCheck;
        } else {
            instance->decoder_state = MitsubishiV0DecoderStepReset;
            mitsubishi_v0_reset_payload(instance);
        }
        break;

    case MitsubishiV0DecoderStepDataCheck:
        if(!level) {
            if(mitsubishi_v0_collect_pair(instance, instance->decoder.te_last, duration)) {
                if(instance->bit_count >= MITSUBISHI_V0_BIT_COUNT) {
                    mitsubishi_v0_publish_frame(instance);
                    mitsubishi_v0_reset_payload(instance);
                    instance->decoder_state = MitsubishiV0DecoderStepReset;
                } else {
                    instance->decoder_state = MitsubishiV0DecoderStepDataSave;
                }
            } else {
                mitsubishi_v0_reset_payload(instance);
                instance->decoder_state = MitsubishiV0DecoderStepReset;
            }
        } else {
            instance->decoder.te_last = duration;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_mitsubishi_v0_get_hash_data(void* context) {
    SubGhzProtocolDecoderMitsubishiV0* instance = context;
    uint8_t hash = 0;
    for(size_t i = 0; i < 12; i++) {
        hash ^= instance->decode_data[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_v0_serialize(
    void* context,
    FlipperFormat* ff,
    SubGhzRadioPreset* preset) {
    SubGhzProtocolDecoderMitsubishiV0* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_serialize(&instance->generic, ff, preset);
    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_write_uint32(ff, "Serial", &instance->generic.serial, 1);
        flipper_format_write_uint32(ff, "Cnt", &instance->generic.cnt, 1);
        uint32_t btn = instance->generic.btn;
        flipper_format_write_uint32(ff, "Btn", &btn, 1);
    }
    return ret;
}

SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_v0_deserialize(void* context, FlipperFormat* ff) {
    SubGhzProtocolDecoderMitsubishiV0* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(&instance->generic, ff, subghz_protocol_mitsubishi_v0_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_mitsubishi_v0_get_string(void* context, FuriString* output) {
    SubGhzProtocolDecoderMitsubishiV0* instance = context;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%llX\r\n"
        "SN:0x%lX Btn:%X\r\n"
        "Cnt:%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint64_t)instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt);
}

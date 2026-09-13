#include "mazda_siemens.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

// [PROTOPIRATE_PORT] custom_btn support
#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolMazdaSiemens"

static const SubGhzBlockConst subghz_protocol_mazda_siemens_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 64,
};

#define MAZDA_PREAMBLE_MIN 13
#define MAZDA_COMPLETION_MIN 80
#define MAZDA_COMPLETION_MAX 105
#define MAZDA_DATA_BUFFER_SIZE 14
#define MAZDA_TX_REPEATS 4
#define MAZDA_PREAMBLE_BYTES 12
#define MAZDA_TX_GAP_US 50000

struct SubGhzProtocolDecoderMazdaSiemens {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t preamble_count;
    uint16_t bit_counter;
    uint8_t prev_state;
    uint8_t data_buffer[MAZDA_DATA_BUFFER_SIZE];
};

struct SubGhzProtocolEncoderMazdaSiemens {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    MazdaSiemensDecoderStepReset = 0,
    MazdaSiemensDecoderStepPreambleSave,
    MazdaSiemensDecoderStepPreambleCheck,
    MazdaSiemensDecoderStepDataSave,
    MazdaSiemensDecoderStepDataCheck,
} MazdaSiemensDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_mazda_siemens_decoder = {
    .alloc = subghz_protocol_decoder_mazda_siemens_alloc,
    .free = subghz_protocol_decoder_mazda_siemens_free,

    .feed = subghz_protocol_decoder_mazda_siemens_feed,
    .reset = subghz_protocol_decoder_mazda_siemens_reset,

    .get_hash_data = subghz_protocol_decoder_mazda_siemens_get_hash_data,
    .serialize = subghz_protocol_decoder_mazda_siemens_serialize,
    .deserialize = subghz_protocol_decoder_mazda_siemens_deserialize,
    .get_string = subghz_protocol_decoder_mazda_siemens_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_mazda_siemens_encoder = {
    .alloc = subghz_protocol_encoder_mazda_siemens_alloc,
    .free = subghz_protocol_encoder_mazda_siemens_free,

    .deserialize = subghz_protocol_encoder_mazda_siemens_deserialize,
    .stop = subghz_protocol_encoder_mazda_siemens_stop,
    .yield = subghz_protocol_encoder_mazda_siemens_yield,
};

const SubGhzProtocol subghz_protocol_mazda_siemens = {
    .name = SUBGHZ_PROTOCOL_MAZDA_SIEMENS_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_mazda_siemens_decoder,
    .encoder = &subghz_protocol_mazda_siemens_encoder,
};

// ============================================================================
// Helpers
// ============================================================================

static uint8_t mazda_byte_parity(uint8_t val);
static void mazda_xor_deobfuscate(uint8_t* data);

static inline bool mazda_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_mazda_siemens_const.te_short) <
        subghz_protocol_mazda_siemens_const.te_delta;
}

static inline bool mazda_is_long(uint32_t duration) {
    return DURATION_DIFF(duration, subghz_protocol_mazda_siemens_const.te_long) <
        subghz_protocol_mazda_siemens_const.te_delta;
}

static void mazda_collect_bit(SubGhzProtocolDecoderMazdaSiemens* instance, uint8_t state_bit) {
    uint8_t byte_idx = instance->bit_counter >> 3;
    if(byte_idx < MAZDA_DATA_BUFFER_SIZE) {
        instance->data_buffer[byte_idx] <<= 1;
        if(state_bit == 0) {
            instance->data_buffer[byte_idx] |= 1;
        }
    }
    instance->bit_counter++;
}

static bool mazda_check_completion(SubGhzProtocolDecoderMazdaSiemens* instance) {
    if(instance->bit_counter < MAZDA_COMPLETION_MIN ||
    instance->bit_counter > MAZDA_COMPLETION_MAX) {
        return false;
    }

    // Shift buffer by 1 byte (discard sync/header byte)
    uint8_t data[8];
    for(int i = 0; i < 8; i++) {
        data[i] = instance->data_buffer[i + 1];
    }

    mazda_xor_deobfuscate(data);

    uint8_t checksum = 0;
    for(int i = 0; i < 7; i++) {
        checksum += data[i];
    }
    if(checksum != data[7]) {
        return false;
    }

    uint64_t packed = 0;
    for(int i = 0; i < 8; i++) {
        packed = (packed << 8) | data[i];
    }

    instance->generic.data = packed;
    instance->generic.data_count_bit = 64;
    return true;
}

static void mazda_parse_data(SubGhzBlockGeneric* instance) {
    instance->serial = (uint32_t)(instance->data >> 32);
    instance->btn = (instance->data >> 24) & 0xFF;
    instance->cnt = (instance->data >> 8) & 0xFFFF;
}

static uint8_t mazda_byte_parity(uint8_t val) {
    val ^= val >> 4;
    val ^= val >> 2;
    val ^= val >> 1;
    return val & 1;
}

static void mazda_xor_deobfuscate(uint8_t* data) {
    uint8_t parity = mazda_byte_parity(data[7]);

    if(parity) {
        // Odd parity: mask = byte[6], XOR bytes 0-5
        uint8_t mask = data[6];
        for(int i = 0; i < 6; i++) {
            data[i] ^= mask;
        }
    } else {
        // Even parity: mask = byte[5], XOR bytes 0-4 and byte[6]
        uint8_t mask = data[5];
        for(int i = 0; i < 5; i++) {
            data[i] ^= mask;
        }
        data[6] ^= mask;
    }

    // Bit deinterleave bytes 5-6
    uint8_t old5 = data[5];
    uint8_t old6 = data[6];
    data[5] = (old5 & 0xAA) | (old6 & 0x55);
    data[6] = (old5 & 0x55) | (old6 & 0xAA);
}

/**
 * Bit interleave + XOR obfuscation (TX path)
 */
static void mazda_xor_obfuscate(uint8_t* data) {
    uint8_t old5 = data[5];
    uint8_t old6 = data[6];
    data[5] = (old5 & 0xAA) | (old6 & 0x55);
    data[6] = (old5 & 0x55) | (old6 & 0xAA);

    uint8_t parity = mazda_byte_parity(data[7]);

    if(parity) {
        uint8_t mask = data[6];
        for(int i = 0; i < 6; i++) {
            data[i] ^= mask;
        }
    } else {
        uint8_t mask = data[5];
        for(int i = 0; i < 5; i++) {
            data[i] ^= mask;
        }
        data[6] ^= mask;
    }
}

// ============================================================================
// Encoder
// ============================================================================

#define MAZDA_UPLOAD_MAX 400

void* subghz_protocol_encoder_mazda_siemens_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMazdaSiemens* instance =
        malloc(sizeof(SubGhzProtocolEncoderMazdaSiemens));

    instance->base.protocol = &subghz_protocol_mazda_siemens;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = MAZDA_TX_REPEATS;
    instance->encoder.size_upload = MAZDA_UPLOAD_MAX;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_mazda_siemens_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderMazdaSiemens* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static size_t mazda_encode_byte(LevelDuration* upload, size_t index, uint8_t byte) {
    uint32_t te = subghz_protocol_mazda_siemens_const.te_short;
    for(int bit = 7; bit >= 0; bit--) {
        if((byte >> bit) & 1) {
            upload[index++] = level_duration_make(true, te);
            upload[index++] = level_duration_make(false, te);
        } else {
            upload[index++] = level_duration_make(false, te);
            upload[index++] = level_duration_make(true, te);
        }
    }
    return index;
}

static bool
    subghz_protocol_encoder_mazda_siemens_get_upload(SubGhzProtocolEncoderMazdaSiemens* instance) {
    furi_assert(instance);

    uint8_t data[8];
    for(int i = 0; i < 8; i++) {
        data[i] = (instance->generic.data >> (56 - 8 * i)) & 0xFF;
    }

    uint8_t cnt_lo = data[6];
    cnt_lo++;
    data[6] = cnt_lo;
    if(cnt_lo == 0) {
        data[5]++;
    }

    uint8_t checksum = 0;
    for(int i = 0; i < 7; i++) {
        checksum += data[i];
    }
    data[7] = checksum;

    // Store cleartext in generic.data (for display/save)
    uint64_t packed = 0;
    for(int i = 0; i < 8; i++) {
        packed = (packed << 8) | data[i];
    }
    instance->generic.data = packed;

    // XOR obfuscate for TX (Pandora sub_142A0)
    uint8_t tx_data[8];
    memcpy(tx_data, data, 8);
    mazda_xor_obfuscate(tx_data);

    size_t index = 0;

    for(int i = 0; i < MAZDA_PREAMBLE_BYTES; i++) {
        index = mazda_encode_byte(instance->encoder.upload, index, 0xFF);
    }

    instance->encoder.upload[index++] = level_duration_make(false, MAZDA_TX_GAP_US);

    index = mazda_encode_byte(instance->encoder.upload, index, 0xFF);
    index = mazda_encode_byte(instance->encoder.upload, index, 0xFF);

    index = mazda_encode_byte(instance->encoder.upload, index, 0xD7);

    for(int i = 0; i < 8; i++) {
        index = mazda_encode_byte(instance->encoder.upload, index, 255 - tx_data[i]);
    }

    index = mazda_encode_byte(instance->encoder.upload, index, 0x5A);

    instance->encoder.upload[index++] = level_duration_make(false, MAZDA_TX_GAP_US);

    if(index > MAZDA_UPLOAD_MAX) {
        FURI_LOG_E(TAG, "Upload size %d exceeds buffer %d", (int)index, MAZDA_UPLOAD_MAX);
        return false;
    }

    instance->encoder.size_upload = index;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_mazda_siemens_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderMazdaSiemens* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        res = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_mazda_siemens_const.min_count_bit_for_found);
        if(res != SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }
        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        // [PROTOPIRATE_PORT] custom_btn support
        // Mazda Siemens button codes (see mazda_get_btn_name):
        //   0x10 = Lock, 0x20 = Unlock, 0x40 = Trunk.
        // Btn occupies bits 24..31 of generic.data.
        mazda_parse_data(&instance->generic);
        {
            const uint8_t original_btn = (uint8_t)instance->generic.btn;
            if(subghz_custom_btn_get_original() == 0) {
                subghz_custom_btn_set_original(original_btn);
            }
            subghz_custom_btn_set_max(4);
            uint8_t custom_btn_id = subghz_custom_btn_get();
            uint8_t new_btn = original_btn;
            switch(custom_btn_id) {
            case SUBGHZ_CUSTOM_BTN_UP:    new_btn = 0x10U; break; // Lock
            case SUBGHZ_CUSTOM_BTN_OK:    new_btn = original_btn; break;
            case SUBGHZ_CUSTOM_BTN_DOWN:  new_btn = 0x20U; break; // Unlock
            case SUBGHZ_CUSTOM_BTN_LEFT:  new_btn = 0x40U; break; // Trunk
            // Only 3 real buttons (Lock/Unlock/Trunk); RIGHT falls back to captured.
            case SUBGHZ_CUSTOM_BTN_RIGHT: new_btn = original_btn; break;
            default:                      new_btn = original_btn; break;
            }
            if(new_btn != original_btn) {
                // Re-encode packet with new button; get_upload recomputes checksum.
                instance->generic.btn = new_btn;
                instance->generic.data =
                    (instance->generic.data & ~((uint64_t)0xFFU << 24U)) |
                    ((uint64_t)new_btn << 24U);
            }
        }

        if(!subghz_protocol_encoder_mazda_siemens_get_upload(instance)) {
            res = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return res;
}

void subghz_protocol_encoder_mazda_siemens_stop(void* context) {
    SubGhzProtocolEncoderMazdaSiemens* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_mazda_siemens_yield(void* context) {
    SubGhzProtocolEncoderMazdaSiemens* instance = context;

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

// ============================================================================
// Decoder
// ============================================================================

void* subghz_protocol_decoder_mazda_siemens_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMazdaSiemens* instance =
        malloc(sizeof(SubGhzProtocolDecoderMazdaSiemens));
    instance->base.protocol = &subghz_protocol_mazda_siemens;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_mazda_siemens_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    free(instance);
}

void subghz_protocol_decoder_mazda_siemens_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    instance->decoder.parser_step = MazdaSiemensDecoderStepReset;
    instance->preamble_count = 0;
    instance->bit_counter = 0;
    instance->prev_state = 0;
    memset(instance->data_buffer, 0, MAZDA_DATA_BUFFER_SIZE);
}

static bool mazda_process_pair(
    SubGhzProtocolDecoderMazdaSiemens* instance,
    uint32_t dur_first,
    uint32_t dur_second) {
    bool first_short = mazda_is_short(dur_first);
    bool first_long = mazda_is_long(dur_first);
    bool second_short = mazda_is_short(dur_second);
    bool second_long = mazda_is_long(dur_second);

    if(first_long && second_short) {
        mazda_collect_bit(instance, 0); 
        mazda_collect_bit(instance, 1);
        instance->prev_state = 1;
        return true;
    }

    if(first_short && second_long) {
        mazda_collect_bit(instance, 1); 
        instance->prev_state = 0;
        return true;
    }

    if(first_short && second_short) {
        mazda_collect_bit(instance, instance->prev_state);
        return true;
    }

    if(first_long && second_long) {
        mazda_collect_bit(instance, 0); 
        mazda_collect_bit(instance, 1); 
        instance->prev_state = 0;
        return true;
    }

    return false;
}

void subghz_protocol_decoder_mazda_siemens_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    UNUSED(level);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;

    switch(instance->decoder.parser_step) {
    case MazdaSiemensDecoderStepReset:
        if(mazda_is_short(duration)) {
            instance->decoder.te_last = duration;
            instance->preamble_count = 0;
            instance->decoder.parser_step = MazdaSiemensDecoderStepPreambleCheck;
        }
        break;

    case MazdaSiemensDecoderStepPreambleSave:
        instance->decoder.te_last = duration;
        instance->decoder.parser_step = MazdaSiemensDecoderStepPreambleCheck;
        break;

    case MazdaSiemensDecoderStepPreambleCheck:
        if(mazda_is_short(instance->decoder.te_last) && mazda_is_short(duration)) {
            instance->preamble_count++;
            instance->decoder.parser_step = MazdaSiemensDecoderStepPreambleSave;
        } else if(
            mazda_is_short(instance->decoder.te_last) && mazda_is_long(duration) &&
            instance->preamble_count >= MAZDA_PREAMBLE_MIN) {

            instance->bit_counter = 1;
            memset(instance->data_buffer, 0, MAZDA_DATA_BUFFER_SIZE);

            mazda_collect_bit(instance, 1);
            instance->prev_state = 0;

            instance->decoder.parser_step = MazdaSiemensDecoderStepDataSave;
        } else {
            instance->decoder.parser_step = MazdaSiemensDecoderStepReset;
        }
        break;

    case MazdaSiemensDecoderStepDataSave:
        instance->decoder.te_last = duration;
        instance->decoder.parser_step = MazdaSiemensDecoderStepDataCheck;
        break;

    case MazdaSiemensDecoderStepDataCheck:
        if(mazda_process_pair(instance, instance->decoder.te_last, duration)) {
            instance->decoder.parser_step = MazdaSiemensDecoderStepDataSave;
        } else {
            if(mazda_check_completion(instance)) {
                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
            }
            instance->decoder.parser_step = MazdaSiemensDecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_mazda_siemens_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    return (uint8_t)(instance->generic.data ^ (instance->generic.data >> 8) ^
                    (instance->generic.data >> 16) ^ (instance->generic.data >> 24) ^
                    (instance->generic.data >> 32) ^ (instance->generic.data >> 40) ^
                    (instance->generic.data >> 48) ^ (instance->generic.data >> 56));
}

SubGhzProtocolStatus subghz_protocol_decoder_mazda_siemens_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mazda_siemens_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mazda_siemens_const.min_count_bit_for_found);
}

static const char* mazda_get_btn_name(uint8_t btn) {
    switch(btn) {
    case 0x10:
        return "Lock";
    case 0x20:
        return "Unlock";
    case 0x40:
        return "Trunk";
    default:
        return "Unknown";
    }
}

void subghz_protocol_decoder_mazda_siemens_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    mazda_parse_data(&instance->generic);

    subghz_block_generic_global.btn_is_available = false;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 8;

    const uint8_t chk = instance->generic.data & 0xFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%llX\r\n"
        "SN:0x%lX Btn:[%s]\r\n"
        "CRC:%02X Cnt:%04lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint64_t)instance->generic.data,
        (uint32_t)instance->generic.serial,
        mazda_get_btn_name(instance->generic.btn),
        chk,
        (uint32_t)instance->generic.cnt);
}

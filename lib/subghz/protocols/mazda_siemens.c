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

    // [PROTOPIRATE_PORT] The rolling counter now comes from generic.cnt (which the
    // car-emulate scene supplies via "Cnt", already incremented). Rebuild the
    // serial/btn/cnt cleartext bytes from the generic fields so the recomputed
    // checksum matches the transmitted counter. Previously get_upload did a local
    // data[6]++ but never persisted Key back to the file, so every TX re-read the
    // same Key and replayed the same counter.
    data[0] = (uint8_t)(instance->generic.serial >> 24);
    data[1] = (uint8_t)(instance->generic.serial >> 16);
    data[2] = (uint8_t)(instance->generic.serial >> 8);
    data[3] = (uint8_t)(instance->generic.serial);
    data[4] = (uint8_t)(instance->generic.btn & 0xFF);
    data[5] = (uint8_t)((instance->generic.cnt >> 8) & 0xFF);
    data[6] = (uint8_t)(instance->generic.cnt & 0xFF);

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

        // [PROTOPIRATE_PORT] Populate serial/btn/cnt from the packed Key first.
        mazda_parse_data(&instance->generic);

        // [PROTOPIRATE_PORT] Read Serial/Btn/Cnt overrides from the flipper_format.
        // The car-emulate scene increments the rolling counter and writes it into
        // "Cnt"; subghz_block_generic_deserialize_check_count_bit only reads Key,
        // so we must read "Cnt" here or we would replay the same frame.
        uint32_t ser_u32 = 0;
        uint32_t btn_u32 = 0;
        uint32_t cnt_u32 = 0;
        flipper_format_rewind(flipper_format);
        bool got_serial = flipper_format_read_uint32(flipper_format, "Serial", &ser_u32, 1);
        flipper_format_rewind(flipper_format);
        bool got_btn = flipper_format_read_uint32(flipper_format, "Btn", &btn_u32, 1);
        flipper_format_rewind(flipper_format);
        bool got_cnt = flipper_format_read_uint32(flipper_format, "Cnt", &cnt_u32, 1);

        if(got_serial) instance->generic.serial = ser_u32;
        // [ROLLING_CNT] If the scene supplied a "Cnt" (car-emulate) use it as-is;
        // otherwise (plain transmitter OK/D-pad press) forward-encode the NEXT
        // counter with the rolling multiplier so the UI shows an incrementing
        // counter, matching VAG/PSA. get_upload() re-packs generic.cnt into the Key
        // and we persist Key+Cnt below, so the decoder shows the advanced value.
        if(got_cnt) {
            instance->generic.cnt = cnt_u32 & 0xFFFF;
        } else {
            uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
            if(mult == 0U) mult = 1U;
            instance->generic.cnt = (instance->generic.cnt + mult) & 0xFFFF;
        }

        // [PROTOPIRATE_PORT] custom_btn support
        // Mazda Siemens button codes (see mazda_get_btn_name):
        //   0x10 = Lock, 0x20 = Unlock, 0x40 = Trunk.
        // Btn occupies bits 24..31 of generic.data.
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
            // Explicit "Btn" override in the file wins over the custom_btn mapping.
            if(got_btn) new_btn = (uint8_t)(btn_u32 & 0xFF);
            instance->generic.btn = new_btn;
        }

        if(!subghz_protocol_encoder_mazda_siemens_get_upload(instance)) {
            res = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        // [PROTOPIRATE_PORT] Persist the re-encoded cleartext Key plus Serial/Btn/Cnt
        // so the next TX (and any save/reload) starts from the advanced counter.
        flipper_format_rewind(flipper_format);
        uint8_t key_data[8];
        for(int i = 0; i < 8; i++) {
            key_data[i] = (uint8_t)((instance->generic.data >> (56 - 8 * i)) & 0xFF);
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, 8)) {
            res = SubGhzProtocolStatusErrorParserKey;
            break;
        }

        uint32_t temp_serial = instance->generic.serial;
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Serial", &temp_serial, 1);

        uint32_t temp_btn = instance->generic.btn;
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Btn", &temp_btn, 1);

        uint32_t temp_cnt = instance->generic.cnt;
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &temp_cnt, 1);

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

    mazda_parse_data(&instance->generic);

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    // [PROTOPIRATE_PORT] Persist Serial/Btn/Cnt so the base counter survives
    // save/reload and the car-emulate scene can read/override them.
    uint32_t v_serial = instance->generic.serial;
    uint32_t v_btn = instance->generic.btn;
    uint32_t v_cnt = instance->generic.cnt;
    if(!flipper_format_write_uint32(flipper_format, "Serial", &v_serial, 1) ||
       !flipper_format_write_uint32(flipper_format, "Btn", &v_btn, 1) ||
       !flipper_format_write_uint32(flipper_format, "Cnt", &v_cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mazda_siemens_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMazdaSiemens* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mazda_siemens_const.min_count_bit_for_found);
    if(ret == SubGhzProtocolStatusOk) {
        mazda_parse_data(&instance->generic);

        // [PROTOPIRATE_PORT] custom_btn support (Lock/Unlock/Trunk → 4 buttons max).
        if(subghz_custom_btn_get_original() == 0) {
            subghz_custom_btn_set_original((uint8_t)instance->generic.btn);
        }
        subghz_custom_btn_set_max(4);
    }
    return ret;
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

// [PROTOPIRATE_PORT] custom_btn UI support
// Re-derive the displayed button from the D-pad selection, mirroring the encoder
// remap (see encoder deserialize): Up=Lock(0x10), Down=Unlock(0x20),
// Left=Trunk(0x40), Right/OK=captured.
static uint8_t mazda_ui_button(uint8_t custom, uint8_t original_btn) {
    switch(custom) {
    case SUBGHZ_CUSTOM_BTN_UP:
        return 0x10U; // Lock
    case SUBGHZ_CUSTOM_BTN_DOWN:
        return 0x20U; // Unlock
    case SUBGHZ_CUSTOM_BTN_LEFT:
        return 0x40U; // Trunk
    case SUBGHZ_CUSTOM_BTN_RIGHT:
    case SUBGHZ_CUSTOM_BTN_OK:
    default:
        return original_btn;
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

    // [BUGFIX UI] Re-derive the displayed button from the current D-pad
    // selection so the transmitter UI reflects subghz_custom_btn_get() (like
    // psa.c/star_line.c). CRC shown is the captured frame's checksum (data&0xFF);
    // the encoder recomputes the real checksum for the transmitted button.
    subghz_custom_btn_set_max(4);
    uint8_t display_btn =
        mazda_ui_button(subghz_custom_btn_get(), (uint8_t)instance->generic.btn);

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
        mazda_get_btn_name(display_btn),
        chk,
        (uint32_t)instance->generic.cnt);
}

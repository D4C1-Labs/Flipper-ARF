#include "kia_v3_v4.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "keeloq_common.h"

#define TAG "SubGhzProtocolKiaV3V4"

#define KIA_MF_KEY 0xA8F5DFFC8DAA5CDBULL

static const char* kia_version_names[] = {"Kia V4", "Kia V3"};

#define KIA_V3_V4_PREAMBLE_PAIRS  12U
#define KIA_V3_V4_BIT_COUNT       64U
#define KIA_V3_V4_CRC_BIT_COUNT   4U
#define KIA_V3_V4_CRC_SWEEP_COUNT 16U
#define KIA_V3_V4_SYNC_DURATION   1200U
#define KIA_V3_V4_END_MARKER_US   800U
#define KIA_V3_V4_DEFAULT_REPEAT  KIA_V3_V4_CRC_SWEEP_COUNT

#define KIA_V3_V4_DATA_OFFSET   ((KIA_V3_V4_PREAMBLE_PAIRS * 2U) + 2U)
#define KIA_V3_V4_CRC_OFFSET    (KIA_V3_V4_DATA_OFFSET + (KIA_V3_V4_BIT_COUNT * 2U))
#define KIA_V3_V4_END_OFFSET    (KIA_V3_V4_CRC_OFFSET + (KIA_V3_V4_CRC_BIT_COUNT * 2U))
#define KIA_V3_V4_BURST_ENTRIES (KIA_V3_V4_END_OFFSET + 2U)

#define KIA_V3_V4_UPLOAD_CAPACITY KIA_V3_V4_BURST_ENTRIES

static const SubGhzBlockConst subghz_protocol_kia_v3_v4_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 150,
    .min_count_bit_for_found = 68,
};

struct SubGhzProtocolDecoderKiaV3V4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    uint8_t raw_bits[32];
    uint16_t raw_bit_count;
    bool is_v3_sync;

    uint32_t encrypted;
    uint32_t decrypted;
    uint8_t crc;
    uint8_t version;
};

struct SubGhzProtocolEncoderKiaV3V4 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
    uint8_t version;

    uint32_t encrypted;
    uint32_t decrypted;

    uint8_t crc_iter;
    uint8_t bursts_sent;
};

typedef enum {
    KiaV3V4DecoderStepReset = 0,
    KiaV3V4DecoderStepCheckPreamble,
    KiaV3V4DecoderStepCollectRawBits,
} KiaV3V4DecoderStep;

static uint8_t kia_v3_v4_reverse8(uint8_t byte) {
    byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
    byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
    byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
    return byte;
}

static void kia_v3_v4_add_raw_bit(SubGhzProtocolDecoderKiaV3V4* instance, bool bit) {
    if(instance->raw_bit_count < 256) {
        uint16_t byte_idx = instance->raw_bit_count / 8;
        uint8_t bit_idx = 7 - (instance->raw_bit_count % 8);
        if(bit) {
            instance->raw_bits[byte_idx] |= (1 << bit_idx);
        } else {
            instance->raw_bits[byte_idx] &= ~(1 << bit_idx);
        }
        instance->raw_bit_count++;
    }
}

static inline void kia_v3_v4_emit_bit_pwm(LevelDuration* upload, size_t* idx, bool bit, bool v4) {
    const uint32_t te_short = subghz_protocol_kia_v3_v4_const.te_short;
    const uint32_t te_long = subghz_protocol_kia_v3_v4_const.te_long;
    const uint32_t first_us = bit ? te_short : te_long;
    const uint32_t second_us = bit ? te_long : te_short;

    if(v4) {
        upload[(*idx)++] = level_duration_make(false, (int32_t)first_us);
        upload[(*idx)++] = level_duration_make(true, (int32_t)second_us);
    } else {
        upload[(*idx)++] = level_duration_make(true, (int32_t)first_us);
        upload[(*idx)++] = level_duration_make(false, (int32_t)second_us);
    }
}

static uint64_t kia_v3_v4_build_tx_bitstream(SubGhzProtocolEncoderKiaV3V4* instance) {
    const uint32_t serial_btn = (instance->serial & 0x0FFFFFFFU) |
                                ((uint32_t)(instance->btn & 0x0FU) << 28);
    const uint64_t key = ((uint64_t)serial_btn << 32) | (uint64_t)instance->encrypted;
    return subghz_protocol_blocks_reverse_key(key, 64);
}

static bool kia_v3_v4_process_buffer(SubGhzProtocolDecoderKiaV3V4* instance) {
    if(instance->raw_bit_count < 68) {
        return false;
    }

    uint8_t* b = instance->raw_bits;

    if(instance->is_v3_sync) {
        uint16_t num_bytes = (instance->raw_bit_count + 7) / 8;
        for(uint16_t i = 0; i < num_bytes; i++) {
            b[i] = ~b[i];
        }
    }

    uint8_t crc = (b[8] >> 4) & 0x0F;

    uint32_t encrypted =
        ((uint32_t)kia_v3_v4_reverse8(b[3]) << 24) | ((uint32_t)kia_v3_v4_reverse8(b[2]) << 16) |
        ((uint32_t)kia_v3_v4_reverse8(b[1]) << 8) | (uint32_t)kia_v3_v4_reverse8(b[0]);

    uint32_t serial = ((uint32_t)kia_v3_v4_reverse8(b[7] & 0xF0) << 24) |
                      ((uint32_t)kia_v3_v4_reverse8(b[6]) << 16) |
                      ((uint32_t)kia_v3_v4_reverse8(b[5]) << 8) | (uint32_t)kia_v3_v4_reverse8(b[4]);

    uint8_t btn = (kia_v3_v4_reverse8(b[7]) & 0xF0) >> 4;
    uint8_t our_serial_lsb = serial & 0xFF;

    uint32_t decrypted = subghz_protocol_keeloq_common_decrypt(encrypted, KIA_MF_KEY);
    uint8_t dec_btn = (decrypted >> 28) & 0x0F;
    uint8_t dec_serial_lsb = (decrypted >> 16) & 0xFF;

    if(dec_btn != btn || dec_serial_lsb != our_serial_lsb) {
        return false;
    }

    instance->encrypted = encrypted;
    instance->decrypted = decrypted;
    instance->crc = crc;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = decrypted & 0xFFFF;
    instance->version = instance->is_v3_sync ? 1 : 0;

    uint64_t key_data = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) |
                        ((uint64_t)b[3] << 32) | ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
                        ((uint64_t)b[6] << 8) | (uint64_t)b[7];
    instance->generic.data = key_data;
    instance->generic.data_count_bit = 68;

    return true;
}

const SubGhzProtocolDecoder subghz_protocol_kia_v3_v4_decoder = {
    .alloc = subghz_protocol_decoder_kia_v3_v4_alloc,
    .free = subghz_protocol_decoder_kia_v3_v4_free,
    .feed = subghz_protocol_decoder_kia_v3_v4_feed,
    .reset = subghz_protocol_decoder_kia_v3_v4_reset,
    .get_hash_data = subghz_protocol_decoder_kia_v3_v4_get_hash_data,
    .serialize = subghz_protocol_decoder_kia_v3_v4_serialize,
    .deserialize = subghz_protocol_decoder_kia_v3_v4_deserialize,
    .get_string = subghz_protocol_decoder_kia_v3_v4_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_kia_v3_v4_encoder = {
    .alloc = subghz_protocol_encoder_kia_v3_v4_alloc,
    .free = subghz_protocol_encoder_kia_v3_v4_free,
    .deserialize = subghz_protocol_encoder_kia_v3_v4_deserialize,
    .stop = subghz_protocol_encoder_kia_v3_v4_stop,
    .yield = subghz_protocol_encoder_kia_v3_v4_yield,
};

const SubGhzProtocol subghz_protocol_kia_v3_v4 = {
    .name = SUBGHZ_PROTOCOL_KIA_V3_V4_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_kia_v3_v4_decoder,
    .encoder = &subghz_protocol_kia_v3_v4_encoder,
};

static const char* subghz_protocol_kia_v3_v4_get_name_button(uint8_t btn) {
    switch(btn) {
    case 0x1:
        return "Lock";
    case 0x2:
        return "Unlock";
    case 0x3:
        return "Trunk";
    case 0x4:
        return "Panic";
    case 0x8:
        return "Horn";
    default:
        return "Unknown";
    }
}

// ============================================================================
// ENCODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_encoder_kia_v3_v4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderKiaV3V4* instance = malloc(sizeof(SubGhzProtocolEncoderKiaV3V4));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_kia_v3_v4;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->serial = 0;
    instance->btn = 0;
    instance->cnt = 0;
    instance->version = 0;
    instance->crc_iter = 0;
    instance->bursts_sent = 0;

    instance->encoder.size_upload = KIA_V3_V4_UPLOAD_CAPACITY;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.repeat = (int32_t)KIA_V3_V4_DEFAULT_REPEAT;
    instance->encoder.front = 0;
    instance->encoder.is_running = false;

    return instance;
}

void subghz_protocol_encoder_kia_v3_v4_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV3V4* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

static void subghz_protocol_encoder_kia_v3_v4_build_packet(
    SubGhzProtocolEncoderKiaV3V4* instance,
    uint8_t* raw_bytes) {
    uint32_t plaintext = (uint32_t)(instance->cnt & 0xFFFFU) |
                         ((uint32_t)(instance->serial & 0x3FFU) << 16) |
                         ((uint32_t)(instance->btn & 0x0FU) << 28);

    instance->decrypted = plaintext;

    uint32_t encrypted = subghz_protocol_keeloq_common_encrypt(plaintext, KIA_MF_KEY);
    instance->encrypted = encrypted;

    raw_bytes[0] = kia_v3_v4_reverse8((encrypted >> 0) & 0xFF);
    raw_bytes[1] = kia_v3_v4_reverse8((encrypted >> 8) & 0xFF);
    raw_bytes[2] = kia_v3_v4_reverse8((encrypted >> 16) & 0xFF);
    raw_bytes[3] = kia_v3_v4_reverse8((encrypted >> 24) & 0xFF);

    uint32_t serial_btn = (instance->serial & 0x0FFFFFFFU) |
                          ((uint32_t)(instance->btn & 0x0F) << 28);
    raw_bytes[4] = kia_v3_v4_reverse8((serial_btn >> 0) & 0xFF);
    raw_bytes[5] = kia_v3_v4_reverse8((serial_btn >> 8) & 0xFF);
    raw_bytes[6] = kia_v3_v4_reverse8((serial_btn >> 16) & 0xFF);
    raw_bytes[7] = kia_v3_v4_reverse8((serial_btn >> 24) & 0xFF);

    instance->generic.data = ((uint64_t)raw_bytes[0] << 56) | ((uint64_t)raw_bytes[1] << 48) |
                             ((uint64_t)raw_bytes[2] << 40) | ((uint64_t)raw_bytes[3] << 32) |
                             ((uint64_t)raw_bytes[4] << 24) | ((uint64_t)raw_bytes[5] << 16) |
                             ((uint64_t)raw_bytes[6] << 8) | (uint64_t)raw_bytes[7];
    instance->generic.data_count_bit = 68;
}

static void subghz_protocol_encoder_kia_v3_v4_patch_crc(SubGhzProtocolEncoderKiaV3V4* instance) {
    if(!instance || !instance->encoder.upload) return;
    const bool v4 = (instance->version == 0);
    const uint8_t crc = instance->crc_iter & 0x0FU;
    size_t idx = KIA_V3_V4_CRC_OFFSET;
    for(int b = 3; b >= 0; b--) {
        const bool bit = (crc >> b) & 1U;
        kia_v3_v4_emit_bit_pwm(instance->encoder.upload, &idx, bit, v4);
    }
}

static void subghz_protocol_encoder_kia_v3_v4_get_upload(SubGhzProtocolEncoderKiaV3V4* instance) {
    furi_assert(instance);

    uint8_t raw_bytes[8];
    subghz_protocol_encoder_kia_v3_v4_build_packet(instance, raw_bytes);

    const bool v4 = (instance->version == 0);
    const uint64_t tx_key = kia_v3_v4_build_tx_bitstream(instance);

    size_t idx = 0;
    LevelDuration* upload = instance->encoder.upload;
    const uint32_t te_short = subghz_protocol_kia_v3_v4_const.te_short;

    for(uint32_t i = 0; i < KIA_V3_V4_PREAMBLE_PAIRS; i++) {
        if(v4) {
            upload[idx++] = level_duration_make(false, (int32_t)te_short);
            upload[idx++] = level_duration_make(true, (int32_t)te_short);
        } else {
            upload[idx++] = level_duration_make(true, (int32_t)te_short);
            upload[idx++] = level_duration_make(false, (int32_t)te_short);
        }
    }

    if(v4) {
        upload[idx++] = level_duration_make(false, (int32_t)te_short);
        upload[idx++] = level_duration_make(true, (int32_t)KIA_V3_V4_SYNC_DURATION);
    } else {
        upload[idx++] = level_duration_make(true, (int32_t)te_short);
        upload[idx++] = level_duration_make(false, (int32_t)KIA_V3_V4_SYNC_DURATION);
    }

    for(int i = 63; i >= 0; i--) {
        const bool bit = (tx_key >> i) & 1ULL;
        kia_v3_v4_emit_bit_pwm(upload, &idx, bit, v4);
    }

    const uint8_t crc = instance->crc_iter & 0x0FU;
    for(int b = 3; b >= 0; b--) {
        const bool bit = (crc >> b) & 1U;
        kia_v3_v4_emit_bit_pwm(upload, &idx, bit, v4);
    }

    if(v4) {
        upload[idx++] = level_duration_make(false, (int32_t)KIA_V3_V4_END_MARKER_US);
        upload[idx++] = level_duration_make(true, (int32_t)KIA_V3_V4_END_MARKER_US);
    } else {
        upload[idx++] = level_duration_make(true, (int32_t)KIA_V3_V4_END_MARKER_US);
        upload[idx++] = level_duration_make(false, (int32_t)KIA_V3_V4_END_MARKER_US);
    }

    furi_check(idx == KIA_V3_V4_BURST_ENTRIES);
    instance->encoder.size_upload = idx;
    instance->encoder.front = 0;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_kia_v3_v4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV3V4* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);

    do {
        FuriString* temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            furi_string_free(temp_str);
            break;
        }

        const char* proto_str = furi_string_get_cstr(temp_str);
        if(!furi_string_equal(temp_str, instance->base.protocol->name) &&
           strcmp(proto_str, "Kia V3") != 0 && strcmp(proto_str, "Kia V4") != 0) {
            FURI_LOG_E(TAG, "Wrong protocol %s", proto_str);
            furi_string_free(temp_str);
            break;
        }

        bool version_from_protocol_name = false;

        if(strcmp(proto_str, "Kia V3") == 0) {
            instance->version = 1;
            version_from_protocol_name = true;
        } else if(strcmp(proto_str, "Kia V4") == 0) {
            instance->version = 0;
            version_from_protocol_name = true;
        }

        furi_string_free(temp_str);

        flipper_format_rewind(flipper_format);
        uint32_t bits = 0;
        if(!flipper_format_read_uint32(flipper_format, "Bit", &bits, 1)) break;
        instance->generic.data_count_bit = 68;

        flipper_format_rewind(flipper_format);
        temp_str = furi_string_alloc();
        if(!flipper_format_read_string(flipper_format, "Key", temp_str)) {
            furi_string_free(temp_str);
            break;
        }

        const char* key_str = furi_string_get_cstr(temp_str);
        uint64_t key = 0;
        size_t str_len = strlen(key_str);
        size_t hex_pos = 0;
        for(size_t i = 0; i < str_len && hex_pos < 16; i++) {
            char c = key_str[i];
            if(c == ' ') continue;
            uint8_t nibble;
            if(c >= '0' && c <= '9') {
                nibble = c - '0';
            } else if(c >= 'A' && c <= 'F') {
                nibble = c - 'A' + 10;
            } else if(c >= 'a' && c <= 'f') {
                nibble = c - 'a' + 10;
            } else {
                break;
            }
            key = (key << 4) | nibble;
            hex_pos++;
        }
        furi_string_free(temp_str);

        if(hex_pos < 14) {
            FURI_LOG_E(TAG, "Invalid key: %zu nibbles", hex_pos);
            break;
        }
        instance->generic.data = key;

        uint32_t serial = UINT32_MAX;
        uint32_t btn = UINT32_MAX;
        uint32_t cnt = UINT32_MAX;

        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "Serial", &serial, 1);
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "Btn", &btn, 1);
        flipper_format_rewind(flipper_format);
        flipper_format_read_uint32(flipper_format, "Cnt", &cnt, 1);

        if(serial == UINT32_MAX || btn == UINT32_MAX || cnt == UINT32_MAX) break;
        instance->serial = serial;
        instance->btn = (uint8_t)btn;
        instance->cnt = (uint16_t)cnt;
        instance->generic.serial = instance->serial;
        instance->generic.btn = instance->btn;
        instance->generic.cnt = instance->cnt;

        flipper_format_rewind(flipper_format);
        uint32_t version_temp;
        if(flipper_format_read_uint32(flipper_format, "KIAVersion", &version_temp, 1)) {
            if(!version_from_protocol_name) {
                instance->version = (uint8_t)version_temp;
            }
        } else if(!version_from_protocol_name) {
            instance->version = 0;
        }

        flipper_format_rewind(flipper_format);
        uint32_t repeat_temp = KIA_V3_V4_DEFAULT_REPEAT;
        flipper_format_read_uint32(flipper_format, "Repeat", &repeat_temp, 1);
        instance->encoder.repeat = (int32_t)repeat_temp;

        instance->crc_iter = 0;
        instance->bursts_sent = 0;

        subghz_protocol_encoder_kia_v3_v4_get_upload(instance);

        instance->encoder.is_running = true;
        instance->encoder.front = 0;

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_kia_v3_v4_stop(void* context) {
    if(!context) return;
    SubGhzProtocolEncoderKiaV3V4* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

LevelDuration subghz_protocol_encoder_kia_v3_v4_yield(void* context) {
    SubGhzProtocolEncoderKiaV3V4* instance = context;

    if(!instance || !instance->encoder.upload || instance->encoder.repeat == 0 ||
       !instance->encoder.is_running) {
        if(instance) {
            instance->encoder.is_running = false;
        }
        return level_duration_reset();
    }

    if(instance->encoder.front >= instance->encoder.size_upload) {
        instance->encoder.is_running = false;
        instance->encoder.front = 0;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->crc_iter = (uint8_t)((instance->crc_iter + 1U) & 0x0FU);
        subghz_protocol_encoder_kia_v3_v4_patch_crc(instance);
        instance->encoder.front = 0;
        instance->encoder.repeat--;
        if(instance->bursts_sent < KIA_V3_V4_CRC_SWEEP_COUNT) {
            instance->bursts_sent++;
        }
    }

    return ret;
}

void subghz_protocol_encoder_kia_v3_v4_set_button(void* context, uint8_t button) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV3V4* instance = context;
    instance->btn = button & 0x0F;
    instance->generic.btn = instance->btn;
    subghz_protocol_encoder_kia_v3_v4_get_upload(instance);
}

void subghz_protocol_encoder_kia_v3_v4_set_counter(void* context, uint16_t counter) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV3V4* instance = context;
    instance->cnt = counter;
    instance->generic.cnt = instance->cnt;
    subghz_protocol_encoder_kia_v3_v4_get_upload(instance);
}

void subghz_protocol_encoder_kia_v3_v4_increment_counter(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV3V4* instance = context;
    instance->cnt++;
    instance->generic.cnt = instance->cnt;
    subghz_protocol_encoder_kia_v3_v4_get_upload(instance);
}

uint16_t subghz_protocol_encoder_kia_v3_v4_get_counter(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV3V4* instance = context;
    return instance->cnt;
}

uint8_t subghz_protocol_encoder_kia_v3_v4_get_button(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV3V4* instance = context;
    return instance->btn;
}

// ============================================================================
// DECODER IMPLEMENTATION
// ============================================================================

void* subghz_protocol_decoder_kia_v3_v4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV3V4* instance = malloc(sizeof(SubGhzProtocolDecoderKiaV3V4));
    furi_assert(instance);

    instance->base.protocol = &subghz_protocol_kia_v3_v4;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_kia_v3_v4_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV3V4* instance = context;
    free(instance);
}

void subghz_protocol_decoder_kia_v3_v4_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV3V4* instance = context;
    instance->decoder.parser_step = KiaV3V4DecoderStepReset;
    instance->header_count = 0;
    instance->raw_bit_count = 0;
    instance->crc = 0;
    memset(instance->raw_bits, 0, sizeof(instance->raw_bits));
}

void subghz_protocol_decoder_kia_v3_v4_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV3V4* instance = context;

    switch(instance->decoder.parser_step) {
    case KiaV3V4DecoderStepReset:
        if(level && (DURATION_DIFF(duration, subghz_protocol_kia_v3_v4_const.te_short) <
                     subghz_protocol_kia_v3_v4_const.te_delta)) {
            instance->decoder.parser_step = KiaV3V4DecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 1;
        }
        break;

    case KiaV3V4DecoderStepCheckPreamble:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_kia_v3_v4_const.te_short) <
               subghz_protocol_kia_v3_v4_const.te_delta) {
                instance->decoder.te_last = duration;
            } else if(duration > 1000 && duration < 1500) {
                if(instance->header_count >= 8) {
                    instance->decoder.parser_step = KiaV3V4DecoderStepCollectRawBits;
                    instance->raw_bit_count = 0;
                    instance->is_v3_sync = false;
                    memset(instance->raw_bits, 0, sizeof(instance->raw_bits));
                } else {
                    instance->decoder.parser_step = KiaV3V4DecoderStepReset;
                }
            } else {
                instance->decoder.parser_step = KiaV3V4DecoderStepReset;
            }
        } else {
            if(duration > 1000 && duration < 1500) {
                if(instance->header_count >= 8) {
                    instance->decoder.parser_step = KiaV3V4DecoderStepCollectRawBits;
                    instance->raw_bit_count = 0;
                    instance->is_v3_sync = true;
                    memset(instance->raw_bits, 0, sizeof(instance->raw_bits));
                } else {
                    instance->decoder.parser_step = KiaV3V4DecoderStepReset;
                }
            } else if(
                (DURATION_DIFF(duration, subghz_protocol_kia_v3_v4_const.te_short) <
                 subghz_protocol_kia_v3_v4_const.te_delta) &&
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v3_v4_const.te_short) <
                 subghz_protocol_kia_v3_v4_const.te_delta)) {
                instance->header_count++;
            } else if(duration > 1500) {
                instance->decoder.parser_step = KiaV3V4DecoderStepReset;
            }
        }
        break;

    case KiaV3V4DecoderStepCollectRawBits:
        if(level) {
            if(duration > 1000 && duration < 1500) {
                if(kia_v3_v4_process_buffer(instance)) {
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.parser_step = KiaV3V4DecoderStepReset;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_kia_v3_v4_const.te_short) <
                subghz_protocol_kia_v3_v4_const.te_delta) {
                kia_v3_v4_add_raw_bit(instance, false);
            } else if(
                DURATION_DIFF(duration, subghz_protocol_kia_v3_v4_const.te_long) <
                subghz_protocol_kia_v3_v4_const.te_delta) {
                kia_v3_v4_add_raw_bit(instance, true);
            } else {
                instance->decoder.parser_step = KiaV3V4DecoderStepReset;
            }
        } else {
            if(duration > 1000 && duration < 1500) {
                if(kia_v3_v4_process_buffer(instance)) {
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.parser_step = KiaV3V4DecoderStepReset;
            } else if(duration > 1500) {
                if(kia_v3_v4_process_buffer(instance)) {
                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.parser_step = KiaV3V4DecoderStepReset;
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_kia_v3_v4_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV3V4* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_kia_v3_v4_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV3V4* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        if(!flipper_format_write_uint32(flipper_format, "Frequency", &preset->frequency, 1)) {
            break;
        }

        if(!flipper_format_write_string_cstr(
               flipper_format, "Preset", furi_string_get_cstr(preset->name))) {
            break;
        }

        const char* version_name = (instance->version == 0) ? "Kia V4" : "Kia V3";
        if(!flipper_format_write_string_cstr(flipper_format, "Protocol", version_name)) {
            break;
        }

        uint32_t bits = instance->generic.data_count_bit;
        if(!flipper_format_write_uint32(flipper_format, "Bit", &bits, 1)) {
            break;
        }

        char key_str[20];
        snprintf(key_str, sizeof(key_str), "%016llX", (unsigned long long)instance->generic.data);
        if(!flipper_format_write_string_cstr(flipper_format, "Key", key_str)) {
            break;
        }

        if(!flipper_format_write_uint32(
               flipper_format, "Serial", &instance->generic.serial, 1)) {
            break;
        }
        uint32_t btn_tmp = instance->generic.btn;
        if(!flipper_format_write_uint32(flipper_format, "Btn", &btn_tmp, 1)) {
            break;
        }
        if(!flipper_format_write_uint32(
               flipper_format, "Cnt", &instance->generic.cnt, 1)) {
            break;
        }

        if(!flipper_format_write_uint32(flipper_format, "Encrypted", &instance->encrypted, 1)) {
            break;
        }

        if(!flipper_format_write_uint32(flipper_format, "Decrypted", &instance->decrypted, 1)) {
            break;
        }

        uint32_t temp = instance->version;
        if(!flipper_format_write_uint32(flipper_format, "KIAVersion", &temp, 1)) {
            break;
        }

        temp = instance->crc;
        if(!flipper_format_write_uint32(flipper_format, "CRC", &temp, 1)) {
            break;
        }

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_kia_v3_v4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV3V4* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize_check_count_bit(&instance->generic, flipper_format, 64);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t temp = 0;

        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Encrypted", &temp, 1)) {
            instance->encrypted = temp;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Decrypted", &temp, 1)) {
            instance->decrypted = temp;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "KIAVersion", &temp, 1)) {
            instance->version = (uint8_t)temp;
        }
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "CRC", &temp, 1)) {
            instance->crc = (uint8_t)temp;
        }
    }

    return ret;
}

static uint64_t compute_yek(uint64_t key) {
    uint64_t yek = 0;
    for(int i = 0; i < 64; i++) {
        yek |= ((key >> i) & 1) << (63 - i);
    }
    return yek;
}

void subghz_protocol_decoder_kia_v3_v4_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV3V4* instance = context;

    uint64_t yek = compute_yek(instance->generic.data);
    uint32_t key_hi = (uint32_t)(instance->generic.data >> 32);
    uint32_t key_lo = (uint32_t)(instance->generic.data & 0xFFFFFFFF);
    uint32_t yek_hi = (uint32_t)(yek >> 32);
    uint32_t yek_lo = (uint32_t)(yek & 0xFFFFFFFF);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%08lX%08lX\r\n"
        "Yek:%08lX%08lX\r\n"
        "Serial:%07lX Btn:%01X [%s]\r\n"
        "Cnt:%04lX CRC:%01X\r\n",
        kia_version_names[instance->version],
        instance->generic.data_count_bit,
        key_hi,
        key_lo,
        yek_hi,
        yek_lo,
        instance->generic.serial,
        instance->generic.btn,
        subghz_protocol_kia_v3_v4_get_name_button(instance->generic.btn),
        instance->generic.cnt,
        instance->crc);
}

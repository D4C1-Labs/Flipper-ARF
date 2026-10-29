#include "beninca_arc.h"
#include "protocols_common.h"
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include "../helpers/subghz_generic_data2_seed_compat.h"
#include <lib/subghz/blocks/math.h>
#include "core/log.h"
#include <stddef.h>
#include <stdint.h>

#include "../helpers/subghz_custom_btn_i_compat.h"
#include "../helpers/subghz_lib_ext_compat.h"
#include "../helpers/subghz_aes128_compat.h"

#define TAG "BenincaARC"

#define BENINCA_ARC_KEY_TYPE 9u

static const SubGhzBlockConst subghz_protocol_beninca_arc_const = {
    .te_short = 300,
    .te_long = 600,
    .te_delta = 155,
    .min_count_bit_for_found = 128,
};

typedef enum {
    BenincaARCDecoderStart = 0,
    BenincaARCDecoderHighLevel,
    BenincaARCDecoderLowLevel,
} BenincaARCDecoderState;

struct SubGhzProtocolDecoderBenincaARC {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;

    SubGhzGenericCompat generic;

    SubGhzKeystore* keystore;
};

struct SubGhzProtocolEncoderBenincaARC {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;

    SubGhzGenericCompat generic;

    SubGhzKeystore* keystore;
};

const SubGhzProtocolDecoder subghz_protocol_beninca_arc_decoder = {
    .alloc = subghz_protocol_decoder_beninca_arc_alloc,
    .free = subghz_protocol_decoder_beninca_arc_free,

    .feed = subghz_protocol_decoder_beninca_arc_feed,
    .reset = subghz_protocol_decoder_beninca_arc_reset,

    .get_hash_data = subghz_protocol_decoder_beninca_arc_get_hash_data,
    .serialize = subghz_protocol_decoder_beninca_arc_serialize,
    .deserialize = subghz_protocol_decoder_beninca_arc_deserialize,
    .get_string = subghz_protocol_decoder_beninca_arc_get_string,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
const SubGhzProtocolEncoder subghz_protocol_beninca_arc_encoder = {
    .alloc = subghz_protocol_encoder_beninca_arc_alloc,
    .free = subghz_protocol_encoder_beninca_arc_free,

    .deserialize = subghz_protocol_encoder_beninca_arc_deserialize,
    .stop = subghz_protocol_encoder_beninca_arc_stop,
    .yield = subghz_protocol_encoder_beninca_arc_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_beninca_arc_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol subghz_protocol_beninca_arc = {
    .name = SUBGHZ_PROTOCOL_BENINCA_ARC_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_beninca_arc_decoder,
    .encoder = &subghz_protocol_beninca_arc_encoder,
};

#if SUBGHZ_GARAGE_WITH_ENCODER
static uint8_t subghz_protocol_beninca_arc_get_btn_code(void) {
    uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t original_btn_code = subghz_custom_btn_get_original();
    uint8_t btn = original_btn_code;

    if((custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) && (original_btn_code != 0)) {
        btn = original_btn_code;
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_UP) {
        switch(original_btn_code) {
        case 0x02:
            btn = 0x04;
            break;
        case 0x04:
            btn = 0x02;
            break;
        case 0x00:
            btn = 0x04;
            break;

        default:
            break;
        }
    } else if(custom_btn_id == SUBGHZ_CUSTOM_BTN_DOWN) {
        switch(original_btn_code) {
        case 0x02:
            btn = 0x00;
            break;
        case 0x04:
            btn = 0x00;
            break;
        case 0x00:
            btn = 0x02;
            break;

        default:
            break;
        }
    }

    return btn;
}
#endif

static void get_subghz_protocol_beninca_arc_aes_key(SubGhzKeystore* keystore, uint8_t* aes_key) {
    uint64_t mfkey = 0;
    for
        M_EACH(manufacture_code, *subghz_keystore_get_data(keystore), SubGhzKeyArray_t) {
            if(manufacture_code->type == BENINCA_ARC_KEY_TYPE) {
                mfkey = manufacture_code->key;
                break;
            }
        }

    uint32_t derived_lo = (uint32_t)(mfkey & 0xFFFFFFFF);
    uint32_t derived_hi = (uint32_t)((mfkey >> 32) & 0xFFFFFFFF);

    uint64_t val64_a = ((uint64_t)derived_hi << 32) | derived_lo;
    for(uint8_t i = 0; i < 8; i++) {
        aes_key[i] = (val64_a >> (56 - i * 8)) & 0xFF;
    }

    uint32_t new_lo = ((derived_hi >> 24) & 0xFF) | ((derived_hi >> 8) & 0xFF00) |
                      ((derived_hi << 8) & 0xFF0000) | ((derived_hi << 24) & 0xFF000000);
    uint32_t new_hi = ((derived_lo >> 24) & 0xFF) | ((derived_lo >> 8) & 0xFF00) |
                      ((derived_lo << 8) & 0xFF0000) | ((derived_lo << 24) & 0xFF000000);

    uint64_t val64_b = ((uint64_t)new_hi << 32) | new_lo;
    for(uint8_t i = 0; i < 8; i++) {
        aes_key[i + 8] = (val64_b >> (56 - i * 8)) & 0xFF;
    }
}

static void reverse_bits_in_bytes(uint8_t* data, uint8_t len) {
    for(uint8_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        uint8_t step1 = ((byte & 0x55) << 1) | ((byte >> 1) & 0x55);
        uint8_t step2 = ((step1 & 0x33) << 2) | ((step1 >> 2) & 0x33);
        data[i] = ((step2 & 0x0F) << 4) | (step2 >> 4);
    }
}

static uint64_t
    subghz_protocol_beninca_arc_decrypt(SubGhzGenericCompat* generic, SubGhzKeystore* keystore) {
    uint8_t encrypted_data[16];

    for(uint8_t i = 0; i < 8; i++) {
        encrypted_data[i] = (generic->data >> (56 - i * 8)) & 0xFF;
        encrypted_data[i + 8] = (generic->data_2 >> (56 - i * 8)) & 0xFF;
    }

    reverse_bits_in_bytes(encrypted_data, 16);

    uint8_t aes_key[16];
    get_subghz_protocol_beninca_arc_aes_key(keystore, aes_key);

    uint8_t decrypted[16];
    subghz_garage_aes128_ecb_decrypt(aes_key, encrypted_data, decrypted);
    memcpy(encrypted_data, decrypted, 16);

    generic->serial = ((uint32_t)encrypted_data[0] << 24) | ((uint32_t)encrypted_data[1] << 16) |
                      ((uint32_t)encrypted_data[2] << 8) | encrypted_data[3];

    generic->btn = encrypted_data[4];

    uint64_t middle_bytes = 0;
    middle_bytes = ((uint64_t)encrypted_data[5] << 32) | ((uint64_t)encrypted_data[6] << 24) |
                   ((uint64_t)encrypted_data[7] << 16) | ((uint64_t)encrypted_data[8] << 8) |
                   encrypted_data[9];

    generic->cnt = ((uint32_t)encrypted_data[10] << 24) | ((uint32_t)encrypted_data[11] << 16) |
                   ((uint32_t)encrypted_data[12] << 8) | encrypted_data[13];

    generic->seed = ((uint16_t)encrypted_data[14] << 8) | encrypted_data[15];

    return middle_bytes;
}

#if SUBGHZ_GARAGE_WITH_ENCODER
static void subghz_protocol_beninca_arc_encrypt(
    SubGhzGenericCompat* generic,
    SubGhzKeystore* keystore,
    uint64_t middle_bytes) {
    uint8_t plaintext[16];

    plaintext[0] = (generic->serial >> 24) & 0xFF;
    plaintext[1] = (generic->serial >> 16) & 0xFF;
    plaintext[2] = (generic->serial >> 8) & 0xFF;
    plaintext[3] = generic->serial & 0xFF;
    plaintext[4] = generic->btn;
    plaintext[5] = (middle_bytes >> 32) & 0xFF;
    plaintext[6] = (middle_bytes >> 24) & 0xFF;
    plaintext[7] = (middle_bytes >> 16) & 0xFF;
    plaintext[8] = (middle_bytes >> 8) & 0xFF;
    plaintext[9] = middle_bytes & 0xFF;
    plaintext[10] = (generic->cnt >> 24) & 0xFF;
    plaintext[11] = (generic->cnt >> 16) & 0xFF;
    plaintext[12] = (generic->cnt >> 8) & 0xFF;
    plaintext[13] = generic->cnt & 0xFF;
    plaintext[14] = (generic->seed >> 8) & 0xFF;
    plaintext[15] = generic->seed & 0xFF;

    uint8_t aes_key[16];
    get_subghz_protocol_beninca_arc_aes_key(keystore, aes_key);

    uint8_t encrypted[16];
    subghz_garage_aes128_ecb_encrypt(aes_key, plaintext, encrypted);
    memcpy(plaintext, encrypted, 16);

    reverse_bits_in_bytes(plaintext, 16);

    for(uint8_t i = 0; i < 8; i++) {
        generic->data = (generic->data << 8) | plaintext[i];
        generic->data_2 = (generic->data_2 << 8) | plaintext[i + 8];
    }
    return;
}

void* subghz_protocol_encoder_beninca_arc_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolEncoderBenincaARC* instance = malloc(sizeof(SubGhzProtocolEncoderBenincaARC));

    instance->base.protocol = &subghz_protocol_beninca_arc;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);

    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 800;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;

    return instance;
}

void subghz_protocol_encoder_beninca_arc_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderBenincaARC* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_beninca_arc_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderBenincaARC* instance = context;
    instance->encoder.is_running = false;
}

static void subghz_protocol_beninca_arc_encoder_get_upload(
    SubGhzProtocolEncoderBenincaARC* instance,
    size_t* index) {
    furi_assert(instance);
    size_t index_local = *index;

    for(uint8_t i = 64; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index_local++] =
                level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_short);
            instance->encoder.upload[index_local++] =
                level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_long);
        } else {
            instance->encoder.upload[index_local++] =
                level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_long);
            instance->encoder.upload[index_local++] =
                level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_short);
        }
    }

    for(uint8_t i = 64; i > 0; i--) {
        if(bit_read(instance->generic.data_2, i - 1)) {
            instance->encoder.upload[index_local++] =
                level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_short);
            instance->encoder.upload[index_local++] =
                level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_long);
        } else {
            instance->encoder.upload[index_local++] =
                level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_long);
            instance->encoder.upload[index_local++] =
                level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_short);
        }
    }

    instance->encoder.upload[index_local++] =
        level_duration_make(true, (uint32_t)subghz_protocol_beninca_arc_const.te_short);

    instance->encoder.upload[index_local++] =
        level_duration_make(false, (uint32_t)subghz_protocol_beninca_arc_const.te_long * 15);

    *index = index_local;
}

static void subghz_protocol_beninca_arc_encoder_prepare_packets(
    SubGhzProtocolEncoderBenincaARC* instance) {
    furi_assert(instance);

    if(subghz_garage_get_rolling_counter_mult() != -0x7FFFFFFF) {
        if(!subghz_garage_counter_override_get(&instance->generic.cnt)) {
            if((instance->generic.cnt + subghz_garage_get_rolling_counter_mult()) > 0xFFFFFFFF) {
                instance->generic.cnt = 0;
            } else {
                instance->generic.cnt += subghz_garage_get_rolling_counter_mult();
            }
        }
    } else {
        instance->generic.cnt += 1;
    }

    size_t index = 0;

    instance->generic.btn = subghz_protocol_beninca_arc_get_btn_code();

    if(subghz_garage_button_override_get(&instance->generic.btn))
        FURI_LOG_D(TAG, "Button sucessfully changed to 0x%X", instance->generic.btn);

    for(uint8_t i = 0; i < 3; i++) {
        subghz_protocol_beninca_arc_encrypt(
            &instance->generic, instance->keystore, (uint64_t)((i + 1) * 2));
        subghz_protocol_beninca_arc_encoder_get_upload(instance, &index);
    }

    instance->encoder.size_upload = index;
}

bool subghz_protocol_beninca_arc_create_data(
    void* context,
    FlipperFormat* flipper_format,
    uint32_t serial,
    uint8_t btn,
    uint32_t cnt,
    SubGhzRadioPreset* preset) {
    furi_assert(context);

    SubGhzProtocolEncoderBenincaARC* instance = context;
    instance->generic.serial = serial;
    instance->generic.btn = btn;
    instance->generic.cnt = cnt;
    instance->generic.seed = 0xAA55;
    instance->generic.data_count_bit = 128;

    subghz_protocol_beninca_arc_encrypt(&instance->generic, instance->keystore, 0x1);

    SubGhzProtocolStatus res =
        subghz_block_generic_serialize((SubGhzBlockGeneric*)&instance->generic, flipper_format, preset);

    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> (i * 8)) & 0xFF;
    }

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        res = SubGhzProtocolStatusErrorParserOthers;
    }

    if((res == SubGhzProtocolStatusOk) &&
       !flipper_format_insert_or_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Unable to add Data2");
        res = SubGhzProtocolStatusErrorParserOthers;
    }

    return res == SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_beninca_arc_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderBenincaARC* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize((SubGhzBlockGeneric*)&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Data");
            break;
        }

        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = instance->generic.data_2 << 8 | key_data[i];
        }

        subghz_protocol_beninca_arc_decrypt(&instance->generic, instance->keystore);

        subghz_protocol_beninca_arc_encoder_prepare_packets(instance);

        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Key", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Key");
            break;
        }

        for(size_t i = 0; i < sizeof(uint64_t); i++) {
            key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> i * 8) & 0xFF;
        }
        if(!flipper_format_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Unable to update Data");
            break;
        }

        instance->encoder.is_running = true;

        res = SubGhzProtocolStatusOk;
    } while(false);

    return res;
}

LevelDuration subghz_protocol_encoder_beninca_arc_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderBenincaARC* instance = context;

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
#endif

void* subghz_protocol_decoder_beninca_arc_alloc(SubGhzEnvironment* environment) {
    SubGhzProtocolDecoderBenincaARC* instance = malloc(sizeof(SubGhzProtocolDecoderBenincaARC));
    instance->base.protocol = &subghz_protocol_beninca_arc;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->keystore = subghz_environment_get_keystore(environment);
    instance->decoder.parser_step = BenincaARCDecoderStart;
    return instance;
}

void subghz_protocol_decoder_beninca_arc_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;
    free(instance);
}

void subghz_protocol_decoder_beninca_arc_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;
    instance->decoder.parser_step = BenincaARCDecoderStart;
}

void subghz_protocol_decoder_beninca_arc_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;

    switch(instance->decoder.parser_step) {
    case BenincaARCDecoderStart:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_beninca_arc_const.te_long * 16) <
                        subghz_protocol_beninca_arc_const.te_delta * 15)) {
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = BenincaARCDecoderHighLevel;
            break;
        }

        break;
    case BenincaARCDecoderHighLevel:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = BenincaARCDecoderLowLevel;

            if((instance->decoder.decode_count_bit ==
                (subghz_protocol_beninca_arc_const.min_count_bit_for_found / 2)) &&
               (instance->decoder.decode_data != 0)) {
                instance->generic.data = instance->decoder.decode_data;
                instance->decoder.decode_data = 0;
            } else if(
                instance->decoder.decode_count_bit ==
                subghz_protocol_beninca_arc_const.min_count_bit_for_found) {
                instance->generic.data_2 = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;
                instance->decoder.parser_step = BenincaARCDecoderStart;

                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }

                break;
            }
        } else {
            instance->decoder.parser_step = BenincaARCDecoderStart;
        }
        break;
    case BenincaARCDecoderLowLevel:
        if(!level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_beninca_arc_const.te_short) <
                subghz_protocol_beninca_arc_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_beninca_arc_const.te_long) <
                subghz_protocol_beninca_arc_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = BenincaARCDecoderHighLevel;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_beninca_arc_const.te_long) <
                 subghz_protocol_beninca_arc_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_beninca_arc_const.te_short) <
                 subghz_protocol_beninca_arc_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = BenincaARCDecoderHighLevel;
            } else {
                instance->decoder.parser_step = BenincaARCDecoderStart;
            }
            break;
        } else {
            instance->decoder.parser_step = BenincaARCDecoderStart;
            break;
        }
    }
}

uint8_t subghz_protocol_decoder_beninca_arc_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_beninca_arc_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize((SubGhzBlockGeneric*)&instance->generic, flipper_format, preset);

    uint8_t key_data[sizeof(uint64_t)] = {0};
    for(size_t i = 0; i < sizeof(uint64_t); i++) {
        key_data[sizeof(uint64_t) - i - 1] = (instance->generic.data_2 >> (i * 8)) & 0xFF;
    }

    if(!flipper_format_rewind(flipper_format)) {
        FURI_LOG_E(TAG, "Rewind error");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }

    if((ret == SubGhzProtocolStatusOk) &&
       !flipper_format_insert_or_update_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
        FURI_LOG_E(TAG, "Unable to add Data");
        ret = SubGhzProtocolStatusErrorParserOthers;
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_beninca_arc_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;

    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            (SubGhzBlockGeneric*)&instance->generic,
            flipper_format,
            subghz_protocol_beninca_arc_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }
        uint8_t key_data[sizeof(uint64_t)] = {0};
        if(!flipper_format_read_hex(flipper_format, "Data", key_data, sizeof(uint64_t))) {
            FURI_LOG_E(TAG, "Missing Data");
            ret = SubGhzProtocolStatusErrorParserOthers;
            break;
        }

        for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
            instance->generic.data_2 = instance->generic.data_2 << 8 | key_data[i];
        }
    } while(false);
    return ret;
}

void subghz_protocol_decoder_beninca_arc_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderBenincaARC* instance = context;

    uint64_t middle_bytes_dec =
        subghz_protocol_beninca_arc_decrypt(&instance->generic, instance->keystore);

    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.cnt_length_bit = 32;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.btn_length_bit = 8;

    furi_string_printf(
        output,
        "%s %db\r\n"
        "Key1:%08llX\r\n"
        "Key2:%08llX\r\n"
        "Sn:%08lX Btn:%02X\r\n"
        "Mc:%0lX Cnt:%0lX\r\n"
        "Fx:%04lX",
        instance->base.protocol->name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.data_2,
        instance->generic.serial,
        instance->generic.btn,
        (uint32_t)(middle_bytes_dec & 0xFFFFFFFF),
        instance->generic.cnt,
        instance->generic.seed & 0xFFFF);
}

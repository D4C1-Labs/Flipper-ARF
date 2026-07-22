#include "kia_v2.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_encoder.h>
#include <furi.h>

#define TAG "KiaV2"

#define KIA_V2_HEADER_PAIRS 252
#define KIA_V2_TOTAL_BURSTS 2
#define KIA_V2_UPLOAD_CAPACITY \
    (KIA_V2_TOTAL_BURSTS * ((KIA_V2_HEADER_PAIRS * 2) + 1 + ((53U - 1U) * 2)))

static const SubGhzBlockConst subghz_protocol_kia_v2_const = {
    .te_short = 500,
    .te_long = 1000,
    .te_delta = 150,
    .min_count_bit_for_found = 53,
};

struct SubGhzProtocolDecoderKiaV2 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    ManchesterState manchester_state;
};

struct SubGhzProtocolEncoderKiaV2 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    KiaV2DecoderStepReset = 0,
    KiaV2DecoderStepCheckPreamble,
    KiaV2DecoderStepCollectRawBits,
} KiaV2DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_kia_v2_decoder = {
    .alloc = subghz_protocol_decoder_kia_v2_alloc,
    .free = subghz_protocol_decoder_kia_v2_free,
    .feed = subghz_protocol_decoder_kia_v2_feed,
    .reset = subghz_protocol_decoder_kia_v2_reset,
    .get_hash_data = subghz_protocol_decoder_kia_v2_get_hash_data,
    .serialize = subghz_protocol_decoder_kia_v2_serialize,
    .deserialize = subghz_protocol_decoder_kia_v2_deserialize,
    .get_string = subghz_protocol_decoder_kia_v2_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_kia_v2_encoder = {
    .alloc = subghz_protocol_encoder_kia_v2_alloc,
    .free = subghz_protocol_encoder_kia_v2_free,
    .deserialize = subghz_protocol_encoder_kia_v2_deserialize,
    .stop = subghz_protocol_encoder_kia_v2_stop,
    .yield = subghz_protocol_encoder_kia_v2_yield,
};

const SubGhzProtocol subghz_protocol_kia_v2 = {
    .name = SUBGHZ_PROTOCOL_KIA_V2_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_kia_v2_decoder,
    .encoder = &subghz_protocol_kia_v2_encoder,
};

static uint8_t kia_v2_calculate_crc(uint64_t data) {
    uint64_t data_without_crc = data >> 4;

    uint8_t bytes[6];
    bytes[0] = (uint8_t)(data_without_crc);
    bytes[1] = (uint8_t)(data_without_crc >> 8);
    bytes[2] = (uint8_t)(data_without_crc >> 16);
    bytes[3] = (uint8_t)(data_without_crc >> 24);
    bytes[4] = (uint8_t)(data_without_crc >> 32);
    bytes[5] = (uint8_t)(data_without_crc >> 40);

    uint8_t crc = 0;
    for(int i = 0; i < 6; i++) {
        crc ^= (bytes[i] & 0x0F) ^ (bytes[i] >> 4);
    }

    return (crc + 1) & 0x0F;
}

static void subghz_protocol_encoder_kia_v2_get_upload(SubGhzProtocolEncoderKiaV2* instance) {
    furi_assert(instance);
    if(instance->encoder.upload == NULL) return;
    size_t index = 0;
    LevelDuration* up = instance->encoder.upload;

    const uint32_t te_short = (uint32_t)subghz_protocol_kia_v2_const.te_short;
    const uint32_t te_long = (uint32_t)subghz_protocol_kia_v2_const.te_long;

    uint8_t crc = kia_v2_calculate_crc(instance->generic.data);
    instance->generic.data = (instance->generic.data & ~0x0FULL) | crc;

    for(uint8_t burst = 0; burst < KIA_V2_TOTAL_BURSTS; burst++) {
        for(int i = 0; i < KIA_V2_HEADER_PAIRS; i++) {
            up[index++] = level_duration_make(false, te_long);
            up[index++] = level_duration_make(true, te_long);
        }

        up[index++] = level_duration_make(false, te_short);

        for(uint8_t i = instance->generic.data_count_bit; i > 1; i--) {
            bool bit = bit_read(instance->generic.data, i - 2);
            up[index++] = level_duration_make(bit, te_short);
            up[index++] = level_duration_make(!bit, te_short);
        }
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;

    FURI_LOG_I(
        TAG,
        "Upload built: %d bursts, size_upload=%zu, data_count_bit=%u, data=0x%016llX",
        KIA_V2_TOTAL_BURSTS,
        instance->encoder.size_upload,
        instance->generic.data_count_bit,
        instance->generic.data);
}

void* subghz_protocol_encoder_kia_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderKiaV2* instance = calloc(1, sizeof(SubGhzProtocolEncoderKiaV2));
    if(!instance) {
        return NULL;
    }

    instance->base.protocol = &subghz_protocol_kia_v2;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    return instance;
}

void subghz_protocol_encoder_kia_v2_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV2* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_kia_v2_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV2* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_kia_v2_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV2* instance = context;

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

SubGhzProtocolStatus
    subghz_protocol_encoder_kia_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV2* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Missing or wrong Protocol");
            break;
        }

        instance->generic.data_count_bit = subghz_protocol_kia_v2_const.min_count_bit_for_found;

        if(instance->generic.data == 0) break;

        instance->generic.serial = (uint32_t)((instance->generic.data >> 20) & 0xFFFFFFFF);
        instance->generic.btn = (uint8_t)((instance->generic.data >> 16) & 0x0F);

        uint16_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
        instance->generic.cnt = ((raw_count >> 4) | (raw_count << 8)) & 0xFFF;

        instance->encoder.repeat = 10;

        uint64_t new_data = 0;

        new_data |= 1ULL << 52;

        new_data |= ((uint64_t)instance->generic.serial << 20) & 0xFFFFFFFFF00000ULL;

        uint32_t uVar6 = ((uint32_t)(instance->generic.cnt & 0xFF) << 8) |
                         ((uint32_t)(instance->generic.btn & 0x0F) << 16) |
                         ((uint32_t)(instance->generic.cnt >> 4) & 0xF0);

        new_data |= (uint64_t)uVar6;

        instance->generic.data = new_data;
        instance->generic.data_count_bit = 53;

        FURI_LOG_I(
            TAG,
            "Encoder reconstruct: serial=0x%08lX, btn=0x%X, cnt=0x%03lX, uVar6=0x%05lX, data=0x%016llX",
            (unsigned long)instance->generic.serial,
            (unsigned int)instance->generic.btn,
            (unsigned long)instance->generic.cnt,
            (unsigned long)uVar6,
            (unsigned long long)instance->generic.data);

        if(instance->encoder.upload == NULL) {
            instance->encoder.size_upload = KIA_V2_UPLOAD_CAPACITY;
            instance->encoder.upload =
                malloc(instance->encoder.size_upload * sizeof(LevelDuration));
        }
        subghz_protocol_encoder_kia_v2_get_upload(instance);

        instance->encoder.is_running = true;

        FURI_LOG_I(
            TAG,
            "Encoder deserialized: repeat=%u, size_upload=%zu, is_running=%d, front=%zu",
            instance->encoder.repeat,
            instance->encoder.size_upload,
            instance->encoder.is_running,
            instance->encoder.front);

        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void* subghz_protocol_decoder_kia_v2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV2* instance = calloc(1, sizeof(SubGhzProtocolDecoderKiaV2));
    if(!instance) {
        return NULL;
    }
    instance->base.protocol = &subghz_protocol_kia_v2;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_kia_v2_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;
    free(instance);
}

void subghz_protocol_decoder_kia_v2_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;
    instance->decoder.parser_step = KiaV2DecoderStepReset;
    instance->header_count = 0;
    instance->manchester_state = ManchesterStateMid1;
    instance->decoder.decode_data = 0;
    instance->decoder.decode_count_bit = 0;
}

void subghz_protocol_decoder_kia_v2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    switch(instance->decoder.parser_step) {
    case KiaV2DecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_long) <
                       subghz_protocol_kia_v2_const.te_delta)) {
            instance->decoder.parser_step = KiaV2DecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        }
        break;

    case KiaV2DecoderStepCheckPreamble:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_long) <
               subghz_protocol_kia_v2_const.te_delta) {
                instance->decoder.te_last = duration;
                instance->header_count++;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_short) <
                subghz_protocol_kia_v2_const.te_delta) {
                if(instance->header_count >= 100) {
                    instance->header_count = 0;
                    instance->decoder.decode_data = 0;
                    instance->decoder.decode_count_bit = 1;
                    instance->decoder.parser_step = KiaV2DecoderStepCollectRawBits;
                    subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                } else {
                    instance->decoder.te_last = duration;
                }
            } else {
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        } else {
            if(DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_long) <
               subghz_protocol_kia_v2_const.te_delta) {
                instance->header_count++;
                instance->decoder.te_last = duration;
            } else if(
                DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_short) <
                subghz_protocol_kia_v2_const.te_delta) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        }
        break;

    case KiaV2DecoderStepCollectRawBits: {
        ManchesterEvent event;
        if(DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_short) <
           subghz_protocol_kia_v2_const.te_delta) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        } else if(DURATION_DIFF(duration, subghz_protocol_kia_v2_const.te_long) <
                  subghz_protocol_kia_v2_const.te_delta) {
            event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
        } else {
            instance->decoder.parser_step = KiaV2DecoderStepReset;
            break;
        }

        bool data_bit;
        if(manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data_bit;
            instance->decoder.decode_count_bit++;

            if(instance->decoder.decode_count_bit == 53) {
                instance->generic.data = instance->decoder.decode_data;
                instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                instance->generic.serial =
                    (uint32_t)((instance->generic.data >> 20) & 0xFFFFFFFF);
                instance->generic.btn = (uint8_t)((instance->generic.data >> 16) & 0x0F);

                uint16_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
                instance->generic.cnt = ((raw_count >> 4) | (raw_count << 8)) & 0xFFF;

                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);

                instance->decoder.decode_data = 0;
                instance->decoder.decode_count_bit = 0;
                instance->header_count = 0;
                instance->decoder.parser_step = KiaV2DecoderStepReset;
            }
        }
        break;
    }
    }
}

uint8_t subghz_protocol_decoder_kia_v2_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    uint32_t hash = instance->generic.serial;
    hash ^= (instance->generic.btn << 24);
    hash ^= (instance->generic.cnt << 12);

    return (uint8_t)(hash ^ (hash >> 8) ^ (hash >> 16) ^ (hash >> 24));
}

SubGhzProtocolStatus subghz_protocol_decoder_kia_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    uint32_t crc = instance->generic.data & 0x0F;
    if(!flipper_format_write_uint32(flipper_format, "CRC", &crc, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t raw_count = (uint16_t)((instance->generic.data >> 4) & 0xFFF);
    if(!flipper_format_write_uint32(flipper_format, "RawCnt", &raw_count, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_kia_v2_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_kia_v2_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_kia_v2_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV2* instance = context;

    uint8_t crc = instance->generic.data & 0x0F;

    bool crc_valid = crc == kia_v2_calculate_crc(instance->generic.data);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%013llX\r\n"
        "Sn:%08lX Btn:%X\r\n"
        "Cnt:%03lX CRC:%X - %s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.data,
        instance->generic.serial,
        instance->generic.btn,
        instance->generic.cnt,
        crc,
        crc_valid ? "OK" : "BAD");
}

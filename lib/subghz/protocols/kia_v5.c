#include "kia_v5.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include <lib/toolbox/manchester_decoder.h>

#define TAG "SubGhzProtocolKiaV5"

static const SubGhzBlockConst subghz_protocol_kia_v5_const = {
    .te_short = 400,
    .te_long = 800,
    .te_delta = 150,
    .min_count_bit_for_found = 64,
};

static const uint8_t keystore_bytes[] = {0x53, 0x54, 0x46, 0x52, 0x4b, 0x45, 0x30, 0x30};

static uint8_t kia_v5_reverse_byte(uint8_t b) {
    uint8_t r = 0;
    for(int i = 0; i < 8; i++) {
        if(b & (1 << i)) r |= (1 << (7 - i));
    }
    return r;
}

static uint64_t kia_v5_bit_reverse_64(uint64_t input) {
    uint64_t output = 0;
    for(int i = 0; i < 8; i++) {
        uint8_t byte = (input >> (i * 8)) & 0xFF;
        uint8_t reversed = kia_v5_reverse_byte(byte);
        output |= ((uint64_t)reversed << ((7 - i) * 8));
    }
    return output;
}

static uint16_t mixer_decode(uint32_t encrypted) {
    uint8_t s0 = (encrypted & 0xFF);
    uint8_t s1 = (encrypted >> 8) & 0xFF;
    uint8_t s2 = (encrypted >> 16) & 0xFF;
    uint8_t s3 = (encrypted >> 24) & 0xFF;

    int round_index = 1;
    for(size_t i = 0; i < 18; i++) {
        uint8_t r = keystore_bytes[round_index] & 0xFF;
        int steps = 8;
        while(steps > 0) {
            uint8_t base;
            if((s3 & 0x40) == 0) {
                base = (s3 & 0x02) == 0 ? 0x74 : 0x2E;
            } else {
                base = (s3 & 0x02) == 0 ? 0x3A : 0x5C;
            }

            if(s2 & 0x08) {
                base = (((base >> 4) & 0x0F) | ((base & 0x0F) << 4)) & 0xFF;
            }
            if(s1 & 0x01) {
                base = ((base & 0x3F) << 2) & 0xFF;
            }
            if(s0 & 0x01) {
                base = (base << 1) & 0xFF;
            }

            uint8_t temp = (s3 ^ s1) & 0xFF;
            s3 = ((s3 & 0x7F) << 1) & 0xFF;
            if(s2 & 0x80) {
                s3 |= 0x01;
            }
            s2 = ((s2 & 0x7F) << 1) & 0xFF;
            if(s1 & 0x80) {
                s2 |= 0x01;
            }
            s1 = ((s1 & 0x7F) << 1) & 0xFF;
            if(s0 & 0x80) {
                s1 |= 0x01;
            }
            s0 = ((s0 & 0x7F) << 1) & 0xFF;

            uint8_t chk = (base ^ (r ^ temp)) & 0xFF;
            if(chk & 0x80) {
                s0 |= 0x01;
            }
            r = ((r & 0x7F) << 1) & 0xFF;
            steps--;
        }
        round_index = (round_index - 1) & 0x7;
    }
    return (s0 + (s1 << 8)) & 0xFFFF;
}

struct SubGhzProtocolDecoderKiaV5 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;

    ManchesterState manchester_state;
    uint64_t decoded_data;
    uint64_t saved_key;
    uint8_t bit_count;
    uint64_t yek;
    uint8_t crc;
};

struct SubGhzProtocolEncoderKiaV5 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint64_t replay_data;
    uint8_t replay_crc;
};

#define KIA_V5_PREAMBLE_PAIRS 200U
#define KIA_V5_SYNC_ENTRIES   4U
#define KIA_V5_DATA_BITS      64U
#define KIA_V5_CRC_BITS       3U
#define KIA_V5_END_ENTRIES    2U
#define KIA_V5_UPLOAD_CAPACITY                          \
    (KIA_V5_PREAMBLE_PAIRS * 2U + KIA_V5_SYNC_ENTRIES + \
     (KIA_V5_DATA_BITS + KIA_V5_CRC_BITS) * 2U + KIA_V5_END_ENTRIES)

typedef enum {
    KiaV5DecoderStepReset = 0,
    KiaV5DecoderStepCheckPreamble,
    KiaV5DecoderStepData,
} KiaV5DecoderStep;

static uint8_t kia_v5_calculate_crc(uint64_t data) {
    uint8_t crc = 0;
    for(int i = 63; i >= 0; i--) {
        const uint8_t bit = (data >> i) & 1U;
        const uint8_t shifted_out = (crc >> 1U) & 1U;
        crc = (uint8_t)(((crc & 1U) << 1U) | bit);
        if(shifted_out) {
            crc ^= 3U;
        }
    }
    return (uint8_t)(crc & 3U);
}

const SubGhzProtocolDecoder subghz_protocol_kia_v5_decoder = {
    .alloc = subghz_protocol_decoder_kia_v5_alloc,
    .free = subghz_protocol_decoder_kia_v5_free,
    .feed = subghz_protocol_decoder_kia_v5_feed,
    .reset = subghz_protocol_decoder_kia_v5_reset,
    .get_hash_data = subghz_protocol_decoder_kia_v5_get_hash_data,
    .serialize = subghz_protocol_decoder_kia_v5_serialize,
    .deserialize = subghz_protocol_decoder_kia_v5_deserialize,
    .get_string = subghz_protocol_decoder_kia_v5_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_kia_v5_encoder = {
    .alloc = subghz_protocol_encoder_kia_v5_alloc,
    .free = subghz_protocol_encoder_kia_v5_free,
    .deserialize = subghz_protocol_encoder_kia_v5_deserialize,
    .stop = subghz_protocol_encoder_kia_v5_stop,
    .yield = subghz_protocol_encoder_kia_v5_yield,
};

const SubGhzProtocol subghz_protocol_kia_v5 = {
    .name = SUBGHZ_PROTOCOL_KIA_V5_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_kia_v5_decoder,
    .encoder = &subghz_protocol_kia_v5_encoder,
};

void* subghz_protocol_encoder_kia_v5_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderKiaV5* instance = calloc(1, sizeof(SubGhzProtocolEncoderKiaV5));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_kia_v5;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = 400;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.repeat = 6;
    instance->encoder.front = 0;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_kia_v5_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    if(instance->encoder.upload) {
        free(instance->encoder.upload);
    }
    free(instance);
}

static bool subghz_protocol_encoder_kia_v5_get_upload(SubGhzProtocolEncoderKiaV5* instance) {
    furi_assert(instance);

    const uint32_t te_short = (uint32_t)subghz_protocol_kia_v5_const.te_short;
    const uint32_t te_long = (uint32_t)subghz_protocol_kia_v5_const.te_long;
    size_t index = 0;

    for(size_t i = 0; i < KIA_V5_PREAMBLE_PAIRS; i++) {
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    }

    instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_long);
    instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);

    for(int b = 63; b >= 0; b--) {
        const bool bv = ((instance->replay_data >> b) & 1ULL) != 0ULL;
        if(bv) {
            instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
            instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
        } else {
            instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
            instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
        }
    }

    instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
    instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);

    const bool crc_b1 = ((instance->replay_crc >> 1U) & 1U) != 0U;
    if(crc_b1) {
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
    } else {
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    }

    const bool crc_b0 = (instance->replay_crc & 1U) != 0U;
    if(crc_b0) {
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
    } else {
        instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);
        instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    }

    instance->encoder.upload[index++] = level_duration_make(false, (int32_t)te_short);
    instance->encoder.upload[index++] = level_duration_make(true, (int32_t)te_short);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_kia_v5_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) break;

        if(instance->generic.data_count_bit < subghz_protocol_kia_v5_const.min_count_bit_for_found) {
            ret = SubGhzProtocolStatusErrorParserBitCount;
            break;
        }

        uint32_t yek_high = 0, yek_low = 0;
        uint64_t yek = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "YekHi", &yek_high, 1) &&
           flipper_format_read_uint32(flipper_format, "YekLo", &yek_low, 1)) {
            yek = ((uint64_t)yek_high << 32) | yek_low;
        } else {
            yek = kia_v5_bit_reverse_64(instance->generic.data);
        }

        instance->generic.serial = (uint32_t)((yek >> 32) & 0x0FFFFFFF);
        instance->generic.btn = (uint8_t)((yek >> 60) & 0x0F);

        uint32_t encrypted = (uint32_t)(yek & 0xFFFFFFFF);
        instance->generic.cnt = mixer_decode(encrypted);

        instance->replay_data = instance->generic.data;
        instance->replay_crc = kia_v5_calculate_crc(instance->replay_data);

        if(!subghz_protocol_encoder_kia_v5_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_kia_v5_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

LevelDuration subghz_protocol_encoder_kia_v5_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0 ||
       instance->encoder.size_upload == 0) {
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

void subghz_protocol_encoder_kia_v5_set_button(void* context, uint8_t button) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    instance->generic.btn = button;
}

void subghz_protocol_encoder_kia_v5_set_counter(void* context, uint16_t counter) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    instance->generic.cnt = counter;
}

void subghz_protocol_encoder_kia_v5_increment_counter(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    if(instance->generic.cnt < 0xFFFF) {
        instance->generic.cnt++;
    } else {
        instance->generic.cnt = 0;
    }
}

uint16_t subghz_protocol_encoder_kia_v5_get_counter(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    return instance->generic.cnt;
}

uint8_t subghz_protocol_encoder_kia_v5_get_button(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV5* instance = context;
    return instance->generic.btn;
}

static void kia_v5_add_bit(SubGhzProtocolDecoderKiaV5* instance, bool bit) {
    instance->decoded_data = (instance->decoded_data << 1) | (bit ? 1 : 0);
    instance->bit_count++;
}

void* subghz_protocol_decoder_kia_v5_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV5* instance = malloc(sizeof(SubGhzProtocolDecoderKiaV5));
    furi_assert(instance);
    instance->base.protocol = &subghz_protocol_kia_v5;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_kia_v5_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;
    free(instance);
}

void subghz_protocol_decoder_kia_v5_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;
    instance->decoder.parser_step = KiaV5DecoderStepReset;
    instance->header_count = 0;
    instance->bit_count = 0;
    instance->decoded_data = 0;
    instance->saved_key = 0;
    instance->yek = 0;
    instance->crc = 0;
    instance->manchester_state = ManchesterStateMid1;
}

void subghz_protocol_decoder_kia_v5_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;

    switch(instance->decoder.parser_step) {
    case KiaV5DecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_short) <
                       subghz_protocol_kia_v5_const.te_delta)) {
            instance->decoder.parser_step = KiaV5DecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 1;
            instance->bit_count = 0;
            instance->decoded_data = 0;
            manchester_advance(
                instance->manchester_state,
                ManchesterEventReset,
                &instance->manchester_state,
                NULL);
        }
        break;

    case KiaV5DecoderStepCheckPreamble:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_long) <
               subghz_protocol_kia_v5_const.te_delta) {
                if(instance->header_count > 40) {
                    instance->decoder.parser_step = KiaV5DecoderStepData;
                    instance->bit_count = 0;
                    instance->decoded_data = 0;
                    instance->saved_key = 0;
                    instance->header_count = 0;
                } else {
                    instance->decoder.te_last = duration;
                }
            } else if(
                DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_short) <
                subghz_protocol_kia_v5_const.te_delta) {
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = KiaV5DecoderStepReset;
            }
        } else {
            if((DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_short) <
                subghz_protocol_kia_v5_const.te_delta) &&
               (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v5_const.te_short) <
                subghz_protocol_kia_v5_const.te_delta)) {
                instance->header_count++;
            } else if(
                (DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_long) <
                 subghz_protocol_kia_v5_const.te_delta) &&
                (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v5_const.te_short) <
                 subghz_protocol_kia_v5_const.te_delta)) {
                instance->header_count++;
            } else if(
                DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v5_const.te_long) <
                subghz_protocol_kia_v5_const.te_delta) {
                instance->header_count++;
            } else {
                instance->decoder.parser_step = KiaV5DecoderStepReset;
            }
            instance->decoder.te_last = duration;
        }
        break;

    case KiaV5DecoderStepData: {
        ManchesterEvent event;

        if(DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_short) <
           subghz_protocol_kia_v5_const.te_delta) {
            event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
        } else if(
            DURATION_DIFF(duration, subghz_protocol_kia_v5_const.te_long) <
            subghz_protocol_kia_v5_const.te_delta) {
            event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
        } else {
            if(instance->bit_count >= subghz_protocol_kia_v5_const.min_count_bit_for_found) {
                instance->generic.data = instance->saved_key;
                instance->generic.data_count_bit =
                    (instance->bit_count > 67) ? 67 : instance->bit_count;

                instance->crc = (uint8_t)(instance->decoded_data & 0x07);

                instance->yek = 0;
                for(int i = 0; i < 8; i++) {
                    uint8_t byte = (instance->generic.data >> (i * 8)) & 0xFF;
                    uint8_t reversed = 0;
                    for(int b = 0; b < 8; b++) {
                        if(byte & (1 << b)) reversed |= (1 << (7 - b));
                    }
                    instance->yek |= ((uint64_t)reversed << ((7 - i) * 8));
                }

                instance->generic.serial = (uint32_t)((instance->yek >> 32) & 0x0FFFFFFF);
                instance->generic.btn = (uint8_t)((instance->yek >> 60) & 0x0F);

                uint32_t encrypted = (uint32_t)(instance->yek & 0xFFFFFFFF);
                instance->generic.cnt = mixer_decode(encrypted);

                instance->decoder.decode_data = instance->generic.data;
                instance->decoder.decode_count_bit = instance->generic.data_count_bit;

                if(instance->base.callback)
                    instance->base.callback(&instance->base, instance->base.context);
            }

            instance->decoder.parser_step = KiaV5DecoderStepReset;
            break;
        }

        bool data_bit;
        if(instance->bit_count <= 66 &&
           manchester_advance(
               instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
            kia_v5_add_bit(instance, data_bit);
            if(instance->bit_count == 64) {
                instance->saved_key = instance->decoded_data;
                instance->decoded_data = 0;
            }
        }

        instance->decoder.te_last = duration;
        break;
    }
    }
}

uint8_t subghz_protocol_decoder_kia_v5_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_kia_v5_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t serial_tmp = instance->generic.serial;
        if(!flipper_format_write_uint32(flipper_format, "Serial", &serial_tmp, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t btn_tmp = instance->generic.btn;
        if(!flipper_format_write_uint32(flipper_format, "Btn", &btn_tmp, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t cnt_tmp = instance->generic.cnt;
        if(!flipper_format_write_uint32(flipper_format, "Cnt", &cnt_tmp, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t crc_temp = instance->crc;
        if(!flipper_format_write_uint32(flipper_format, "CRC", &crc_temp, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    if(ret == SubGhzProtocolStatusOk) {
        uint32_t yek_high = (uint32_t)(instance->yek >> 32);
        uint32_t yek_low = (uint32_t)(instance->yek & 0xFFFFFFFF);
        if(!flipper_format_write_uint32(flipper_format, "YekHi", &yek_high, 1)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
        if(ret == SubGhzProtocolStatusOk) {
            if(!flipper_format_write_uint32(flipper_format, "YekLo", &yek_low, 1)) {
                ret = SubGhzProtocolStatusErrorParserOthers;
            }
        }
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_kia_v5_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_kia_v5_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_rewind(flipper_format);
        uint32_t temp_crc = 0;
        if(flipper_format_read_uint32(flipper_format, "CRC", &temp_crc, 1)) {
            instance->crc = (uint8_t)temp_crc;
        } else {
            instance->crc = 0;
        }

        flipper_format_rewind(flipper_format);
        uint32_t yek_high = 0, yek_low = 0;
        if(flipper_format_read_uint32(flipper_format, "YekHi", &yek_high, 1) &&
           flipper_format_read_uint32(flipper_format, "YekLo", &yek_low, 1)) {
            instance->yek = ((uint64_t)yek_high << 32) | yek_low;
        } else {
            instance->yek = 0;
            for(int i = 0; i < 8; i++) {
                uint8_t byte = (instance->generic.data >> (i * 8)) & 0xFF;
                uint8_t reversed = 0;
                for(int j = 0; j < 8; j++) {
                    if(byte & (1 << j)) {
                        reversed |= (1 << (7 - j));
                    }
                }
                instance->yek |= ((uint64_t)reversed << ((7 - i) * 8));
            }
        }

        instance->generic.serial = (uint32_t)((instance->yek >> 32) & 0x0FFFFFFF);
        instance->generic.btn = (uint8_t)((instance->yek >> 60) & 0x0F);

        uint32_t encrypted = (uint32_t)(instance->yek & 0xFFFFFFFF);
        instance->generic.cnt = mixer_decode(encrypted);
    }

    return ret;
}

static const char* subghz_protocol_kia_v5_get_name_button(uint8_t btn) {
    switch(btn) {
    case 0x01:
        return "Unlock";
    case 0x02:
        return "Lock";
    case 0x04:
        return "Trunk";
    case 0x08:
        return "Horn";
    default:
        return "Unknown";
    }
}

void subghz_protocol_decoder_kia_v5_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV5* instance = context;

    uint8_t kb[8];
    for(int i = 0; i < 8; i++) {
        kb[i] = (instance->generic.data >> ((7 - i) * 8)) & 0xFF;
    }

    uint8_t calculated_crc = kia_v5_calculate_crc(instance->yek);
    bool crc_valid = (instance->crc == calculated_crc);

    uint16_t seed = ((uint16_t)(instance->generic.btn & 0x0F) << 12) |
                    (instance->generic.serial & 0x0FFF);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%02X %02X %02X %02X %02X %02X %02X %02X\r\n"
        "Sn:%07lX Cnt:%04lX\r\n"
        "Btn:%02X [%s] Seed:%04X\r\n"
        "CRC:%u %s",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        kb[0], kb[1], kb[2], kb[3], kb[4], kb[5], kb[6], kb[7],
        (unsigned long)instance->generic.serial,
        (unsigned long)instance->generic.cnt,
        (unsigned)instance->generic.btn,
        subghz_protocol_kia_v5_get_name_button(instance->generic.btn),
        (unsigned)seed,
        (unsigned)instance->crc,
        crc_valid ? "(OK)" : "(FAIL)");
}

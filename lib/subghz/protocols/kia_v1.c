#include "kia_v1.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>

#define TAG "KiaV1"

#define KIA_V1_TOTAL_BURSTS       3
#define KIA_V1_INTER_BURST_GAP_US 25000
#define KIA_V1_HEADER_PULSES      90
#define KIA_V1_UPLOAD_CAPACITY                                                     \
    ((KIA_V1_TOTAL_BURSTS * ((KIA_V1_HEADER_PULSES * 2) + 1 + ((57U - 1U) * 2))) + \
     (KIA_V1_TOTAL_BURSTS - 1))

static const SubGhzBlockConst subghz_protocol_kia_v1_const = {
    .te_short = 800,
    .te_long = 1600,
    .te_delta = 200,
    .min_count_bit_for_found = 57,
};

struct SubGhzProtocolDecoderKiaV1 {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t header_count;
    ManchesterState manchester_saved_state;
    uint8_t crc;
    bool crc_check;
};

struct SubGhzProtocolEncoderKiaV1 {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    KiaV1DecoderStepReset = 0,
    KiaV1DecoderStepCheckPreamble,
    KiaV1DecoderStepDecodeData,
} KiaV1DecoderStep;

const SubGhzProtocolDecoder subghz_protocol_kia_v1_decoder = {
    .alloc = subghz_protocol_decoder_kia_v1_alloc,
    .free = subghz_protocol_decoder_kia_v1_free,

    .feed = subghz_protocol_decoder_kia_v1_feed,
    .reset = subghz_protocol_decoder_kia_v1_reset,

    .get_hash_data = subghz_protocol_decoder_kia_v1_get_hash_data,
    .serialize = subghz_protocol_decoder_kia_v1_serialize,
    .deserialize = subghz_protocol_decoder_kia_v1_deserialize,
    .get_string = subghz_protocol_decoder_kia_v1_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_kia_v1_encoder = {
    .alloc = subghz_protocol_encoder_kia_v1_alloc,
    .free = subghz_protocol_encoder_kia_v1_free,

    .deserialize = subghz_protocol_encoder_kia_v1_deserialize,
    .stop = subghz_protocol_encoder_kia_v1_stop,
    .yield = subghz_protocol_encoder_kia_v1_yield,
};

const SubGhzProtocol subghz_protocol_kia_v1 = {
    .name = SUBGHZ_PROTOCOL_KIA_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_kia_v1_decoder,
    .encoder = &subghz_protocol_kia_v1_encoder,
};

static void subghz_protocol_kia_v1_check_remote_controller(SubGhzProtocolDecoderKiaV1* instance);

static uint8_t kia_v1_crc4(const uint8_t* bytes, int count, uint8_t offset) {
    uint8_t crc = 0;

    for(int i = 0; i < count; i++) {
        uint8_t b = bytes[i];
        crc ^= ((b & 0x0F) ^ (b >> 4));
    }

    crc = (crc + offset) & 0x0F;
    return crc;
}

static void subghz_protocol_kia_v1_check_remote_controller(SubGhzProtocolDecoderKiaV1* instance) {
    instance->generic.serial = instance->generic.data >> 24;
    instance->generic.btn = (instance->generic.data >> 16) & 0xFF;
    instance->generic.cnt = ((instance->generic.data >> 4) & 0xF) << 8 |
                            ((instance->generic.data >> 8) & 0xFF);

    uint8_t cnt_high = (instance->generic.cnt >> 8) & 0xF;
    uint8_t char_data[7];
    char_data[0] = (instance->generic.serial >> 24) & 0xFF;
    char_data[1] = (instance->generic.serial >> 16) & 0xFF;
    char_data[2] = (instance->generic.serial >> 8) & 0xFF;
    char_data[3] = instance->generic.serial & 0xFF;
    char_data[4] = instance->generic.btn;
    char_data[5] = instance->generic.cnt & 0xFF;

    char_data[6] = cnt_high;
    uint8_t crc = kia_v1_crc4(char_data, 7, 1);

    instance->crc = crc;
    instance->crc_check = (crc == (instance->generic.data & 0xF));
}

static const char* subghz_protocol_kia_v1_get_name_button(uint8_t btn) {
    const char* name;
    switch(btn) {
    case 0x1:
        name = "Close";
        break;
    case 0x2:
        name = "Open";
        break;
    case 0x3:
        name = "Boot";
        break;
    default:
        name = "??";
        break;
    }
    return name;
}

void* subghz_protocol_encoder_kia_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderKiaV1* instance = calloc(1, sizeof(SubGhzProtocolEncoderKiaV1));
    if(!instance) {
        return NULL;
    }

    instance->base.protocol = &subghz_protocol_kia_v1;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    return instance;
}

void subghz_protocol_encoder_kia_v1_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV1* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_kia_v1_stop(void* context) {
    SubGhzProtocolEncoderKiaV1* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_kia_v1_yield(void* context) {
    SubGhzProtocolEncoderKiaV1* instance = context;

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

static void subghz_protocol_encoder_kia_v1_get_upload(SubGhzProtocolEncoderKiaV1* instance) {
    furi_assert(instance);
    if(instance->encoder.upload == NULL) return;
    size_t index = 0;
    LevelDuration* up = instance->encoder.upload;

    uint8_t cnt_high = (instance->generic.cnt >> 8) & 0xF;
    uint8_t char_data[7];
    char_data[0] = (instance->generic.serial >> 24) & 0xFF;
    char_data[1] = (instance->generic.serial >> 16) & 0xFF;
    char_data[2] = (instance->generic.serial >> 8) & 0xFF;
    char_data[3] = instance->generic.serial & 0xFF;
    char_data[4] = instance->generic.btn;
    char_data[5] = instance->generic.cnt & 0xFF;

    char_data[6] = cnt_high;
    uint8_t crc = kia_v1_crc4(char_data, 7, 1);

    instance->generic.data = (uint64_t)instance->generic.serial << 24 |
                             instance->generic.btn << 16 | (instance->generic.cnt & 0xFF) << 8 |
                             ((instance->generic.cnt >> 8) & 0xF) << 4 | crc;

    const uint32_t te_short = (uint32_t)subghz_protocol_kia_v1_const.te_short;
    const uint32_t te_long = (uint32_t)subghz_protocol_kia_v1_const.te_long;

    for(uint8_t burst = 0; burst < KIA_V1_TOTAL_BURSTS; burst++) {
        if(burst > 0) {
            up[index++] = level_duration_make(false, KIA_V1_INTER_BURST_GAP_US);
        }

        for(int i = 0; i < KIA_V1_HEADER_PULSES; i++) {
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
        KIA_V1_TOTAL_BURSTS,
        instance->encoder.size_upload,
        instance->generic.data_count_bit,
        instance->generic.data);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_kia_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV1* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    flipper_format_rewind(flipper_format);

    do {
        ret = subghz_block_generic_deserialize(&instance->generic, flipper_format);
        if(ret != SubGhzProtocolStatusOk) {
            FURI_LOG_E(TAG, "Missing or wrong Protocol");
            break;
        }

        instance->generic.data_count_bit = subghz_protocol_kia_v1_const.min_count_bit_for_found;

        if(instance->generic.data == 0) break;

        instance->generic.serial = instance->generic.data >> 24;
        instance->generic.btn = (instance->generic.data >> 16) & 0xFF;
        instance->generic.cnt = ((instance->generic.data >> 4) & 0xF) << 8 |
                                ((instance->generic.data >> 8) & 0xFF);

        instance->encoder.repeat = 10;

        if(instance->encoder.upload == NULL) {
            instance->encoder.size_upload = KIA_V1_UPLOAD_CAPACITY;
            instance->encoder.upload =
                malloc(instance->encoder.size_upload * sizeof(LevelDuration));
        }
        subghz_protocol_encoder_kia_v1_get_upload(instance);

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

void subghz_protocol_encoder_kia_v1_set_button(void* context, uint8_t button) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV1* instance = context;
    instance->generic.btn = button & 0xFF;
    subghz_protocol_encoder_kia_v1_get_upload(instance);
    FURI_LOG_I(TAG, "Button set to 0x%02X, upload rebuilt with new CRC", instance->generic.btn);
}

void subghz_protocol_encoder_kia_v1_set_counter(void* context, uint16_t counter) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV1* instance = context;
    instance->generic.cnt = counter & 0xFFF;
    subghz_protocol_encoder_kia_v1_get_upload(instance);
    FURI_LOG_I(
        TAG,
        "Counter set to 0x%03X, upload rebuilt with new CRC",
        (uint16_t)instance->generic.cnt);
}

void subghz_protocol_encoder_kia_v1_increment_counter(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV1* instance = context;
    instance->generic.cnt = (instance->generic.cnt + 1) & 0xFFF;
    subghz_protocol_encoder_kia_v1_get_upload(instance);
    FURI_LOG_I(
        TAG,
        "Counter incremented to 0x%03X, upload rebuilt with new CRC",
        (uint16_t)instance->generic.cnt);
}

uint16_t subghz_protocol_encoder_kia_v1_get_counter(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV1* instance = context;
    return instance->generic.cnt;
}

uint8_t subghz_protocol_encoder_kia_v1_get_button(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderKiaV1* instance = context;
    return instance->generic.btn;
}

void* subghz_protocol_decoder_kia_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderKiaV1* instance = calloc(1, sizeof(SubGhzProtocolDecoderKiaV1));
    if(!instance) {
        return NULL;
    }
    instance->base.protocol = &subghz_protocol_kia_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_kia_v1_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV1* instance = context;
    free(instance);
}

void subghz_protocol_decoder_kia_v1_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV1* instance = context;
    instance->decoder.parser_step = KiaV1DecoderStepReset;
}

void subghz_protocol_decoder_kia_v1_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV1* instance = context;

    ManchesterEvent event = ManchesterEventReset;

    switch(instance->decoder.parser_step) {
    case KiaV1DecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_kia_v1_const.te_long) <
                       subghz_protocol_kia_v1_const.te_delta)) {
            instance->decoder.parser_step = KiaV1DecoderStepCheckPreamble;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            manchester_advance(
                instance->manchester_saved_state,
                ManchesterEventReset,
                &instance->manchester_saved_state,
                NULL);
        }
        break;

    case KiaV1DecoderStepCheckPreamble:
        if(!level) {
            if((DURATION_DIFF(duration, subghz_protocol_kia_v1_const.te_long) <
                subghz_protocol_kia_v1_const.te_delta) &&
               (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v1_const.te_long) <
                subghz_protocol_kia_v1_const.te_delta)) {
                instance->header_count++;
                instance->decoder.te_last = duration;
            } else {
                instance->decoder.parser_step = KiaV1DecoderStepReset;
            }
        }
        if(instance->header_count > 70) {
            if((!level) &&
               (DURATION_DIFF(duration, subghz_protocol_kia_v1_const.te_short) <
                subghz_protocol_kia_v1_const.te_delta) &&
               (DURATION_DIFF(instance->decoder.te_last, subghz_protocol_kia_v1_const.te_long) <
                subghz_protocol_kia_v1_const.te_delta)) {
                instance->decoder.decode_count_bit = 1;
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->header_count = 0;
                instance->decoder.parser_step = KiaV1DecoderStepDecodeData;
            }
        }
        break;

    case KiaV1DecoderStepDecodeData:
        if((DURATION_DIFF(duration, subghz_protocol_kia_v1_const.te_short) <
            subghz_protocol_kia_v1_const.te_delta)) {
            event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
        } else if((DURATION_DIFF(duration, subghz_protocol_kia_v1_const.te_long) <
                   subghz_protocol_kia_v1_const.te_delta)) {
            event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
        } else {
            instance->decoder.parser_step = KiaV1DecoderStepReset;
            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            break;
        }

        if(event != ManchesterEventReset) {
            bool data;
            bool data_ok = manchester_advance(
                instance->manchester_saved_state, event, &instance->manchester_saved_state, &data);
            if(data_ok) {
                instance->decoder.decode_data = (instance->decoder.decode_data << 1) | data;
                instance->decoder.decode_count_bit++;
            }
        }

        if(instance->decoder.decode_count_bit == subghz_protocol_kia_v1_const.min_count_bit_for_found) {
            instance->generic.data = instance->decoder.decode_data;
            instance->generic.data_count_bit = instance->decoder.decode_count_bit;
            if(instance->base.callback)
                instance->base.callback(&instance->base, instance->base.context);

            instance->decoder.decode_data = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = KiaV1DecoderStepReset;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_kia_v1_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV1* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_kia_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV1* instance = context;

    subghz_protocol_kia_v1_check_remote_controller(instance);

    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_kia_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV1* instance = context;
    flipper_format_rewind(flipper_format);
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_kia_v1_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_kia_v1_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderKiaV1* instance = context;

    subghz_protocol_kia_v1_check_remote_controller(instance);
    uint32_t code_found_hi = instance->generic.data >> 32;
    uint32_t code_found_lo = instance->generic.data & 0xFFFFFFFF;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%06lX%08lX\r\n"
        "Serial:%08lX\r\n"
        "Cnt:%03lX CRC:%01X %s\r\n"
        "Btn:%02X:%s\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        code_found_hi,
        code_found_lo,
        instance->generic.serial,
        instance->generic.cnt,
        instance->crc,
        instance->crc_check ? "OK" : "WRONG",
        instance->generic.btn,
        subghz_protocol_kia_v1_get_name_button(instance->generic.btn));
}

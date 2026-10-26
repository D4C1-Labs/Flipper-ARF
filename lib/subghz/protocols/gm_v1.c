#include "gm_v1.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <furi.h>
#include <string.h>

#define GM_TE_SHORT       300U
#define GM_TE_LONG        500U
#define GM_TE_DELTA       120U
#define GM_PULSE_MIN_US   100U
#define GM_PULSE_MAX_US   1000U
#define GM_RESET_US       12000U

#define GM_WIRE_BYTES     14U
#define GM_WIRE_BITS      112U

#define GM_DEFAULT_REPEAT 5U
#define GM_DATA_FIELD     "Data"

#define GM_UPLOAD_CAPACITY (2U + (GM_WIRE_BITS * 2U) + 2U)

static const SubGhzBlockConst subghz_protocol_gm_const = {
    .te_short = GM_TE_SHORT,
    .te_long = GM_TE_LONG,
    .te_delta = GM_TE_DELTA,
    .min_count_bit_for_found = GM_WIRE_BITS,
};

typedef enum {
    GMDecoderStepReset = 0,
    GMDecoderStepData,
} GMDecoderStep;

struct SubGhzProtocolDecoderGM {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t bits[GM_WIRE_BYTES];
    uint16_t bit_count;

    uint8_t raw_data[GM_WIRE_BYTES];
    uint8_t last_raw_data[GM_WIRE_BYTES];
    bool last_raw_valid;
};

struct SubGhzProtocolEncoderGM {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[GM_WIRE_BYTES];
};

const SubGhzProtocolDecoder subghz_protocol_gm_v1_decoder = {
    .alloc = subghz_protocol_decoder_gm_v1_alloc,
    .free = subghz_protocol_decoder_gm_v1_free,
    .feed = subghz_protocol_decoder_gm_v1_feed,
    .reset = subghz_protocol_decoder_gm_v1_reset,
    .get_hash_data = subghz_protocol_decoder_gm_v1_get_hash_data,
    .serialize = subghz_protocol_decoder_gm_v1_serialize,
    .deserialize = subghz_protocol_decoder_gm_v1_deserialize,
    .get_string = subghz_protocol_decoder_gm_v1_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_gm_v1_encoder = {
    .alloc = subghz_protocol_encoder_gm_v1_alloc,
    .free = subghz_protocol_encoder_gm_v1_free,
    .deserialize = subghz_protocol_encoder_gm_v1_deserialize,
    .stop = subghz_protocol_encoder_gm_v1_stop,
    .yield = subghz_protocol_encoder_gm_v1_yield,
};

const SubGhzProtocol subghz_protocol_gm_v1 = {
    .name = GM_V1_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_gm_v1_decoder,
    .encoder = &subghz_protocol_gm_v1_encoder,
};

static uint32_t gm_add_nibbles(const uint8_t* b, size_t n) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < n; i++) {
        sum += (uint32_t)(b[i] >> 4U) + (uint32_t)(b[i] & 0x0FU);
    }
    return sum;
}

static uint32_t gm_add_bytes(const uint8_t* b, size_t n) {
    uint32_t sum = 0U;
    for(size_t i = 0U; i < n; i++) {
        sum += b[i];
    }
    return sum;
}

static bool gm_frame_valid(const uint8_t raw[GM_WIRE_BYTES]) {

    if(raw[0] != 0xFFU) return false;

    const uint32_t btn_ck = gm_add_nibbles(&raw[2], 1U);
    if(btn_ck == 0U || (btn_ck & 0x0FU) != 0U) return false;

    const uint32_t full_ck = gm_add_bytes(&raw[1], 13U);
    if(full_ck == 0U || (full_ck & 0xFFU) != 0U) return false;

    return true;
}

static uint32_t gm_id(const uint8_t raw[GM_WIRE_BYTES]) {
    return ((uint32_t)raw[3] << 24U) | ((uint32_t)raw[4] << 16U) | ((uint32_t)raw[5] << 8U) |
           raw[6];
}

static uint32_t gm_sequence(const uint8_t raw[GM_WIRE_BYTES]) {
    return ((uint32_t)raw[7] << 16U) | ((uint32_t)raw[8] << 8U) | raw[9];
}

static uint8_t gm_button(const uint8_t raw[GM_WIRE_BYTES]) {
    return (uint8_t)(raw[2] & 0x07U);
}

static void gm_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[GM_WIRE_BYTES]) {
    generic->serial = gm_id(raw);
    generic->btn = gm_button(raw);
    generic->cnt = (uint16_t)(gm_sequence(raw) & 0xFFFFU);
    generic->data_count_bit = GM_WIRE_BITS;

    generic->data = ((uint64_t)generic->serial << 16U) | generic->cnt;
}

static void gm_data_reset(SubGhzProtocolDecoderGM* instance) {
    instance->bit_count = 0U;
    memset(instance->bits, 0, sizeof(instance->bits));
}

static void gm_push_bit(SubGhzProtocolDecoderGM* instance, bool bit) {
    uint8_t carry = bit ? 1U : 0U;
    for(int i = (int)GM_WIRE_BYTES - 1; i >= 0; i--) {
        const uint8_t new_carry = (uint8_t)((instance->bits[i] >> 7U) & 1U);
        instance->bits[i] = (uint8_t)((instance->bits[i] << 1U) | carry);
        carry = new_carry;
    }
    if(instance->bit_count < 0xFFFFU) instance->bit_count++;
}

static void gm_report(SubGhzProtocolDecoderGM* instance) {
    if(instance->bit_count < GM_WIRE_BITS) return;
    if(!gm_frame_valid(instance->bits)) return;

    if(instance->last_raw_valid &&
       memcmp(instance->last_raw_data, instance->bits, GM_WIRE_BYTES) == 0) {
        return;
    }

    memcpy(instance->raw_data, instance->bits, GM_WIRE_BYTES);
    memcpy(instance->last_raw_data, instance->bits, GM_WIRE_BYTES);
    instance->last_raw_valid = true;
    gm_decode_fields(&instance->generic, instance->raw_data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void subghz_protocol_decoder_gm_v1_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    switch(instance->decoder.parser_step) {
    case GMDecoderStepReset:

        if(level && duration >= GM_PULSE_MIN_US && duration <= GM_PULSE_MAX_US) {
            instance->decoder.parser_step = GMDecoderStepData;
            gm_data_reset(instance);
        }
        break;

    case GMDecoderStepData:
        if(level) {

            if(duration < GM_PULSE_MIN_US || duration > GM_PULSE_MAX_US) {
                gm_report(instance);
                instance->decoder.parser_step = GMDecoderStepReset;
                gm_data_reset(instance);
            }

        } else {

            if(duration >= GM_RESET_US) {
                gm_report(instance);
                instance->decoder.parser_step = GMDecoderStepReset;
                gm_data_reset(instance);
            } else if(DURATION_DIFF(duration, GM_TE_SHORT) < GM_TE_DELTA) {
                gm_push_bit(instance, false);
                if(instance->bit_count >= GM_WIRE_BITS) gm_report(instance);
            } else if(DURATION_DIFF(duration, GM_TE_LONG) < GM_TE_DELTA) {
                gm_push_bit(instance, true);
                if(instance->bit_count >= GM_WIRE_BITS) gm_report(instance);
            } else {

                gm_report(instance);
                instance->decoder.parser_step = GMDecoderStepReset;
                gm_data_reset(instance);
            }
        }
        break;
    }
}

void* subghz_protocol_decoder_gm_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderGM* instance = calloc(1, sizeof(SubGhzProtocolDecoderGM));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_gm_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_gm_v1_reset(instance);
    return instance;
}

void subghz_protocol_decoder_gm_v1_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;
    free(instance);
}

void subghz_protocol_decoder_gm_v1_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;
    instance->decoder.parser_step = GMDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->last_raw_valid = false;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    memset(instance->last_raw_data, 0, sizeof(instance->last_raw_data));
    gm_data_reset(instance);
}

uint8_t subghz_protocol_decoder_gm_v1_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0U; i < GM_WIRE_BYTES; i++) {
        hash ^= instance->raw_data[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_gm_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, GM_DATA_FIELD, instance->raw_data, GM_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    if(!flipper_format_insert_or_update_uint32(
           flipper_format, "Serial", &instance->generic.serial, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t btn = instance->generic.btn;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t cnt = instance->generic.cnt;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_gm_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_gm_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, GM_DATA_FIELD, instance->raw_data, GM_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!gm_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    gm_decode_fields(&instance->generic, instance->raw_data);
    return SubGhzProtocolStatusOk;
}

static const char* gm_button_name(uint8_t button) {
    switch(button) {
    case 0x1U: return "Unlock";
    case 0x2U: return "Lock";
    case 0x3U: return "Trunk";
    case 0x4U: return "Panic";
    default:   return "?";
    }
}

void subghz_protocol_decoder_gm_v1_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderGM* instance = context;

    const uint8_t full_ck = instance->raw_data[13];
    const bool full_ck_ok = ((gm_add_bytes(&instance->raw_data[1], 13U) & 0xFFU) == 0U);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%08lX Btn:%X[%s]\r\n"
        "Seq:%06lX CS:%02X[%s]\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X\r\n"
        "     %02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        GM_WIRE_BITS,
        (unsigned long)instance->generic.serial,
        (unsigned)gm_button(instance->raw_data),
        gm_button_name(gm_button(instance->raw_data)),
        (unsigned long)gm_sequence(instance->raw_data),
        full_ck,
        full_ck_ok ? "OK" : "ERR",
        instance->raw_data[0],
        instance->raw_data[1],
        instance->raw_data[2],
        instance->raw_data[3],
        instance->raw_data[4],
        instance->raw_data[5],
        instance->raw_data[6],
        instance->raw_data[7],
        instance->raw_data[8],
        instance->raw_data[9],
        instance->raw_data[10],
        instance->raw_data[11],
        instance->raw_data[12],
        instance->raw_data[13]);
}

static bool gm_encoder_build_upload(SubGhzProtocolEncoderGM* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    size_t index = 0U;

    for(uint8_t bit_index = 0U; bit_index < GM_WIRE_BITS; bit_index++) {
        const bool bit =
            ((instance->raw_data[bit_index >> 3U] >> (7U - (bit_index & 7U))) & 1U) != 0U;

        upload[index++] = level_duration_make(true, GM_TE_SHORT);

        upload[index++] = level_duration_make(false, bit ? GM_TE_LONG : GM_TE_SHORT);
    }

    upload[index++] = level_duration_make(false, GM_RESET_US);

    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
    return true;
}

void* subghz_protocol_encoder_gm_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderGM* instance = calloc(1, sizeof(SubGhzProtocolEncoderGM));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_gm_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = GM_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_gm_v1_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderGM* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_gm_v1_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderGM* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

LevelDuration subghz_protocol_encoder_gm_v1_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderGM* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0 ||
       instance->encoder.size_upload == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0U;
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_gm_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderGM* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0U;

    flipper_format_rewind(flipper_format);
    FuriString* protocol_name = furi_string_alloc();
    bool protocol_ok = flipper_format_read_string(flipper_format, "Protocol", protocol_name) &&
                       furi_string_equal(protocol_name, instance->base.protocol->name);
    furi_string_free(protocol_name);
    if(!protocol_ok) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t bit_count = 0U;
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count, 1) ||
       bit_count != GM_WIRE_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, GM_DATA_FIELD, instance->raw_data, GM_WIRE_BYTES) ||
       !gm_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = gm_id(instance->raw_data);
    instance->generic.btn = gm_button(instance->raw_data);
    instance->generic.cnt = (uint16_t)(gm_sequence(instance->raw_data) & 0xFFFFU);

    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(instance->raw_data[2] & 0x07U);
    }
    subghz_custom_btn_set_max(0);

    instance->generic.data =
        ((uint64_t)instance->generic.serial << 16U) | instance->generic.cnt;
    instance->generic.data_count_bit = GM_WIRE_BITS;

    uint32_t repeat = GM_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? GM_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(GM_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!gm_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

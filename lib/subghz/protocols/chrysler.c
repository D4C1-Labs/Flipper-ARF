#include "chrysler.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include <string.h>

#define TAG "ChryslerV0"



#define CHRYSLER_V0_TE_SHORT         0x12C
#define CHRYSLER_V0_TE_DELTA         0x96
#define CHRYSLER_V0_TE_LONG_A        0xD48
#define CHRYSLER_V0_TE_LONG_B        0xE74
#define CHRYSLER_V0_TE_LONG_DELTA    0x190
#define CHRYSLER_V0_TE_GAP           0x1F40
#define CHRYSLER_V0_TE_ONE_SHORT     0x258
#define CHRYSLER_V0_FRAME_GAP        0x3CF0
#define CHRYSLER_V0_PREAMBLE_PAIRS   24U
#define CHRYSLER_V0_DECODE_BIT_COUNT 0x50

#define CHRYSLER_V0_UPLOAD_CAPACITY 0x200U

static const uint8_t chrysler_v0_xor_table[16] = {
    0x0F,
    0x02,
    0x40,
    0x0C,
    0x30,
    0x0E,
    0x70,
    0x08,
    0x10,
    0x0A,
    0x50,
    0xF4,
    0x2F,
    0xF6,
    0x6F,
    0xF0,
};

typedef enum {
    Chrysler_V0DecoderStepReset = 0,
    Chrysler_V0DecoderStepSeek = 1,
    Chrysler_V0DecoderStepData = 2,
} Chrysler_V0DecoderStep;

struct SubGhzProtocolDecoderChrysler {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint16_t packet_bit_count;
    uint8_t decoded_button;

    uint32_t te_last;
    uint8_t plain_a[9];
    uint8_t plain_b[9];

    uint8_t plain_a_present;
    uint8_t plain_b_present;

    uint8_t check_ok;
    uint32_t sn_b;

    uint16_t data_2;
    uint8_t seed;
};

struct SubGhzProtocolEncoderChrysler {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t tx_button;
    uint8_t plain_header;

    uint8_t plain_a[9];
    uint8_t plain_b[9];

    uint16_t data_2;
    uint8_t seed;
};

static void chrysler_v0_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[i] = (uint8_t)((data >> ((7U - i) * 8U)) & 0xFFU);
    }
}

static uint64_t chrysler_v0_bytes_to_u64_be(const uint8_t bytes[8]) {
    uint64_t data = 0;
    for(size_t i = 0; i < 8; i++) {
        data = (data << 8U) | bytes[i];
    }
    return data;
}

static uint8_t chrysler_v0_reverse6(uint32_t value) {
    uint8_t out = 0;
    uint8_t bits = 6;

    while(bits--) {
        out = (uint8_t)((out << 1U) | (value & 1U));
        value >>= 1U;
    }

    return out;
}

static void
    chrysler_v0_transform_block(const uint8_t in[9], uint8_t out[9], uint32_t key, uint8_t button) {
    uint8_t mask = chrysler_v0_xor_table[key & 0x0FU];
    if(button == 1U) {
        mask ^= (key & 1U) ? 0xF0U : 0x0FU;
    }

    for(size_t i = 0; i < 9; i++) {
        out[i] = in[i] ^ mask;
    }
}

static bool chrysler_v0_is_short(uint32_t duration) {
    return DURATION_DIFF(duration, CHRYSLER_V0_TE_SHORT) <= CHRYSLER_V0_TE_DELTA;
}

static bool chrysler_v0_is_long_mark(uint32_t duration) {
    return (DURATION_DIFF(duration, CHRYSLER_V0_TE_LONG_A) <= CHRYSLER_V0_TE_LONG_DELTA) ||
           (DURATION_DIFF(duration, CHRYSLER_V0_TE_LONG_B) <= CHRYSLER_V0_TE_LONG_DELTA);
}

static const char* chrysler_v0_get_button_name(uint8_t button) {
    switch(button) {
    case 1:
        return "Lock";
    case 2:
        return "Unlock";
    default:
        return "??";
    }
}

static uint32_t chrysler_v0_get_sn_b(const SubGhzProtocolDecoderChrysler* instance) {
    return instance->sn_b;
}

static void chrysler_v0_set_sn_b(SubGhzProtocolDecoderChrysler* instance, uint32_t sn_b) {
    instance->sn_b = sn_b;
}

static void chrysler_v0_decode_packet(SubGhzProtocolDecoderChrysler* instance) {
    uint8_t key[8];
    uint8_t encoded[9];
    uint8_t decoded[9];
    const uint16_t key2 = instance->data_2;

    chrysler_v0_u64_to_bytes_be(instance->generic.data, key);
    instance->seed = chrysler_v0_reverse6(key[0] >> 2U);

    const uint8_t b1_xor_b6 = key[6] ^ key[1];
    const bool msb_set = (key[0] & 0x80U) != 0U;

    if(msb_set) {
        const uint8_t key2_low = (uint8_t)(key2 & 0xFFU);
        instance->check_ok = (key[1] == key[5]) && (b1_xor_b6 == 0x62U);
        instance->decoded_button = (((uint8_t)(key2_low ^ key[4])) == 0x10U) ? 2U : 1U;
    } else {
        instance->check_ok = 0U;
        instance->decoded_button = 1U;

        if(((uint8_t)(key[1] ^ 0xC3U)) == key[5]) {
            if(b1_xor_b6 == 0x04U) {
                instance->check_ok = 1U;
            } else {
                instance->check_ok = (b1_xor_b6 == 0x08U);
                if(b1_xor_b6 == 0x08U) {
                    instance->decoded_button = 2U;
                } else {
                    FURI_LOG_D(TAG, "BtnDetect: unknown b1^b6=%02X (MSB=0)", b1_xor_b6);
                }
            }
        } else {
            if(b1_xor_b6 == 0x08U) {
                instance->decoded_button = 2U;
            } else if(b1_xor_b6 != 0x04U) {
                FURI_LOG_D(TAG, "BtnDetect: unknown b1^b6=%02X (MSB=0)", b1_xor_b6);
            }
        }
    }

    encoded[0] = key[1];
    encoded[1] = key[2];
    encoded[2] = key[3];
    encoded[3] = key[4];
    encoded[4] = key[5];
    encoded[5] = key[6];
    encoded[6] = key[7];
    encoded[7] = (uint8_t)(key2 >> 8U);
    encoded[8] = (uint8_t)(key2 & 0xFFU);
    chrysler_v0_transform_block(encoded, decoded, instance->seed, instance->decoded_button);

    if(instance->seed & 1U) {
        memcpy(instance->plain_b, decoded, sizeof(instance->plain_b));
        instance->plain_b_present = 1U;

        const uint32_t sn_b = ((uint32_t)decoded[0] << 24U) | ((uint32_t)decoded[1] << 16U) |
                              ((uint32_t)decoded[2] << 8U) | (uint32_t)decoded[7];
        chrysler_v0_set_sn_b(instance, sn_b);
    } else {
        memcpy(instance->plain_a, decoded, sizeof(instance->plain_a));
        instance->plain_a_present = 1U;

        instance->generic.cnt = ((uint32_t)decoded[0] << 24U) | ((uint32_t)decoded[1] << 16U) |
                                ((uint32_t)decoded[2] << 8U) | (uint32_t)decoded[3];
    }

    instance->generic.btn = instance->decoded_button;
}

static void chrysler_v0_decoder_commit(SubGhzProtocolDecoderChrysler* instance) {
    instance->packet_bit_count = CHRYSLER_V0_DECODE_BIT_COUNT;
    instance->decoder.decode_count_bit = CHRYSLER_V0_DECODE_BIT_COUNT;
    instance->generic.data_count_bit = CHRYSLER_V0_DECODE_BIT_COUNT;
    chrysler_v0_decode_packet(instance);

    if(instance->check_ok && instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

static uint8_t chrysler_v0_payload_get_bit(const uint8_t payload[10], uint8_t index) {
    const uint8_t byte = payload[index >> 3U];
    const uint8_t shift = 7U - (index & 7U);
    return (byte >> shift) & 1U;
}

static void chrysler_v0_build_payload(
    const uint8_t plain[9],
    uint8_t counter,
    uint8_t button,
    uint8_t header_low2,
    uint8_t out[10]) {
    uint8_t transformed[9];
    chrysler_v0_transform_block(plain, transformed, counter, button);

    out[0] = (uint8_t)((chrysler_v0_reverse6(counter) << 2U) | (header_low2 & 0x03U));
    memcpy(&out[1], transformed, sizeof(transformed));
}

static size_t chrysler_v0_build_upload(
    SubGhzProtocolEncoderChrysler* instance,
    const uint8_t payload_a[10],
    const uint8_t payload_b[10]) {
    size_t i = 0;
    LevelDuration* upload = instance->encoder.upload;

    for(size_t preamble = 0; preamble < CHRYSLER_V0_PREAMBLE_PAIRS; preamble++) {
        upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
        upload[i++] = level_duration_make(false, CHRYSLER_V0_TE_LONG_B);
    }

    upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
    upload[i++] = level_duration_make(false, CHRYSLER_V0_FRAME_GAP);

    for(uint8_t bit = 0; bit < 80; bit++) {
        const bool value = chrysler_v0_payload_get_bit(payload_a, bit);
        upload[i++] = level_duration_make(true, value ? CHRYSLER_V0_TE_ONE_SHORT : CHRYSLER_V0_TE_SHORT);
        upload[i++] = level_duration_make(false, value ? CHRYSLER_V0_TE_LONG_A : CHRYSLER_V0_TE_LONG_B);
    }

    upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
    upload[i++] = level_duration_make(false, CHRYSLER_V0_FRAME_GAP);

    for(size_t preamble = 0; preamble < CHRYSLER_V0_PREAMBLE_PAIRS; preamble++) {
        upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
        upload[i++] = level_duration_make(false, CHRYSLER_V0_TE_LONG_B);
    }

    upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
    upload[i++] = level_duration_make(false, CHRYSLER_V0_FRAME_GAP);

    for(uint8_t bit = 0; bit < 80; bit++) {
        const bool value = chrysler_v0_payload_get_bit(payload_b, bit);
        upload[i++] = level_duration_make(true, value ? CHRYSLER_V0_TE_ONE_SHORT : CHRYSLER_V0_TE_SHORT);
        upload[i++] = level_duration_make(false, value ? CHRYSLER_V0_TE_LONG_A : CHRYSLER_V0_TE_LONG_B);
    }

    upload[i++] = level_duration_make(true, CHRYSLER_V0_TE_SHORT);
    upload[i++] = level_duration_make(false, CHRYSLER_V0_FRAME_GAP);

    instance->encoder.size_upload = i;
    return i;
}

void subghz_protocol_decoder_chrysler_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_encoder_chrysler_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderChrysler* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_chrysler_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderChrysler* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_chrysler_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderChrysler* instance = context;
    if(instance->encoder.repeat == 0 || !instance->encoder.is_running || instance->encoder.size_upload == 0) {
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

uint8_t subghz_protocol_decoder_chrysler_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderChrysler* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8U) + 1U);
}

const SubGhzProtocolDecoder subghz_protocol_chrysler_v0_decoder = {
    .alloc = subghz_protocol_decoder_chrysler_alloc,
    .free = subghz_protocol_decoder_chrysler_free,
    .feed = subghz_protocol_decoder_chrysler_feed,
    .reset = subghz_protocol_decoder_chrysler_reset,
    .get_hash_data = subghz_protocol_decoder_chrysler_get_hash_data,
    .serialize = subghz_protocol_decoder_chrysler_serialize,
    .deserialize = subghz_protocol_decoder_chrysler_deserialize,
    .get_string = subghz_protocol_decoder_chrysler_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_chrysler_v0_encoder = {
    .alloc = subghz_protocol_encoder_chrysler_alloc,
    .free = subghz_protocol_encoder_chrysler_free,
    .deserialize = subghz_protocol_encoder_chrysler_deserialize,
    .stop = subghz_protocol_encoder_chrysler_stop,
    .yield = subghz_protocol_encoder_chrysler_yield,
};

const SubGhzProtocol subghz_protocol_chrysler = {
    .name = CHRYSLER_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_chrysler_v0_decoder,
    .encoder = &subghz_protocol_chrysler_v0_encoder,
};

void* subghz_protocol_encoder_chrysler_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolEncoderChrysler* instance =
        calloc(1, sizeof(SubGhzProtocolEncoderChrysler));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_chrysler;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 2;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;

    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_chrysler_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolEncoderChrysler* instance = context;
    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusError;
    }
    FuriString* tmp_str = furi_string_alloc();
    bool protocol_match = false;
    if(flipper_format_read_string(flipper_format, "Protocol", tmp_str) &&
       furi_string_equal(tmp_str, instance->base.protocol->name)) {
        protocol_match = true;
    }
    furi_string_free(tmp_str);
    if(!protocol_match) {
        return SubGhzProtocolStatusError;
    }

    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, CHRYSLER_V0_DECODE_BIT_COUNT);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusError;
    }

    uint16_t key2 = 0U;
    if(!flipper_format_read_hex(flipper_format, "Key_2", (uint8_t*)&key2, 2)) {
        return SubGhzProtocolStatusError;
    }

    key2 = __builtin_bswap16(key2);
    instance->data_2 = key2;

    uint8_t key[8];
    chrysler_v0_u64_to_bytes_be(instance->generic.data, key);
    const uint8_t b0 = key[0];

    instance->seed = chrysler_v0_reverse6(((uint32_t)(instance->generic.data >> 56U)) >> 2U);
    instance->plain_header = (uint8_t)((instance->generic.data >> 56U) & 0x03U);

    if((b0 & 0x80U) == 0U) {
        instance->tx_button = (((uint8_t)(key[1] ^ key[6])) == 0x08U) ? 2U : 1U;
    } else {
        instance->tx_button = (((uint8_t)(key2 & 0xFFU) ^ key[4]) == 0x10U) ? 2U : 1U;
    }

    const uint8_t original_button = instance->tx_button;

    uint8_t encoded[9];
    uint8_t generated[9];
    encoded[0] = key[1];
    encoded[1] = key[2];
    encoded[2] = key[3];
    encoded[3] = key[4];
    encoded[4] = key[5];
    encoded[5] = key[6];
    encoded[6] = key[7];
    encoded[7] = (uint8_t)(key2 >> 8U);
    encoded[8] = (uint8_t)(key2 & 0xFFU);
    chrysler_v0_transform_block(encoded, generated, instance->seed, instance->tx_button);

    if(flipper_format_rewind(flipper_format) &&
       flipper_format_read_hex(flipper_format, "Plain_A", instance->plain_a, 9)) {
        if(!(flipper_format_rewind(flipper_format) &&
             flipper_format_read_hex(flipper_format, "Plain_B", instance->plain_b, 9))) {
            memcpy(instance->plain_b, instance->plain_a, sizeof(instance->plain_b));
        }
    } else if(
        flipper_format_rewind(flipper_format) &&
        flipper_format_read_hex(flipper_format, "Plain_B", instance->plain_b, 9)) {
        memcpy(instance->plain_a, instance->plain_b, sizeof(instance->plain_a));
    } else {
        memcpy(instance->plain_a, generated, sizeof(instance->plain_a));
        memcpy(instance->plain_b, generated, sizeof(instance->plain_b));
    }

    uint32_t btn_u32 = 0;
    uint32_t cnt_u32 = instance->seed & 0x3FU;
    {
        uint32_t tmp = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Btn", &tmp, 1)) btn_u32 = tmp;
        tmp = 0;
        flipper_format_rewind(flipper_format);
        if(flipper_format_read_uint32(flipper_format, "Cnt", &tmp, 1)) cnt_u32 = tmp;
    }

    uint8_t tx_button = original_button;
    if(btn_u32 == 1U || btn_u32 == 2U) {
        tx_button = (uint8_t)btn_u32;
    }

    instance->tx_button = tx_button;
    if(tx_button != original_button) {
        instance->plain_a[5] ^= 0x0CU;
        instance->plain_b[3] ^= 0x30U;
    }

    {
        flipper_format_rewind(flipper_format);
        uint32_t repeat_val = 0;
        instance->encoder.repeat =
            flipper_format_read_uint32(flipper_format, "Repeat", &repeat_val, 1) ? repeat_val : 2;
    }

    uint32_t counter = cnt_u32 & 0x3FU;

    uint8_t counter_a = (uint8_t)(counter & 0x3FU);
    if(counter_a & 1U) {
        counter_a = (uint8_t)((counter_a - 1U) & 0x3FU);
    }
    instance->seed = counter_a;
    const uint8_t counter_b = (counter_a == 0U) ? 0x3FU : (uint8_t)(counter_a - 1U);

    uint8_t payload_a[10];
    uint8_t payload_b[10];
    chrysler_v0_build_payload(
        instance->plain_a, counter_a, instance->tx_button, instance->plain_header, payload_a);
    chrysler_v0_build_payload(
        instance->plain_b, counter_b, instance->tx_button, instance->plain_header, payload_b);

    instance->encoder.upload = malloc(CHRYSLER_V0_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    instance->encoder.size_upload = CHRYSLER_V0_UPLOAD_CAPACITY;
    chrysler_v0_build_upload(instance, payload_a, payload_b);

    instance->generic.data = chrysler_v0_bytes_to_u64_be(payload_a);
    instance->data_2 = ((uint16_t)payload_a[8] << 8U) | payload_a[9];

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusError;
    }
    if(!flipper_format_update_hex(flipper_format, "Key", payload_a, 8)) {
        return SubGhzProtocolStatusError;
    }

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusError;
    }

    uint16_t key2_out = __builtin_bswap16(instance->data_2);
    if(!flipper_format_update_hex(flipper_format, "Key_2", (uint8_t*)&key2_out, 2)) {
        return SubGhzProtocolStatusError;
    }

    instance->encoder.front = 0;
    instance->encoder.is_running = true;
    return status;
}

void* subghz_protocol_decoder_chrysler_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderChrysler* instance =
        calloc(1, sizeof(SubGhzProtocolDecoderChrysler));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_chrysler;
    instance->generic.protocol_name = instance->base.protocol->name;

    return instance;
}

void subghz_protocol_decoder_chrysler_reset(void* context) {
    furi_check(context);

    SubGhzProtocolDecoderChrysler* instance = context;
    instance->decoder.decode_data = 0;
    instance->data_2 = 0;
    instance->seed = 0;
    instance->decoder.parser_step = Chrysler_V0DecoderStepReset;
    instance->decoder.decode_count_bit = 0;
    instance->packet_bit_count = 0;
    instance->te_last = 0;
    instance->plain_a_present = 0;
    instance->plain_b_present = 0;
    instance->sn_b = 0;
}

void subghz_protocol_decoder_chrysler_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);

    SubGhzProtocolDecoderChrysler* instance = context;

    switch(instance->decoder.parser_step) {
    case Chrysler_V0DecoderStepReset:
        if(level && chrysler_v0_is_short(duration)) {
            instance->packet_bit_count = 0;
            instance->te_last = duration;
            instance->decoder.parser_step = Chrysler_V0DecoderStepSeek;
        }
        break;

    case Chrysler_V0DecoderStepSeek:
        if(level) {
            instance->te_last = duration;
            break;
        }

        if(chrysler_v0_is_long_mark(duration)) {
            if(chrysler_v0_is_short(instance->te_last)) {
                instance->packet_bit_count++;
            } else if(instance->packet_bit_count > 0x0F) {
                instance->data_2 = 0;
                instance->decoder.parser_step = Chrysler_V0DecoderStepData;
                instance->decoder.decode_data = 1;
                instance->decoder.decode_count_bit = 1;
            } else {
                instance->packet_bit_count = 0;
                instance->decoder.parser_step = Chrysler_V0DecoderStepSeek;
            }
            break;
        }

        if((duration > CHRYSLER_V0_TE_GAP) && (instance->packet_bit_count > 0x0F)) {
            instance->decoder.decode_data = 0;
            instance->data_2 = 0;
            instance->decoder.decode_count_bit = 0;
            instance->decoder.parser_step = Chrysler_V0DecoderStepData;
            break;
        }

        instance->decoder.parser_step = Chrysler_V0DecoderStepReset;
        instance->packet_bit_count = 0;
        break;

    case Chrysler_V0DecoderStepData: {
        if(level) {
            instance->te_last = duration;
            break;
        }

        const uint8_t count = instance->decoder.decode_count_bit;
        if(duration > CHRYSLER_V0_TE_GAP) {
            if(count > 0x4FU) {
                instance->generic.data = instance->decoder.decode_data;
                chrysler_v0_decoder_commit(instance);
            }

            instance->decoder.parser_step = Chrysler_V0DecoderStepReset;
            instance->packet_bit_count = 0;
            break;
        }

        uint8_t bit_value = 0;
        if(instance->te_last < CHRYSLER_V0_TE_SHORT) {
            if(!chrysler_v0_is_short(instance->te_last) || !chrysler_v0_is_long_mark(duration)) {
                if(count > 0x4FU) {
                    instance->generic.data = instance->decoder.decode_data;
                    chrysler_v0_decoder_commit(instance);
                }
                instance->decoder.parser_step = Chrysler_V0DecoderStepReset;
                instance->packet_bit_count = 0;
                break;
            }

            bit_value = 1U;
        } else {
            if(instance->te_last > 0x2EEU || !chrysler_v0_is_long_mark(duration)) {
                if(count > 0x4FU) {
                    instance->generic.data = instance->decoder.decode_data;
                    chrysler_v0_decoder_commit(instance);
                }
                instance->decoder.parser_step = Chrysler_V0DecoderStepReset;
                instance->packet_bit_count = 0;
                break;
            }

            bit_value = chrysler_v0_is_short(instance->te_last) ? 1U : 0U;
        }

        const uint8_t bit = bit_value ^ 1U;
        const uint8_t new_count = (uint8_t)(count + 1U);
        if(count <= 0x3FU) {
            instance->decoder.decode_data = (instance->decoder.decode_data << 1U) | bit;
            instance->decoder.decode_count_bit = new_count;
            break;
        }

        instance->data_2 = (uint16_t)((instance->data_2 << 1U) | bit);
        instance->decoder.decode_count_bit = new_count;
        if(new_count != CHRYSLER_V0_DECODE_BIT_COUNT) {
            break;
        }

        instance->generic.data = instance->decoder.decode_data;
        chrysler_v0_decoder_commit(instance);
        instance->decoder.decode_data = 0;
        instance->data_2 = 0;
        instance->decoder.decode_count_bit = 0;
        instance->decoder.parser_step = Chrysler_V0DecoderStepReset;
        instance->packet_bit_count = 0;
        break;
    }

    default:
        instance->decoder.parser_step = Chrysler_V0DecoderStepReset;
        break;
    }
}

SubGhzProtocolStatus subghz_protocol_decoder_chrysler_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);

    SubGhzProtocolDecoderChrysler* instance = context;
    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    const uint16_t key2 = __builtin_bswap16(instance->data_2);
    if(!flipper_format_write_hex(flipper_format, "Key_2", (const uint8_t*)&key2, 2)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    if(instance->plain_a_present) {
        if(!flipper_format_write_hex(flipper_format, "Plain_A", instance->plain_a, 9)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
    }

    if(instance->plain_b_present) {
        if(!flipper_format_write_hex(flipper_format, "Plain_B", instance->plain_b, 9)) {
            return SubGhzProtocolStatusErrorParserOthers;
        }
    }

    const uint32_t serial_value = instance->plain_b_present ? chrysler_v0_get_sn_b(instance) :
                                                              instance->generic.cnt;
    {
        uint32_t v = (uint32_t)(serial_value);
        flipper_format_rewind(flipper_format);
        if(!flipper_format_update_uint32(flipper_format, "Serial", &v, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Serial", &v, 1);
        }
    }
    {
        uint32_t v = (uint32_t)(instance->decoded_button);
        flipper_format_rewind(flipper_format);
        if(!flipper_format_update_uint32(flipper_format, "Btn", &v, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Btn", &v, 1);
        }
    }
    {
        uint32_t v = (uint32_t)(instance->seed);
        flipper_format_rewind(flipper_format);
        if(!flipper_format_update_uint32(flipper_format, "Cnt", &v, 1)) {
            flipper_format_rewind(flipper_format);
            flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &v, 1);
        }
    }

    return status;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_chrysler_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolDecoderChrysler* instance = context;
    SubGhzProtocolStatus status =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(status != SubGhzProtocolStatusOk) {
        return status;
    }

    if(!flipper_format_rewind(flipper_format)) {
        return SubGhzProtocolStatusError;
    }

    uint16_t key2 = 0U;
    if(!flipper_format_read_hex(flipper_format, "Key_2", (uint8_t*)&key2, 2)) {
        return SubGhzProtocolStatusError;
    }

    key2 = __builtin_bswap16(key2);
    instance->data_2 = key2;
    instance->packet_bit_count = CHRYSLER_V0_DECODE_BIT_COUNT;
    instance->decoder.decode_count_bit = CHRYSLER_V0_DECODE_BIT_COUNT;
    instance->generic.data_count_bit = CHRYSLER_V0_DECODE_BIT_COUNT;

    chrysler_v0_decode_packet(instance);

    if(flipper_format_rewind(flipper_format) &&
       flipper_format_read_hex(flipper_format, "Plain_A", instance->plain_a, 9)) {
        instance->plain_a_present = 1U;
        uint32_t sn_a = 0;
        memcpy(&sn_a, instance->plain_a, sizeof(sn_a));
        instance->generic.cnt = __builtin_bswap32(sn_a);
    }

    if(flipper_format_rewind(flipper_format) &&
       flipper_format_read_hex(flipper_format, "Plain_B", instance->plain_b, 9)) {
        instance->plain_b_present = 1U;
        const uint32_t sn_b =
            ((uint32_t)instance->plain_b[0] << 24U) | ((uint32_t)instance->plain_b[1] << 16U) |
            ((uint32_t)instance->plain_b[2] << 8U) | (uint32_t)instance->plain_b[7];
        chrysler_v0_set_sn_b(instance, sn_b);
    }

    instance->generic.protocol_name = instance->base.protocol->name;

    return status;
}

void subghz_protocol_decoder_chrysler_get_string(void* context, FuriString* output) {
    furi_check(context);

    SubGhzProtocolDecoderChrysler* instance = context;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n%016llX%04X\r\n",
        instance->generic.protocol_name,
        instance->packet_bit_count,
        instance->generic.data,
        instance->data_2);

    if(instance->plain_a_present) {
        if(instance->plain_b_present) {
            furi_string_cat_printf(
                output,
                "SnA:%08lX\r\nSnB:%08lX\r\n",
                instance->generic.cnt,
                chrysler_v0_get_sn_b(instance));
        } else {
            furi_string_cat_printf(output, "SnA:%08lX\r\n", instance->generic.cnt);
        }
    } else if(instance->plain_b_present) {
        furi_string_cat_printf(output, "SnB:%08lX\r\n", chrysler_v0_get_sn_b(instance));
    }

    furi_string_cat_printf(
        output,
        "Btn:%02X [%s] Cnt:%02X\r\nChk:%s",
        instance->decoded_button,
        chrysler_v0_get_button_name(instance->decoded_button),
        instance->seed,
        instance->check_ok ? "OK" : "ERR");
}

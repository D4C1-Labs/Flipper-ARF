#include "opel_v1.h"

#include "common.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <lib/toolbox/manchester_decoder.h>

#include <string.h>

#define OPEL_V1_FRAME_BITS     93U
#define OPEL_V1_FRAMING        0x02U
#define OPEL_V1_CIPHER_BITS    64U
#define OPEL_V1_BTN_TRUNK      0x20U
#define OPEL_V1_BTN_LOCK       0x04U
#define OPEL_V1_BTN_UNLOCK     0x08U
#define OPEL_V1_PLAINTEXT_TAIL 0x05U

static const SubGhzBlockConst subghz_protocol_opel_v1_const = {
    .te_short = 120,
    .te_long = 240,
    .te_delta = 70,
    .min_count_bit_for_found = 64,
};

static const uint8_t opel_v1_key[16] =
    {0x52, 0x6F, 0x62, 0x65, 0x72, 0x74, 0x42, 0x4F, 0x53, 0x43, 0x48, 0x34, 0x38, 0x33, 0x33, 0x31};

struct SubGhzProtocolDecoderOpelV1 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint64_t data_high;
    uint64_t data_low;
    uint16_t bit_count;

    uint8_t xtea_key[16];
    uint64_t cipher;
    uint32_t serial;
    uint8_t button;
    uint8_t burst;
    uint8_t tail;
    uint8_t check_byte;
    uint32_t count;
    bool valid;
};

SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderOpelV1);

static void opel_v1_xtea_decrypt(const uint8_t key[16], uint8_t block[8]) {
    uint32_t k[4];
    for(uint8_t i = 0; i < 4; i++) {
        k[i] = ((uint32_t)key[i * 4] << 24) | ((uint32_t)key[i * 4 + 1] << 16) |
               ((uint32_t)key[i * 4 + 2] << 8) | (uint32_t)key[i * 4 + 3];
    }

    uint32_t v0 = ((uint32_t)block[0] << 24) | ((uint32_t)block[1] << 16) |
                  ((uint32_t)block[2] << 8) | (uint32_t)block[3];
    uint32_t v1 = ((uint32_t)block[4] << 24) | ((uint32_t)block[5] << 16) |
                  ((uint32_t)block[6] << 8) | (uint32_t)block[7];

    uint32_t sum = 0xC6EF3720UL;
    for(uint8_t i = 0; i < 32; i++) {
        v1 -= ((sum + k[(sum >> 11) & 3]) ^ (v0 + (((v0 >> 5) ^ (v0 << 4)))));
        sum -= 0x9E3779B9UL;
        v0 -= ((sum + k[sum & 3]) ^ (v1 + (((v1 >> 5) ^ (v1 << 4)))));
    }

    block[0] = (uint8_t)(v0 >> 24);
    block[1] = (uint8_t)(v0 >> 16);
    block[2] = (uint8_t)(v0 >> 8);
    block[3] = (uint8_t)v0;
    block[4] = (uint8_t)(v1 >> 24);
    block[5] = (uint8_t)(v1 >> 16);
    block[6] = (uint8_t)(v1 >> 8);
    block[7] = (uint8_t)v1;
}

static const char* opel_v1_button_name(uint8_t button) {
    switch(button) {
    case OPEL_V1_BTN_TRUNK:
        return "Trunk";
    case OPEL_V1_BTN_LOCK:
        return "Lock";
    case OPEL_V1_BTN_UNLOCK:
        return "Unlock";
    default:
        return "Unknown";
    }
}

static void opel_v1_parse_fields(SubGhzProtocolDecoderOpelV1* instance) {
    uint8_t block[8];
    for(uint8_t i = 0; i < 8; i++) {
        block[i] = (uint8_t)(instance->cipher >> (56 - i * 8));
    }

    opel_v1_xtea_decrypt(instance->xtea_key, block);

    instance->check_byte = block[0];
    instance->button = block[1];
    instance->burst = block[2];
    instance->count = ((uint32_t)block[3] << 24) | ((uint32_t)block[4] << 16) |
                      ((uint32_t)block[5] << 8) | (uint32_t)block[6];
    instance->tail = block[7];
    instance->valid = (instance->tail == OPEL_V1_PLAINTEXT_TAIL);

    instance->decoder.decode_data = instance->cipher;
    instance->decoder.decode_count_bit = OPEL_V1_CIPHER_BITS;
    instance->generic.data = instance->cipher;
    instance->generic.data_count_bit = subghz_protocol_opel_v1_const.min_count_bit_for_found;
    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->button;
    instance->generic.cnt = instance->count;
}

static void opel_v1_accumulator_reset(SubGhzProtocolDecoderOpelV1* instance) {
    instance->data_high = 0;
    instance->data_low = 0;
    instance->bit_count = 0;
    manchester_advance(
        instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
}

static void opel_v1_add_bit(SubGhzProtocolDecoderOpelV1* instance, bool bit) {
    instance->data_high = (instance->data_high << 1) | ((instance->data_low >> 63) & 1U);
    instance->data_low = (instance->data_low << 1) | (bit ? 1U : 0U);
    if(instance->bit_count < OPEL_V1_FRAME_BITS) {
        instance->bit_count++;
    }
}

static bool opel_v1_try_finish(SubGhzProtocolDecoderOpelV1* instance) {
    if(instance->bit_count < OPEL_V1_FRAME_BITS) {
        return false;
    }

    const uint8_t framing = (uint8_t)((instance->data_high >> 24) & 0x1FU);
    if(framing != OPEL_V1_FRAMING) {
        return false;
    }

    instance->serial = (uint32_t)(instance->data_high & 0xFFFFFFUL);
    instance->cipher = instance->data_low;
    opel_v1_parse_fields(instance);
    return true;
}

const SubGhzProtocolDecoder subghz_protocol_opel_v1_decoder = {
    .alloc = subghz_protocol_decoder_opel_v1_alloc,
    .free = subghz_protocol_decoder_opel_v1_free,
    .feed = subghz_protocol_decoder_opel_v1_feed,
    .reset = subghz_protocol_decoder_opel_v1_reset,
    .get_hash_data = subghz_protocol_decoder_opel_v1_get_hash_data,
    .serialize = subghz_protocol_decoder_opel_v1_serialize,
    .deserialize = subghz_protocol_decoder_opel_v1_deserialize,
    .get_string = subghz_protocol_decoder_opel_v1_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_opel_v1_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};

const SubGhzProtocol subghz_protocol_opel_v1 = {
    .name = SUBGHZ_PROTOCOL_OPEL_V1_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_opel_v1_decoder,
    .encoder = &subghz_protocol_opel_v1_encoder,
};

void* subghz_protocol_decoder_opel_v1_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderOpelV1* instance = calloc(1, sizeof(SubGhzProtocolDecoderOpelV1));
    furi_check(instance);

    instance->base.protocol = &subghz_protocol_opel_v1;
    instance->generic.protocol_name = instance->base.protocol->name;
    memcpy(instance->xtea_key, opel_v1_key, sizeof(instance->xtea_key));
    subghz_protocol_decoder_opel_v1_reset(instance);

    return instance;
}

void subghz_protocol_decoder_opel_v1_free(void* context) {
    subghz_protocol_decoder_common_free(context);
}

void subghz_protocol_decoder_opel_v1_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderOpelV1* instance = context;

    instance->decoder.parser_step = 0;
    instance->decoder.te_last = 0;
    instance->manchester_state = ManchesterStateMid1;
    opel_v1_accumulator_reset(instance);
}

void subghz_protocol_decoder_opel_v1_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderOpelV1* instance = context;

    const uint32_t te_short = subghz_protocol_opel_v1_const.te_short;
    const uint32_t te_long = subghz_protocol_opel_v1_const.te_long;
    const uint32_t te_delta = subghz_protocol_opel_v1_const.te_delta;

    ManchesterEvent event;
    if(DURATION_DIFF(duration, te_short) < te_delta) {
        event = level ? ManchesterEventShortLow : ManchesterEventShortHigh;
    } else if(DURATION_DIFF(duration, te_long) < te_delta) {
        event = level ? ManchesterEventLongLow : ManchesterEventLongHigh;
    } else {
        opel_v1_accumulator_reset(instance);
        instance->decoder.te_last = duration;
        return;
    }

    bool data_bit = false;
    if(manchester_advance(
           instance->manchester_state, event, &instance->manchester_state, &data_bit)) {
        opel_v1_add_bit(instance, data_bit);
        if(opel_v1_try_finish(instance)) {
            if(instance->valid && instance->base.callback) {
                instance->base.callback(&instance->base, instance->base.context);
            }
            instance->data_high = 0;
            instance->data_low = 0;
            instance->bit_count = 0;
        }
    }

    instance->decoder.te_last = duration;
}

uint8_t subghz_protocol_decoder_opel_v1_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderOpelV1* instance = context;

    uint8_t hash = subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
    hash ^= (uint8_t)(instance->serial >> 16);
    hash ^= (uint8_t)(instance->serial >> 8);
    hash ^= (uint8_t)instance->serial;
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_opel_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderOpelV1* instance = context;

    instance->generic.data = instance->cipher;
    instance->generic.data_count_bit = subghz_protocol_opel_v1_const.min_count_bit_for_found;
    instance->generic.serial = instance->serial;
    instance->generic.btn = instance->button;
    instance->generic.cnt = instance->count;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    if(!flipper_format_insert_or_update_hex(
           flipper_format, "XteaKey", instance->xtea_key, sizeof(instance->xtea_key))) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    FuriString* display = furi_string_alloc();
    furi_string_printf(
        display, "%s - %s", instance->generic.protocol_name, opel_v1_button_name(instance->button));
    const bool disp_ok = flipper_format_insert_or_update_string_cstr(
        flipper_format, "Disp", furi_string_get_cstr(display));
    furi_string_free(display);
    if(!disp_ok) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t burst = instance->burst;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Burst", &burst, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_opel_v1_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderOpelV1* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_opel_v1_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    instance->cipher = instance->generic.data;
    instance->serial = instance->generic.serial & 0xFFFFFFUL;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, "XteaKey", instance->xtea_key, sizeof(instance->xtea_key))) {
        memcpy(instance->xtea_key, opel_v1_key, sizeof(instance->xtea_key));
    }

    uint32_t temp = 0;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Serial", &temp, 1)) {
        instance->serial = temp & 0xFFFFFFUL;
    }

    opel_v1_parse_fields(instance);
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_opel_v1_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderOpelV1* instance = context;

    furi_string_printf(
        output,
        "%s %dbit\r\n"
        "Sn:%06lX  Btn:%02X - %s\r\n"
        "Cipher:%016llX\r\n"
        "Cnt:%08lX [%s]\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)instance->serial,
        instance->button,
        opel_v1_button_name(instance->button),
        (unsigned long long)instance->cipher,
        (unsigned long)instance->count,
        instance->valid ? "OK" : "BAD");
}

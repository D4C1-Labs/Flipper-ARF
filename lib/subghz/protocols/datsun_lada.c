#include "datsun_lada.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include <lib/subghz/blocks/custom_btn_i.h>
#include <stdlib.h>
#include <string.h>

#define DATSUN_LADA_UPLOAD_CAPACITY 3200U
#define DATSUN_LADA_FRAME_BYTES     11U
#define DATSUN_LADA_BITS            88U
#define DATSUN_LADA_COUNTER_MASK    0x3FFFU

static const SubGhzBlockConst subghz_protocol_datsun_lada_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 110,
    .min_count_bit_for_found = DATSUN_LADA_BITS,
};

typedef enum {
    DatsunLadaDecoderStepReset = 0,
    DatsunLadaDecoderStepData,
} DatsunLadaDecoderStep;

struct SubGhzProtocolDecoderDatsunLada {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t receive_data[DATSUN_LADA_FRAME_BYTES];
    bool marker;
    bool glue;
    uint8_t raw[DATSUN_LADA_FRAME_BYTES];
    uint32_t state;
    bool learnable;
    uint8_t checkpoint[DATSUN_LADA_FRAME_BYTES];
    bool has_checkpoint;
};

struct SubGhzProtocolEncoderDatsunLada {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
    uint8_t raw[DATSUN_LADA_FRAME_BYTES];
    bool overflow;
};

static uint8_t datsun_lada_bit(uint8_t value, uint8_t index) {
    return (value >> index) & 1U;
}

static uint8_t datsun_lada_mask(uint8_t value) {
    return 1U << (value & 7U);
}

static bool datsun_lada_predicate(const uint8_t* r) {
    uint8_t acc = datsun_lada_bit(r[10], 5) | (datsun_lada_bit(r[10], 4) << 1) |
                  (datsun_lada_bit(r[10], 2) << 2);
    uint8_t flag = !!(datsun_lada_mask(acc) & (datsun_lada_bit(r[10], 1) ? 0x2C : 0x79));
    acc = datsun_lada_bit(r[9], 7) | (datsun_lada_bit(r[9], 3) << 1) |
          (datsun_lada_bit(r[9], 1) << 2);
    if(datsun_lada_mask(acc) & (datsun_lada_bit(r[9], 0) ? 0x66 : 0x71)) flag |= 2;
    acc = datsun_lada_bit(r[8], 6) | (datsun_lada_bit(r[8], 2) << 1) |
          datsun_lada_bit(r[8], 0) << 2;
    if(datsun_lada_mask(acc) & (datsun_lada_bit(r[7], 5) ? 0x66 : 0x71)) flag |= 4;
    acc = datsun_lada_bit(r[7], 3) | (datsun_lada_bit(r[7], 2) << 1) |
          (datsun_lada_bit(r[7], 0) << 2);
    if(datsun_lada_mask(acc) & (datsun_lada_bit(r[6], 6) ? 0x66 : 0x71)) flag |= 8;
    acc = datsun_lada_bit(r[6], 5) | (datsun_lada_bit(r[5], 4) << 1) |
          (datsun_lada_bit(r[5], 3) << 2);
    uint8_t slot;
    if(datsun_lada_mask(acc) & (datsun_lada_bit(r[5], 1) ? 0x2C : 0x79)) {
        slot = datsun_lada_bit(flag, 3) ? 0x79 : 7;
    } else {
        slot = datsun_lada_bit(flag, 3) ? 0x28 : 0x7B;
    }
    return (datsun_lada_mask(flag) & slot) != 0;
}

static uint8_t datsun_lada_loop_carry(const uint8_t* r) {
    uint8_t mixed = (r[10] & 0xB3) ^ (r[9] & 0x80) ^ (r[8] & 0x83) ^ (r[7] & 0x22) ^ (r[5] & 0x73);
    uint8_t carry = !!(datsun_lada_mask(mixed) & (datsun_lada_bit(mixed, 3) ? 0x69 : 0x96));
    mixed >>= 4;
    if(datsun_lada_mask(mixed) & (datsun_lada_bit(mixed, 3) ? 0x69 : 0x96)) carry ^= 1;
    return carry;
}

static uint32_t datsun_lada_read32(const uint8_t* r) {
    return r[0] | ((uint32_t)r[1] << 8) | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24);
}

static void datsun_lada_write32(uint8_t* r, uint32_t word) {
    for(size_t i = 0; i < 4; i++)
        r[i] = word >> (8 * i);
}

static void datsun_lada_shift_left(uint8_t* r, size_t first, size_t last, uint8_t carry) {
    for(size_t i = first; i <= last; i++) {
        uint8_t next = r[i] >> 7;
        r[i] = (r[i] << 1) | (carry & 1U);
        carry = next;
    }
}

static void datsun_lada_shift_right(uint8_t* r) {
    for(size_t pass = 0; pass < 2; pass++) {
        uint8_t carry = 0;
        for(size_t i = 6; i > 0; i--) {
            uint8_t next = r[i] & 1U;
            r[i] = (r[i] >> 1) | (carry << 7);
            carry = next;
        }
    }
}

static void datsun_lada_rounds(uint8_t* r, uint32_t* stack, uint32_t* state) {
    for(size_t i = 0; i < 32; i++) {
        uint8_t hit = datsun_lada_predicate(r) ^ (*stack >> 31) ^ (*state >> 31);
        datsun_lada_shift_left(r, 5, 10, hit);
        *stack <<= 1;
        *state = (*state << 1) | (*state >> 31);
    }
    for(size_t i = 0; i < 32; i++) {
        *stack = (*stack << 1) | datsun_lada_predicate(r);
        uint8_t carry = datsun_lada_loop_carry(r);
        datsun_lada_shift_left(r, 5, 10, carry);
    }
}

static void datsun_lada_restore(uint8_t* r, uint32_t stack, uint32_t state, uint8_t saved) {
    datsun_lada_write32(r + 1, stack);
    r[5] = state;
    r[6] = saved;
    datsun_lada_shift_left(r, 1, 6, 0);
    datsun_lada_shift_left(r, 1, 6, 0);
}

static bool datsun_lada_checksum(const uint8_t* frame) {
    uint8_t sum = 0;
    for(size_t i = 0; i < 10; i++)
        sum ^= frame[i];
    return sum == frame[10];
}

static bool datsun_lada_learn(const uint8_t* frame, uint32_t* state) {
    uint8_t r[11];
    memcpy(r, frame, sizeof(r));
    datsun_lada_shift_right(r);
    *state = r[5] | ((uint32_t)(r[6] & 3U) << 8) | ((uint32_t)((r[6] << 2) & 0xF0U) << 24);
    uint32_t stack = datsun_lada_read32(r + 7);
    r[6] = 0x46;
    r[5] = 6;
    datsun_lada_rounds(r, &stack, state);
    return stack == datsun_lada_read32(r + 1);
}

static bool datsun_lada_short_width(uint32_t width) {
    return width > 140U && width < 340U;
}

static bool datsun_lada_long_width(uint32_t width) {
    return width > 360 && width < 750;
}

static bool
    datsun_lada_receive(SubGhzProtocolDecoderDatsunLada* instance, bool level, uint32_t duration) {

    if(duration < 50) {
        instance->glue = true;
        return false;
    }
    uint32_t previous = instance->decoder.te_last;
    if(instance->glue) {
        duration += previous;
        instance->glue = false;
    }
    instance->decoder.te_last = duration;
    level = !level;
    if(instance->decoder.parser_step == DatsunLadaDecoderStepReset) {
        instance->decoder.decode_count_bit = 0;
        if(level && datsun_lada_short_width(previous) && datsun_lada_long_width(duration)) {
            instance->decoder.parser_step = DatsunLadaDecoderStepData;
            instance->marker = false;
            memset(instance->receive_data, 0, sizeof(instance->receive_data));
        }
        return false;
    }
    if(instance->decoder.decode_count_bit == 0 && datsun_lada_long_width(previous) &&
       datsun_lada_short_width(duration)) {
        instance->marker = false;
        return false;
    }
    if(datsun_lada_short_width(duration) || datsun_lada_long_width(duration)) {
        if(datsun_lada_short_width(duration) && instance->marker) {
            instance->marker = false;
            return false;
        }

        if(instance->decoder.decode_count_bit >= DATSUN_LADA_BITS) {
            instance->decoder.parser_step = DatsunLadaDecoderStepReset;
            instance->decoder.decode_count_bit = 0;
            return false;
        }
        size_t byte = 10 - (instance->decoder.decode_count_bit >> 3);
        if(level)
            instance->receive_data[byte] |= 1U << (7 - (instance->decoder.decode_count_bit & 7));
        instance->decoder.decode_count_bit++;
        instance->marker = true;
        return false;
    }
    bool found = instance->decoder.decode_count_bit == DATSUN_LADA_BITS &&
                 datsun_lada_checksum(instance->receive_data);
    instance->decoder.parser_step = DatsunLadaDecoderStepReset;
    instance->decoder.decode_count_bit = 0;
    return found;
}

static uint8_t datsun_lada_button_code(uint8_t selector) {
    static const uint8_t codes[] = {0x40, 0x80, 0x20, 0x20, 0x72, 0x6A, 0x5A, 0x32};
    return codes[selector & 7U];
}

static void datsun_lada_build(
    const uint8_t* learned,
    uint32_t state,
    uint16_t counter,
    uint8_t selector,
    uint8_t* air,
    uint8_t* tail_seed) {
    uint8_t r[11] = {0};
    memcpy(r + 7, learned + 7, 4);
    uint8_t code = datsun_lada_button_code(selector);
    uint8_t button = (learned[6] & 0x0FU) | code;
    r[6] = button;
    state = (state & 0x00FF0000U) | (counter & 0xFFU) |
            ((uint32_t)(((counter >> 6) & 0xFCU) | ((learned[6] >> 2) & 3U)) << 8) |
            ((uint32_t)(((state >> 24) & 0x0FU) | code) << 24);
    datsun_lada_shift_right(r);
    uint8_t saved = r[6];
    uint32_t stack = datsun_lada_read32(r + 7);
    r[6] = 0x46;
    r[5] = 6;
    datsun_lada_rounds(r, &stack, &state);
    datsun_lada_restore(r, stack, state, saved);
    if(stack == datsun_lada_read32(r + 1)) datsun_lada_restore(r, stack, state, saved);
    memcpy(r + 7, learned + 7, 4);
    r[6] = button;
    r[1] = (r[1] & 0xFEU) | 2U;
    r[0] = 0;
    for(size_t i = 1; i < 11; i++)
        r[0] ^= r[i];
    memcpy(air, r, 11);
    *tail_seed = state;
}

static void
    datsun_lada_emit(SubGhzProtocolEncoderDatsunLada* instance, bool level, uint32_t duration) {
    size_t size = instance->encoder.size_upload;
    LevelDuration* upload = instance->encoder.upload;
    if(size && level_duration_get_level(upload[size - 1]) == level) {
        upload[size - 1] =
            level_duration_make(level, duration + level_duration_get_duration(upload[size - 1]));
    } else if(size < DATSUN_LADA_UPLOAD_CAPACITY) {
        upload[size] = level_duration_make(level, duration);
        instance->encoder.size_upload++;
    } else {
        instance->overflow = true;
    }
}

static void datsun_lada_preamble(SubGhzProtocolEncoderDatsunLada* instance, size_t toggles) {
    datsun_lada_emit(instance, false, 2000);
    for(size_t i = 0; i < toggles; i++)
        datsun_lada_emit(instance, !(i & 1U), subghz_protocol_datsun_lada_const.te_short);
    datsun_lada_emit(instance, false, subghz_protocol_datsun_lada_const.te_long);
    datsun_lada_emit(instance, true, subghz_protocol_datsun_lada_const.te_short);
}

static void datsun_lada_window(
    SubGhzProtocolEncoderDatsunLada* instance,
    const uint8_t* frame,
    size_t bytes) {
    for(size_t i = 0; i < bytes * 8; i++) {
        bool value = datsun_lada_bit(frame[bytes - 1 - (i >> 3)], 7 - (i & 7));
        datsun_lada_emit(instance, !value, subghz_protocol_datsun_lada_const.te_short);
        datsun_lada_emit(instance, value, subghz_protocol_datsun_lada_const.te_short);
    }
}

static void datsun_lada_transmit(
    SubGhzProtocolEncoderDatsunLada* instance,
    const uint8_t* air,
    uint8_t tail_seed,
    uint8_t selector) {
    datsun_lada_emit(instance, true, 200);
    for(size_t i = 0; i < 2; i++) {
        datsun_lada_preamble(instance, 125);
        datsun_lada_window(instance, air, 11);
        datsun_lada_emit(instance, true, 4000);
    }
    size_t limit = selector == 3 ? 128 : 16;
    for(size_t step = 0; step < limit; step += 4) {
        uint8_t tail[3];
        tail[2] = ((air[6] >> 2) & 3U) + step;
        tail[1] = tail_seed;
        tail[0] = tail[1] ^ tail[2];
        datsun_lada_preamble(instance, 29);
        datsun_lada_window(instance, tail, 3);
        datsun_lada_emit(instance, true, 36000);
    }
}

const SubGhzProtocolDecoder subghz_protocol_datsun_lada_decoder = {
    .alloc = subghz_protocol_decoder_datsun_lada_alloc,
    .free = subghz_protocol_decoder_datsun_lada_free,
    .reset = subghz_protocol_decoder_datsun_lada_reset,
    .feed = subghz_protocol_decoder_datsun_lada_feed,
    .get_hash_data = subghz_protocol_decoder_datsun_lada_get_hash_data,
    .serialize = subghz_protocol_decoder_datsun_lada_serialize,
    .deserialize = subghz_protocol_decoder_datsun_lada_deserialize,
    .get_string = subghz_protocol_decoder_datsun_lada_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_datsun_lada_encoder = {
    .alloc = subghz_protocol_encoder_datsun_lada_alloc,
    .free = subghz_protocol_encoder_datsun_lada_free,
    .deserialize = subghz_protocol_encoder_datsun_lada_deserialize,
    .stop = subghz_protocol_encoder_datsun_lada_stop,
    .yield = subghz_protocol_encoder_datsun_lada_yield,
};

const SubGhzProtocol subghz_protocol_datsun_lada = {
    .name = SUBGHZ_PROTOCOL_DATSUN_LADA_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_datsun_lada_decoder,
    .encoder = &subghz_protocol_datsun_lada_encoder,
};

static uint16_t datsun_lada_counter(uint32_t state) {
    return (state & 0xFFU) | ((state >> 2) & 0x3F00U);
}

static void datsun_lada_fields(SubGhzProtocolDecoderDatsunLada* instance) {
    const uint8_t* raw = instance->raw;
    instance->generic.data = 0;
    for(size_t i = 0; i < 8; i++) {
        instance->generic.data = (instance->generic.data << 8) | raw[10 - i];
    }
    instance->generic.serial = ((uint32_t)raw[10] << 24) | ((uint32_t)raw[9] << 16) |
                               ((uint32_t)raw[8] << 8) | raw[7];
    instance->generic.btn = raw[6] & 0xF0U;
    instance->generic.data_count_bit = DATSUN_LADA_BITS;
    instance->learnable = datsun_lada_learn(raw, &instance->state);
    instance->generic.cnt = datsun_lada_counter(instance->state);
}

void* subghz_protocol_decoder_datsun_lada_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderDatsunLada* instance = calloc(1, sizeof(*instance));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_datsun_lada;
    instance->generic.protocol_name = SUBGHZ_PROTOCOL_DATSUN_LADA_NAME;
    return instance;
}

void subghz_protocol_decoder_datsun_lada_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_datsun_lada_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    memset(&instance->decoder, 0, sizeof(instance->decoder));
    memset(instance->receive_data, 0, sizeof(instance->receive_data));
    instance->marker = false;
    instance->glue = false;
}

void subghz_protocol_decoder_datsun_lada_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    if(datsun_lada_receive(instance, level, duration)) {
        instance->has_checkpoint = false;
        memcpy(instance->raw, instance->receive_data, sizeof(instance->raw));
        datsun_lada_fields(instance);
        if(instance->base.callback)
            instance->base.callback(&instance->base, instance->base.context);
    }
}

uint8_t subghz_protocol_decoder_datsun_lada_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    uint8_t hash = 0;
    for(size_t i = 0; i < sizeof(instance->raw); i++)
        hash = (hash * 33U) ^ instance->raw[i];
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_datsun_lada_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(status != SubGhzProtocolStatusOk) return status;
    if(!flipper_format_write_hex(flipper_format, "Raw", instance->raw, sizeof(instance->raw)))
        return SubGhzProtocolStatusErrorParserOthers;
    if(instance->has_checkpoint &&
       !flipper_format_write_hex(
           flipper_format, "Learned", instance->checkpoint, sizeof(instance->checkpoint)))
        return SubGhzProtocolStatusErrorParserOthers;
    return SubGhzProtocolStatusOk;
}

static SubGhzProtocolStatus
    datsun_lada_read(FlipperFormat* ff, SubGhzBlockGeneric* generic, uint8_t* raw) {
    FuriString* name = furi_string_alloc();
    flipper_format_rewind(ff);
    bool match = flipper_format_read_string(ff, "Protocol", name) &&
                 furi_string_equal_str(name, SUBGHZ_PROTOCOL_DATSUN_LADA_NAME);
    furi_string_free(name);
    if(!match) return SubGhzProtocolStatusErrorParserOthers;
    SubGhzProtocolStatus status =
        subghz_block_generic_deserialize_check_count_bit(generic, ff, DATSUN_LADA_BITS);
    if(status != SubGhzProtocolStatusOk) return status;
    if(generic->data_count_bit != DATSUN_LADA_BITS) return SubGhzProtocolStatusErrorValueBitCount;
    flipper_format_rewind(ff);
    if(!flipper_format_read_hex(ff, "Raw", raw, DATSUN_LADA_FRAME_BYTES) ||
       !datsun_lada_checksum(raw))
        return SubGhzProtocolStatusErrorParserOthers;
    return SubGhzProtocolStatusOk;
}

static bool datsun_lada_checkpoint(
    FlipperFormat* ff,
    const uint8_t* raw,
    uint8_t* checkpoint,
    uint32_t* state,
    uint32_t* counter) {
    flipper_format_rewind(ff);
    if(!flipper_format_read_hex(ff, "Learned", checkpoint, DATSUN_LADA_FRAME_BYTES) ||
       memcmp(raw + 7, checkpoint + 7, 4) != 0 || !datsun_lada_checksum(checkpoint) ||
       !datsun_lada_learn(checkpoint, state))
        return false;
    flipper_format_rewind(ff);
    if(!flipper_format_read_uint32(ff, "Cnt", counter, 1) || *counter > DATSUN_LADA_COUNTER_MASK)
        return false;
    uint8_t rebuilt[DATSUN_LADA_FRAME_BYTES];
    uint8_t seed;
    for(uint8_t selector = 0; selector < 8; selector++) {
        datsun_lada_build(checkpoint, *state, *counter, selector, rebuilt, &seed);
        if(memcmp(raw, rebuilt, sizeof(rebuilt)) == 0) return true;
    }
    return false;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_datsun_lada_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    SubGhzProtocolStatus status =
        datsun_lada_read(flipper_format, &instance->generic, instance->raw);
    instance->has_checkpoint = false;
    if(status == SubGhzProtocolStatusOk) {
        datsun_lada_fields(instance);
        uint32_t counter;
        if(datsun_lada_checkpoint(
               flipper_format, instance->raw, instance->checkpoint, &instance->state, &counter)) {
            instance->has_checkpoint = true;
            instance->learnable = true;
            instance->generic.cnt = counter;
        }
    }
    return status;
}

void subghz_protocol_decoder_datsun_lada_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderDatsunLada* instance = context;
    subghz_block_generic_global.btn_is_available = instance->learnable;
    subghz_block_generic_global.cnt_is_available = instance->learnable;
    subghz_block_generic_global.btn_length_bit = 8;
    subghz_block_generic_global.cnt_length_bit = 14;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    subghz_block_generic_global.current_cnt = instance->generic.cnt;
    if(subghz_custom_btn_get_original() == 0) subghz_custom_btn_set_original(instance->generic.btn);
    subghz_custom_btn_set_max(instance->learnable ? 4 : 0);
    furi_string_cat_printf(output, "%s 88bit\r\nKey:", instance->generic.protocol_name);
    for(size_t i = 0; i < sizeof(instance->raw); i++) {
        if(i == 6) furi_string_cat_printf(output, "\r\n");
        furi_string_cat_printf(output, "%02X", instance->raw[10 - i]);
    }
    furi_string_cat_printf(
        output,
        "\r\nSn:%08lX Btn:%02X\r\n",
        (unsigned long)instance->generic.serial,
        instance->generic.btn);
    if(instance->learnable) {
        furi_string_cat_printf(output, "Cnt:%04lX\r\n", (unsigned long)instance->generic.cnt);
    } else {
        furi_string_cat_printf(output, "Check failed\r\n");
    }
}

void* subghz_protocol_encoder_datsun_lada_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderDatsunLada* instance = calloc(1, sizeof(*instance));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_datsun_lada;
    instance->generic.protocol_name = SUBGHZ_PROTOCOL_DATSUN_LADA_NAME;
    instance->encoder.upload = malloc(DATSUN_LADA_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    return instance;
}

void subghz_protocol_encoder_datsun_lada_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderDatsunLada* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_datsun_lada_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderDatsunLada* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
}

LevelDuration subghz_protocol_encoder_datsun_lada_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderDatsunLada* instance = context;
    if(!instance->encoder.is_running || !instance->encoder.repeat) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }
    LevelDuration value = instance->encoder.upload[instance->encoder.front++];
    if(instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.front = 0;
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
    }
    return value;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_datsun_lada_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderDatsunLada* instance = context;
    subghz_protocol_encoder_datsun_lada_stop(instance);
    SubGhzProtocolStatus status =
        datsun_lada_read(flipper_format, &instance->generic, instance->raw);
    if(status != SubGhzProtocolStatusOk) return status;
    uint32_t state;
    uint8_t learned[11];
    uint32_t counter;
    if(!datsun_lada_checkpoint(flipper_format, instance->raw, learned, &state, &counter)) {
        memcpy(learned, instance->raw, sizeof(learned));
        if(!datsun_lada_learn(learned, &state)) return SubGhzProtocolStatusErrorParserOthers;
        counter = datsun_lada_counter(state);
    }
    uint8_t button = instance->raw[6] & 0xF0U;
    if(subghz_custom_btn_get_original() == 0) subghz_custom_btn_set_original(button);
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP:
        button = 0x40;
        break;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        button = 0x20;
        break;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        button = 0x60;
        break;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        button = 0x80;
        break;
    default:
        button = subghz_custom_btn_get_original();
        break;
    }
    subghz_block_generic_global_button_override_get(&button);

    uint8_t selector;
    switch(button) {
    case 0x40:
        selector = 0;
        break;
    case 0x80:
        selector = 1;
        break;
    case 0x20:
        selector = 3;
        break;
    case 0x60:
        selector = 5;
        break;
    default:
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t saved_selector = selector;
    flipper_format_rewind(flipper_format);
    if(flipper_format_read_uint32(flipper_format, "Selector", &saved_selector, 1) &&
       saved_selector < 8 &&
       datsun_lada_button_code(saved_selector) == datsun_lada_button_code(selector))
        selector = saved_selector;
    if(!subghz_block_generic_global_counter_override_get(&counter))
        counter += furi_hal_subghz_get_rolling_counter_mult();
    counter &= DATSUN_LADA_COUNTER_MASK;
    uint8_t air[11];
    uint8_t seed;
    datsun_lada_build(learned, state, counter, selector, air, &seed);
    instance->encoder.size_upload = 0;
    instance->overflow = false;
    datsun_lada_transmit(instance, air, seed, selector);
    if(instance->overflow) return SubGhzProtocolStatusErrorEncoderGetUpload;
    uint32_t repeat = 1;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = repeat;
    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(flipper_format, "Raw", air, sizeof(air)) ||
       !flipper_format_insert_or_update_hex(flipper_format, "Learned", learned, sizeof(learned)))
        return SubGhzProtocolStatusErrorParserOthers;
    uint8_t key[8];
    for(size_t i = 0; i < sizeof(key); i++)
        key[i] = air[10 - i];
    uint32_t saved_button = button;
    if(!flipper_format_insert_or_update_hex(flipper_format, "Key", key, sizeof(key)) ||
       !flipper_format_insert_or_update_uint32(flipper_format, "Btn", &saved_button, 1) ||
       !flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &counter, 1))
        return SubGhzProtocolStatusErrorParserOthers;
    subghz_block_generic_global.current_cnt = counter;
    subghz_block_generic_global.current_btn = button;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

#pragma once

#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/protocols/base.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/subghz/blocks/encoder.h>


extern const char SUBGHZ_PROTOCOL_COMMON_KEY[];
extern const char SUBGHZ_PROTOCOL_COMMON_SERIAL[];
extern const char SUBGHZ_PROTOCOL_COMMON_BTN[];
extern const char SUBGHZ_PROTOCOL_COMMON_CNT[];
extern const char SUBGHZ_PROTOCOL_COMMON_REPEAT[];
extern const char SUBGHZ_PROTOCOL_COMMON_PROTOCOL[];
extern const char SUBGHZ_PROTOCOL_COMMON_PRESET[];
extern const char SUBGHZ_PROTOCOL_COMMON_FREQUENCY[];
extern const char SUBGHZ_PROTOCOL_COMMON_TYPE[];

bool subghz_protocol_common_preset_name_is_custom_marker(const char* preset_name);

const char* subghz_protocol_common_get_short_preset_name(const char* preset_name);
bool subghz_protocol_common_parse_hex_u64_strict(const char* str, uint64_t* out_key);
bool subghz_protocol_common_flipper_read_hex_u64(
    FlipperFormat* flipper_format,
    const char* key,
    uint64_t* out_key);
void subghz_protocol_common_flipper_update_or_insert_u32(
    FlipperFormat* flipper_format,
    const char* key,
    uint32_t value);

SubGhzProtocolStatus
    subghz_protocol_common_verify_protocol_name(FlipperFormat* ff, const char* expected_name);

#define SUBGHZ_PROTOCOL_COMMON_FIELD_SERIAL 0x01U
#define SUBGHZ_PROTOCOL_COMMON_FIELD_BTN 0x02U
#define SUBGHZ_PROTOCOL_COMMON_FIELD_CNT 0x04U
#define SUBGHZ_PROTOCOL_COMMON_FIELD_TYPE 0x08U

SubGhzProtocolStatus subghz_protocol_common_encoder_read_bit(
    FlipperFormat* ff,
    const uint16_t* allowed_bits,
    size_t allowed_bits_count,
    uint32_t* out_bit);

void subghz_protocol_common_encoder_read_fields(
    FlipperFormat* ff,
    uint32_t* serial_out,
    uint32_t* btn_out,
    uint32_t* cnt_out,
    uint32_t* type_out);

#define SUBGHZ_PROTOCOL_COMMON_ENCODER_REPEAT_MAX 50U

uint32_t subghz_protocol_common_encoder_read_repeat(FlipperFormat* ff, uint32_t default_repeat);

SubGhzProtocolStatus subghz_protocol_common_serialize_fields(
    FlipperFormat* ff,
    uint32_t field_mask,
    uint32_t serial,
    uint32_t btn,
    uint32_t cnt,
    uint32_t type);

SubGhzProtocolStatus
    subghz_protocol_common_write_display(FlipperFormat* ff, const char* protocol_name, const char* suffix);

static inline size_t subghz_protocol_common_emit(
    LevelDuration* up,
    size_t i,
    size_t cap,
    bool level,
    uint32_t us) {
    if(i < cap) up[i++] = level_duration_make(level, us);
    return i;
}

size_t subghz_protocol_common_emit_merge(
    LevelDuration* up,
    size_t i,
    size_t cap,
    bool level,
    uint32_t us);

static inline size_t subghz_protocol_common_emit_manchester_bit(
    LevelDuration* up,
    size_t i,
    size_t cap,
    bool bit_value,
    uint32_t te) {
    i = subghz_protocol_common_emit(up, i, cap, bit_value, te);
    i = subghz_protocol_common_emit(up, i, cap, !bit_value, te);
    return i;
}

size_t subghz_protocol_common_emit_byte_manchester(
    LevelDuration* up,
    size_t i,
    size_t cap,
    uint8_t value,
    uint32_t te);

size_t subghz_protocol_common_emit_short_pairs(
    LevelDuration* up,
    size_t i,
    size_t cap,
    uint32_t te,
    size_t pair_count);

uint8_t subghz_protocol_common_reverse_bits8(uint8_t value);
void subghz_protocol_common_u64_to_bytes_be(uint64_t data, uint8_t bytes[8]);
uint64_t subghz_protocol_common_bytes_to_u64_be(const uint8_t bytes[8]);

static inline bool
    subghz_protocol_common_is_short(uint32_t duration, const SubGhzBlockConst* t) {
    return DURATION_DIFF(duration, t->te_short) < t->te_delta;
}

static inline bool
    subghz_protocol_common_is_long(uint32_t duration, const SubGhzBlockConst* t) {
    return DURATION_DIFF(duration, t->te_long) < t->te_delta;
}

static inline ManchesterEvent
    subghz_protocol_common_manchester_event(uint32_t duration, bool level, const SubGhzBlockConst* t) {
    if(DURATION_DIFF(duration, t->te_short) < t->te_delta) {
        return level ? ManchesterEventShortLow : ManchesterEventShortHigh;
    }
    if(DURATION_DIFF(duration, t->te_long) < t->te_delta) {
        return level ? ManchesterEventLongLow : ManchesterEventLongHigh;
    }
    return ManchesterEventReset;
}

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
} SubGhzProtocolCommonDecoder;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
} SubGhzProtocolCommonEncoder;

uint8_t subghz_protocol_common_decoder_hash_blocks(void* context);

void subghz_protocol_common_decoder_free_default(void* context);

#define SUBGHZ_PROTOCOL_COMMON_SHARED_UPLOAD_CAPACITY 2048U

void subghz_protocol_common_encoder_free(void* context);
void subghz_protocol_common_encoder_stop(void* context);
LevelDuration subghz_protocol_common_encoder_yield(void* context);

void subghz_protocol_common_encoder_buffer_ensure(void* context, size_t capacity);

LevelDuration* subghz_protocol_common_shared_upload_buffer(void);
size_t subghz_protocol_common_shared_upload_capacity(void);
void subghz_protocol_common_shared_upload_release(void);

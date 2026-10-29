#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_TREADMILL37_NAME "Treadmill37"

typedef struct SubGhzProtocolDecoderTreadmill37 SubGhzProtocolDecoderTreadmill37;
typedef struct SubGhzProtocolEncoderTreadmill37 SubGhzProtocolEncoderTreadmill37;

extern const SubGhzProtocolDecoder subghz_protocol_treadmill37_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_treadmill37_encoder;
extern const SubGhzProtocol subghz_protocol_treadmill37;

void* subghz_protocol_encoder_treadmill37_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_treadmill37_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_treadmill37_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_treadmill37_stop(void* context);

LevelDuration subghz_protocol_encoder_treadmill37_yield(void* context);

void* subghz_protocol_decoder_treadmill37_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_treadmill37_free(void* context);

void subghz_protocol_decoder_treadmill37_reset(void* context);

void subghz_protocol_decoder_treadmill37_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_treadmill37_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_treadmill37_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_treadmill37_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_treadmill37_get_string(void* context, FuriString* output);

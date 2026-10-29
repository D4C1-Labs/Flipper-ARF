#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME "LinearDelta3"

typedef struct SubGhzProtocolDecoderLinearDelta3 SubGhzProtocolDecoderLinearDelta3;
typedef struct SubGhzProtocolEncoderLinearDelta3 SubGhzProtocolEncoderLinearDelta3;

extern const SubGhzProtocolDecoder subghz_protocol_linear_delta3_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_linear_delta3_encoder;
extern const SubGhzProtocol subghz_protocol_linear_delta3;

void* subghz_protocol_encoder_linear_delta3_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_linear_delta3_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_linear_delta3_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_linear_delta3_stop(void* context);

LevelDuration subghz_protocol_encoder_linear_delta3_yield(void* context);

void* subghz_protocol_decoder_linear_delta3_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_linear_delta3_free(void* context);

void subghz_protocol_decoder_linear_delta3_reset(void* context);

void subghz_protocol_decoder_linear_delta3_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_linear_delta3_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_linear_delta3_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_linear_delta3_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_linear_delta3_get_string(void* context, FuriString* output);

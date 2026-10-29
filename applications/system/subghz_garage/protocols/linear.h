#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_LINEAR_NAME "Linear"

typedef struct SubGhzProtocolDecoderLinear SubGhzProtocolDecoderLinear;
typedef struct SubGhzProtocolEncoderLinear SubGhzProtocolEncoderLinear;

extern const SubGhzProtocolDecoder subghz_protocol_linear_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_linear_encoder;
extern const SubGhzProtocol subghz_protocol_linear;

void* subghz_protocol_encoder_linear_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_linear_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_linear_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_linear_stop(void* context);

LevelDuration subghz_protocol_encoder_linear_yield(void* context);

void* subghz_protocol_decoder_linear_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_linear_free(void* context);

void subghz_protocol_decoder_linear_reset(void* context);

void subghz_protocol_decoder_linear_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_linear_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_linear_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_linear_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_linear_get_string(void* context, FuriString* output);

#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_LEGRAND_NAME "Legrand"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzProtocolDecoderLegrand SubGhzProtocolDecoderLegrand;
typedef struct SubGhzProtocolEncoderLegrand SubGhzProtocolEncoderLegrand;

extern const SubGhzProtocolDecoder subghz_protocol_legrand_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_legrand_encoder;
extern const SubGhzProtocol subghz_protocol_legrand;

void* subghz_protocol_encoder_legrand_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_legrand_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_legrand_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_legrand_stop(void* context);

LevelDuration subghz_protocol_encoder_legrand_yield(void* context);

void* subghz_protocol_decoder_legrand_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_legrand_free(void* context);

void subghz_protocol_decoder_legrand_reset(void* context);

void subghz_protocol_decoder_legrand_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_legrand_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_legrand_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_legrand_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_legrand_get_string(void* context, FuriString* output);

#ifdef __cplusplus
}
#endif

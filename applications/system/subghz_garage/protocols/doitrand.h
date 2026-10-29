#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_DOITRAND_NAME "Doitrand"

typedef struct SubGhzProtocolDecoderDoitrand SubGhzProtocolDecoderDoitrand;
typedef struct SubGhzProtocolEncoderDoitrand SubGhzProtocolEncoderDoitrand;

extern const SubGhzProtocolDecoder subghz_protocol_doitrand_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_doitrand_encoder;
extern const SubGhzProtocol subghz_protocol_doitrand;

void* subghz_protocol_encoder_doitrand_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_doitrand_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_doitrand_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_doitrand_stop(void* context);

LevelDuration subghz_protocol_encoder_doitrand_yield(void* context);

void* subghz_protocol_decoder_doitrand_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_doitrand_free(void* context);

void subghz_protocol_decoder_doitrand_reset(void* context);

void subghz_protocol_decoder_doitrand_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_doitrand_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_doitrand_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_doitrand_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_doitrand_get_string(void* context, FuriString* output);

#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_PHOENIX_V2_NAME "Phoenix_V2"

typedef struct SubGhzProtocolDecoderPhoenix_V2 SubGhzProtocolDecoderPhoenix_V2;
typedef struct SubGhzProtocolEncoderPhoenix_V2 SubGhzProtocolEncoderPhoenix_V2;

extern const SubGhzProtocolDecoder subghz_protocol_phoenix_v2_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_phoenix_v2_encoder;
extern const SubGhzProtocol subghz_protocol_phoenix_v2;

void* subghz_protocol_encoder_phoenix_v2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_phoenix_v2_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_phoenix_v2_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_phoenix_v2_stop(void* context);

LevelDuration subghz_protocol_encoder_phoenix_v2_yield(void* context);

void* subghz_protocol_decoder_phoenix_v2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_phoenix_v2_free(void* context);

void subghz_protocol_decoder_phoenix_v2_reset(void* context);

void subghz_protocol_decoder_phoenix_v2_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_phoenix_v2_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_phoenix_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_phoenix_v2_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_phoenix_v2_get_string(void* context, FuriString* output);

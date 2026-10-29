#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_KEYFINDER2_NAME "KeyFinder2"

typedef struct SubGhzProtocolDecoderKeyFinder2 SubGhzProtocolDecoderKeyFinder2;
typedef struct SubGhzProtocolEncoderKeyFinder2 SubGhzProtocolEncoderKeyFinder2;

extern const SubGhzProtocolDecoder subghz_protocol_keyfinder2_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_keyfinder2_encoder;
extern const SubGhzProtocol subghz_protocol_keyfinder2;

void* subghz_protocol_encoder_keyfinder2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_keyfinder2_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_keyfinder2_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_keyfinder2_stop(void* context);

LevelDuration subghz_protocol_encoder_keyfinder2_yield(void* context);

void* subghz_protocol_decoder_keyfinder2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_keyfinder2_free(void* context);

void subghz_protocol_decoder_keyfinder2_reset(void* context);

void subghz_protocol_decoder_keyfinder2_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_keyfinder2_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_keyfinder2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_keyfinder2_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_keyfinder2_get_string(void* context, FuriString* output);

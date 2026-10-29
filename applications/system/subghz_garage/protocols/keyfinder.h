#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_KEYFINDER_NAME "KeyFinder"

typedef struct SubGhzProtocolDecoderKeyFinder SubGhzProtocolDecoderKeyFinder;
typedef struct SubGhzProtocolEncoderKeyFinder SubGhzProtocolEncoderKeyFinder;

extern const SubGhzProtocolDecoder subghz_protocol_keyfinder_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_keyfinder_encoder;
extern const SubGhzProtocol subghz_protocol_keyfinder;

void* subghz_protocol_encoder_keyfinder_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_keyfinder_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_keyfinder_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_keyfinder_stop(void* context);

LevelDuration subghz_protocol_encoder_keyfinder_yield(void* context);

void* subghz_protocol_decoder_keyfinder_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_keyfinder_free(void* context);

void subghz_protocol_decoder_keyfinder_reset(void* context);

void subghz_protocol_decoder_keyfinder_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_keyfinder_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_keyfinder_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_keyfinder_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_keyfinder_get_string(void* context, FuriString* output);

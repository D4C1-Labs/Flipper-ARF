#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_CLEMSA_NAME "Clemsa"

typedef struct SubGhzProtocolDecoderClemsa SubGhzProtocolDecoderClemsa;
typedef struct SubGhzProtocolEncoderClemsa SubGhzProtocolEncoderClemsa;

extern const SubGhzProtocolDecoder subghz_protocol_clemsa_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_clemsa_encoder;
extern const SubGhzProtocol subghz_protocol_clemsa;

void* subghz_protocol_encoder_clemsa_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_clemsa_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_clemsa_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_clemsa_stop(void* context);

LevelDuration subghz_protocol_encoder_clemsa_yield(void* context);

void* subghz_protocol_decoder_clemsa_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_clemsa_free(void* context);

void subghz_protocol_decoder_clemsa_reset(void* context);

void subghz_protocol_decoder_clemsa_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_clemsa_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_clemsa_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_clemsa_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_clemsa_get_string(void* context, FuriString* output);

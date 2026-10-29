#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_HOLLARM_NAME "Hollarm"

typedef struct SubGhzProtocolDecoderHollarm SubGhzProtocolDecoderHollarm;
typedef struct SubGhzProtocolEncoderHollarm SubGhzProtocolEncoderHollarm;

extern const SubGhzProtocolDecoder subghz_protocol_hollarm_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_hollarm_encoder;
extern const SubGhzProtocol subghz_protocol_hollarm;

void* subghz_protocol_encoder_hollarm_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_hollarm_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_hollarm_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_hollarm_stop(void* context);

LevelDuration subghz_protocol_encoder_hollarm_yield(void* context);

void* subghz_protocol_decoder_hollarm_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_hollarm_free(void* context);

void subghz_protocol_decoder_hollarm_reset(void* context);

void subghz_protocol_decoder_hollarm_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_hollarm_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_hollarm_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_hollarm_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_hollarm_get_string(void* context, FuriString* output);

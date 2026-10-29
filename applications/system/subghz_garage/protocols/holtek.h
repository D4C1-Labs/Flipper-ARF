#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_HOLTEK_NAME "Holtek"

typedef struct SubGhzProtocolDecoderHoltek SubGhzProtocolDecoderHoltek;
typedef struct SubGhzProtocolEncoderHoltek SubGhzProtocolEncoderHoltek;

extern const SubGhzProtocolDecoder subghz_protocol_holtek_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_holtek_encoder;
extern const SubGhzProtocol subghz_protocol_holtek;

void* subghz_protocol_encoder_holtek_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_holtek_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_holtek_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_holtek_stop(void* context);

LevelDuration subghz_protocol_encoder_holtek_yield(void* context);

void* subghz_protocol_decoder_holtek_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_holtek_free(void* context);

void subghz_protocol_decoder_holtek_reset(void* context);

void subghz_protocol_decoder_holtek_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_holtek_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_holtek_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_holtek_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_holtek_get_string(void* context, FuriString* output);

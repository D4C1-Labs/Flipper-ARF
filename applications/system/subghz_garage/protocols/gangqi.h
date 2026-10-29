#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_GANGQI_NAME "GangQi"

typedef struct SubGhzProtocolDecoderGangQi SubGhzProtocolDecoderGangQi;
typedef struct SubGhzProtocolEncoderGangQi SubGhzProtocolEncoderGangQi;

extern const SubGhzProtocolDecoder subghz_protocol_gangqi_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_gangqi_encoder;
extern const SubGhzProtocol subghz_protocol_gangqi;

void* subghz_protocol_encoder_gangqi_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_gangqi_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_gangqi_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_gangqi_stop(void* context);

LevelDuration subghz_protocol_encoder_gangqi_yield(void* context);

void* subghz_protocol_decoder_gangqi_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_gangqi_free(void* context);

void subghz_protocol_decoder_gangqi_reset(void* context);

void subghz_protocol_decoder_gangqi_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_gangqi_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_gangqi_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_gangqi_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_gangqi_get_string(void* context, FuriString* output);

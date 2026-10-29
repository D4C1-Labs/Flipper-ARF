#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_MAGELLAN_NAME "Magellan"

typedef struct SubGhzProtocolDecoderMagellan SubGhzProtocolDecoderMagellan;
typedef struct SubGhzProtocolEncoderMagellan SubGhzProtocolEncoderMagellan;

extern const SubGhzProtocolDecoder subghz_protocol_magellan_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_magellan_encoder;
extern const SubGhzProtocol subghz_protocol_magellan;

void* subghz_protocol_encoder_magellan_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_magellan_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_magellan_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_magellan_stop(void* context);

LevelDuration subghz_protocol_encoder_magellan_yield(void* context);

void* subghz_protocol_decoder_magellan_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_magellan_free(void* context);

void subghz_protocol_decoder_magellan_reset(void* context);

void subghz_protocol_decoder_magellan_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_magellan_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_magellan_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_magellan_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_magellan_get_string(void* context, FuriString* output);

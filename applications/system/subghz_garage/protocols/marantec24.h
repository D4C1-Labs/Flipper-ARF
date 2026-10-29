#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_MARANTEC24_NAME "Marantec24"

typedef struct SubGhzProtocolDecoderMarantec24 SubGhzProtocolDecoderMarantec24;
typedef struct SubGhzProtocolEncoderMarantec24 SubGhzProtocolEncoderMarantec24;

extern const SubGhzProtocolDecoder subghz_protocol_marantec24_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_marantec24_encoder;
extern const SubGhzProtocol subghz_protocol_marantec24;

void* subghz_protocol_encoder_marantec24_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_marantec24_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_marantec24_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_marantec24_stop(void* context);

LevelDuration subghz_protocol_encoder_marantec24_yield(void* context);

void* subghz_protocol_decoder_marantec24_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_marantec24_free(void* context);

void subghz_protocol_decoder_marantec24_reset(void* context);

void subghz_protocol_decoder_marantec24_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_marantec24_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_marantec24_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_marantec24_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_marantec24_get_string(void* context, FuriString* output);

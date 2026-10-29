#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_HAY21_NAME "Hay21"

typedef struct SubGhzProtocolDecoderHay21 SubGhzProtocolDecoderHay21;
typedef struct SubGhzProtocolEncoderHay21 SubGhzProtocolEncoderHay21;

extern const SubGhzProtocolDecoder subghz_protocol_hay21_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_hay21_encoder;
extern const SubGhzProtocol subghz_protocol_hay21;

void* subghz_protocol_encoder_hay21_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_hay21_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_hay21_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_hay21_stop(void* context);

LevelDuration subghz_protocol_encoder_hay21_yield(void* context);

void* subghz_protocol_decoder_hay21_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_hay21_free(void* context);

void subghz_protocol_decoder_hay21_reset(void* context);

void subghz_protocol_decoder_hay21_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_hay21_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_hay21_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_hay21_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_hay21_get_string(void* context, FuriString* output);

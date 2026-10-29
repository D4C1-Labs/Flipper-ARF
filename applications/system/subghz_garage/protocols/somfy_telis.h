#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME "Somfy Telis"

typedef struct SubGhzProtocolDecoderSomfyTelis SubGhzProtocolDecoderSomfyTelis;
typedef struct SubGhzProtocolEncoderSomfyTelis SubGhzProtocolEncoderSomfyTelis;

extern const SubGhzProtocolDecoder subghz_protocol_somfy_telis_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_somfy_telis_encoder;
extern const SubGhzProtocol subghz_protocol_somfy_telis;

void* subghz_protocol_encoder_somfy_telis_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_somfy_telis_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_somfy_telis_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_somfy_telis_stop(void* context);

LevelDuration subghz_protocol_encoder_somfy_telis_yield(void* context);

void* subghz_protocol_decoder_somfy_telis_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_somfy_telis_free(void* context);

void subghz_protocol_decoder_somfy_telis_reset(void* context);

void subghz_protocol_decoder_somfy_telis_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_somfy_telis_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_somfy_telis_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_somfy_telis_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_somfy_telis_get_string(void* context, FuriString* output);

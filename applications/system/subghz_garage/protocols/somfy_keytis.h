#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME "Somfy Keytis"

typedef struct SubGhzProtocolDecoderSomfyKeytis SubGhzProtocolDecoderSomfyKeytis;
typedef struct SubGhzProtocolEncoderSomfyKeytis SubGhzProtocolEncoderSomfyKeytis;

extern const SubGhzProtocolDecoder subghz_protocol_somfy_keytis_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_somfy_keytis_encoder;
extern const SubGhzProtocol subghz_protocol_somfy_keytis;

void* subghz_protocol_encoder_somfy_keytis_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_somfy_keytis_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_somfy_keytis_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_somfy_keytis_stop(void* context);

LevelDuration subghz_protocol_encoder_somfy_keytis_yield(void* context);

void* subghz_protocol_decoder_somfy_keytis_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_somfy_keytis_free(void* context);

void subghz_protocol_decoder_somfy_keytis_reset(void* context);

void subghz_protocol_decoder_somfy_keytis_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_somfy_keytis_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_somfy_keytis_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_somfy_keytis_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_somfy_keytis_get_string(void* context, FuriString* output);

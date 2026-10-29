#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_INTERTECHNO_V3_NAME "Intertechno_V3"

typedef struct SubGhzProtocolDecoderIntertechno_V3 SubGhzProtocolDecoderIntertechno_V3;
typedef struct SubGhzProtocolEncoderIntertechno_V3 SubGhzProtocolEncoderIntertechno_V3;

extern const SubGhzProtocolDecoder subghz_protocol_intertechno_v3_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_intertechno_v3_encoder;
extern const SubGhzProtocol subghz_protocol_intertechno_v3;

void* subghz_protocol_encoder_intertechno_v3_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_intertechno_v3_free(void* context);

SubGhzProtocolStatus subghz_protocol_encoder_intertechno_v3_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void subghz_protocol_encoder_intertechno_v3_stop(void* context);

LevelDuration subghz_protocol_encoder_intertechno_v3_yield(void* context);

void* subghz_protocol_decoder_intertechno_v3_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_intertechno_v3_free(void* context);

void subghz_protocol_decoder_intertechno_v3_reset(void* context);

void subghz_protocol_decoder_intertechno_v3_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_intertechno_v3_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_intertechno_v3_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus subghz_protocol_decoder_intertechno_v3_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void subghz_protocol_decoder_intertechno_v3_get_string(void* context, FuriString* output);

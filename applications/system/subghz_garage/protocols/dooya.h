#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_DOOYA_NAME "Dooya"

typedef struct SubGhzProtocolDecoderDooya SubGhzProtocolDecoderDooya;
typedef struct SubGhzProtocolEncoderDooya SubGhzProtocolEncoderDooya;

extern const SubGhzProtocolDecoder subghz_protocol_dooya_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_dooya_encoder;
extern const SubGhzProtocol subghz_protocol_dooya;

void* subghz_protocol_encoder_dooya_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_dooya_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_dooya_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_dooya_stop(void* context);

LevelDuration subghz_protocol_encoder_dooya_yield(void* context);

void* subghz_protocol_decoder_dooya_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_dooya_free(void* context);

void subghz_protocol_decoder_dooya_reset(void* context);

void subghz_protocol_decoder_dooya_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_dooya_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_dooya_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_dooya_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_dooya_get_string(void* context, FuriString* output);

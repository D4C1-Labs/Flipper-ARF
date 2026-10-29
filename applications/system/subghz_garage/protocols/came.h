#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_CAME_NAME "CAME"

typedef struct SubGhzProtocolDecoderCame SubGhzProtocolDecoderCame;
typedef struct SubGhzProtocolEncoderCame SubGhzProtocolEncoderCame;

extern const SubGhzProtocolDecoder subghz_protocol_came_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_came_encoder;
extern const SubGhzProtocol subghz_protocol_came;

void* subghz_protocol_encoder_came_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_came_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_came_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_came_stop(void* context);

LevelDuration subghz_protocol_encoder_came_yield(void* context);

void* subghz_protocol_decoder_came_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_came_free(void* context);

void subghz_protocol_decoder_came_reset(void* context);

void subghz_protocol_decoder_came_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_came_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_came_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_came_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_came_get_string(void* context, FuriString* output);

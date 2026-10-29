#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_CAME_TWEE_NAME "CAME TWEE"

typedef struct SubGhzProtocolDecoderCameTwee SubGhzProtocolDecoderCameTwee;
typedef struct SubGhzProtocolEncoderCameTwee SubGhzProtocolEncoderCameTwee;

extern const SubGhzProtocolDecoder subghz_protocol_came_twee_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_came_twee_encoder;
extern const SubGhzProtocol subghz_protocol_came_twee;

void* subghz_protocol_encoder_came_twee_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_came_twee_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_came_twee_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_came_twee_stop(void* context);

LevelDuration subghz_protocol_encoder_came_twee_yield(void* context);

void* subghz_protocol_decoder_came_twee_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_came_twee_free(void* context);

void subghz_protocol_decoder_came_twee_reset(void* context);

void subghz_protocol_decoder_came_twee_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_came_twee_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_came_twee_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_came_twee_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_came_twee_get_string(void* context, FuriString* output);

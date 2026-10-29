#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_ALLSTAR_FIREFLY_NAME "Allstar Firefly"

typedef struct SubGhzProtocolDecoderAllstarFirefly SubGhzProtocolDecoderAllstarFirefly;
typedef struct SubGhzProtocolEncoderAllstarFirefly SubGhzProtocolEncoderAllstarFirefly;

extern const SubGhzProtocolDecoder subghz_protocol_allstar_firefly_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_allstar_firefly_encoder;
extern const SubGhzProtocol subghz_protocol_allstar_firefly;

void* subghz_protocol_encoder_allstar_firefly_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_allstar_firefly_free(void* context);

SubGhzProtocolStatus subghz_protocol_encoder_allstar_firefly_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void subghz_protocol_encoder_allstar_firefly_stop(void* context);

LevelDuration subghz_protocol_encoder_allstar_firefly_yield(void* context);

void* subghz_protocol_decoder_allstar_firefly_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_allstar_firefly_free(void* context);

void subghz_protocol_decoder_allstar_firefly_reset(void* context);

void subghz_protocol_decoder_allstar_firefly_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_allstar_firefly_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_allstar_firefly_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus subghz_protocol_decoder_allstar_firefly_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void subghz_protocol_decoder_allstar_firefly_get_string(void* context, FuriString* output);

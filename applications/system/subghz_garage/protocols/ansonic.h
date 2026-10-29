#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_ANSONIC_NAME "Ansonic"

typedef struct SubGhzProtocolDecoderAnsonic SubGhzProtocolDecoderAnsonic;
typedef struct SubGhzProtocolEncoderAnsonic SubGhzProtocolEncoderAnsonic;

extern const SubGhzProtocolDecoder subghz_protocol_ansonic_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_ansonic_encoder;
extern const SubGhzProtocol subghz_protocol_ansonic;

void* subghz_protocol_encoder_ansonic_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_ansonic_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_ansonic_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_ansonic_stop(void* context);

LevelDuration subghz_protocol_encoder_ansonic_yield(void* context);

void* subghz_protocol_decoder_ansonic_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_ansonic_free(void* context);

void subghz_protocol_decoder_ansonic_reset(void* context);

void subghz_protocol_decoder_ansonic_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_ansonic_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_ansonic_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_ansonic_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_ansonic_get_string(void* context, FuriString* output);

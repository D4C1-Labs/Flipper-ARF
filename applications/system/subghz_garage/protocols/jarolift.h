#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_JAROLIFT_NAME "Jarolift"

typedef struct SubGhzProtocolDecoderJarolift SubGhzProtocolDecoderJarolift;
typedef struct SubGhzProtocolEncoderJarolift SubGhzProtocolEncoderJarolift;

extern const SubGhzProtocolDecoder subghz_protocol_jarolift_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_jarolift_encoder;
extern const SubGhzProtocol subghz_protocol_jarolift;

void* subghz_protocol_encoder_jarolift_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_jarolift_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_jarolift_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_jarolift_stop(void* context);

LevelDuration subghz_protocol_encoder_jarolift_yield(void* context);

void* subghz_protocol_decoder_jarolift_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_jarolift_free(void* context);

void subghz_protocol_decoder_jarolift_reset(void* context);

void subghz_protocol_decoder_jarolift_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_jarolift_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_jarolift_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_jarolift_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_jarolift_get_string(void* context, FuriString* output);

#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_FERON_NAME "Feron"

typedef struct SubGhzProtocolDecoderFeron SubGhzProtocolDecoderFeron;
typedef struct SubGhzProtocolEncoderFeron SubGhzProtocolEncoderFeron;

extern const SubGhzProtocolDecoder subghz_protocol_feron_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_feron_encoder;
extern const SubGhzProtocol subghz_protocol_feron;

void* subghz_protocol_encoder_feron_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_feron_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_feron_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_feron_stop(void* context);

LevelDuration subghz_protocol_encoder_feron_yield(void* context);

void* subghz_protocol_decoder_feron_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_feron_free(void* context);

void subghz_protocol_decoder_feron_reset(void* context);

void subghz_protocol_decoder_feron_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_feron_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_feron_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_feron_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_feron_get_string(void* context, FuriString* output);

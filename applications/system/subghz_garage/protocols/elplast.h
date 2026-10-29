#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_ELPLAST_NAME "Elplast"

typedef struct SubGhzProtocolDecoderElplast SubGhzProtocolDecoderElplast;
typedef struct SubGhzProtocolEncoderElplast SubGhzProtocolEncoderElplast;

extern const SubGhzProtocolDecoder subghz_protocol_elplast_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_elplast_encoder;
extern const SubGhzProtocol subghz_protocol_elplast;

void* subghz_protocol_encoder_elplast_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_elplast_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_elplast_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_elplast_stop(void* context);

LevelDuration subghz_protocol_encoder_elplast_yield(void* context);

void* subghz_protocol_decoder_elplast_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_elplast_free(void* context);

void subghz_protocol_decoder_elplast_reset(void* context);

void subghz_protocol_decoder_elplast_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_elplast_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_elplast_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_elplast_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_elplast_get_string(void* context, FuriString* output);

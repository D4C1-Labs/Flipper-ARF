#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_NICE_FLO_NAME "Nice FLO"

typedef struct SubGhzProtocolDecoderNiceFlo SubGhzProtocolDecoderNiceFlo;
typedef struct SubGhzProtocolEncoderNiceFlo SubGhzProtocolEncoderNiceFlo;

extern const SubGhzProtocolDecoder subghz_protocol_nice_flo_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_nice_flo_encoder;
extern const SubGhzProtocol subghz_protocol_nice_flo;

void* subghz_protocol_encoder_nice_flo_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_nice_flo_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_nice_flo_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_nice_flo_stop(void* context);

LevelDuration subghz_protocol_encoder_nice_flo_yield(void* context);

void* subghz_protocol_decoder_nice_flo_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_nice_flo_free(void* context);

void subghz_protocol_decoder_nice_flo_reset(void* context);

void subghz_protocol_decoder_nice_flo_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_nice_flo_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_nice_flo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_nice_flo_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_nice_flo_get_string(void* context, FuriString* output);

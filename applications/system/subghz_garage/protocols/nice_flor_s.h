#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME "Nice FloR-S"

typedef struct SubGhzProtocolDecoderNiceFlorS SubGhzProtocolDecoderNiceFlorS;
typedef struct SubGhzProtocolEncoderNiceFlorS SubGhzProtocolEncoderNiceFlorS;

extern const SubGhzProtocolDecoder subghz_protocol_nice_flor_s_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_nice_flor_s_encoder;
extern const SubGhzProtocol subghz_protocol_nice_flor_s;

void* subghz_protocol_encoder_nice_flor_s_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_nice_flor_s_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_nice_flor_s_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_nice_flor_s_stop(void* context);

LevelDuration subghz_protocol_encoder_nice_flor_s_yield(void* context);

uint64_t subghz_protocol_nice_flor_s_encrypt(uint64_t data, const char* file_name);

void* subghz_protocol_decoder_nice_flor_s_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_nice_flor_s_free(void* context);

void subghz_protocol_decoder_nice_flor_s_reset(void* context);

void subghz_protocol_decoder_nice_flor_s_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_nice_flor_s_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_nice_flor_s_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_nice_flor_s_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_nice_flor_s_get_string(void* context, FuriString* output);

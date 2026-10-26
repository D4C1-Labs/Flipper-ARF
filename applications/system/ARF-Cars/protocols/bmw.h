#pragma once

#include <lib/subghz/protocols/base.h>

#define SUBGHZ_PROTOCOL_BMW_NAME "BMW"

typedef struct SubGhzProtocolDecoderBMW SubGhzProtocolDecoderBMW;
typedef struct SubGhzProtocolEncoderBMW SubGhzProtocolEncoderBMW;

extern const SubGhzProtocolDecoder subghz_protocol_bmw_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_bmw_encoder;
extern const SubGhzProtocol subghz_protocol_bmw;

void* subghz_protocol_encoder_bmw_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_bmw_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_bmw_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_bmw_stop(void* context);
LevelDuration subghz_protocol_encoder_bmw_yield(void* context);

void* subghz_protocol_decoder_bmw_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_bmw_free(void* context);
void subghz_protocol_decoder_bmw_reset(void* context);
void subghz_protocol_decoder_bmw_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_bmw_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_bmw_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_bmw_get_string(void* context, FuriString* output);

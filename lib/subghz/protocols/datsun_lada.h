#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_DATSUN_LADA_NAME "Datsun Lada"

typedef struct SubGhzProtocolDecoderDatsunLada SubGhzProtocolDecoderDatsunLada;
typedef struct SubGhzProtocolEncoderDatsunLada SubGhzProtocolEncoderDatsunLada;

extern const SubGhzProtocolDecoder subghz_protocol_datsun_lada_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_datsun_lada_encoder;
extern const SubGhzProtocol subghz_protocol_datsun_lada;

// Decoder functions
void* subghz_protocol_decoder_datsun_lada_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_datsun_lada_free(void* context);
void subghz_protocol_decoder_datsun_lada_reset(void* context);
void subghz_protocol_decoder_datsun_lada_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_datsun_lada_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_datsun_lada_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_datsun_lada_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_datsun_lada_get_string(void* context, FuriString* output);

// Encoder functions
void* subghz_protocol_encoder_datsun_lada_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_datsun_lada_free(void* context);
void subghz_protocol_encoder_datsun_lada_stop(void* context);
LevelDuration subghz_protocol_encoder_datsun_lada_yield(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_datsun_lada_deserialize(void* context, FlipperFormat* flipper_format);

#pragma once

#include "base.h"

#define HONDA_ACURA_PROTOCOL_NAME "Honda/Acura"

typedef struct SubGhzProtocolDecoderHondaAcura SubGhzProtocolDecoderHondaAcura;
typedef struct SubGhzProtocolEncoderHondaAcura SubGhzProtocolEncoderHondaAcura;

extern const SubGhzProtocol subghz_protocol_honda_acura;

void* subghz_protocol_decoder_honda_acura_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_honda_acura_free(void* context);
void subghz_protocol_decoder_honda_acura_reset(void* context);
void subghz_protocol_decoder_honda_acura_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_honda_acura_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_honda_acura_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_honda_acura_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_honda_acura_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_honda_acura_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_honda_acura_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_honda_acura_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_honda_acura_stop(void* context);
LevelDuration subghz_protocol_encoder_honda_acura_yield(void* context);

#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME "Holtek_HT12X"

typedef struct SubGhzProtocolDecoderHoltek_HT12X SubGhzProtocolDecoderHoltek_HT12X;
typedef struct SubGhzProtocolEncoderHoltek_HT12X SubGhzProtocolEncoderHoltek_HT12X;

extern const SubGhzProtocolDecoder subghz_protocol_holtek_th12x_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_holtek_th12x_encoder;
extern const SubGhzProtocol subghz_protocol_holtek_th12x;

void* subghz_protocol_encoder_holtek_th12x_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_holtek_th12x_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_holtek_th12x_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_holtek_th12x_stop(void* context);

LevelDuration subghz_protocol_encoder_holtek_th12x_yield(void* context);

void* subghz_protocol_decoder_holtek_th12x_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_holtek_th12x_free(void* context);

void subghz_protocol_decoder_holtek_th12x_reset(void* context);

void subghz_protocol_decoder_holtek_th12x_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_holtek_th12x_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_holtek_th12x_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_holtek_th12x_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_holtek_th12x_get_string(void* context, FuriString* output);

#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_BETT_NAME "BETT"

typedef struct SubGhzProtocolDecoderBETT SubGhzProtocolDecoderBETT;
typedef struct SubGhzProtocolEncoderBETT SubGhzProtocolEncoderBETT;

extern const SubGhzProtocolDecoder subghz_protocol_bett_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_bett_encoder;
extern const SubGhzProtocol subghz_protocol_bett;

void* subghz_protocol_encoder_bett_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_bett_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_bett_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_bett_stop(void* context);

LevelDuration subghz_protocol_encoder_bett_yield(void* context);

void* subghz_protocol_decoder_bett_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_bett_free(void* context);

void subghz_protocol_decoder_bett_reset(void* context);

void subghz_protocol_decoder_bett_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_bett_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_bett_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_bett_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_bett_get_string(void* context, FuriString* output);

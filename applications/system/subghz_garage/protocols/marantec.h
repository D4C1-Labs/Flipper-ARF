#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_MARANTEC_NAME "Marantec"

typedef struct SubGhzProtocolDecoderMarantec SubGhzProtocolDecoderMarantec;
typedef struct SubGhzProtocolEncoderMarantec SubGhzProtocolEncoderMarantec;

extern const SubGhzProtocolDecoder subghz_protocol_marantec_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_marantec_encoder;
extern const SubGhzProtocol subghz_protocol_marantec;

void* subghz_protocol_encoder_marantec_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_marantec_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_marantec_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_marantec_stop(void* context);

LevelDuration subghz_protocol_encoder_marantec_yield(void* context);

void* subghz_protocol_decoder_marantec_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_marantec_free(void* context);

void subghz_protocol_decoder_marantec_reset(void* context);

void subghz_protocol_decoder_marantec_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_marantec_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_marantec_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_marantec_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_marantec_get_string(void* context, FuriString* output);

uint8_t subghz_protocol_marantec_crc8(uint8_t* data, size_t len);

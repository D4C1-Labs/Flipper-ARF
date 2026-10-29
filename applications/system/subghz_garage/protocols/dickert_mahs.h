#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME "Dickert_MAHS"

typedef struct SubGhzProtocolDecoderDickertMAHS SubGhzProtocolDecoderDickertMAHS;
typedef struct SubGhzProtocolEncoderDickertMAHS SubGhzProtocolEncoderDickertMAHS;

extern const SubGhzProtocolDecoder subghz_protocol_dickert_mahs_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_dickert_mahs_encoder;
extern const SubGhzProtocol subghz_protocol_dickert_mahs;

void* subghz_protocol_encoder_dickert_mahs_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_dickert_mahs_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_dickert_mahs_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_dickert_mahs_stop(void* context);

LevelDuration subghz_protocol_encoder_dickert_mahs_yield(void* context);

void* subghz_protocol_decoder_dickert_mahs_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_dickert_mahs_free(void* context);

void subghz_protocol_decoder_dickert_mahs_reset(void* context);

void subghz_protocol_decoder_dickert_mahs_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_dickert_mahs_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_dickert_mahs_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_dickert_mahs_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_dickert_mahs_get_string(void* context, FuriString* output);

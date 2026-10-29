#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_REVERSRB2_NAME "Revers_RB2"

typedef struct SubGhzProtocolDecoderRevers_RB2 SubGhzProtocolDecoderRevers_RB2;
typedef struct SubGhzProtocolEncoderRevers_RB2 SubGhzProtocolEncoderRevers_RB2;

extern const SubGhzProtocolDecoder subghz_protocol_revers_rb2_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_revers_rb2_encoder;
extern const SubGhzProtocol subghz_protocol_revers_rb2;

void* subghz_protocol_encoder_revers_rb2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_revers_rb2_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_revers_rb2_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_revers_rb2_stop(void* context);

LevelDuration subghz_protocol_encoder_revers_rb2_yield(void* context);

void* subghz_protocol_decoder_revers_rb2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_revers_rb2_free(void* context);

void subghz_protocol_decoder_revers_rb2_reset(void* context);

void subghz_protocol_decoder_revers_rb2_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_revers_rb2_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_revers_rb2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_revers_rb2_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_revers_rb2_get_string(void* context, FuriString* output);

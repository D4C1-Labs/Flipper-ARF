#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME "Alutech AT-4N"

typedef struct SubGhzProtocolDecoderAlutech_at_4n SubGhzProtocolDecoderAlutech_at_4n;
typedef struct SubGhzProtocolEncoderAlutech_at_4n SubGhzProtocolEncoderAlutech_at_4n;

extern const SubGhzProtocolDecoder subghz_protocol_alutech_at_4n_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_alutech_at_4n_encoder;
extern const SubGhzProtocol subghz_protocol_alutech_at_4n;

void* subghz_protocol_encoder_alutech_at_4n_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_alutech_at_4n_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_alutech_at_4n_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_alutech_at_4n_stop(void* context);

LevelDuration subghz_protocol_encoder_alutech_at_4n_yield(void* context);

void* subghz_protocol_decoder_alutech_at_4n_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_alutech_at_4n_free(void* context);

void subghz_protocol_decoder_alutech_at_4n_reset(void* context);

void subghz_protocol_decoder_alutech_at_4n_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_alutech_at_4n_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_alutech_at_4n_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_alutech_at_4n_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_alutech_at_4n_get_string(void* context, FuriString* output);

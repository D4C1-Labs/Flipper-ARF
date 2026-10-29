#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_FAAC_SLH_NAME "Faac SLH"

typedef struct SubGhzProtocolDecoderFaacSLH SubGhzProtocolDecoderFaacSLH;
typedef struct SubGhzProtocolEncoderFaacSLH SubGhzProtocolEncoderFaacSLH;

extern const SubGhzProtocolDecoder subghz_protocol_faac_slh_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_faac_slh_encoder;
extern const SubGhzProtocol subghz_protocol_faac_slh;

void* subghz_protocol_encoder_faac_slh_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_faac_slh_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_faac_slh_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_faac_slh_stop(void* context);

LevelDuration subghz_protocol_encoder_faac_slh_yield(void* context);

void* subghz_protocol_decoder_faac_slh_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_faac_slh_free(void* context);

void subghz_protocol_decoder_faac_slh_reset(void* context);

void subghz_protocol_decoder_faac_slh_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_faac_slh_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_faac_slh_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_faac_slh_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_faac_slh_get_string(void* context, FuriString* output);

void faac_slh_reset_prog_mode(void);

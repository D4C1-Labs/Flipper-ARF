#pragma once

#include "base.h"
#include "public_api.h"

#define SUBGHZ_PROTOCOL_SECPLUS_V1_NAME "Security+ 1.0"

typedef struct SubGhzProtocolDecoderSecPlus_v1 SubGhzProtocolDecoderSecPlus_v1;
typedef struct SubGhzProtocolEncoderSecPlus_v1 SubGhzProtocolEncoderSecPlus_v1;

extern const SubGhzProtocolDecoder subghz_protocol_secplus_v1_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_secplus_v1_encoder;
extern const SubGhzProtocol subghz_protocol_secplus_v1;

void* subghz_protocol_encoder_secplus_v1_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_secplus_v1_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_secplus_v1_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_secplus_v1_stop(void* context);

LevelDuration subghz_protocol_encoder_secplus_v1_yield(void* context);

void* subghz_protocol_decoder_secplus_v1_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_secplus_v1_free(void* context);

void subghz_protocol_decoder_secplus_v1_reset(void* context);

void subghz_protocol_decoder_secplus_v1_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_secplus_v1_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_secplus_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_secplus_v1_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_secplus_v1_get_string(void* context, FuriString* output);

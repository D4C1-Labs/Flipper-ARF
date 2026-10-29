#pragma once

#include "base.h"
#include "public_api.h"

#define SUBGHZ_PROTOCOL_SECPLUS_V2_NAME "Security+ 2.0"

typedef struct SubGhzProtocolDecoderSecPlus_v2 SubGhzProtocolDecoderSecPlus_v2;
typedef struct SubGhzProtocolEncoderSecPlus_v2 SubGhzProtocolEncoderSecPlus_v2;

extern const SubGhzProtocolDecoder subghz_protocol_secplus_v2_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_secplus_v2_encoder;
extern const SubGhzProtocol subghz_protocol_secplus_v2;

void* subghz_protocol_encoder_secplus_v2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_secplus_v2_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_secplus_v2_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_secplus_v2_stop(void* context);

LevelDuration subghz_protocol_encoder_secplus_v2_yield(void* context);

void* subghz_protocol_decoder_secplus_v2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_secplus_v2_free(void* context);

void subghz_protocol_decoder_secplus_v2_reset(void* context);

void subghz_protocol_decoder_secplus_v2_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_secplus_v2_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_secplus_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_secplus_v2_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_secplus_v2_get_string(void* context, FuriString* output);

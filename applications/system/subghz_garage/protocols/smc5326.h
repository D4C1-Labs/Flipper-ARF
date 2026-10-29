#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_SMC5326_NAME "SMC5326"

typedef struct SubGhzProtocolDecoderSMC5326 SubGhzProtocolDecoderSMC5326;
typedef struct SubGhzProtocolEncoderSMC5326 SubGhzProtocolEncoderSMC5326;

extern const SubGhzProtocolDecoder subghz_protocol_smc5326_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_smc5326_encoder;
extern const SubGhzProtocol subghz_protocol_smc5326;

void* subghz_protocol_encoder_smc5326_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_smc5326_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_smc5326_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_smc5326_stop(void* context);

LevelDuration subghz_protocol_encoder_smc5326_yield(void* context);

void* subghz_protocol_decoder_smc5326_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_smc5326_free(void* context);

void subghz_protocol_decoder_smc5326_reset(void* context);

void subghz_protocol_decoder_smc5326_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_smc5326_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_smc5326_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_smc5326_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_smc5326_get_string(void* context, FuriString* output);

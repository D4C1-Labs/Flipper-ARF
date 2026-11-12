#pragma once

#include "base.h"

#define GM_ROLLING_PROTOCOL_NAME "GM Rolling"

typedef struct SubGhzProtocolDecoderGmRolling SubGhzProtocolDecoderGmRolling;
typedef struct SubGhzProtocolEncoderGmRolling SubGhzProtocolEncoderGmRolling;

extern const SubGhzProtocol subghz_protocol_gm_rolling;

void* subghz_protocol_decoder_gm_rolling_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_gm_rolling_free(void* context);
void subghz_protocol_decoder_gm_rolling_reset(void* context);
void subghz_protocol_decoder_gm_rolling_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_gm_rolling_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_gm_rolling_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_gm_rolling_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_gm_rolling_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_gm_rolling_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_gm_rolling_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_gm_rolling_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_gm_rolling_stop(void* context);
LevelDuration subghz_protocol_encoder_gm_rolling_yield(void* context);

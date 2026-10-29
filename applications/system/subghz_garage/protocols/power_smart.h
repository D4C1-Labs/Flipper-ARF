#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_POWER_SMART_NAME "Power Smart"

typedef struct SubGhzProtocolDecoderPowerSmart SubGhzProtocolDecoderPowerSmart;
typedef struct SubGhzProtocolEncoderPowerSmart SubGhzProtocolEncoderPowerSmart;

extern const SubGhzProtocolDecoder subghz_protocol_power_smart_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_power_smart_encoder;
extern const SubGhzProtocol subghz_protocol_power_smart;

void* subghz_protocol_encoder_power_smart_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_power_smart_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_power_smart_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_power_smart_stop(void* context);

LevelDuration subghz_protocol_encoder_power_smart_yield(void* context);

void* subghz_protocol_decoder_power_smart_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_power_smart_free(void* context);

void subghz_protocol_decoder_power_smart_reset(void* context);

void subghz_protocol_decoder_power_smart_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_power_smart_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_power_smart_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_power_smart_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_power_smart_get_string(void* context, FuriString* output);

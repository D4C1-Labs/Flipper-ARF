#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_HONEYWELL_NAME "Honeywell Sec"

typedef struct SubGhzProtocolDecoderHoneywell SubGhzProtocolDecoderHoneywell;
typedef struct SubGhzProtocolEncoderHoneywell SubGhzProtocolEncoderHoneywell;

extern const SubGhzProtocolDecoder subghz_protocol_honeywell_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_honeywell_encoder;
extern const SubGhzProtocol subghz_protocol_honeywell;

void* subghz_protocol_encoder_honeywell_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_honeywell_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_honeywell_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_honeywell_stop(void* context);

LevelDuration subghz_protocol_encoder_honeywell_yield(void* context);

void* subghz_protocol_decoder_honeywell_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_honeywell_free(void* context);

void subghz_protocol_decoder_honeywell_reset(void* context);

void subghz_protocol_decoder_honeywell_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_honeywell_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_honeywell_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_honeywell_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_honeywell_get_string(void* context, FuriString* output);

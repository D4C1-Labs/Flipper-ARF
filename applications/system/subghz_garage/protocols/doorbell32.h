#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_DOORBELL32_NAME "Doorbell32"

typedef struct SubGhzProtocolDecoderDoorbell32 SubGhzProtocolDecoderDoorbell32;
typedef struct SubGhzProtocolEncoderDoorbell32 SubGhzProtocolEncoderDoorbell32;

extern const SubGhzProtocolDecoder subghz_protocol_doorbell32_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_doorbell32_encoder;
extern const SubGhzProtocol subghz_protocol_doorbell32;

void* subghz_protocol_encoder_doorbell32_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_doorbell32_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_doorbell32_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_doorbell32_stop(void* context);

LevelDuration subghz_protocol_encoder_doorbell32_yield(void* context);

void* subghz_protocol_decoder_doorbell32_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_doorbell32_free(void* context);

void subghz_protocol_decoder_doorbell32_reset(void* context);

void subghz_protocol_decoder_doorbell32_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_doorbell32_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_doorbell32_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_doorbell32_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_doorbell32_get_string(void* context, FuriString* output);

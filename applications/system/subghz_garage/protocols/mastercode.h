#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_MASTERCODE_NAME "Mastercode"

typedef struct SubGhzProtocolDecoderMastercode SubGhzProtocolDecoderMastercode;
typedef struct SubGhzProtocolEncoderMastercode SubGhzProtocolEncoderMastercode;

extern const SubGhzProtocolDecoder subghz_protocol_mastercode_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_mastercode_encoder;
extern const SubGhzProtocol subghz_protocol_mastercode;

void* subghz_protocol_encoder_mastercode_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_mastercode_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_mastercode_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_mastercode_stop(void* context);

LevelDuration subghz_protocol_encoder_mastercode_yield(void* context);

void* subghz_protocol_decoder_mastercode_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_mastercode_free(void* context);

void subghz_protocol_decoder_mastercode_reset(void* context);

void subghz_protocol_decoder_mastercode_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_mastercode_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_mastercode_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_mastercode_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_mastercode_get_string(void* context, FuriString* output);

#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_X10_NAME "X10"

typedef struct SubGhzProtocolDecoderX10 SubGhzProtocolDecoderX10;
typedef struct SubGhzProtocolEncoderX10 SubGhzProtocolEncoderX10;

extern const SubGhzProtocolDecoder subghz_protocol_x10_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_x10_encoder;
extern const SubGhzProtocol subghz_protocol_x10;

void* subghz_protocol_decoder_x10_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_x10_free(void* context);

void subghz_protocol_decoder_x10_reset(void* context);

void subghz_protocol_decoder_x10_feed(void* context, bool level, uint32_t duration);

bool subghz_protocol_x10_validate(void* context);

uint8_t subghz_protocol_decoder_x10_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_x10_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_x10_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_x10_get_string(void* context, FuriString* output);

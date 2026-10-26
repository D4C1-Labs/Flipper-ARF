#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_TOYOTA_V0_NAME "Toyota V0"

typedef struct SubGhzProtocolDecoderToyota SubGhzProtocolDecoderToyota;
typedef struct SubGhzProtocolEncoderToyota SubGhzProtocolEncoderToyota;

extern const SubGhzProtocolDecoder subghz_protocol_toyota_v0_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_toyota_v0_encoder;
extern const SubGhzProtocol subghz_protocol_toyota_v0;

void* subghz_protocol_encoder_toyota_v0_alloc(SubGhzEnvironment* environment);
SubGhzProtocolStatus
    subghz_protocol_encoder_toyota_v0_deserialize(void* context, FlipperFormat* flipper_format);

void* subghz_protocol_decoder_toyota_v0_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_toyota_v0_reset(void* context);
void subghz_protocol_decoder_toyota_v0_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_toyota_v0_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_toyota_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_toyota_v0_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_toyota_v0_get_string(void* context, FuriString* output);

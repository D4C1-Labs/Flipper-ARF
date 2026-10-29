#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_TOYOTA_NAME "Toyota"

typedef struct SubGhzProtocolDecoderToyota SubGhzProtocolDecoderToyota;
typedef struct SubGhzProtocolEncoderToyota SubGhzProtocolEncoderToyota;

extern const SubGhzProtocolDecoder subghz_protocol_toyota_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_toyota_encoder;
extern const SubGhzProtocol subghz_protocol_toyota;

void* subghz_protocol_encoder_toyota_alloc(SubGhzEnvironment* environment);
SubGhzProtocolStatus
    subghz_protocol_encoder_toyota_deserialize(void* context, FlipperFormat* flipper_format);

void* subghz_protocol_decoder_toyota_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_toyota_reset(void* context);
void subghz_protocol_decoder_toyota_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_toyota_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_toyota_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_toyota_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_toyota_get_string(void* context, FuriString* output);

#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_MEGACODE_NAME "MegaCode"

typedef struct SubGhzProtocolDecoderMegaCode SubGhzProtocolDecoderMegaCode;
typedef struct SubGhzProtocolEncoderMegaCode SubGhzProtocolEncoderMegaCode;

extern const SubGhzProtocolDecoder subghz_protocol_megacode_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_megacode_encoder;
extern const SubGhzProtocol subghz_protocol_megacode;

void* subghz_protocol_encoder_megacode_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_megacode_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_megacode_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_megacode_stop(void* context);

LevelDuration subghz_protocol_encoder_megacode_yield(void* context);

void* subghz_protocol_decoder_megacode_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_megacode_free(void* context);

void subghz_protocol_decoder_megacode_reset(void* context);

void subghz_protocol_decoder_megacode_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_megacode_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_megacode_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_megacode_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_megacode_get_string(void* context, FuriString* output);

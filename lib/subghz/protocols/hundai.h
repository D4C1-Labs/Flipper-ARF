#pragma once

#include "base.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define HUNDAI_PROTOCOL_NAME "Hundai"

typedef struct SubGhzProtocolDecoderHundai SubGhzProtocolDecoderHundai;
typedef struct SubGhzProtocolEncoderHundai SubGhzProtocolEncoderHundai;

extern const SubGhzProtocol subghz_protocol_hundai;

void* subghz_protocol_decoder_hundai_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_hundai_free(void* context);
void subghz_protocol_decoder_hundai_reset(void* context);
void subghz_protocol_decoder_hundai_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_hundai_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_hundai_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_hundai_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_hundai_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_hundai_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_hundai_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_hundai_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_hundai_stop(void* context);
LevelDuration subghz_protocol_encoder_hundai_yield(void* context);

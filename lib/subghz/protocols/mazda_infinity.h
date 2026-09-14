#pragma once

#include "base.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include <lib/toolbox/manchester_decoder.h>

#define MAZDA_INFINITY_PROTOCOL_NAME "Mazda Infinity"

typedef struct SubGhzProtocolDecoderMazdaInfinity SubGhzProtocolDecoderMazdaInfinity;
typedef struct SubGhzProtocolEncoderMazdaInfinity SubGhzProtocolEncoderMazdaInfinity;

extern const SubGhzProtocol subghz_protocol_mazda_infinity;

void* subghz_protocol_decoder_mazda_infinity_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_mazda_infinity_free(void* context);
void subghz_protocol_decoder_mazda_infinity_reset(void* context);
void subghz_protocol_decoder_mazda_infinity_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_mazda_infinity_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_mazda_infinity_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus subghz_protocol_decoder_mazda_infinity_deserialize(
    void* context,
    FlipperFormat* flipper_format);
void subghz_protocol_decoder_mazda_infinity_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_mazda_infinity_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_mazda_infinity_free(void* context);
SubGhzProtocolStatus subghz_protocol_encoder_mazda_infinity_deserialize(
    void* context,
    FlipperFormat* flipper_format);
void subghz_protocol_encoder_mazda_infinity_stop(void* context);
LevelDuration subghz_protocol_encoder_mazda_infinity_yield(void* context);

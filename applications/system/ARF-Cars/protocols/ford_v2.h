#pragma once

#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/manchester_encoder.h>

#include <flipper_format/flipper_format.h>
#include <furi.h>

#define FORD_PROTOCOL_V2_NAME "Ford V2"

extern const SubGhzProtocol ford_protocol_v2;
extern const SubGhzProtocolDecoder subghz_protocol_ford_v2_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_ford_v2_encoder;

void* subghz_protocol_decoder_ford_v2_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_ford_v2_free(void* context);
void subghz_protocol_decoder_ford_v2_reset(void* context);
void subghz_protocol_decoder_ford_v2_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_ford_v2_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_ford_v2_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus subghz_protocol_decoder_ford_v2_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void subghz_protocol_decoder_ford_v2_get_string(void* context, FuriString* output);
void* subghz_protocol_encoder_ford_v2_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_ford_v2_free(void* context);

SubGhzProtocolStatus subghz_protocol_encoder_ford_v2_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void subghz_protocol_encoder_ford_v2_stop(void* context);

LevelDuration subghz_protocol_encoder_ford_v2_yield(void* context);

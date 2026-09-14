#pragma once

#include "base.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include <lib/toolbox/manchester_decoder.h>

#define MERCEDES_PROTOCOL_NAME "Mercedes"

typedef struct SubGhzProtocolDecoderMercedes SubGhzProtocolDecoderMercedes;
typedef struct SubGhzProtocolEncoderMercedes SubGhzProtocolEncoderMercedes;

extern const SubGhzProtocol subghz_protocol_mercedes;

void* subghz_protocol_decoder_mercedes_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_mercedes_free(void* context);
void subghz_protocol_decoder_mercedes_reset(void* context);
void subghz_protocol_decoder_mercedes_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_mercedes_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_mercedes_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_mercedes_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_mercedes_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_mercedes_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_mercedes_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_mercedes_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_mercedes_stop(void* context);
LevelDuration subghz_protocol_encoder_mercedes_yield(void* context);

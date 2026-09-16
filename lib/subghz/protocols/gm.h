#pragma once

#include "base.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define GM_PROTOCOL_NAME "GM"

typedef struct SubGhzProtocolDecoderGM SubGhzProtocolDecoderGM;
typedef struct SubGhzProtocolEncoderGM SubGhzProtocolEncoderGM;

extern const SubGhzProtocol subghz_protocol_gm;

void* subghz_protocol_decoder_gm_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_gm_free(void* context);
void subghz_protocol_decoder_gm_reset(void* context);
void subghz_protocol_decoder_gm_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_gm_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_gm_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_gm_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_gm_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_gm_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_gm_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_gm_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_gm_stop(void* context);
LevelDuration subghz_protocol_encoder_gm_yield(void* context);

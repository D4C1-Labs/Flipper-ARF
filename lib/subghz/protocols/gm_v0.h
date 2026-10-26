#pragma once

#include "base.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define GM_V0_PROTOCOL_NAME "GM V0"

typedef struct SubGhzProtocolDecoderGM SubGhzProtocolDecoderGM;
typedef struct SubGhzProtocolEncoderGM SubGhzProtocolEncoderGM;

extern const SubGhzProtocol subghz_protocol_gm_v0;

void* subghz_protocol_decoder_gm_v0_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_gm_v0_free(void* context);
void subghz_protocol_decoder_gm_v0_reset(void* context);
void subghz_protocol_decoder_gm_v0_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_gm_v0_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_gm_v0_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_gm_v0_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_gm_v0_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_gm_v0_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_gm_v0_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_gm_v0_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_gm_v0_stop(void* context);
LevelDuration subghz_protocol_encoder_gm_v0_yield(void* context);

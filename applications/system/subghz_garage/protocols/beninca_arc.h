#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_BENINCA_ARC_NAME "Beninca ARC"

typedef struct SubGhzProtocolDecoderBenincaARC SubGhzProtocolDecoderBenincaARC;
typedef struct SubGhzProtocolEncoderBenincaARC SubGhzProtocolEncoderBenincaARC;

extern const SubGhzProtocolDecoder subghz_protocol_beninca_arc_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_beninca_arc_encoder;
extern const SubGhzProtocol subghz_protocol_beninca_arc;

void* subghz_protocol_encoder_beninca_arc_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_beninca_arc_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_beninca_arc_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_beninca_arc_stop(void* context);

LevelDuration subghz_protocol_encoder_beninca_arc_yield(void* context);

void* subghz_protocol_decoder_beninca_arc_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_beninca_arc_free(void* context);

void subghz_protocol_decoder_beninca_arc_reset(void* context);

void subghz_protocol_decoder_beninca_arc_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_beninca_arc_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_beninca_arc_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_beninca_arc_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_beninca_arc_get_string(void* context, FuriString* output);

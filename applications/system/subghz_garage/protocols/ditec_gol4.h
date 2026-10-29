#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_DITEC_GOL4_NAME "Ditec GOL4"

typedef struct SubGhzProtocolDecoderDitecGOL4 SubGhzProtocolDecoderDitecGOL4;
typedef struct SubGhzProtocolEncoderDitecGOL4 SubGhzProtocolEncoderDitecGOL4;

extern const SubGhzProtocolDecoder subghz_protocol_ditec_gol4_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_ditec_gol4_encoder;
extern const SubGhzProtocol subghz_protocol_ditec_gol4;

void* subghz_protocol_encoder_ditec_gol4_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_ditec_gol4_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_ditec_gol4_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_ditec_gol4_stop(void* context);

LevelDuration subghz_protocol_encoder_ditec_gol4_yield(void* context);

void* subghz_protocol_decoder_ditec_gol4_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_ditec_gol4_free(void* context);

void subghz_protocol_decoder_ditec_gol4_reset(void* context);

void subghz_protocol_decoder_ditec_gol4_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_ditec_gol4_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_ditec_gol4_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_ditec_gol4_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_ditec_gol4_get_string(void* context, FuriString* output);

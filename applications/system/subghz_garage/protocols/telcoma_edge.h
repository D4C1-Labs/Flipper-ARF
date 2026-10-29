#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME "Telcoma/Cardin EDGE"

typedef struct SubGhzProtocolDecoderTelcomaEdge SubGhzProtocolDecoderTelcomaEdge;
typedef struct SubGhzProtocolEncoderTelcomaEdge SubGhzProtocolEncoderTelcomaEdge;

extern const SubGhzProtocolDecoder subghz_protocol_telcoma_edge_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_telcoma_edge_encoder;
extern const SubGhzProtocol subghz_protocol_telcoma_edge;

void* subghz_protocol_encoder_telcoma_edge_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_telcoma_edge_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_telcoma_edge_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_telcoma_edge_stop(void* context);

LevelDuration subghz_protocol_encoder_telcoma_edge_yield(void* context);

void* subghz_protocol_decoder_telcoma_edge_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_telcoma_edge_free(void* context);

void subghz_protocol_decoder_telcoma_edge_reset(void* context);

void subghz_protocol_decoder_telcoma_edge_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_telcoma_edge_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_telcoma_edge_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_telcoma_edge_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_telcoma_edge_get_string(void* context, FuriString* output);

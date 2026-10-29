#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME "Honeywell"

typedef struct SubGhzProtocolDecoderHoneywell_WDB SubGhzProtocolDecoderHoneywell_WDB;
typedef struct SubGhzProtocolEncoderHoneywell_WDB SubGhzProtocolEncoderHoneywell_WDB;

extern const SubGhzProtocolDecoder subghz_protocol_honeywell_wdb_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_honeywell_wdb_encoder;
extern const SubGhzProtocol subghz_protocol_honeywell_wdb;

void* subghz_protocol_encoder_honeywell_wdb_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_honeywell_wdb_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_honeywell_wdb_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_honeywell_wdb_stop(void* context);

LevelDuration subghz_protocol_encoder_honeywell_wdb_yield(void* context);

void* subghz_protocol_decoder_honeywell_wdb_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_honeywell_wdb_free(void* context);

void subghz_protocol_decoder_honeywell_wdb_reset(void* context);

void subghz_protocol_decoder_honeywell_wdb_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_honeywell_wdb_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_honeywell_wdb_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_honeywell_wdb_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_honeywell_wdb_get_string(void* context, FuriString* output);

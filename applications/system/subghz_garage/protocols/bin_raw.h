#pragma once

#include "base.h"
#include "public_api.h"

#define SUBGHZ_PROTOCOL_BIN_RAW_NAME "BinRAW"

typedef struct SubGhzProtocolDecoderBinRAW SubGhzProtocolDecoderBinRAW;
typedef struct SubGhzProtocolEncoderBinRAW SubGhzProtocolEncoderBinRAW;

extern const SubGhzProtocolDecoder subghz_protocol_bin_raw_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_bin_raw_encoder;
extern const SubGhzProtocol subghz_protocol_bin_raw;

void* subghz_protocol_encoder_bin_raw_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_bin_raw_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_bin_raw_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_bin_raw_stop(void* context);

LevelDuration subghz_protocol_encoder_bin_raw_yield(void* context);

void* subghz_protocol_decoder_bin_raw_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_bin_raw_free(void* context);

void subghz_protocol_decoder_bin_raw_reset(void* context);

void subghz_protocol_decoder_bin_raw_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_bin_raw_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_bin_raw_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_bin_raw_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_bin_raw_get_string(void* context, FuriString* output);

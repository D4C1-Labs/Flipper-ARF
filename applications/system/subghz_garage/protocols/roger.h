#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_ROGER_NAME "Roger"

typedef struct SubGhzProtocolDecoderRoger SubGhzProtocolDecoderRoger;
typedef struct SubGhzProtocolEncoderRoger SubGhzProtocolEncoderRoger;

extern const SubGhzProtocolDecoder subghz_protocol_roger_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_roger_encoder;
extern const SubGhzProtocol subghz_protocol_roger;

void* subghz_protocol_encoder_roger_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_roger_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_roger_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_roger_stop(void* context);

LevelDuration subghz_protocol_encoder_roger_yield(void* context);

void* subghz_protocol_decoder_roger_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_roger_free(void* context);

void subghz_protocol_decoder_roger_reset(void* context);

void subghz_protocol_decoder_roger_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_roger_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_roger_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_roger_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_roger_get_string(void* context, FuriString* output);

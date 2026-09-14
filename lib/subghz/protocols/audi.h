#pragma once

#include "base.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define AUDI_PROTOCOL_NAME "Audi"

typedef struct SubGhzProtocolDecoderAudi SubGhzProtocolDecoderAudi;
typedef struct SubGhzProtocolEncoderAudi SubGhzProtocolEncoderAudi;

extern const SubGhzProtocol subghz_protocol_audi;

void* subghz_protocol_decoder_audi_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_audi_free(void* context);
void subghz_protocol_decoder_audi_reset(void* context);
void subghz_protocol_decoder_audi_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_audi_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_audi_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_audi_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_audi_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_audi_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_audi_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_audi_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_audi_stop(void* context);
LevelDuration subghz_protocol_encoder_audi_yield(void* context);

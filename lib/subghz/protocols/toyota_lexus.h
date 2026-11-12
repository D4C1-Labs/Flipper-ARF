#pragma once

#include "base.h"

#define TOYOTA_LEXUS_PROTOCOL_NAME "Toyota"

typedef struct SubGhzProtocolDecoderToyotaLexus SubGhzProtocolDecoderToyotaLexus;
typedef struct SubGhzProtocolEncoderToyotaLexus SubGhzProtocolEncoderToyotaLexus;

extern const SubGhzProtocol subghz_protocol_toyota_lexus;

void* subghz_protocol_decoder_toyota_lexus_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_toyota_lexus_free(void* context);
void subghz_protocol_decoder_toyota_lexus_reset(void* context);
void subghz_protocol_decoder_toyota_lexus_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_toyota_lexus_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_toyota_lexus_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_toyota_lexus_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_toyota_lexus_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_toyota_lexus_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_toyota_lexus_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_toyota_lexus_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_toyota_lexus_stop(void* context);
LevelDuration subghz_protocol_encoder_toyota_lexus_yield(void* context);

#pragma once

#include "base.h"

#define NISSAN_PROTOCOL_NAME "Nissan"

typedef struct SubGhzProtocolDecoderNissan SubGhzProtocolDecoderNissan;
typedef struct SubGhzProtocolEncoderNissan SubGhzProtocolEncoderNissan;

extern const SubGhzProtocol subghz_protocol_nissan;

void* subghz_protocol_decoder_nissan_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_nissan_free(void* context);
void subghz_protocol_decoder_nissan_reset(void* context);
void subghz_protocol_decoder_nissan_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_nissan_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_nissan_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_nissan_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_nissan_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_nissan_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_nissan_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_nissan_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_nissan_stop(void* context);
LevelDuration subghz_protocol_encoder_nissan_yield(void* context);

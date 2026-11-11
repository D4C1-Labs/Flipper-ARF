#pragma once

#include "base.h"

#define HYUNDAI_NEW_PROTOCOL_NAME "Hyundai New"

typedef struct SubGhzProtocolDecoderHyundaiNew SubGhzProtocolDecoderHyundaiNew;
typedef struct SubGhzProtocolEncoderHyundaiNew SubGhzProtocolEncoderHyundaiNew;

extern const SubGhzProtocol subghz_protocol_hyundai_new;

void* subghz_protocol_decoder_hyundai_new_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_hyundai_new_free(void* context);
void subghz_protocol_decoder_hyundai_new_reset(void* context);
void subghz_protocol_decoder_hyundai_new_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_hyundai_new_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_hyundai_new_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_hyundai_new_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_hyundai_new_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_hyundai_new_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_hyundai_new_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_hyundai_new_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_hyundai_new_stop(void* context);
LevelDuration subghz_protocol_encoder_hyundai_new_yield(void* context);

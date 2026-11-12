#pragma once

#include "base.h"

#define VOLVO_PROTOCOL_NAME "Volvo"

typedef struct SubGhzProtocolDecoderVolvo SubGhzProtocolDecoderVolvo;
typedef struct SubGhzProtocolEncoderVolvo SubGhzProtocolEncoderVolvo;

extern const SubGhzProtocol subghz_protocol_volvo;

void* subghz_protocol_decoder_volvo_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_volvo_free(void* context);
void subghz_protocol_decoder_volvo_reset(void* context);
void subghz_protocol_decoder_volvo_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_volvo_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_volvo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_volvo_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_volvo_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_volvo_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_volvo_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_volvo_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_volvo_stop(void* context);
LevelDuration subghz_protocol_encoder_volvo_yield(void* context);

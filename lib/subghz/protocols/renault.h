#pragma once

#include "base.h"

#define RENAULT_PROTOCOL_NAME "Renault"

typedef struct SubGhzProtocolDecoderRenault SubGhzProtocolDecoderRenault;
typedef struct SubGhzProtocolEncoderRenault SubGhzProtocolEncoderRenault;

extern const SubGhzProtocol subghz_protocol_renault;

void* subghz_protocol_decoder_renault_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_renault_free(void* context);
void subghz_protocol_decoder_renault_reset(void* context);
void subghz_protocol_decoder_renault_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_renault_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_renault_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_renault_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_renault_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_renault_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_renault_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_renault_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_renault_stop(void* context);
LevelDuration subghz_protocol_encoder_renault_yield(void* context);

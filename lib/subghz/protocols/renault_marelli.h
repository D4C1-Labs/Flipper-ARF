#pragma once

#include "base.h"
#include <flipper_format/flipper_format.h>

#define RENAULT_MARELLI_PROTOCOL_NAME "Ren_MARELLI"

typedef struct SubGhzProtocolDecoderRenaultMarelli SubGhzProtocolDecoderRenaultMarelli;
typedef struct SubGhzProtocolEncoderRenaultMarelli SubGhzProtocolEncoderRenaultMarelli;

extern const SubGhzProtocol subghz_protocol_renault_marelli;

void* subghz_protocol_decoder_renault_marelli_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_renault_marelli_free(void* context);
void subghz_protocol_decoder_renault_marelli_reset(void* context);
void subghz_protocol_decoder_renault_marelli_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_renault_marelli_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_renault_marelli_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_renault_marelli_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_renault_marelli_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_renault_marelli_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_renault_marelli_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_renault_marelli_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_renault_marelli_stop(void* context);
LevelDuration subghz_protocol_encoder_renault_marelli_yield(void* context);

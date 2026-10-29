#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_CHAMB_CODE_NAME "Cham_Code"

typedef struct SubGhzProtocolDecoderChamb_Code SubGhzProtocolDecoderChamb_Code;
typedef struct SubGhzProtocolEncoderChamb_Code SubGhzProtocolEncoderChamb_Code;

extern const SubGhzProtocolDecoder subghz_protocol_chamb_code_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_chamb_code_encoder;
extern const SubGhzProtocol subghz_protocol_chamb_code;

void* subghz_protocol_encoder_chamb_code_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_chamb_code_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_chamb_code_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_chamb_code_stop(void* context);

LevelDuration subghz_protocol_encoder_chamb_code_yield(void* context);

void* subghz_protocol_decoder_chamb_code_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_chamb_code_free(void* context);

void subghz_protocol_decoder_chamb_code_reset(void* context);

void subghz_protocol_decoder_chamb_code_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_chamb_code_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_chamb_code_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_chamb_code_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_chamb_code_get_string(void* context, FuriString* output);

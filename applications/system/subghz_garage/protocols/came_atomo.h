#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_CAME_ATOMO_NAME "CAME Atomo"

typedef struct SubGhzProtocolDecoderCameAtomo SubGhzProtocolDecoderCameAtomo;
typedef struct SubGhzProtocolEncoderCameAtomo SubGhzProtocolEncoderCameAtomo;

extern const SubGhzProtocolDecoder subghz_protocol_came_atomo_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_came_atomo_encoder;
extern const SubGhzProtocol subghz_protocol_came_atomo;

void atomo_decrypt(uint8_t* buff);

void atomo_encrypt(uint8_t* buff);

void* subghz_protocol_encoder_came_atomo_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_came_atomo_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_came_atomo_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_came_atomo_stop(void* context);

LevelDuration subghz_protocol_encoder_came_atomo_yield(void* context);

void* subghz_protocol_decoder_came_atomo_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_came_atomo_free(void* context);

void subghz_protocol_decoder_came_atomo_reset(void* context);

void subghz_protocol_decoder_came_atomo_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_came_atomo_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_came_atomo_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_came_atomo_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_came_atomo_get_string(void* context, FuriString* output);

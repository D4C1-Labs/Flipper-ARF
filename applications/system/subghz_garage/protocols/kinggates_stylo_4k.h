#pragma once
#include "base.h"

#define SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME "KingGates Stylo4k"

typedef struct SubGhzProtocolDecoderKingGates_stylo_4k SubGhzProtocolDecoderKingGates_stylo_4k;
typedef struct SubGhzProtocolEncoderKingGates_stylo_4k SubGhzProtocolEncoderKingGates_stylo_4k;

extern const SubGhzProtocolDecoder subghz_protocol_kinggates_stylo_4k_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_kinggates_stylo_4k_encoder;
extern const SubGhzProtocol subghz_protocol_kinggates_stylo_4k;

void* subghz_protocol_encoder_kinggates_stylo_4k_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_kinggates_stylo_4k_free(void* context);

SubGhzProtocolStatus subghz_protocol_encoder_kinggates_stylo_4k_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void subghz_protocol_encoder_kinggates_stylo_4k_stop(void* context);

LevelDuration subghz_protocol_encoder_kinggates_stylo_4k_yield(void* context);

void* subghz_protocol_decoder_kinggates_stylo_4k_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_kinggates_stylo_4k_free(void* context);

void subghz_protocol_decoder_kinggates_stylo_4k_reset(void* context);

void subghz_protocol_decoder_kinggates_stylo_4k_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_kinggates_stylo_4k_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_kinggates_stylo_4k_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus subghz_protocol_decoder_kinggates_stylo_4k_deserialize(
    void* context,
    FlipperFormat* flipper_format);

void subghz_protocol_decoder_kinggates_stylo_4k_get_string(void* context, FuriString* output);

#pragma once

#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_PROTOCOL_PSA2_NAME "PSA OLD"

typedef struct SubGhzProtocolDecoderPSA SubGhzProtocolDecoderPSA;
typedef struct SubGhzProtocolEncoderPSA SubGhzProtocolEncoderPSA;

extern const SubGhzProtocolDecoder subghz_protocol_psa_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_psa_encoder;
extern const SubGhzProtocol        subghz_protocol_psa2;

void* subghz_protocol_decoder_psa2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_psa2_free(void* context);

void subghz_protocol_decoder_psa2_reset(void* context);

void subghz_protocol_decoder_psa2_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_psa2_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_psa2_serialize(
    void*             context,
    FlipperFormat*    ff,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus subghz_protocol_decoder_psa2_deserialize(
    void*          context,
    FlipperFormat* ff);

void subghz_protocol_decoder_psa2_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_psa2_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_psa2_free(void* context);

SubGhzProtocolStatus subghz_protocol_encoder_psa2_deserialize(
    void*          context,
    FlipperFormat* ff);

void subghz_protocol_encoder_psa2_stop(void* context);

LevelDuration subghz_protocol_encoder_psa2_yield(void* context);

#ifdef __cplusplus
}
#endif

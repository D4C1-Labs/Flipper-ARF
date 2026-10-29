#pragma once

#include "base.h"
#include <flipper_format/flipper_format.h>

#define SUBGHZ_PROTOCOL_PSA_NAME "PSA GROUP"

typedef struct SubGhzProtocolDecoderPSA SubGhzProtocolDecoderPSA;
typedef struct SubGhzProtocolEncoderPSA SubGhzProtocolEncoderPSA;

extern const SubGhzProtocol subghz_protocol_psa;

void* subghz_protocol_decoder_psa_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_psa_free(void* context);
void subghz_protocol_decoder_psa_reset(void* context);
void subghz_protocol_decoder_psa_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_psa_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_psa_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_psa_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_psa_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_psa_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_psa_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_psa_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_psa_stop(void* context);
LevelDuration subghz_protocol_encoder_psa_yield(void* context);

typedef bool (*PsaDecryptProgressCallback)(uint8_t progress, uint32_t keys_tested, void* context);

bool subghz_protocol_psa_decrypt_file(
    FlipperFormat* flipper_format,
    FuriString* result_str,
    PsaDecryptProgressCallback progress_cb,
    void* progress_ctx);

bool subghz_protocol_psa_get_bf_params(
    FlipperFormat* flipper_format,
    uint32_t* w0,
    uint32_t* w1);

bool subghz_protocol_psa_apply_bf_result(
    FlipperFormat* flipper_format,
    FuriString* result_str,
    uint32_t counter,
    uint32_t dec_v0,
    uint32_t dec_v1,
    int bf_type);

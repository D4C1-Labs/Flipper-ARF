#pragma once

#include "base.h"
#include "public_api.h"

#define SUBGHZ_PROTOCOL_KEELOQ_NAME "KeeLoq"
#define KEELOQ_MF_UNKNOWN           "KL Unknown"

typedef struct SubGhzProtocolDecoderKeeloq SubGhzProtocolDecoderKeeloq;
typedef struct SubGhzProtocolEncoderKeeloq SubGhzProtocolEncoderKeeloq;

extern const SubGhzProtocolDecoder subghz_protocol_keeloq_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_keeloq_encoder;
extern const SubGhzProtocol subghz_protocol_keeloq;

void* subghz_protocol_encoder_keeloq_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_keeloq_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_keeloq_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_keeloq_stop(void* context);

LevelDuration subghz_protocol_encoder_keeloq_yield(void* context);

void* subghz_protocol_decoder_keeloq_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_keeloq_free(void* context);

void subghz_protocol_decoder_keeloq_reset(void* context);

void subghz_protocol_decoder_keeloq_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_keeloq_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_keeloq_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_keeloq_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_keeloq_get_string(void* context, FuriString* output);

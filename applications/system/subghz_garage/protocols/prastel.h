#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_PRASTEL_NAME "Prastel"

/* The 42 bit frame only carries these bits of the serial; the others are not sent
 * and read back as zero */
#define SUBGHZ_PROTOCOL_PRASTEL_SERIAL_MASK (0x001FFFFFUL)

typedef struct SubGhzProtocolDecoderPrastel SubGhzProtocolDecoderPrastel;
typedef struct SubGhzProtocolEncoderPrastel SubGhzProtocolEncoderPrastel;

extern const SubGhzProtocolDecoder subghz_protocol_prastel_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_prastel_encoder;
extern const SubGhzProtocol subghz_protocol_prastel;

void* subghz_protocol_encoder_prastel_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_prastel_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_prastel_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_prastel_stop(void* context);

LevelDuration subghz_protocol_encoder_prastel_yield(void* context);

void* subghz_protocol_decoder_prastel_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_prastel_free(void* context);

void subghz_protocol_decoder_prastel_reset(void* context);

void subghz_protocol_decoder_prastel_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_prastel_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_prastel_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_prastel_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_prastel_get_string(void* context, FuriString* output);

#pragma once
#include <furi.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_decoder.h>
#include <flipper_format/flipper_format.h>

#include "../defines.h"

#define SUBGHZ_PROTOCOL_SCHER_KHAN_NAME "Scher-Khan"

typedef struct SubGhzProtocolDecoderScherKhan SubGhzProtocolDecoderScherKhan;

extern const SubGhzProtocolDecoder subghz_protocol_scher_khan_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_scher_khan_encoder;
extern const SubGhzProtocol subghz_protocol_scher_khan;

void* subghz_protocol_decoder_scher_khan_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_scher_khan_free(void* context);

void subghz_protocol_decoder_scher_khan_reset(void* context);

void subghz_protocol_decoder_scher_khan_feed(void* context, bool level, uint32_t duration);

SubGhzProtocolStatus subghz_protocol_decoder_scher_khan_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_scher_khan_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_scher_khan_get_string(void* context, FuriString* output);

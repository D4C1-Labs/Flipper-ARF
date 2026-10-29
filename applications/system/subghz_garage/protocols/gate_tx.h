#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_GATE_TX_NAME "GateTX"

typedef struct SubGhzProtocolDecoderGateTx SubGhzProtocolDecoderGateTx;
typedef struct SubGhzProtocolEncoderGateTx SubGhzProtocolEncoderGateTx;

extern const SubGhzProtocolDecoder subghz_protocol_gate_tx_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_gate_tx_encoder;
extern const SubGhzProtocol subghz_protocol_gate_tx;

void* subghz_protocol_encoder_gate_tx_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_gate_tx_free(void* context);

SubGhzProtocolStatus
    subghz_protocol_encoder_gate_tx_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_encoder_gate_tx_stop(void* context);

LevelDuration subghz_protocol_encoder_gate_tx_yield(void* context);

void* subghz_protocol_decoder_gate_tx_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_gate_tx_free(void* context);

void subghz_protocol_decoder_gate_tx_reset(void* context);

void subghz_protocol_decoder_gate_tx_feed(void* context, bool level, uint32_t duration);

uint8_t subghz_protocol_decoder_gate_tx_get_hash_data(void* context);

SubGhzProtocolStatus subghz_protocol_decoder_gate_tx_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

SubGhzProtocolStatus
    subghz_protocol_decoder_gate_tx_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_gate_tx_get_string(void* context, FuriString* output);

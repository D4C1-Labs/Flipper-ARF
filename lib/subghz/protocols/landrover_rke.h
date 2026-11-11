#pragma once

#include "base.h"

#define LAND_ROVER_RKE_PROTOCOL_NAME "Land Rover RKE"

typedef struct SubGhzProtocolDecoderLandRoverRke SubGhzProtocolDecoderLandRoverRke;
typedef struct SubGhzProtocolEncoderLandRoverRke SubGhzProtocolEncoderLandRoverRke;

extern const SubGhzProtocol subghz_protocol_land_rover_rke;

void* subghz_protocol_decoder_land_rover_rke_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_land_rover_rke_free(void* context);
void subghz_protocol_decoder_land_rover_rke_reset(void* context);
void subghz_protocol_decoder_land_rover_rke_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_land_rover_rke_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_land_rover_rke_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_land_rover_rke_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_land_rover_rke_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_land_rover_rke_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_land_rover_rke_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_land_rover_rke_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_land_rover_rke_stop(void* context);
LevelDuration subghz_protocol_encoder_land_rover_rke_yield(void* context);

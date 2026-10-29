#pragma once

#include "base.h"

#define SUBGHZ_PROTOCOL_RAW_NAME "RAW"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*SubGhzProtocolEncoderRAWCallbackEnd)(void* context);

typedef struct SubGhzProtocolDecoderRAW SubGhzProtocolDecoderRAW;
typedef struct SubGhzProtocolEncoderRAW SubGhzProtocolEncoderRAW;

extern const SubGhzProtocolDecoder subghz_protocol_raw_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_raw_encoder;
extern const SubGhzProtocol subghz_protocol_raw;

bool subghz_protocol_raw_save_to_file_init(
    SubGhzProtocolDecoderRAW* instance,
    const char* dev_name,
    SubGhzRadioPreset* preset);

void subghz_protocol_raw_save_to_file_stop(SubGhzProtocolDecoderRAW* instance);

size_t subghz_protocol_raw_get_sample_write(SubGhzProtocolDecoderRAW* instance);

void* subghz_protocol_decoder_raw_alloc(SubGhzEnvironment* environment);

void subghz_protocol_decoder_raw_free(void* context);

void subghz_protocol_decoder_raw_reset(void* context);

void subghz_protocol_decoder_raw_feed(void* context, bool level, uint32_t duration);

SubGhzProtocolStatus
    subghz_protocol_decoder_raw_deserialize(void* context, FlipperFormat* flipper_format);

void subghz_protocol_decoder_raw_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_raw_alloc(SubGhzEnvironment* environment);

void subghz_protocol_encoder_raw_free(void* context);

void subghz_protocol_encoder_raw_stop(void* context);

void subghz_protocol_raw_save_to_file_pause(SubGhzProtocolDecoderRAW* instance, bool pause);

void subghz_protocol_raw_file_encoder_worker_set_callback_end(
    SubGhzProtocolEncoderRAW* instance,
    SubGhzProtocolEncoderRAWCallbackEnd callback_end,
    void* context_end);

void subghz_protocol_raw_gen_fff_data(
    FlipperFormat* flipper_format,
    const char* file_path,
    const char* radio_dev_name);

SubGhzProtocolStatus
    subghz_protocol_encoder_raw_deserialize(void* context, FlipperFormat* flipper_format);

LevelDuration subghz_protocol_encoder_raw_yield(void* context);

#ifdef __cplusplus
}
#endif

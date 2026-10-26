#pragma once

#include <lib/subghz/protocols/base.h>

#define SUBGHZ_PROTOCOL_URMET_NAME "Urmet"

typedef struct SubGhzProtocolDecoderUrmet SubGhzProtocolDecoderUrmet;
typedef struct SubGhzProtocolEncoderUrmet SubGhzProtocolEncoderUrmet;

extern const SubGhzProtocolDecoder subghz_protocol_urmet_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_urmet_encoder;
extern const SubGhzProtocol subghz_protocol_urmet;

/**
 * Allocate SubGhzProtocolEncoderUrmet.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return SubGhzProtocolEncoderUrmet* pointer to a SubGhzProtocolEncoderUrmet instance
 */
void* subghz_protocol_encoder_urmet_alloc(SubGhzEnvironment* environment);

/**
 * Free SubGhzProtocolEncoderUrmet.
 * @param context Pointer to a SubGhzProtocolEncoderUrmet instance
 */
void subghz_protocol_encoder_urmet_free(void* context);

/**
 * Deserialize and generating an upload to send.
 * @param context Pointer to a SubGhzProtocolEncoderUrmet instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
SubGhzProtocolStatus
    subghz_protocol_encoder_urmet_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Forced transmission stop.
 * @param context Pointer to a SubGhzProtocolEncoderUrmet instance
 */
void subghz_protocol_encoder_urmet_stop(void* context);

/**
 * Getting the level and duration of the upload to be loaded into DMA.
 * @param context Pointer to a SubGhzProtocolEncoderUrmet instance
 * @return LevelDuration
 */
LevelDuration subghz_protocol_encoder_urmet_yield(void* context);

/**
 * Allocate SubGhzProtocolDecoderUrmet.
 * @param environment Pointer to a SubGhzEnvironment instance
 * @return SubGhzProtocolDecoderUrmet* pointer to a SubGhzProtocolDecoderUrmet instance
 */
void* subghz_protocol_decoder_urmet_alloc(SubGhzEnvironment* environment);

/**
 * Free SubGhzProtocolDecoderUrmet.
 * @param context Pointer to a SubGhzProtocolDecoderUrmet instance
 */
void subghz_protocol_decoder_urmet_free(void* context);

/**
 * Reset decoder SubGhzProtocolDecoderUrmet.
 * @param context Pointer to a SubGhzProtocolDecoderUrmet instance
 */
void subghz_protocol_decoder_urmet_reset(void* context);

/**
 * Parse a raw sequence of levels and durations received from the air.
 * @param context Pointer to a SubGhzProtocolDecoderUrmet instance
 * @param level Signal level true-high false-low
 * @param duration Duration of this level in, us
 */
void subghz_protocol_decoder_urmet_feed(void* context, bool level, uint32_t duration);

/**
 * Getting the hash sum of the last randomly received parcel.
 * @param context Pointer to a SubGhzProtocolDecoderUrmet instance
 * @return hash Hash sum
 */
uint8_t subghz_protocol_decoder_urmet_get_hash_data(void* context);

/**
 * Serialize data SubGhzProtocolDecoderUrmet.
 * @param context Pointer to a SubGhzProtocolDecoderUrmet instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @param preset The modulation on which the signal was received, SubGhzRadioPreset
 * @return status
 */
SubGhzProtocolStatus subghz_protocol_decoder_urmet_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserialize data SubGhzProtocolDecoderUrmet.
 * @param context Pointer to a SubGhzProtocolDecoderUrmet instance
 * @param flipper_format Pointer to a FlipperFormat instance
 * @return status
 */
SubGhzProtocolStatus
    subghz_protocol_decoder_urmet_deserialize(void* context, FlipperFormat* flipper_format);

/**
 * Getting a textual representation of the received data.
 * @param context Pointer to a SubGhzProtocolDecoderUrmet instance
 * @param output Resulting text
 */
void subghz_protocol_decoder_urmet_get_string(void* context, FuriString* output);

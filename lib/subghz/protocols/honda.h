#pragma once

#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define HONDA_PROTOCOL_NAME "Honda"

extern const SubGhzProtocol subghz_protocol_honda;
extern const SubGhzProtocolDecoder subghz_protocol_honda_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_honda_encoder;

/**
 * Allocates memory for the Honda protocol decoder.
 * @param environment Pointer to SubGhzEnvironment
 * @return Pointer to the allocated decoder instance
 */
void* subghz_protocol_decoder_honda_alloc(SubGhzEnvironment* environment);

/**
 * Frees memory used by the Honda protocol decoder.
 * @param context Pointer to the decoder instance
 */
void subghz_protocol_decoder_honda_free(void* context);

/**
 * Resets the Honda protocol decoder state.
 * @param context Pointer to the decoder instance
 */
void subghz_protocol_decoder_honda_reset(void* context);

/**
 * Feeds a pulse/gap into the Honda protocol decoder.
 * @param context Pointer to the decoder instance
 * @param level Signal level (true = high, false = low)
 * @param duration Duration of the level in microseconds
 */
void subghz_protocol_decoder_honda_feed(void* context, bool level, uint32_t duration);

/**
 * Returns a hash of the decoded Honda data.
 * @param context Pointer to the decoder instance
 * @return Hash byte
 */
uint8_t subghz_protocol_decoder_honda_get_hash_data(void* context);

/**
 * Serializes the decoded Honda data into a FlipperFormat file.
 * @param context Pointer to the decoder instance
 * @param flipper_format Pointer to the FlipperFormat instance
 * @param preset Pointer to the radio preset
 * @return SubGhzProtocolStatus result
 */
SubGhzProtocolStatus subghz_protocol_decoder_honda_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserializes Honda data from a FlipperFormat file.
 * @param context Pointer to the decoder instance
 * @param flipper_format Pointer to the FlipperFormat instance
 * @return SubGhzProtocolStatus result
 */
SubGhzProtocolStatus subghz_protocol_decoder_honda_deserialize(
    void* context,
    FlipperFormat* flipper_format);

/**
 * Formats the decoded Honda data into a human-readable string.
 * @param context Pointer to the decoder instance
 * @param output Pointer to the FuriString output buffer
 */
void subghz_protocol_decoder_honda_get_string(void* context, FuriString* output);

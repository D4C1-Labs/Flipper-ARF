#pragma once

#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define CITROEN_PROTOCOL_NAME "Citroen"

extern const SubGhzProtocol subghz_protocol_citroen;
extern const SubGhzProtocolDecoder subghz_protocol_citroen_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_citroen_encoder;

/**
 * Allocates memory for the Citroën protocol decoder.
 * @param environment Pointer to SubGhzEnvironment
 * @return Pointer to the allocated decoder instance
 */
void* subghz_protocol_decoder_citroen_alloc(SubGhzEnvironment* environment);

/**
 * Frees memory used by the Citroën protocol decoder.
 * @param context Pointer to the decoder instance
 */
void subghz_protocol_decoder_citroen_free(void* context);

/**
 * Resets the Citroën protocol decoder state.
 * @param context Pointer to the decoder instance
 */
void subghz_protocol_decoder_citroen_reset(void* context);

/**
 * Feeds a pulse/gap into the Citroën protocol decoder.
 * @param context Pointer to the decoder instance
 * @param level Signal level (true = high, false = low)
 * @param duration Duration of the level in microseconds
 */
void subghz_protocol_decoder_citroen_feed(void* context, bool level, uint32_t duration);

/**
 * Returns a hash of the decoded Citroën data.
 * @param context Pointer to the decoder instance
 * @return Hash byte
 */
uint8_t subghz_protocol_decoder_citroen_get_hash_data(void* context);

/**
 * Serializes the decoded Citroën data into a FlipperFormat file.
 * @param context Pointer to the decoder instance
 * @param flipper_format Pointer to the FlipperFormat instance
 * @param preset Pointer to the radio preset
 * @return SubGhzProtocolStatus result
 */
SubGhzProtocolStatus subghz_protocol_decoder_citroen_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserializes Citroën data from a FlipperFormat file.
 * @param context Pointer to the decoder instance
 * @param flipper_format Pointer to the FlipperFormat instance
 * @return SubGhzProtocolStatus result
 */
SubGhzProtocolStatus subghz_protocol_decoder_citroen_deserialize(
    void* context,
    FlipperFormat* flipper_format);

/**
 * Formats the decoded Citroën data into a human-readable string.
 * @param context Pointer to the decoder instance
 * @param output Pointer to the FuriString output buffer
 */
void subghz_protocol_decoder_citroen_get_string(void* context, FuriString* output);

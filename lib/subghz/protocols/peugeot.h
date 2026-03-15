#pragma once

#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define PEUGEOT_PROTOCOL_NAME "Peugeot"

extern const SubGhzProtocol subghz_protocol_peugeot;
extern const SubGhzProtocolDecoder subghz_protocol_peugeot_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_peugeot_encoder;

/**
 * Allocates memory for the Peugeot protocol decoder.
 * @param environment Pointer to SubGhzEnvironment
 * @return Pointer to the allocated decoder instance
 */
void* subghz_protocol_decoder_peugeot_alloc(SubGhzEnvironment* environment);

/**
 * Frees memory used by the Peugeot protocol decoder.
 * @param context Pointer to the decoder instance
 */
void subghz_protocol_decoder_peugeot_free(void* context);

/**
 * Resets the Peugeot protocol decoder state.
 * @param context Pointer to the decoder instance
 */
void subghz_protocol_decoder_peugeot_reset(void* context);

/**
 * Feeds a pulse/gap into the Peugeot protocol decoder.
 * @param context Pointer to the decoder instance
 * @param level Signal level (true = high, false = low)
 * @param duration Duration of the level in microseconds
 */
void subghz_protocol_decoder_peugeot_feed(void* context, bool level, uint32_t duration);

/**
 * Returns a hash of the decoded Peugeot data.
 * @param context Pointer to the decoder instance
 * @return Hash byte
 */
uint8_t subghz_protocol_decoder_peugeot_get_hash_data(void* context);

/**
 * Serializes the decoded Peugeot data into a FlipperFormat file.
 * @param context Pointer to the decoder instance
 * @param flipper_format Pointer to the FlipperFormat instance
 * @param preset Pointer to the radio preset
 * @return SubGhzProtocolStatus result
 */
SubGhzProtocolStatus subghz_protocol_decoder_peugeot_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserializes Peugeot data from a FlipperFormat file.
 * @param context Pointer to the decoder instance
 * @param flipper_format Pointer to the FlipperFormat instance
 * @return SubGhzProtocolStatus result
 */
SubGhzProtocolStatus subghz_protocol_decoder_peugeot_deserialize(
    void* context,
    FlipperFormat* flipper_format);

/**
 * Formats the decoded Peugeot data into a human-readable string.
 * @param context Pointer to the decoder instance
 * @param output Pointer to the FuriString output buffer
 */
void subghz_protocol_decoder_peugeot_get_string(void* context, FuriString* output);

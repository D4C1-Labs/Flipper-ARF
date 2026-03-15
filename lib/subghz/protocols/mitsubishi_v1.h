#pragma once

#include <lib/subghz/protocols/base.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>

#define MITSUBISHI_PROTOCOL_NAME "Mitsubishi v1"

extern const SubGhzProtocol subghz_protocol_mitsubishi_v1;
extern const SubGhzProtocolDecoder subghz_protocol_mitsubishi_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_mitsubishi_encoder;

/**
 * Allocates memory for the Mitsubishi protocol decoder.
 * @param environment Pointer to SubGhzEnvironment
 * @return Pointer to the allocated decoder instance
 */
void* subghz_protocol_decoder_mitsubishi_alloc(SubGhzEnvironment* environment);

/**
 * Frees memory used by the Mitsubishi protocol decoder.
 * @param context Pointer to the decoder instance
 */
void subghz_protocol_decoder_mitsubishi_free(void* context);

/**
 * Resets the Mitsubishi protocol decoder state.
 * @param context Pointer to the decoder instance
 */
void subghz_protocol_decoder_mitsubishi_reset(void* context);

/**
 * Feeds a pulse/gap into the Mitsubishi protocol decoder.
 * @param context Pointer to the decoder instance
 * @param level Signal level (true = high, false = low)
 * @param duration Duration of the level in microseconds
 */
void subghz_protocol_decoder_mitsubishi_feed(void* context, bool level, uint32_t duration);

/**
 * Returns a hash of the decoded Mitsubishi data.
 * @param context Pointer to the decoder instance
 * @return Hash byte
 */
uint8_t subghz_protocol_decoder_mitsubishi_get_hash_data(void* context);

/**
 * Serializes the decoded Mitsubishi data into a FlipperFormat file.
 * @param context Pointer to the decoder instance
 * @param flipper_format Pointer to the FlipperFormat instance
 * @param preset Pointer to the radio preset
 * @return SubGhzProtocolStatus result
 */
SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);

/**
 * Deserializes Mitsubishi data from a FlipperFormat file.
 * @param context Pointer to the decoder instance
 * @param flipper_format Pointer to the FlipperFormat instance
 * @return SubGhzProtocolStatus result
 */
SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_deserialize(
    void* context,
    FlipperFormat* flipper_format);

/**
 * Formats the decoded Mitsubishi data into a human-readable string.
 * @param context Pointer to the decoder instance
 * @param output Pointer to the FuriString output buffer
 */
void subghz_protocol_decoder_mitsubishi_get_string(void* context, FuriString* output);

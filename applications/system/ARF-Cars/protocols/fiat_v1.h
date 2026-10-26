#pragma once

#include <furi.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <flipper_format/flipper_format.h>

#define FIAT_V1_PROTOCOL_NAME "Fiat V1"

typedef struct SubGhzProtocolDecoderFiatV1 SubGhzProtocolDecoderFiatV1;
typedef struct SubGhzProtocolEncoderFiatV1 SubGhzProtocolEncoderFiatV1;

extern const SubGhzProtocol fiat_v1_protocol;

void* subghz_protocol_decoder_fiat_v1_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_fiat_v1_reset(void* context);
void subghz_protocol_decoder_fiat_v1_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_fiat_v1_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_fiat_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_fiat_v1_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_fiat_v1_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_fiat_v1_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_fiat_v1_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_fiat_v1_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_fiat_v1_stop(void* context);
LevelDuration subghz_protocol_encoder_fiat_v1_yield(void* context);

// [HITAG2_BF] Public crypto API exposed for Hitag2 bruteforce helper
#define FIAT_V1_KNOWN_KEY_COUNT 8U

/**
 * Compute the 32-bit authenticator for a given (uid, button, control, key, epoch).
 * This is the Hitag2-based cipher used by Fiat V1 keyfobs.
 * @param uid 32-bit vehicle UID
 * @param button 4-bit button code (0x1/0x2/0x4/0x8)
 * @param control 10-bit rolling counter
 * @param key 48-bit master key (6 bytes)
 * @param epoch 18-bit epoch (typically 0)
 * @return 32-bit auth field (matches raw[7..11] of the frame)
 */
uint32_t subghz_protocol_fiat_v1_compute_auth(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch);

/**
 * Verify if a candidate key matches a captured (uid, button, control, hop).
 * @return true if the key produces the observed hop
 */
bool subghz_protocol_fiat_v1_verify_key(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t hop,
    const uint8_t key[6],
    uint32_t epoch);

/**
 * Access the hard-coded 8 known keys table.
 */
const uint8_t (*subghz_protocol_fiat_v1_get_known_keys(void))[6];

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
#include "hitag2_seed.h" // Hitag2SeedProgressCallback for the cooperative Seed BF

#define RENAULT_PROTOCOL_V1_NAME "Renault V1"

extern const SubGhzProtocol renault_v1_protocol;

void* subghz_protocol_decoder_renault_v1_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_renault_v1_reset(void* context);
void subghz_protocol_decoder_renault_v1_feed(void* context, bool level, uint32_t duration);
uint8_t subghz_protocol_decoder_renault_v1_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_renault_v1_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset);
SubGhzProtocolStatus
    subghz_protocol_decoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_renault_v1_get_string(void* context, FuriString* output);

void* subghz_protocol_encoder_renault_v1_alloc(SubGhzEnvironment* environment);
void subghz_protocol_encoder_renault_v1_free(void* context);
SubGhzProtocolStatus
    subghz_protocol_encoder_renault_v1_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_encoder_renault_v1_stop(void* context);
LevelDuration subghz_protocol_encoder_renault_v1_yield(void* context);

// [HITAG2_BF] Renault V1 reuses the Fiat V1 hitag2 cipher. Unlike Fiat V2, the
// 32-bit hop is NOT at a fixed byte position: Renault packs its 82 logical bits
// as 64-bit `data` + 18-bit `key2`, and the exact 32-bit slice within the 42
// "spare" payload bits that carries the hop is UNKNOWN (pending real capture).
// We therefore auto-detect BOTH the hop bit-slice AND the IV normalization combo.
//
// The public helpers below let the BF scene reproduce the exact same search from
// the stored fields (serial/button/counter + payload42) so both trees agree.

// Number of candidate 32-bit hop slices tried within the 42 payload bits.
#define RENAULT_V1_HOP_SLICE_COUNT 3U

// Number of IV normalization combos (2 button options x 2 control options).
#define RENAULT_V1_IV_COMBO_COUNT 4U

/**
 * Assemble the 42-bit payload field from the low 24 bits of `data` and the low
 * 18 bits of `key2` (MSB-first: data[23:0] as the top 24 bits, key2[17:0] as
 * the low 18 bits). Returned as a uint64_t with the payload in bits [41:0].
 */
uint64_t subghz_protocol_renault_v1_payload42(uint64_t data, uint32_t key2);

/**
 * Extract the candidate 32-bit hop for a given slice index (0..HOP_SLICE_COUNT-1)
 * from the 42-bit payload. slice start offsets are counted from the MSB of the
 * 42-bit field: {0, 5, 10}. hop = (payload42 >> (42 - 32 - start)) & 0xFFFFFFFF.
 * These candidate positions are pending validation against a real capture.
 */
uint32_t subghz_protocol_renault_v1_candidate_hop(uint64_t payload42, uint8_t slice);

/**
 * Compute the 4-bit button code fed to the hitag2 IV for a given combo index.
 * combo bit0 = button option (0 = counter low nibble of button, 1 = one-hot remap).
 */
uint8_t subghz_protocol_renault_v1_iv_button(uint8_t button, uint8_t combo);

/**
 * Compute the 10-bit control fed to the hitag2 IV for a given combo index.
 * combo bit1 = control option (0 = counter & 0x3FF, 1 = (~counter) & 0x3FF).
 */
uint16_t subghz_protocol_renault_v1_iv_control(uint8_t counter, uint8_t combo);

// ---------------------------------------------------------------------------
// [HITAG2_SEED] Classic-Hitag2 SEED model (ported from ProtoPirate).
//
// This is an ADDITIVE, second recovery/TX path that is completely SEPARATE from
// the Fiat-BCM key-recovery above and from the Hitag2Hell BF engine. It uses the
// classic byte-array Hitag2 cipher (lib/subghz/protocols/hitag2_seed.c) to:
//   (a) recover a 4-byte SEED from a captured frame, and
//   (b) forward re-encode a valid NEXT code from serial+cnt+btn+seed.
//
// The SEED is persisted in the .sub via the "Seed" field and its recovered-state
// via "Recovered". These fields are OPTIONAL on load: old .sub files without them
// still deserialize fine.
// ---------------------------------------------------------------------------

// Flipper format field name carrying the recovered 4-byte SEED (big-endian hex).
#define RENAULT_V1_SEED_FIELD "Seed"

// Flipper format field name carrying the classic-Hitag2 recovered marker.
#define RENAULT_V1_RECOVERED_FIELD "Recovered"

// Flipper format field name carrying the 18-bit key2 (uint32). Exposed here so
// the manual "Seed BF" scene can read it back from a saved signal.
#define RENAULT_V1_KEY2_FIELD "Key2"

/**
 * [HITAG2_SEED] Manual, on-demand classic-Hitag2 SEED brute force.
 *
 * Runs the heavy (~0x40000-candidate) classic Hitag2 brute force over a single
 * decoded Renault V1 frame given its `data` (64-bit) and `key2` (18-bit). This
 * is intentionally NOT called during live capture or on signal load; it is
 * invoked ONLY from the saved-signal "Seed BF" menu action so that capturing
 * never stalls and no button presses are dropped.
 *
 * @param      data      the decoded 64-bit frame body
 * @param      key2      the 18-bit key2 field
 * @param[out] seed_out  receives the recovered 4-byte SEED on success
 * @return true if a SEED was recovered, false on brute-force miss
 */
bool subghz_protocol_renault_v1_run_seed_bf(uint64_t data, uint32_t key2, uint32_t* seed_out);

/**
 * [HITAG2_SEED] Cooperative variant of the Seed BF. Invokes @p progress_cb every
 * HITAG2_SEED_BF_YIELD_STEP candidates so the worker thread can yield the CPU
 * (furi_delay_ms), report progress, and cancel — preventing the single-core M4
 * from freezing under the tight brute-force loop. progress_cb == NULL behaves
 * like subghz_protocol_renault_v1_run_seed_bf().
 */
bool subghz_protocol_renault_v1_run_seed_bf_ex(
    uint64_t data,
    uint32_t key2,
    uint32_t* seed_out,
    Hitag2SeedProgressCallback progress_cb,
    void* progress_ctx);

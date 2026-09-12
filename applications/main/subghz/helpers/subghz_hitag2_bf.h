#pragma once

// [HITAG2_BF] Hitag2 bruteforce state machine for Fiat V1
//
// Attack strategy is a cascade of levels; each level is tried in order and
// stops as soon as a matching key is found:
//   L1 - 8 hardcoded known keys (from fiat_v1.c)
//   L2 - Extended dictionary embedded in flash (see subghz_hitag2_bf_dict.c)
//   L3 - Extended dictionary loaded from SD card
//        (path: apps_data/subghz/assets/hitag2, one hex key per line)
//   L4 - Heuristic mutations (UID-derived patterns, XOR masks, ASCII patterns)
//   L5 - Hitag2Hell guess-and-determine attack (Verstegen 2018, bitsliced 32-way)
//
// Multi-capture support: any number of captures (>=1) for the same UID can be
// added. Each candidate key is verified against ALL captures before being
// declared a match, reducing the false-positive rate from ~2^-32 (1 cap) to
// ~2^-64 (2 caps) to essentially 0.

#include <furi.h>
#include <stdint.h>
#include <stdbool.h>
#include <storage/storage.h>

#define SUBGHZ_HITAG2_BF_MAX_CAPTURES 8U
#define SUBGHZ_HITAG2_BF_SD_DICT_PATH "apps_data/subghz/assets/hitag2"

typedef struct {
    uint32_t uid;     // 32-bit vehicle UID
    uint16_t control; // 10-bit rolling counter
    uint8_t button;   // 4-bit button (1/2/4/8)
    uint32_t hop;     // 32-bit observed auth (ciphertext)
    uint8_t raw[14];  // Fiat V2 raw frame (for per-combo IV derivation); unused for V1
    bool is_fiat_v2;  // true if this capture is Fiat V2 (use combo-based IV)
    uint8_t iv_combo; // resolved IV combo (0..3) once a key validates; 0 for V1
    // [HITAG2_BF] Renault V1 support. Renault V1 reuses the Fiat V1 hitag2 cipher
    // but the 32-bit hop is at one of RENAULT_V1_HOP_SLICE_COUNT candidate
    // bit-slices of the 42-bit payload AND the IV is one of
    // RENAULT_V1_IV_COMBO_COUNT combos: a 3x4=12-way (slice, combo) search.
    bool is_renault_v1; // true => use payload42/slice + combo search
    uint64_t payload42; // Renault 42-bit payload (data[23:0]<<18 | key2)
    uint8_t rv1_button; // raw 8-bit button (for iv_button)
    uint8_t rv1_counter; // raw 8-bit counter (for iv_control)
    uint8_t hop_slice;  // resolved slice (0..2) once a key validates; 0 otherwise
} SubGhzHitag2BfCapture;

typedef enum {
    SubGhzHitag2BfLevelIdle = 0,
    SubGhzHitag2BfLevelKnown = 1,
    SubGhzHitag2BfLevelFlashDict = 2,
    SubGhzHitag2BfLevelSDDict = 3,
    SubGhzHitag2BfLevelHeuristic = 4,
    SubGhzHitag2BfLevelHitag2Hell = 5,
    SubGhzHitag2BfLevelDone = 6,
} SubGhzHitag2BfLevel;

typedef struct SubGhzHitag2Bf SubGhzHitag2Bf;

/**
 * Progress callback signature. Called by the cracker with the current stats.
 * Returning false requests cancellation.
 * @param level current level 1..5
 * @param level_name text label for UI
 * @param progress 0..100 within the current level
 * @param keys_tested total across all levels
 * @param context user context
 */
typedef bool (*SubGhzHitag2BfProgressCallback)(
    uint8_t level,
    const char* level_name,
    uint8_t progress,
    uint64_t keys_tested,
    void* context);

SubGhzHitag2Bf* subghz_hitag2_bf_alloc(void);
void subghz_hitag2_bf_free(SubGhzHitag2Bf* instance);

/**
 * Add a captured packet. All captures MUST share the same UID; second and
 * subsequent captures with a different UID are rejected.
 * @return true if added successfully
 */
bool subghz_hitag2_bf_add_capture(
    SubGhzHitag2Bf* instance,
    uint32_t uid,
    uint16_t control,
    uint8_t button,
    uint32_t hop);

/**
 * Add a captured Fiat V2 packet. The (control, button) IV pair is unknown
 * until validated: it is one of 4 combos derived from the 14-byte raw frame.
 * The stored control/button are combo-0 placeholders; the raw frame is kept so
 * verify/invert can try all 4 combos. All captures MUST share the same UID.
 * @return true if added successfully
 */
bool subghz_hitag2_bf_add_capture_v2(
    SubGhzHitag2Bf* instance,
    uint32_t uid,
    uint32_t hop,
    const uint8_t raw[14]);

/**
 * Add a captured Renault V1 packet. Renault V1 reuses the Fiat V1 hitag2 cipher
 * but has TWO unknown dimensions: the 32-bit hop bit-slice within the 42-bit
 * payload (RENAULT_V1_HOP_SLICE_COUNT candidates) AND the IV normalization combo
 * (RENAULT_V1_IV_COMBO_COUNT candidates). verify/invert search all
 * slice x combo pairs; a single (slice, combo) must validate every Renault V1
 * capture. All captures MUST share the same UID.
 * @param uid       serial & 0xFFFFFF
 * @param payload42 42-bit payload from subghz_protocol_renault_v1_payload42()
 * @param button    raw 8-bit button (for iv_button)
 * @param counter   raw 8-bit counter (for iv_control)
 * @return true if added successfully
 */
bool subghz_hitag2_bf_add_capture_renault_v1(
    SubGhzHitag2Bf* instance,
    uint32_t uid,
    uint64_t payload42,
    uint8_t button,
    uint8_t counter);

/**
 * Retrieve the resolved IV combo (0..3) of a stored capture after a key has
 * been found. Meaningful only for Fiat V2 and Renault V1 captures.
 */
uint8_t subghz_hitag2_bf_get_capture_iv_combo(const SubGhzHitag2Bf* instance, uint8_t index);

/**
 * Retrieve the resolved hop slice (0..2) of a stored capture after a key has
 * been found. Meaningful only for Renault V1 captures.
 */
uint8_t subghz_hitag2_bf_get_capture_hop_slice(const SubGhzHitag2Bf* instance, uint8_t index);

uint8_t subghz_hitag2_bf_get_capture_count(const SubGhzHitag2Bf* instance);
uint32_t subghz_hitag2_bf_get_uid(const SubGhzHitag2Bf* instance);

/**
 * Retrieve the fields of a single stored capture by index. Used to build the
 * BLE compute-offload request. Returns false if index is out of range.
 * @param index capture index (0 .. capture_count-1)
 * @param uid_out optional output for the 32-bit UID
 * @param control_out optional output for the 10-bit rolling counter
 * @param button_out optional output for the 4-bit button
 * @param hop_out optional output for the 32-bit observed auth
 */
bool subghz_hitag2_bf_get_capture(
    const SubGhzHitag2Bf* instance,
    uint8_t index,
    uint32_t* uid_out,
    uint16_t* control_out,
    uint8_t* button_out,
    uint32_t* hop_out);

/**
 * Whether the stored capture at `index` is a Fiat V2 capture.
 * @return false if index is out of range.
 */
bool subghz_hitag2_bf_get_capture_is_fiat_v2(const SubGhzHitag2Bf* instance, uint8_t index);

/**
 * Whether the stored capture at `index` is a Renault V1 capture.
 * @return false if index is out of range.
 */
bool subghz_hitag2_bf_get_capture_is_renault_v1(const SubGhzHitag2Bf* instance, uint8_t index);

/**
 * Retrieve the 14-byte Fiat V2 raw frame of a stored capture. Used to build the
 * combo/slice-aware BLE compute-offload request.
 * @param index   capture index (0 .. capture_count-1)
 * @param raw_out 14-byte buffer to receive the raw frame
 * @return false if index is out of range.
 */
bool subghz_hitag2_bf_get_capture_raw(
    const SubGhzHitag2Bf* instance,
    uint8_t index,
    uint8_t raw_out[14]);

/**
 * Retrieve the Renault V1 per-capture data (42-bit payload, raw 8-bit button,
 * raw 8-bit counter). Used to build the combo/slice-aware BLE offload request.
 * @param index         capture index (0 .. capture_count-1)
 * @param payload42_out optional output for the 42-bit payload
 * @param button_out    optional output for the raw 8-bit button
 * @param counter_out   optional output for the raw 8-bit counter
 * @return false if index is out of range.
 */
bool subghz_hitag2_bf_get_capture_rv1(
    const SubGhzHitag2Bf* instance,
    uint8_t index,
    uint64_t* payload42_out,
    uint8_t* button_out,
    uint8_t* counter_out);

/**
 * Set which levels are enabled. Bitmask: (1<<L1)|(1<<L2)|... Default: all.
 */
void subghz_hitag2_bf_set_levels(SubGhzHitag2Bf* instance, uint8_t levels_mask);

/**
 * Run the attack. Blocks until success, exhaustion, or cancellation.
 * The progress_cb is invoked periodically.
 * @return true on success (key found); result stored via getters below.
 */
bool subghz_hitag2_bf_run(
    SubGhzHitag2Bf* instance,
    SubGhzHitag2BfProgressCallback progress_cb,
    void* context);

/**
 * Retrieve the found key (after run returned true).
 * @param key_out output buffer (6 bytes)
 * @param epoch_out output epoch (usually 0)
 * @param level_out level where key was found (1..5)
 */
bool subghz_hitag2_bf_get_result(
    const SubGhzHitag2Bf* instance,
    uint8_t key_out[6],
    uint32_t* epoch_out,
    uint8_t* level_out);

uint64_t subghz_hitag2_bf_get_total_keys_tested(const SubGhzHitag2Bf* instance);

/**
 * Verify a candidate key against all stored captures.
 * @return true if the key produces the observed hop for every capture
 */
bool subghz_hitag2_bf_verify_multi(
    const SubGhzHitag2Bf* instance,
    const uint8_t key[6],
    uint32_t epoch);

/**
 * Verify a candidate key against all stored captures AND, on success, record the
 * winning IV combo (Fiat V2 and Renault V1) and hop slice (Renault V1) into the
 * matching captures so they can be read back via
 * subghz_hitag2_bf_get_capture_iv_combo() / _get_capture_hop_slice().
 *
 * This is the resolving counterpart of subghz_hitag2_bf_verify_multi(). It is
 * used on BLE offload success to locally re-resolve slice/combo for a key the
 * phone returned, instead of trusting a packed value over the wire.
 * @return true if the key validates every capture (with a single slice/combo).
 */
bool subghz_hitag2_bf_verify_multi_resolve_key(
    SubGhzHitag2Bf* instance,
    const uint8_t key[6],
    uint32_t epoch);

// --- Public accessors to the flash-embedded dictionary ---
uint32_t subghz_hitag2_bf_flash_dict_size(void);
const uint8_t (*subghz_hitag2_bf_flash_dict_get(uint32_t index))[6];

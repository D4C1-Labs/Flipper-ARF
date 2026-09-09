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

uint8_t subghz_hitag2_bf_get_capture_count(const SubGhzHitag2Bf* instance);
uint32_t subghz_hitag2_bf_get_uid(const SubGhzHitag2Bf* instance);

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

// --- Public accessors to the flash-embedded dictionary ---
uint32_t subghz_hitag2_bf_flash_dict_size(void);
const uint8_t (*subghz_hitag2_bf_flash_dict_get(uint32_t index))[6];

#pragma once

// [HITAG2_HELL] Bitsliced 32-way Hitag2Hell (guess-and-determine) attack
// adapted to the Fiat V1 BCM variant.
//
// Given a 32-bit authenticator (typically the 'Hop' field of a captured Fiat V1
// .sub file) plus the public inputs (uid, button, control, epoch), this module
// recovers plausible state31 values (state at the end of the init phase).
// Each state31 can then be inverted with hitag2_fiat_invert_init() to recover
// the 48-bit key.
//
// With only 32 keystream bits (the observed authenticator) the search is
// under-constrained: expect on the order of 2^(48-32) = 65536 spurious
// candidates in the worst case. In practice the layered pruning + early
// termination + filter propagation keeps live candidates within
// HITAG2_HELL_MAX_CANDIDATES for typical inputs, but callers should treat the
// results as a set that requires a SECOND capture (with different button /
// epoch / control) to disambiguate.
//
// Algorithm: 32-way bitsliced guess-and-determine, port of Hitag2Hell (see
// hitag2crack). Convention: state[0] = LSB. See subghz_hitag2_core.h for the
// core cipher.
//
// Memory footprint on the target (Cortex-M4):
//   - stack:  ~2 KB (state array of 80 bitslices of uint32_t = 320 B + locals
//                    inside deep_search)
//   - heap:   ~1 KB (candidates array in Hitag2HellResult = 128 * 8 bytes)
// Total: ~3 KB, well within a 4 KB thread stack.
//
// Expected wall time (single 32-bit authenticator, full 2^20 layer-0 sweep):
//   - x86 (2.5 GHz):        several hours (this port uses 32-lane bitslicing,
//                           vs. the reference 256-lane AVX2 implementation).
//   - Cortex-M4 (64 MHz):   many hours to ~1 day.
//
// For pragmatic use on device, callers SHOULD provide a work-splitting mechanism
// (l0_start / l0_end) plus a second capture to constrain candidates; see
// hitag2_hell_recover documentation.

#include <stdint.h>
#include <stdbool.h>

#include "subghz_hitag2_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of state31 candidates retained by the search.
#define HITAG2_HELL_MAX_CANDIDATES 128U

typedef struct {
    // Progress callback. Return false to abort the search. May be NULL.
    // Called at coarse-grained intervals (a few times per second at most).
    // Args:
    //   pct            : rough percentage 0..100 through the layer-0 sweep
    //   states_tested  : total scalar states tested so far
    //   ctx            : opaque
    bool (*progress_cb)(uint8_t pct, uint64_t states_tested, void* ctx);
    void* progress_ctx;

    // Approximate timeout in milliseconds. 0 = no timeout.
    // Enforced at the same granularity as progress_cb.
    uint32_t timeout_ms;

    // Optional platform-specific "now in ms" clock for enforcing timeout.
    // If NULL, timeout is disabled. On Flipper: pass furi_get_tick.
    uint32_t (*now_ms_cb)(void);

    // Optional restriction of the layer-0 sweep to indices [l0_start, l0_end).
    // Use (0, 1<<20) for a full sweep (default when both are 0).
    // Useful for splitting work across multiple runs or for tests that
    // start close to a known-good L0 index.
    uint32_t l0_start;
    uint32_t l0_end;   // exclusive; 0 means "full sweep"
} Hitag2HellConfig;

typedef struct {
    Hitag2State candidates[HITAG2_HELL_MAX_CANDIDATES];
    uint32_t candidate_count;   // number of valid entries in candidates[]
    uint64_t states_tested_total;
    bool cancelled;             // progress_cb returned false
    bool timed_out;             // timeout_ms exceeded
    bool overflow;              // true if > MAX_CANDIDATES matches were seen
} Hitag2HellResult;

/**
 * Run the Hitag2Hell attack on a Fiat V1 authenticator.
 *
 * @param authenticator 32-bit authenticator (e.g. 'Hop' field of the .sub).
 * @param config        Optional runtime configuration (may be NULL).
 * @param result        Output: candidate state31 values.
 * @return true iff at least one state31 candidate was found.
 */
bool hitag2_hell_recover(
    uint32_t authenticator,
    const Hitag2HellConfig* config,
    Hitag2HellResult* result);

/**
 * Deterministic self-test.
 *
 * For each of the 8 known Fiat V1 keys, generates an authenticator with a
 * pseudo-random (uid, button, control, epoch), computes the correct L0 index
 * from the true state31, and asks hitag2_hell_recover() to search ONLY that
 * L0 slot. Confirms the search kernel recovers the true state31 in every
 * trial. Runs in approximately 5..15 seconds on a mid-range x86 CPU.
 *
 * This is a KERNEL-correctness test, not a full-sweep integration test. A
 * production full-sweep run takes hours and should be exercised out of band.
 *
 * @return true iff every trial recovers the correct state31.
 */
bool hitag2_hell_self_test(void);

#ifdef __cplusplus
}
#endif

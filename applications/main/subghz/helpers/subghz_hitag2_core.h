#pragma once

// [HITAG2_CORE] Fiat V1 BCM Hitag2 cipher core
//
// This module reimplements the Fiat V1 BCM Hitag2 cipher (see fiat_v1.c) using
// a single 48-bit state packed into a uint64_t. This form is required by the
// upcoming bitsliced Hitag2Hell attack (32-way / 64-way parallel LFSR
// operations) and also enables the init-phase inversion needed for the
// guess-and-determine phase of that attack.
//
// State convention (compact):
//   * bit 0 = LSB = most-recently-shifted-in bit.
//   * bit 47 = MSB = about-to-be-discarded bit.
//   * Mapping from the byte-array representation used in fiat_v1.c:
//         state_bytes[i].bit[j]  <->  state_u64_bit[(5-i)*8 + j]
//
// Filter groups (bit indices into the u64 state):
//   g0 (fa=0x2C79): (41, 42, 44, 45)   // state[0].bit1,2,4,5
//   g1 (fb=0x6671): (32, 33, 35, 39)   // state[1].bit0,1,3,7
//   g2 (fb=0x6671): (21, 24, 26, 30)   // state[3].bit5, state[2].bit0,2,6
//   g3 (fb=0x6671): (14, 16, 18, 19)   // state[4].bit6, state[3].bit0,2,3
//   g4 (fa=0x2C79): ( 1,  3,  4, 13)   // state[5].bit1,3,4, state[4].bit5
//   Combined via fc=0x7907287B with g0 at bit0 of the LUT index, g4 at bit4.
//
// LFSR taps (bit indices into the u64 state):
//   {0, 1, 4, 5, 6, 17, 21, 24, 25, 31, 39, 40, 41, 44, 45, 47}
//
// These mappings are cross-validated bit-for-bit against fiat_v1.c on
// randomised states, and the full authenticator matches for all 8 known keys
// on hundreds of randomised (uid, button, control, epoch) tuples. See
// hitag2_fiat_self_test().

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 48-bit state packed into a uint64_t. Bit 0 is the LSB (most-recently
// shifted-in bit); bit 47 is the MSB (about to be discarded).
typedef uint64_t Hitag2State;

#define HITAG2_STATE_MASK ((uint64_t)0xFFFFFFFFFFFFULL)

/** Fiat V1 filter function: 48-bit state -> 1 output bit. */
uint8_t hitag2_fiat_filter(Hitag2State state);

/** Fiat V1 LFSR feedback: 48-bit state -> 1 output bit. */
uint8_t hitag2_fiat_lfsr_feedback(Hitag2State state);

/** Shift the state left by one, appending `input` in bit 0. */
static inline Hitag2State hitag2_shift(Hitag2State state, uint8_t input) {
    return ((state << 1U) | ((uint64_t)(input & 1U))) & HITAG2_STATE_MASK;
}

/**
 * Run the 32-round init phase and return the state at the end of the phase
 * (i.e. the state right before the first output round). This is exactly the
 * state produced by fiat_v1_bcm_generate_authenticator() at the end of its
 * first for-loop.
 */
Hitag2State hitag2_fiat_init_phase(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch);

/**
 * Run the complete cipher (init phase + 32 output rounds) and return the
 * 32-bit authenticator. Byte-for-byte identical to
 * subghz_protocol_fiat_v1_compute_auth() / fiat_v1_bcm_generate_authenticator().
 */
uint32_t hitag2_fiat_full_auth(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch);

/**
 * Invert the init phase: given state31 (state at end of init) plus the public
 * inputs (uid, iv=derived from button+control+epoch), recover the 48-bit key.
 *
 * Returns true when the reconstruction is self-consistent (the top 32 bits
 * of the recovered initial state equal `uid`); always true when `state31` is
 * genuinely the output of hitag2_fiat_init_phase() for the given inputs.
 */
bool hitag2_fiat_invert_init(
    Hitag2State state31,
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t epoch,
    uint8_t key_out[6]);

/**
 * Self-test: for each of the 8 known Fiat V1 keys, verify
 *   1) hitag2_fiat_full_auth() matches subghz_protocol_fiat_v1_compute_auth();
 *   2) hitag2_fiat_invert_init() recovers the key from state31.
 * Uses a fixed pseudo-random sequence of (uid, button, control, epoch) to keep
 * the test deterministic and side-effect free. Returns true iff every trial
 * for every known key passes.
 */
bool hitag2_fiat_self_test(void);

#ifdef __cplusplus
}
#endif

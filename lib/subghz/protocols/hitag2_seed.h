#pragma once

// ---------------------------------------------------------------------------
// [HITAG2_SEED] Classic byte-array Hitag2 cipher + 4-byte SEED model.
//
// This is a faithful C-to-C port of the CLASSIC Hitag2 implementation from the
// ProtoPirate app (applications/system/ProtoPirate/protocols/renault_v1.c). It
// operates on a byte-array state[6] (filter/feedback/clock/permute) and models a
// 4-byte SEED so that a captured Renault V1 frame can be:
//   (a) reduced to a 4-byte SEED (via seed brute-force over the classic cipher),
//   (b) re-encoded forward with serial+cnt+btn+seed into a valid NEXT code.
//
// It is DELIBERATELY SEPARATE from:
//   - the Fiat V1 BCM Hitag2 cipher (fiat_v1.c/.h, uint64_t state), which the
//     existing renault_v1 key-recovery path reuses, and
//   - the Hitag2Hell brute-force engine
//     (applications/main/subghz/helpers/subghz_hitag2_hell.c).
//
// Every public symbol here carries the `hitag2_seed_` prefix to guarantee no
// link-time collision with either of those, or with the static helpers inside
// renault_v1.c / fiat_v1.c.
// ---------------------------------------------------------------------------

#include <furi.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Recovered-state markers (mirror ProtoPirate HITAG2_RECOVERED_*).
#define HITAG2_SEED_RECOVERED_NO      0U
#define HITAG2_SEED_RECOVERED_YES     1U
#define HITAG2_SEED_RECOVERED_BF_MISS 2U

// Number of SEED candidates tried during recovery (18-bit search space, matches
// ProtoPirate HITAG2_BF_CANDIDATES = 0x40000).
#define HITAG2_SEED_BF_CANDIDATES 0x40000U

// ---- Low-level classic cipher primitives ---------------------------------

/**
 * Serial permutation. Expands the 4-byte big-endian serial into the 6-byte
 * permuted seed used to initialize the cipher state / IV working buffer.
 */
void hitag2_seed_serial_permute(const uint8_t serial[4], uint8_t perm[6]);

/**
 * Non-linear filter over the 6-byte cipher state.
 * @return the filtered output bit (0/1).
 */
uint8_t hitag2_seed_filter(const uint8_t state[6]);

/**
 * Linear feedback over the 6-byte cipher state.
 * @return the feedback bit (0/1).
 */
uint8_t hitag2_seed_feedback(const uint8_t state[6]);

/** Shift the 6-byte cipher state left by one, injecting `input` at the LSB. */
void hitag2_seed_shift_state(uint8_t state[6], uint8_t input);

/** Shift the 4-byte little-endian buffer left by one, injecting `inject`. */
void hitag2_seed_shift_u32(uint8_t buf[4], uint8_t inject);

/**
 * Run the classic cipher (32 mixing rounds + 32 output rounds). On return
 * iv_work[0..3] holds the 32-bit keystream/hop used by encrypt/verify.
 */
void hitag2_seed_clock_cipher(uint8_t state[6], uint8_t iv_work[4], uint8_t iv_orig[4]);

/** XOR of raw[0..9] (the frame checksum byte). */
uint8_t hitag2_seed_frame_xor(const uint8_t raw[11]);

// ---- 4-byte SEED model ----------------------------------------------------

/** Build the 4-byte IV from counter, button, and 4-byte seed. */
void hitag2_seed_build_iv(uint32_t cnt, uint8_t btn, uint32_t seed, uint8_t iv[4]);

/** Recover the 4-byte SEED (big-endian) from a 4-byte IV. */
uint32_t hitag2_seed_from_iv(const uint8_t iv[4]);

/**
 * Encrypt an 11-byte frame from the big-endian serial and a fully-formed IV.
 * out[0..10] is the wire frame (out[10] is the XOR checksum byte).
 */
void hitag2_seed_encrypt_from_iv(const uint8_t serial_be[4], const uint8_t iv[4], uint8_t out[11]);

/**
 * Forward-encrypt an 11-byte frame from serial+cnt+btn+seed. The generated IV
 * is returned in iv[0..3]. This is the real forward encoder used to transmit a
 * NEXT code (not a replay).
 */
void hitag2_seed_encrypt_frame(
    uint32_t serial,
    uint32_t cnt,
    uint8_t btn,
    uint32_t seed,
    uint8_t out[11],
    uint8_t iv[4]);

// ---- SEED recovery (brute force over classic cipher) ----------------------

/**
 * Recover the 4-byte SEED for a captured 11-byte frame by brute-forcing the
 * 18-bit seed search space against the classic cipher's hop output.
 * @param frame captured 11-byte frame (frame[10] = XOR checksum)
 * @param iv_out on success receives the 4-byte IV (seed = hitag2_seed_from_iv)
 * @return true if a matching IV/seed was found
 */
bool hitag2_seed_recover(const uint8_t frame[11], uint8_t iv_out[4]);

#ifdef __cplusplus
}
#endif

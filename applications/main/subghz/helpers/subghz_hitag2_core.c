// [HITAG2_CORE] Fiat V1 BCM Hitag2 cipher in uint64_t state form.
//
// See subghz_hitag2_core.h for the mapping conventions. This implementation is
// bit-for-bit equivalent to fiat_v1_bcm_generate_authenticator() in
// lib/subghz/protocols/fiat_v1.c. Equivalence is asserted by
// hitag2_fiat_self_test() against subghz_protocol_fiat_v1_compute_auth() for
// all 8 hard-coded Fiat V1 keys.

#include "subghz_hitag2_core.h"

#include <string.h>
#include <lib/subghz/protocols/fiat_v1.h>

// --- 4-to-1 truth-table LUTs (same constants as fiat_v1.c) -----------------

#define HITAG2_FA_TABLE 0x2C79UL
#define HITAG2_FB_TABLE 0x6671UL
#define HITAG2_FC_TABLE 0x7907287BUL

static inline uint8_t hitag2_truth(uint32_t table, uint8_t index) {
    return (uint8_t)((table >> index) & 1U);
}

static inline uint8_t hitag2_bit(Hitag2State state, uint8_t n) {
    return (uint8_t)((state >> n) & 1U);
}

static inline uint8_t hitag2_fi(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint8_t)((a << 3U) | (b << 2U) | (c << 1U) | d);
}

// --- Filter and feedback ---------------------------------------------------

uint8_t hitag2_fiat_filter(Hitag2State s) {
    // g0 (fa): bits 41, 42, 44, 45 -> state[0].bit1,2,4,5
    const uint8_t g0 = hitag2_truth(
        HITAG2_FA_TABLE,
        hitag2_fi(hitag2_bit(s, 41U), hitag2_bit(s, 42U), hitag2_bit(s, 44U), hitag2_bit(s, 45U)));
    // g1 (fb): bits 32, 33, 35, 39 -> state[1].bit0,1,3,7
    const uint8_t g1 = hitag2_truth(
        HITAG2_FB_TABLE,
        hitag2_fi(hitag2_bit(s, 32U), hitag2_bit(s, 33U), hitag2_bit(s, 35U), hitag2_bit(s, 39U)));
    // g2 (fb): bits 21, 24, 26, 30 -> state[3].bit5, state[2].bit0,2,6
    const uint8_t g2 = hitag2_truth(
        HITAG2_FB_TABLE,
        hitag2_fi(hitag2_bit(s, 21U), hitag2_bit(s, 24U), hitag2_bit(s, 26U), hitag2_bit(s, 30U)));
    // g3 (fb): bits 14, 16, 18, 19 -> state[4].bit6, state[3].bit0,2,3
    const uint8_t g3 = hitag2_truth(
        HITAG2_FB_TABLE,
        hitag2_fi(hitag2_bit(s, 14U), hitag2_bit(s, 16U), hitag2_bit(s, 18U), hitag2_bit(s, 19U)));
    // g4 (fa): bits 1, 3, 4, 13 -> state[5].bit1,3,4, state[4].bit5
    const uint8_t g4 = hitag2_truth(
        HITAG2_FA_TABLE,
        hitag2_fi(hitag2_bit(s, 1U), hitag2_bit(s, 3U), hitag2_bit(s, 4U), hitag2_bit(s, 13U)));
    const uint8_t group = (uint8_t)(g0 | (g1 << 1U) | (g2 << 2U) | (g3 << 3U) | (g4 << 4U));
    return hitag2_truth(HITAG2_FC_TABLE, group);
}

uint8_t hitag2_fiat_lfsr_feedback(Hitag2State s) {
    // Taps: 0, 1, 4, 5, 6, 17, 21, 24, 25, 31, 39, 40, 41, 44, 45, 47.
    // Packed as a 48-bit mask, then XOR-reduced (parity).
    static const uint64_t tap_mask =
        (1ULL << 0) | (1ULL << 1) | (1ULL << 4) | (1ULL << 5) | (1ULL << 6) | (1ULL << 17) |
        (1ULL << 21) | (1ULL << 24) | (1ULL << 25) | (1ULL << 31) | (1ULL << 39) |
        (1ULL << 40) | (1ULL << 41) | (1ULL << 44) | (1ULL << 45) | (1ULL << 47);
    uint64_t v = s & tap_mask;
    // XOR-fold to a single bit.
    v ^= v >> 32;
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (uint8_t)(v & 1U);
}

// --- Input helpers (identical to fiat_v1.c) --------------------------------

static inline uint8_t hitag2_iv_bit_be(uint32_t iv, uint8_t index) {
    return (uint8_t)((iv >> (31U - index)) & 1U);
}

static inline uint8_t hitag2_key_bit_be(const uint8_t* key, uint8_t index) {
    return (uint8_t)((key[index >> 3U] >> (7U - (index & 7U))) & 1U);
}

static inline uint32_t hitag2_build_iv(uint8_t button, uint16_t control, uint32_t epoch) {
    return ((epoch & 0x3FFFFUL) << 14U) | (((uint32_t)control & 0x3FFUL) << 4U) |
           ((uint32_t)button & 0xFUL);
}

static inline Hitag2State hitag2_build_initial_state(uint32_t uid, const uint8_t key[6]) {
    // state_bytes = { uid>>24, uid>>16, uid>>8, uid, key[4], key[5] }
    // Byte i occupies u64 bits [(5-i)*8 + 7 : (5-i)*8].
    return (((uint64_t)((uid >> 24) & 0xFFU)) << 40) |
           (((uint64_t)((uid >> 16) & 0xFFU)) << 32) |
           (((uint64_t)((uid >>  8) & 0xFFU)) << 24) |
           (((uint64_t)((uid      ) & 0xFFU)) << 16) |
           (((uint64_t)(key[4])) << 8) | ((uint64_t)key[5]);
}

// --- Public: init phase ----------------------------------------------------

Hitag2State hitag2_fiat_init_phase(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch) {
    Hitag2State s = hitag2_build_initial_state(uid, key);
    const uint32_t iv = hitag2_build_iv(button, control, epoch);
    for(uint8_t i = 0U; i < 32U; i++) {
        const uint8_t input =
            (uint8_t)(hitag2_iv_bit_be(iv, i) ^ hitag2_key_bit_be(key, i) ^ hitag2_fiat_filter(s));
        s = hitag2_shift(s, input);
    }
    return s;
}

// --- Public: full authenticator -------------------------------------------

uint32_t hitag2_fiat_full_auth(
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    const uint8_t key[6],
    uint32_t epoch) {
    Hitag2State s = hitag2_fiat_init_phase(uid, button, control, key, epoch);
    uint32_t authenticator = 0U;
    for(uint8_t i = 0U; i < 32U; i++) {
        authenticator = (authenticator << 1U) | hitag2_fiat_filter(s);
        s = hitag2_shift(s, hitag2_fiat_lfsr_feedback(s));
    }
    return authenticator;
}

// --- Public: init phase inversion -----------------------------------------

bool hitag2_fiat_invert_init(
    Hitag2State state31,
    uint32_t uid,
    uint8_t button,
    uint16_t control,
    uint32_t epoch,
    uint8_t key_out[6]) {
    const uint32_t iv = hitag2_build_iv(button, control, epoch);
    Hitag2State s = state31 & HITAG2_STATE_MASK;
    uint8_t key_bits[32];

    for(int i = 31; i >= 0; i--) {
        // Bit that was pushed into position 0 during round i:
        const uint8_t input_i = (uint8_t)(s & 1U);
        // Reconstruct state at the START of round i by "un-shifting":
        //   - drop the input bit (right-shift by 1)
        //   - restore the MSB that was discarded by that round's shift.
        // Round i discards state_bit[47]_before_shift; the initial state has
        //   bit[47-i] = UID.bit[31-i]  (i in 0..31),
        // so the MSB discarded at round i is UID.bit[31-i].
        const uint8_t msb_prev = (uint8_t)((uid >> (31 - i)) & 1U);
        s = (s >> 1) | ((uint64_t)msb_prev << 47);
        s &= HITAG2_STATE_MASK;

        // Now s is the state at the start of round i; extract this round's
        // key bit from the round equation
        //   input_i = iv_bit_i XOR key_bit_i XOR filter(state_i).
        const uint8_t filter_i = hitag2_fiat_filter(s);
        const uint8_t iv_bit = hitag2_iv_bit_be(iv, (uint8_t)i);
        key_bits[i] = (uint8_t)(input_i ^ iv_bit ^ filter_i);
    }

    // After 32 un-rounds, s must equal the initial state:
    //   bits[47..16] = uid
    //   bits[15.. 8] = key[4]
    //   bits[ 7.. 0] = key[5]
    const uint32_t recovered_uid = (uint32_t)((s >> 16) & 0xFFFFFFFFULL);
    const bool consistent = (recovered_uid == uid);

    // Repack key[0..3] from the 32 individual bits (MSB-first per byte).
    for(uint8_t b = 0U; b < 4U; b++) {
        uint8_t byte = 0U;
        for(uint8_t j = 0U; j < 8U; j++) {
            byte = (uint8_t)((byte << 1U) | key_bits[b * 8U + j]);
        }
        key_out[b] = byte;
    }
    key_out[4] = (uint8_t)((s >> 8) & 0xFFU);
    key_out[5] = (uint8_t)(s & 0xFFU);

    return consistent;
}

// --- Self-test -------------------------------------------------------------

// Local copy of the 8 hard-coded Fiat V1 keys (matches fiat_v1_known_keys[]
// in lib/subghz/protocols/fiat_v1.c). Kept private to the self-test.
static const uint8_t k_hitag2_fiat_self_test_keys[8][6] = {
    {0xB7U, 0x92U, 0x80U, 0xAEU, 0xCCU, 0x37U},
    {0xD4U, 0x24U, 0x28U, 0xF7U, 0xD9U, 0x66U},
    {0x4DU, 0x34U, 0x3FU, 0xD4U, 0xE7U, 0xB6U},
    {0x6DU, 0x6BU, 0xF2U, 0x1DU, 0x3AU, 0x1AU},
    {0xA3U, 0xF3U, 0xACU, 0xF7U, 0xB9U, 0x10U},
    {0x4DU, 0x49U, 0x4BU, 0x52U, 0x4FU, 0x4EU},
    {0xCDU, 0x49U, 0x4BU, 0x52U, 0x4FU, 0x4EU},
    {0x33U, 0xFAU, 0x2FU, 0xCDU, 0xC3U, 0x3BU},
};

// Simple deterministic 32-bit LCG for self-test reproducibility. Isolated from
// the platform RNG so the self-test is side-effect free and callable early.
static uint32_t hitag2_self_test_rand(uint32_t* seed) {
    *seed = (*seed) * 1664525U + 1013904223U;
    return *seed;
}

bool hitag2_fiat_self_test(void) {
    const uint8_t trials_per_key = 16U;
    uint32_t seed = 0xC0FFEE01U;

    for(uint8_t k = 0U; k < 8U; k++) {
        const uint8_t* key = k_hitag2_fiat_self_test_keys[k];
        for(uint8_t t = 0U; t < trials_per_key; t++) {
            const uint32_t uid = hitag2_self_test_rand(&seed);
            const uint32_t r1 = hitag2_self_test_rand(&seed);
            const uint8_t button = (uint8_t)(r1 & 0x0FU);
            const uint16_t control = (uint16_t)((r1 >> 4) & 0x3FFU);
            const uint32_t epoch = hitag2_self_test_rand(&seed) & 0x3FFFFU;

            // (1) Full authenticator matches the reference implementation.
            const uint32_t reference =
                subghz_protocol_fiat_v1_compute_auth(uid, button, control, key, epoch);
            const uint32_t mine = hitag2_fiat_full_auth(uid, button, control, key, epoch);
            if(mine != reference) {
                return false;
            }

            // (2) Init-phase inversion recovers the exact 6-byte key.
            const Hitag2State state31 =
                hitag2_fiat_init_phase(uid, button, control, key, epoch);
            uint8_t recovered[6];
            const bool consistent = hitag2_fiat_invert_init(
                state31, uid, button, control, epoch, recovered);
            if(!consistent) {
                return false;
            }
            if(memcmp(recovered, key, 6) != 0) {
                return false;
            }
        }
    }
    return true;
}

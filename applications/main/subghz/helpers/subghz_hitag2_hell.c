// [HITAG2_HELL] Bitsliced Hitag2Hell attack adapted to Fiat V1 BCM.
//
// The attack proceeds in layers. Each layer guesses a small set of new state
// bits and immediately checks the corresponding filter-output bit against the
// observed authenticator. Failed guesses are pruned early. When all 32 filter
// output bits agree, the resulting state31 is emitted as a candidate.
//
// State convention (matches subghz_hitag2_core.h):
//   state[0..47]   = state31 (bit 0 = LSB = most-recently-shifted-in bit)
//   state[48+k]    = LFSR feedback bit produced at post-init step k
//
// Bit at position `p` of the round-r state, S_r[p], maps to:
//   state[p - r]        if p >= r
//   state[47 + r - p]   if p <  r   (new LFSR bits)
//
// See gen_kernel.py / gen_filters.py for how the per-round filter and LFSR
// expressions below were generated.

#include "subghz_hitag2_hell.h"

#include <string.h>

// The firmware is built at -Og by default (debug-friendly). This bitslice
// kernel is the single hottest integer loop in the app; force -O3 + loop
// unrolling on it alone so the M4 runs it at full speed (empirically ~2-4x vs
// -Og) without changing the rest of the firmware's build. Pure integer
// AND/OR/XOR/shift work, no FPU, so this is safe and deterministic.
#pragma GCC optimize("O3", "unroll-loops")

// ---------------------------------------------------------------------------
// Bitslice type (32-way SIMD across 32 lanes of a uint32_t)
// ---------------------------------------------------------------------------

typedef uint32_t bitslice_t;
#define BS_WIDTH 32U
#define BS_ALL_ONES  ((bitslice_t)0xFFFFFFFFU)
#define BS_ALL_ZEROS ((bitslice_t)0U)

// Fiat V1 bitsliced filter macros. See docstring comments below and the
// derivation notes in gen_filters.py.
//
// f_a_bs(a,b,c,d)     implements truth-table 0x3C65 with index a<<3|b<<2|c<<1|d
// f_b_bs(a,b,c,d)     implements truth-table 0x0EE5 with index a<<3|b<<2|c<<1|d
// f_c_bs(a,b,c,d,e)   implements truth-table 0x0DD3929B with index e<<4|d<<3|c<<2|b<<1|a
//
// For Fiat V1 we need truth(0x2C79, fi(i0,i1,i2,i3)) which equals
//     f_a_bs(bit[i3], bit[i2], bit[i1], bit[i0])
// (arguments in REVERSED order relative to the source i0..i3). Same rule for
// f_b_bs vs truth(0x6671). f_c_bs is called with the group values in NATURAL
// order: f_c_bs(g0, g1, g2, g3, g4) == truth(0x7907287B, g0|g1<<1|...|g4<<4).
#define f_a_bs(a, b, c, d) \
    ((bitslice_t) ~( (((a) | (b)) & (c)) ^ ((a) | (d)) ^ (b) ))
#define f_b_bs(a, b, c, d) \
    ((bitslice_t) ~( (((d) | (c)) & ((a) ^ (b))) ^ ((d) | (a) | (b)) ))
#define f_c_bs(a, b, c, d, e)                                                       \
    ((bitslice_t) ~( ((((((c) ^ (e)) | (d)) & (a)) ^ (b)) & ((c) ^ (b))) ^          \
                     ((((d) ^ (e)) | (a)) & (((d) ^ (b)) | (c))) ))

// State array: 48 state31 bits + 32 LFSR outputs (rounds 0..31).
// Extra slack because state[47+r-p] can index up to 47+31-0 = 78; last accessed
// index is state[77] (in round 31 filter with p=1 -> 47+31-1 = 77).
#define STATE_ARR_LEN 80U

// Per-round specialized filter/LFSR kernels with compile-time-constant state[]
// indices (bit-identical to the runtime-index bs_filter_at/bs_lfsr_at below).
// Ported from qUnleashed; requires bitslice_t, STATE_ARR_LEN and the f_*_bs
// macros above, which are byte-identical between the two trees. Using these at
// the hot call sites replaces per-call index arithmetic + non-inlined dispatch
// with immediate-offset loads, a further speedup on top of -O3.
#include "hell_kernels_gen.h"

// ---------------------------------------------------------------------------
// Layer 0 mask & layer new-bit tables (see gen_kernel.py)
// ---------------------------------------------------------------------------

// Bits used by filter round 0 (the "layer 0 mask"). 20 bits.
#define LAYER0_MASK_FIATV1 ((uint64_t)0x368B452D601AULL)

// Positions of the 20 bits in LAYER0_MASK_FIATV1 (ascending).
static const uint8_t k_layer0_bits[20] = {
    1, 3, 4, 13, 14, 16, 18, 19, 21, 24, 26, 30, 32, 33, 35, 39, 41, 42, 44, 45,
};

// L1..L8 new-bit positions (absolute state[] indices).
static const uint8_t k_layer1_bits[14] = {
    0, 2, 12, 15, 17, 20, 23, 25, 29, 31, 34, 38, 40, 43,
};
static const uint8_t k_layer2_bits[5] = {11, 22, 28, 37, 48};
static const uint8_t k_layer3_bits[4] = {10, 27, 36, 49};
static const uint8_t k_layer4_bits[2] = {9, 50};
static const uint8_t k_layer5_bits[2] = {8, 51};
static const uint8_t k_layer6_bits[2] = {7, 52};
static const uint8_t k_layer7_bits[2] = {6, 53};
static const uint8_t k_layer8_bits[2] = {5, 54};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline bitslice_t bs_from_bit(uint8_t b) {
    return b ? BS_ALL_ONES : BS_ALL_ZEROS;
}

// Extract lane `lane` (0..31) from a bitslice_t.
static inline uint8_t bs_get_lane(bitslice_t v, uint8_t lane) {
    return (uint8_t)((v >> lane) & 1U);
}

// Layer-0 scalar filter: pack a 48-bit state where only the 20 LAYER0_MASK
// bits are set (all other bits zero), then evaluate hitag2_fiat_filter on it.
// The Fiat V1 filter only reads bits in the layer-0 mask for round 0, so this
// gives the correct filter output.
static inline uint8_t layer0_filter(uint64_t partial_state) {
    return hitag2_fiat_filter(partial_state);
}

// Expand a 20-bit index into a 48-bit state, using the layer-0 mask bit
// positions.
static inline uint64_t expand_layer0(uint32_t idx) {
    uint64_t s = 0;
    for(uint8_t i = 0; i < 20U; i++) {
        if((idx >> i) & 1U) {
            s |= ((uint64_t)1 << k_layer0_bits[i]);
        }
    }
    return s;
}

// Precomputed lane-pattern bitslices for the L1 "spread" bits.
// We spread 5 L1 bits across the 32 lanes so that each lane holds a unique
// (bit0, bit1, bit2, bit3, bit4) triple:
//   spread[0] = 0xAAAAAAAA  (lane bit0)
//   spread[1] = 0xCCCCCCCC  (lane bit1)
//   spread[2] = 0xF0F0F0F0  (lane bit2)
//   spread[3] = 0xFF00FF00  (lane bit3)
//   spread[4] = 0xFFFF0000  (lane bit4)
static const bitslice_t k_spread_patterns[5] = {
    0xAAAAAAAAU, 0xCCCCCCCCU, 0xF0F0F0F0U, 0xFF00FF00U, 0xFFFF0000U,
};

// Which 5 L1 bits get bitsliced-spread (indices into k_layer1_bits[]).
// Choose the 5 highest-index positions so they occupy widely-separated state[]
// slots. This is somewhat arbitrary; any 5 choices work.
static const uint8_t k_layer1_spread_sel[5] = {9, 10, 11, 12, 13}; // -> layer1 bits {31, 34, 38, 40, 43}

// The remaining 9 L1 bits (scalar-iterated).
static const uint8_t k_layer1_scalar_sel[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8}; // -> {0,2,12,15,17,20,23,25,29}

// ---------------------------------------------------------------------------
// Deep search routine (layers 2..31) for one layer-0 candidate.
// Returns nothing; appends found state31 candidates to `result`.
// ---------------------------------------------------------------------------

typedef struct {
    Hitag2HellResult* result;
    const bitslice_t* keystream;   // length 32; each lane holds INVERSE bit_r
    uint64_t states_tested;
    bool aborted;                  // cancel/timeout requested
} SearchCtx;

// Extract state31 for lane `lane` from the current state[] array. Uses state[0..47].
static uint64_t extract_state31(const bitslice_t state[STATE_ARR_LEN], uint8_t lane) {
    uint64_t s = 0;
    for(uint8_t i = 0; i < 48U; i++) {
        if(bs_get_lane(state[i], lane)) {
            s |= ((uint64_t)1 << i);
        }
    }
    return s;
}

// Append a state31 to the result set (deduplicated).
static void emit_candidate(Hitag2HellResult* result, uint64_t state31) {
    for(uint32_t i = 0; i < result->candidate_count; i++) {
        if(result->candidates[i] == state31) return;
    }
    if(result->candidate_count >= HITAG2_HELL_MAX_CANDIDATES) {
        result->overflow = true;
        return;
    }
    result->candidates[result->candidate_count++] = state31;
}

// ---------------------------------------------------------------------------
// Bitsliced filter/LFSR generators (per-round). Emitted from gen_kernel.py.
// state[i] indexing: 0..47 = state31, 48..79 = LFSR outputs (deterministic
// after layer 8).
// ---------------------------------------------------------------------------

// The former runtime-index bs_filter_at(state, r) / bs_lfsr_at(state, r) were
// replaced by the compile-time-constant per-round kernels in hell_kernels_gen.h
// (bs_filter_at_rN / bs_lfsr_at_rN), which are bit-identical but far faster on
// the M4 (immediate-offset loads, fully inlined). All call sites now use the
// specialized kernels, so the runtime versions were removed.

// ---------------------------------------------------------------------------
// Deep search: enters with state[] populated with L0+L1(spread+scalar) bits.
// Bitslice lanes carry the 32 combinations of the 5 spread L1 bits.
// Then recursively guesses L2..L8 (scalar per bit) and propagates LFSR.
// ---------------------------------------------------------------------------

static bool deep_search(
    SearchCtx* ctx, bitslice_t state[STATE_ARR_LEN], bitslice_t alive_mask) {
    // Filter round 1 was checked prior to entry. Continue with layers 2..8.
    // Filter round r is checked *after* guessing L_r's new bits.

    // Layer 2: 5 new bits (k_layer2_bits[]) -- guess scalar, 32 combinations.
    for(uint32_t i2 = 0; i2 < (1U << 5); i2++) {
        for(uint8_t k = 0; k < 5U; k++) {
            state[k_layer2_bits[k]] = bs_from_bit((i2 >> k) & 1U);
        }
        bitslice_t f2 = bs_filter_at_r2(state);
        bitslice_t alive2 = alive_mask & ~(f2 ^ ctx->keystream[2]);
        if(alive2 == 0) continue;

        // Layer 3: 4 bits, 16 combinations
        for(uint32_t i3 = 0; i3 < (1U << 4); i3++) {
            for(uint8_t k = 0; k < 4U; k++) {
                state[k_layer3_bits[k]] = bs_from_bit((i3 >> k) & 1U);
            }
            bitslice_t f3 = bs_filter_at_r3(state);
            bitslice_t alive3 = alive2 & ~(f3 ^ ctx->keystream[3]);
            if(alive3 == 0) continue;

            // Layer 4: 2 bits
            for(uint32_t i4 = 0; i4 < (1U << 2); i4++) {
                for(uint8_t k = 0; k < 2U; k++) {
                    state[k_layer4_bits[k]] = bs_from_bit((i4 >> k) & 1U);
                }
                bitslice_t f4 = bs_filter_at_r4(state);
                bitslice_t alive4 = alive3 & ~(f4 ^ ctx->keystream[4]);
                if(alive4 == 0) continue;

                for(uint32_t i5 = 0; i5 < (1U << 2); i5++) {
                    for(uint8_t k = 0; k < 2U; k++) {
                        state[k_layer5_bits[k]] = bs_from_bit((i5 >> k) & 1U);
                    }
                    bitslice_t f5 = bs_filter_at_r5(state);
                    bitslice_t alive5 = alive4 & ~(f5 ^ ctx->keystream[5]);
                    if(alive5 == 0) continue;

                    for(uint32_t i6 = 0; i6 < (1U << 2); i6++) {
                        for(uint8_t k = 0; k < 2U; k++) {
                            state[k_layer6_bits[k]] = bs_from_bit((i6 >> k) & 1U);
                        }
                        bitslice_t f6 = bs_filter_at_r6(state);
                        bitslice_t alive6 = alive5 & ~(f6 ^ ctx->keystream[6]);
                        if(alive6 == 0) continue;

                        for(uint32_t i7 = 0; i7 < (1U << 2); i7++) {
                            for(uint8_t k = 0; k < 2U; k++) {
                                state[k_layer7_bits[k]] = bs_from_bit((i7 >> k) & 1U);
                            }
                            bitslice_t f7 = bs_filter_at_r7(state);
                            bitslice_t alive7 = alive6 & ~(f7 ^ ctx->keystream[7]);
                            if(alive7 == 0) continue;

                            for(uint32_t i8 = 0; i8 < (1U << 2); i8++) {
                                for(uint8_t k = 0; k < 2U; k++) {
                                    state[k_layer8_bits[k]] = bs_from_bit((i8 >> k) & 1U);
                                }
                                bitslice_t f8 = bs_filter_at_r8(state);
                                bitslice_t alive8 = alive7 & ~(f8 ^ ctx->keystream[8]);
                                if(alive8 == 0) continue;

                                // At this point state[0..45] and state[48..54] are set.
                                // state[46] and state[47] are NOT constrained by any
                                // filter check for rounds 0..8, but they ARE
                                // constrained by the LFSR feedback equations for
                                // rounds 0 and 1:
                                //   state[48] = XOR of taps at round 0, which INCLUDES state[47]
                                //   state[49] = XOR of taps at round 1, which INCLUDES state[46]
                                // Solve for state[46], state[47] directly.
                                //
                                // Round 0 taps: {0,1,4,5,6,17,21,24,25,31,39,40,41,44,45,47}
                                //   -> state[47] = state[48] XOR (all other taps at r=0)
                                {
                                    bitslice_t v = state[0] ^ state[1] ^ state[4] ^ state[5] ^
                                                   state[6] ^ state[17] ^ state[21] ^ state[24] ^
                                                   state[25] ^ state[31] ^ state[39] ^ state[40] ^
                                                   state[41] ^ state[44] ^ state[45];
                                    state[47] = state[48] ^ v;
                                }
                                // Round 1 taps in absolute-state indices (from gen_lfsr.py):
                                //   state[49] = state[0] ^ state[3] ^ state[4] ^ state[5] ^
                                //              state[16] ^ state[20] ^ state[23] ^ state[24] ^
                                //              state[30] ^ state[38] ^ state[39] ^ state[40] ^
                                //              state[43] ^ state[44] ^ state[46] ^ state[48]
                                //   -> state[46] = state[49] XOR (all other terms)
                                {
                                    bitslice_t v = state[0] ^ state[3] ^ state[4] ^ state[5] ^
                                                   state[16] ^ state[20] ^ state[23] ^ state[24] ^
                                                   state[30] ^ state[38] ^ state[39] ^ state[40] ^
                                                   state[43] ^ state[44] ^ state[48];
                                    state[46] = state[49] ^ v;
                                }
                                // Now state[0..54] all set.
                                // Verify LFSR consistency for rounds 2..6 (state[50..54]).
                                // Any lane inconsistent means the guesses were wrong.
                                // Unrolled with compile-time-constant kernels.
#define HELL_LFSR_VERIFY(R)                                       \
    alive8 &= ~(bs_lfsr_at_r##R(state) ^ state[48 + (R)]);        \
    if(alive8 == 0) goto lfsr_verify_done;
                                HELL_LFSR_VERIFY(2)
                                HELL_LFSR_VERIFY(3)
                                HELL_LFSR_VERIFY(4)
                                HELL_LFSR_VERIFY(5)
                                HELL_LFSR_VERIFY(6)
#undef HELL_LFSR_VERIFY
                                lfsr_verify_done:
                                if(alive8 == 0) continue;
                                // Compute state[55..79] via LFSR (deterministic).
#define HELL_LFSR_COMPUTE(R) state[48 + (R)] = bs_lfsr_at_r##R(state);
                                HELL_LFSR_COMPUTE(7)
                                HELL_LFSR_COMPUTE(8)
                                HELL_LFSR_COMPUTE(9)
                                HELL_LFSR_COMPUTE(10)
                                HELL_LFSR_COMPUTE(11)
                                HELL_LFSR_COMPUTE(12)
                                HELL_LFSR_COMPUTE(13)
                                HELL_LFSR_COMPUTE(14)
                                HELL_LFSR_COMPUTE(15)
                                HELL_LFSR_COMPUTE(16)
                                HELL_LFSR_COMPUTE(17)
                                HELL_LFSR_COMPUTE(18)
                                HELL_LFSR_COMPUTE(19)
                                HELL_LFSR_COMPUTE(20)
                                HELL_LFSR_COMPUTE(21)
                                HELL_LFSR_COMPUTE(22)
                                HELL_LFSR_COMPUTE(23)
                                HELL_LFSR_COMPUTE(24)
                                HELL_LFSR_COMPUTE(25)
                                HELL_LFSR_COMPUTE(26)
                                HELL_LFSR_COMPUTE(27)
                                HELL_LFSR_COMPUTE(28)
                                HELL_LFSR_COMPUTE(29)
                                HELL_LFSR_COMPUTE(30)
                                HELL_LFSR_COMPUTE(31)
#undef HELL_LFSR_COMPUTE
                                // Filter rounds 9..31 check.
#define HELL_FILTER_CHECK(R)                                      \
    alive8 &= ~(bs_filter_at_r##R(state) ^ ctx->keystream[R]);    \
    if(alive8 == 0) goto filter_check_done;
                                HELL_FILTER_CHECK(9)
                                HELL_FILTER_CHECK(10)
                                HELL_FILTER_CHECK(11)
                                HELL_FILTER_CHECK(12)
                                HELL_FILTER_CHECK(13)
                                HELL_FILTER_CHECK(14)
                                HELL_FILTER_CHECK(15)
                                HELL_FILTER_CHECK(16)
                                HELL_FILTER_CHECK(17)
                                HELL_FILTER_CHECK(18)
                                HELL_FILTER_CHECK(19)
                                HELL_FILTER_CHECK(20)
                                HELL_FILTER_CHECK(21)
                                HELL_FILTER_CHECK(22)
                                HELL_FILTER_CHECK(23)
                                HELL_FILTER_CHECK(24)
                                HELL_FILTER_CHECK(25)
                                HELL_FILTER_CHECK(26)
                                HELL_FILTER_CHECK(27)
                                HELL_FILTER_CHECK(28)
                                HELL_FILTER_CHECK(29)
                                HELL_FILTER_CHECK(30)
                                HELL_FILTER_CHECK(31)
#undef HELL_FILTER_CHECK
                                filter_check_done:
                                if(alive8 == 0) continue;

                                // Any surviving lanes are candidates.
                                for(uint8_t lane = 0; lane < BS_WIDTH; lane++) {
                                    if(!((alive8 >> lane) & 1U)) continue;
                                    uint64_t state31 = extract_state31(state, lane);
                                    emit_candidate(ctx->result, state31);
                                    if(ctx->result->overflow) return true;
                                }
                            } // i8
                            if(ctx->result->overflow) return true;
                        } // i7
                    } // i6
                } // i5
            } // i4
        } // i3
    } // i2
    return false;
}

// ---------------------------------------------------------------------------
// Main recovery entry
// ---------------------------------------------------------------------------

bool hitag2_hell_recover(
    uint32_t authenticator,
    const Hitag2HellConfig* config,
    Hitag2HellResult* result) {
    if(!result) return false;
    memset(result, 0, sizeof(*result));

    // Prepare keystream bitslices: keystream[r] = broadcast of authenticator bit r.
    // Convention: auth bit r = (authenticator >> (31 - r)) & 1.
    // We store the ACTUAL bit here (unlike x86 which stores inverse); we XOR
    // filter output with it and AND-NOT into alive mask.
    bitslice_t keystream[32];
    for(uint8_t r = 0; r < 32U; r++) {
        uint8_t b = (uint8_t)((authenticator >> (31U - r)) & 1U);
        keystream[r] = bs_from_bit(b);
    }

    const uint32_t l0_full = 1U << 20;
    uint32_t l0_start = 0;
    uint32_t l0_end = l0_full;
    if(config) {
        if(config->l0_end > 0) {
            l0_start = config->l0_start;
            l0_end = config->l0_end;
            if(l0_end > l0_full) l0_end = l0_full;
        }
    }
    const uint32_t l0_total = l0_end - l0_start;
    // [FREEZE FIX] Poll progress/cancel/timeout on a cadence measured in loop
    // ITERATIONS since the start of THIS call, not against an absolute i0
    // boundary. The previous code compared i0 against (l0_start + 128); a chunk
    // whose length was <= 128 (the BF driver uses 256, and could use less) or
    // that simply exited before crossing that absolute boundary NEVER reached
    // the checkpoint, so progress_cb / cancel / timeout were never evaluated and
    // the kernel ran the whole chunk without yielding — on the M4 that is many
    // minutes, during which BACK's furi_thread_join blocked the UI and the
    // watchdog eventually rebooted the device.
    //
    // We now checkpoint (a) every `progress_step` iterations AND (b) after every
    // slot that actually ran the deep search (a single heavy slot can take
    // seconds on the M4, so we must give the driver a chance to yield/cancel
    // after each one). The filtered-out (cheap) slots only hit the counter path,
    // which is a couple of integer ops — negligible overhead.
    const uint32_t progress_step = 16U;
    uint32_t since_checkpoint = 0;
    uint32_t t_start = 0;
    if(config && config->timeout_ms > 0 && config->now_ms_cb) {
        t_start = config->now_ms_cb();
    }

    SearchCtx ctx = {
        .result = result,
        .keystream = keystream,
        .states_tested = 0,
        .aborted = false,
    };

    for(uint32_t i0 = l0_start; i0 < l0_end; i0++) {
        bool did_deep_search = false;
        uint64_t s0 = expand_layer0(i0);
        // Layer 0 check: filter@round 0.
        if(layer0_filter(s0) != ((authenticator >> 31) & 1U)) {
            ctx.states_tested++;
        } else {
            // Set up state[] bitslices: L0 bits scalar (broadcast to all lanes),
            // L1 spread bits use k_spread_patterns, everything else zero.
            bitslice_t state[STATE_ARR_LEN];
            memset(state, 0, sizeof(state));
            for(uint8_t k = 0; k < 20U; k++) {
                state[k_layer0_bits[k]] =
                    bs_from_bit((uint8_t)((s0 >> k_layer0_bits[k]) & 1U));
            }
            // L1 spread bits: 5 patterns
            for(uint8_t k = 0; k < 5U; k++) {
                uint8_t bit_pos = k_layer1_bits[k_layer1_spread_sel[k]];
                state[bit_pos] = k_spread_patterns[k];
            }

            // Iterate the 9 scalar L1 bits: 512 combinations.
            for(uint32_t i1 = 0; i1 < (1U << 9); i1++) {
                for(uint8_t k = 0; k < 9U; k++) {
                    uint8_t bit_pos = k_layer1_bits[k_layer1_scalar_sel[k]];
                    state[bit_pos] = bs_from_bit((uint8_t)((i1 >> k) & 1U));
                }

                // Round-1 filter check.
                bitslice_t f1 = bs_filter_at_r1(state);
                bitslice_t alive = ~(f1 ^ keystream[1]);
                if(alive == 0) continue;

                // Descend into layers 2..31.
                deep_search(&ctx, state, alive);
                if(result->overflow) break;

                // [FREEZE FIX v2] A single "heavy" L0 slot runs 512 deep_search
                // descents; on the M4 that can take many seconds/minutes with NO
                // yield if we only checkpoint AFTER the whole slot (below). That
                // was the residual L5 freeze (BACK/progress dead during a heavy
                // slot). Checkpoint INSIDE this loop too: every 32 descents give
                // progress_cb a chance to yield the CPU and honor cancel.
                if(config && config->progress_cb && ((i1 & 0x1FU) == 0x1FU)) {
                    uint8_t pct_inner =
                        (uint8_t)(l0_total ? ((uint64_t)(i0 - l0_start) * 100U / l0_total) : 100U);
                    if(!config->progress_cb(
                           pct_inner,
                           ctx.states_tested + i1,
                           config->progress_ctx)) {
                        result->cancelled = true;
                        break;
                    }
                }
            }

            ctx.states_tested += (1U << 9);
            did_deep_search = true;
            // If cancelled inside the inner loop, stop the whole sweep.
            if(result->cancelled) break;
        }

        // [FREEZE FIX] Checkpoint after every heavy (deep-searched) slot, and at
        // least every `progress_step` cheap slots. This guarantees the driver's
        // progress_cb (which yields the CPU and polls the BACK/cancel flag)
        // runs frequently regardless of chunk length, so BACK is honored
        // promptly and the timeout backstop can actually fire.
        since_checkpoint++;
        if(did_deep_search || since_checkpoint >= progress_step) {
            since_checkpoint = 0;
            uint8_t pct =
                (uint8_t)(l0_total ? ((uint64_t)(i0 - l0_start) * 100U / l0_total) : 100U);
            if(config && config->progress_cb) {
                if(!config->progress_cb(pct, ctx.states_tested, config->progress_ctx)) {
                    result->cancelled = true;
                    break;
                }
            }
            if(config && config->timeout_ms > 0 && config->now_ms_cb) {
                uint32_t now = config->now_ms_cb();
                if((now - t_start) >= config->timeout_ms) {
                    result->timed_out = true;
                    break;
                }
            }
        }
        if(result->overflow) break;
    }

    result->states_tested_total = ctx.states_tested;
    return result->candidate_count > 0;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

static const uint8_t k_hell_self_test_keys[8][6] = {
    {0xB7U, 0x92U, 0x80U, 0xAEU, 0xCCU, 0x37U},
    {0xD4U, 0x24U, 0x28U, 0xF7U, 0xD9U, 0x66U},
    {0x4DU, 0x34U, 0x3FU, 0xD4U, 0xE7U, 0xB6U},
    {0x6DU, 0x6BU, 0xF2U, 0x1DU, 0x3AU, 0x1AU},
    {0xA3U, 0xF3U, 0xACU, 0xF7U, 0xB9U, 0x10U},
    {0x4DU, 0x49U, 0x4BU, 0x52U, 0x4FU, 0x4EU},
    {0xCDU, 0x49U, 0x4BU, 0x52U, 0x4FU, 0x4EU},
    {0x33U, 0xFAU, 0x2FU, 0xCDU, 0xC3U, 0x3BU},
};

// Compute the L0 index that matches a state31 (used to bound the self-test).
static uint32_t compute_correct_l0(uint64_t state31) {
    uint32_t idx = 0;
    for(uint8_t i = 0; i < 20U; i++) {
        if((state31 >> k_layer0_bits[i]) & 1U) idx |= (1U << i);
    }
    return idx;
}

bool hitag2_hell_self_test(void) {
    // The full 2^20 layer-0 sweep is very slow on a low-power CPU (hours).
    // This self-test therefore only verifies that the search KERNEL correctly
    // recovers state31 when pointed at the correct layer-0 slot, for one trial
    // per known key. A full-sweep integration test should be run on demand by
    // the caller, not from a bring-up self-test.
    uint32_t seed = 0xC0FFEE01U;
    for(uint8_t k = 0; k < 8U; k++) {
        const uint8_t* key = k_hell_self_test_keys[k];
        seed = seed * 1664525U + 1013904223U;
        uint32_t uid = seed;
        seed = seed * 1664525U + 1013904223U;
        uint8_t button = (uint8_t)(seed & 0xFU);
        uint16_t control = (uint16_t)((seed >> 4) & 0x3FFU);
        seed = seed * 1664525U + 1013904223U;
        uint32_t epoch = seed & 0x3FFFFU;

        uint32_t auth = hitag2_fiat_full_auth(uid, button, control, key, epoch);
        Hitag2State true_s31 = hitag2_fiat_init_phase(uid, button, control, key, epoch);
        uint32_t correct_l0 = compute_correct_l0(true_s31);

        Hitag2HellResult result;
        Hitag2HellConfig cfg = {0};
        cfg.l0_start = correct_l0;
        cfg.l0_end = correct_l0 + 1U;
        (void)hitag2_hell_recover(auth, &cfg, &result);

        bool matched = false;
        for(uint32_t i = 0; i < result.candidate_count; i++) {
            if(result.candidates[i] == true_s31) {
                matched = true;
                break;
            }
        }
        if(!matched) return false;
    }
    return true;
}

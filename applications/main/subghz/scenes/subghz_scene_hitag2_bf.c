// [HITAG2_BF] Scene: Hitag2 bruteforce for Fiat V1 signals.
//
// Mirrors PSA Decrypt: opens on a saved Fiat V1 .sub, extracts UID/control/button/hop,
// scans the same directory for additional .sub files sharing the same UID to
// enable multi-capture verification, then spawns a worker thread running the
// cascade L1..L5. On success, writes the found Hitag2 Key + Epoch back into
// the .sub file and marks the signal as re-emittable.

#include "../subghz_i.h"
#include "../helpers/subghz_hitag2_bf.h"

#include <lib/subghz/protocols/fiat_v1.h>
#include <lib/subghz/protocols/fiat_v2.h>
#include <lib/subghz/protocols/renault_v1.h>
#include <furi.h>
#include <storage/storage.h>
#include <toolbox/path.h>
#include <bt/bt_service/bt.h>

#define TAG "SubGhzSceneHitag2Bf"

#define HITAG2_BF_EVENT_DONE (0xD2)

// Maximum files to scan in the same directory when auto-loading captures
#define HITAG2_BF_MAX_SCAN_FILES 64U

// --- BLE compute-offload wire protocol (must match qUnleashed exactly) ---
// HT_MSG_BF_REQUEST (0x20) is the LEGACY Flipper->phone request. The firmware no
// longer emits it — it now always sends the combo/slice-aware V2 request (0x24).
// The define is retained only as documentation of the legacy opcode value so the
// two protocol families never collide; it is intentionally unused in firmware.
#define HT_MSG_BF_REQUEST    0x20 // (legacy, no longer emitted; kept for reference)
#define HT_MSG_BF_PROGRESS   0x21 // phone -> Flipper (reused unchanged by V2)
#define HT_MSG_BF_RESULT     0x22 // phone -> Flipper (reused unchanged by V2)
#define HT_MSG_BF_CANCEL     0x23 // Flipper -> phone (reused unchanged by V2)
#define HT_MSG_BF_REQUEST_V2 0x24 // Flipper -> phone, combo/slice-aware

// -----------------------------------------------------------------------------
// REQUEST V2 (0x24) wire format — MUST match qUnleashed's parser EXACTLY.
// -----------------------------------------------------------------------------
//   [0]      = 0x24  (HT_MSG_BF_REQUEST_V2)
//   [1]      = proto : 0=Fiat V1, 1=Fiat V2, 2=Renault V1
//   [2..5]   = uid LE (uint32)
//   [6..9]   = l0_start LE (uint32)
//   [10..13] = l0_end LE (uint32)
//   [14]     = capture_count
// then per capture, size depends on proto:
//   proto 0 (Fiat V1):    btn(1) + cnt(2 LE) + hop(4 LE)                 =  7 bytes
//   proto 1 (Fiat V2):    raw[14]                                        = 14 bytes
//   proto 2 (Renault V1): payload42(6 bytes LE) + button(1) + counter(1) =  8 bytes
//
// payload42 is 42 bits -> serialized as 6 bytes little-endian; only the low 42
// bits are meaningful (the top 6 bits of byte[5] are zero).
//
// Header is 15 bytes. Budget = BLE_SVC_SERIAL_CUSTOM_DATA_LEN_MAX (64) -> 49
// payload bytes. Hence: Fiat V1 max 7 (7*7=49), Fiat V2 max 3 (3*14=42),
// Renault V1 max 6 (6*8=48).
// -----------------------------------------------------------------------------
#define HT_MSG_BF_REQUEST_V2_HEADER_LEN 15U

// Per-proto capture caps so the request fits BLE_SVC_SERIAL_CUSTOM_DATA_LEN_MAX.
#define HT_MSG_BF_MAX_OFFLOAD_CAPTURES_V1  7U // 7B each
#define HT_MSG_BF_MAX_OFFLOAD_CAPTURES_V2  3U // 14B each
#define HT_MSG_BF_MAX_OFFLOAD_CAPTURES_RV1 6U // 8B each

// Wire proto codes (byte [1] of the V2 request).
#define HT_MSG_BF_PROTO_FIAT_V1    0U
#define HT_MSG_BF_PROTO_FIAT_V2    1U
#define HT_MSG_BF_PROTO_RENAULT_V1 2U

typedef struct {
    SubGhz* subghz;
    FuriThread* thread;
    SubGhzHitag2Bf* bf;
    volatile bool cancel;
    uint32_t start_tick;
    bool success;
    FuriString* result;
    uint8_t found_key[6];
    uint32_t found_epoch;
    uint8_t found_level;
    bool ble_offload;
    bool is_fiat_v2;    // [HITAG2_BF] primary capture is Fiat V2
    bool is_renault_v1; // [HITAG2_BF] primary capture is Renault V1
    uint8_t iv_combo;   // [HITAG2_BF] resolved IV combo (0..3) for V2/RV1 write-back
    uint8_t hop_slice;  // [HITAG2_BF] resolved hop slice (0..2) for RV1 write-back
    uint64_t rv1_payload42; // [HITAG2_BF] Renault V1 primary payload for Hop write-back
} Hitag2BfCtx;

// -----------------------------------------------------------------------------
// Helpers to parse a Fiat V1 .sub file
// -----------------------------------------------------------------------------

// Extract a Hitag2-BF capture from a Fiat V1 / Fiat V2 / Renault V1 .sub.
//   - Fiat V1: the IV (control, button) is read directly.
//   - Fiat V2: the IV is unknown (one of 4 combos) so only the 14-byte Raw
//     frame + uid + hop are extracted; control/button are derived per-combo.
//   - Renault V1: BOTH the hop bit-slice AND the IV combo are unknown; the
//     42-bit payload + uid + raw button/counter are extracted and the attack
//     searches slice x combo.
// @param raw_out       14-byte buffer to receive the V2 raw frame (untouched otherwise)
// @param is_v2_out     set to true if this is a Fiat V2 capture
// @param is_rv1_out    set to true if this is a Renault V1 capture
// @param payload42_out receives the Renault V1 42-bit payload (RV1 only)
// @param rv1_btn_out   receives the Renault V1 raw 8-bit button (RV1 only)
// @param rv1_cnt_out   receives the Renault V1 raw 8-bit counter (RV1 only)
static bool hitag2_bf_extract_capture(
    FlipperFormat* fff,
    uint32_t* uid_out,
    uint16_t* control_out,
    uint8_t* button_out,
    uint32_t* hop_out,
    uint8_t raw_out[14],
    bool* is_v2_out,
    bool* is_rv1_out,
    uint64_t* payload42_out,
    uint8_t* rv1_btn_out,
    uint8_t* rv1_cnt_out) {
    // Determine protocol: accept "Fiat V1", "Fiat V2" or "Renault V1".
    FuriString* proto = furi_string_alloc();
    bool is_fiat_v1 = false;
    bool is_fiat_v2 = false;
    bool is_renault_v1 = false;
    flipper_format_rewind(fff);
    if(flipper_format_read_string(fff, "Protocol", proto)) {
        is_fiat_v1 = furi_string_equal_str(proto, FIAT_V1_PROTOCOL_NAME);
        is_fiat_v2 = furi_string_equal_str(proto, FIAT_V2_PROTOCOL_NAME);
        is_renault_v1 = furi_string_equal_str(proto, RENAULT_PROTOCOL_V1_NAME);
    }
    furi_string_free(proto);
    if(!is_fiat_v1 && !is_fiat_v2 && !is_renault_v1) return false;

    if(is_renault_v1) {
        // Renault V1: read the generic 64-bit "Key" (8 bytes big-endian) and the
        // 18-bit "Key2". Derive serial/button/counter from `data` exactly as
        // renault_v1_parse_fields() does, then build uid + payload42.
        uint8_t key_data[8] = {0};
        flipper_format_rewind(fff);
        if(!flipper_format_read_hex(fff, "Key", key_data, sizeof(key_data))) return false;
        uint64_t data = 0;
        for(size_t i = 0; i < sizeof(key_data); i++) {
            data = (data << 8) | key_data[i];
        }

        uint32_t key2 = 0;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "Key2", &key2, 1)) return false;

        uint32_t serial = (uint32_t)(data >> 40);
        uint8_t button = (uint8_t)((data >> 32) & 0xFFU);
        uint8_t counter = (uint8_t)((data >> 24) & 0xFFU);

        *uid_out = serial & 0xFFFFFFUL;
        *payload42_out = subghz_protocol_renault_v1_payload42(data, key2);
        *rv1_btn_out = button;
        *rv1_cnt_out = counter;
        // control/button/hop are unused for Renault V1 (slice/combo derived).
        *control_out = 0;
        *button_out = 0;
        *hop_out = 0;
        *is_v2_out = false;
        *is_rv1_out = true;
        return true;
    }

    *is_rv1_out = false;

    if(is_fiat_v2) {
        // Fiat V2: read the 14-byte Raw frame; derive uid + hop from fields.
        uint8_t raw[14] = {0};
        flipper_format_rewind(fff);
        if(!flipper_format_read_hex(fff, "Raw", raw, 14)) return false;

        uint32_t serial = 0, hop = 0;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "Serial", &serial, 1)) return false;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "Hop", &hop, 1)) return false;

        *uid_out = serial;
        *hop_out = hop;
        // control/button are placeholders; the real IV combo is resolved during
        // the attack. Fill with combo-0 values for completeness.
        *control_out = subghz_protocol_fiat_v2_iv_control_for_combo(raw, 0);
        *button_out = subghz_protocol_fiat_v2_iv_button_for_combo(raw, 0);
        memcpy(raw_out, raw, 14);
        *is_v2_out = true;
        return true;
    }

    // Fiat V1 (unchanged behavior).
    uint32_t serial = 0, cnt = 0, btn = 0, hop = 0;
    flipper_format_rewind(fff);
    if(!flipper_format_read_uint32(fff, "Serial", &serial, 1)) return false;
    flipper_format_rewind(fff);
    if(!flipper_format_read_uint32(fff, "Cnt", &cnt, 1)) return false;
    flipper_format_rewind(fff);
    if(!flipper_format_read_uint32(fff, "Btn", &btn, 1)) return false;
    flipper_format_rewind(fff);
    if(!flipper_format_read_uint32(fff, "Hop", &hop, 1)) return false;

    *uid_out = serial;
    *control_out = (uint16_t)(cnt & 0x03FFU);
    *button_out = (uint8_t)(btn & 0x0FU);
    *hop_out = hop;
    *is_v2_out = false;
    return true;
}

// Scan same directory as `main_path` for other Fiat V1 .sub files with the
// same UID. Adds up to (MAX_CAPTURES - 1) additional captures to `bf`.
static uint8_t hitag2_bf_scan_directory_for_captures(
    SubGhzHitag2Bf* bf,
    const char* main_path,
    uint32_t target_uid) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    FuriString* dir_path = furi_string_alloc();
    FuriString* main_name = furi_string_alloc();
    path_extract_dirname(main_path, dir_path);
    path_extract_filename_no_ext(main_path, main_name);

    File* dir = storage_file_alloc(storage);
    uint8_t added = 0;

    if(storage_dir_open(dir, furi_string_get_cstr(dir_path))) {
        FileInfo info;
        char name_buf[128];
        uint32_t scanned = 0;
        while(scanned < HITAG2_BF_MAX_SCAN_FILES &&
              storage_dir_read(dir, &info, name_buf, sizeof(name_buf))) {
            scanned++;
            // Skip directories and non-.sub files
            if(info.flags & FSF_DIRECTORY) continue;
            size_t nlen = strlen(name_buf);
            if(nlen < 5) continue;
            if(strcmp(name_buf + nlen - 4, ".sub") != 0) continue;

            // Skip the file we already loaded (the "main" one)
            FuriString* candidate_name = furi_string_alloc_set_str(name_buf);
            path_extract_filename(candidate_name, candidate_name, true);
            bool same_as_main = furi_string_equal(candidate_name, main_name);
            furi_string_free(candidate_name);
            if(same_as_main) continue;

            // Read the file
            FuriString* full_path = furi_string_alloc_printf(
                "%s/%s", furi_string_get_cstr(dir_path), name_buf);
            FlipperFormat* fff = flipper_format_file_alloc(storage);
            if(flipper_format_file_open_existing(fff, furi_string_get_cstr(full_path))) {
                uint32_t uid;
                uint16_t control;
                uint8_t button;
                uint32_t hop;
                uint8_t raw[14];
                bool is_v2 = false;
                bool is_rv1 = false;
                uint64_t payload42 = 0;
                uint8_t rv1_btn = 0;
                uint8_t rv1_cnt = 0;
                if(hitag2_bf_extract_capture(
                       fff,
                       &uid,
                       &control,
                       &button,
                       &hop,
                       raw,
                       &is_v2,
                       &is_rv1,
                       &payload42,
                       &rv1_btn,
                       &rv1_cnt)) {
                    // Renault V1 matches siblings on uid = serial & 0xFFFFFF
                    // (already masked inside the extractor for RV1).
                    if(uid == target_uid) {
                        bool ok;
                        if(is_rv1) {
                            ok = subghz_hitag2_bf_add_capture_renault_v1(
                                bf, uid, payload42, rv1_btn, rv1_cnt);
                        } else if(is_v2) {
                            ok = subghz_hitag2_bf_add_capture_v2(bf, uid, hop, raw);
                        } else {
                            ok = subghz_hitag2_bf_add_capture(bf, uid, control, button, hop);
                        }
                        if(ok) {
                            added++;
                            FURI_LOG_I(
                                TAG,
                                "Added %s capture from %s: cnt=%u btn=%02X hop=%08lX",
                                is_rv1 ? "RV1" : (is_v2 ? "V2" : "V1"),
                                name_buf,
                                control,
                                button,
                                (unsigned long)hop);
                            if(subghz_hitag2_bf_get_capture_count(bf) >=
                               SUBGHZ_HITAG2_BF_MAX_CAPTURES) {
                                flipper_format_free(fff);
                                furi_string_free(full_path);
                                break;
                            }
                        }
                    }
                }
            }
            flipper_format_free(fff);
            furi_string_free(full_path);
        }
        storage_dir_close(dir);
    }

    storage_file_free(dir);
    furi_string_free(main_name);
    furi_string_free(dir_path);
    furi_record_close(RECORD_STORAGE);

    return added;
}

// Write the found key back into the .sub file
static void hitag2_bf_write_key_to_fff(Hitag2BfCtx* ctx) {
    FlipperFormat* real_fff = subghz_txrx_get_fff_data(ctx->subghz->txrx);
    if(!real_fff) return;

    // Format the key as "XX XX XX XX XX XX"
    char key_str[32];
    snprintf(
        key_str,
        sizeof(key_str),
        "%02X %02X %02X %02X %02X %02X",
        ctx->found_key[0],
        ctx->found_key[1],
        ctx->found_key[2],
        ctx->found_key[3],
        ctx->found_key[4],
        ctx->found_key[5]);

    flipper_format_rewind(real_fff);
    flipper_format_insert_or_update_string_cstr(real_fff, "Hitag2 Key", key_str);
    flipper_format_rewind(real_fff);
    flipper_format_insert_or_update_uint32(
        real_fff, "Hitag2 Epoch", &ctx->found_epoch, 1);

    // [HITAG2_BF] For Fiat V2, also persist which of the 4 IV combos validated
    // so the encoder/decoder knows how to derive (button, control) for the hop.
    if(ctx->is_fiat_v2) {
        uint32_t iv_combo = ctx->iv_combo;
        flipper_format_rewind(real_fff);
        flipper_format_insert_or_update_uint32(real_fff, "Hitag2 IV", &iv_combo, 1);
    }

    // [HITAG2_BF] For Renault V1, persist the resolved IV combo AND hop slice, plus
    // the concrete "Hop" derived from (payload42, slice) so the .sub carries the
    // resolved values. The renault_v1 deserialize reads Hitag2 Key/IV/Slice back.
    if(ctx->is_renault_v1) {
        uint32_t iv_combo = ctx->iv_combo;
        uint32_t slice = ctx->hop_slice;
        uint32_t hop = subghz_protocol_renault_v1_candidate_hop(
            ctx->rv1_payload42, ctx->hop_slice);
        flipper_format_rewind(real_fff);
        flipper_format_insert_or_update_uint32(real_fff, "Hitag2 IV", &iv_combo, 1);
        flipper_format_rewind(real_fff);
        flipper_format_insert_or_update_uint32(real_fff, "Hitag2 Slice", &slice, 1);
        flipper_format_rewind(real_fff);
        flipper_format_insert_or_update_uint32(real_fff, "Hop", &hop, 1);
    }
}

// Append the recovered key to the known-keys dictionary at
// /ext/subghz/assets/hitag2 so the list grows over time and future attacks hit
// it at L1/L3 instantly. Format: one 12-hex-char line per key (no spaces),
// matching the file's convention. Deduplicates by scanning existing lines
// first. Best-effort: any storage failure is silently ignored (the key is
// already saved in the .sub). Used for BOTH local and offloaded finds.
static void hitag2_bf_append_key_to_dict(Hitag2BfCtx* ctx) {
    char key_hex[13];
    snprintf(
        key_hex,
        sizeof(key_hex),
        "%02X%02X%02X%02X%02X%02X",
        ctx->found_key[0],
        ctx->found_key[1],
        ctx->found_key[2],
        ctx->found_key[3],
        ctx->found_key[4],
        ctx->found_key[5]);

    Storage* storage = furi_record_open(RECORD_STORAGE);

    // Ensure the assets directory exists (mirrors the keeloq keystore pattern).
    storage_simply_mkdir(storage, EXT_PATH("subghz/assets"));

    const char* path = EXT_PATH("subghz/assets/hitag2");

    // Dedup: scan the existing file for this key (case-insensitive-ish; the
    // file uses uppercase, and we write uppercase, so a plain substring match
    // over the whole content is sufficient and cheap for a small dictionary).
    bool already_present = false;
    File* rf = storage_file_alloc(storage);
    if(storage_file_open(rf, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[256];
        FuriString* content = furi_string_alloc();
        size_t n;
        while((n = storage_file_read(rf, buf, sizeof(buf))) > 0) {
            furi_string_cat_str(content, ""); // ensure alloc
            for(size_t i = 0; i < n; i++) {
                furi_string_push_back(content, buf[i]);
            }
        }
        if(furi_string_search_str(content, key_hex, 0) != FURI_STRING_FAILURE) {
            already_present = true;
        }
        furi_string_free(content);
    }
    storage_file_close(rf);
    storage_file_free(rf);

    if(!already_present) {
        File* wf = storage_file_alloc(storage);
        // Open for append (create if missing).
        if(storage_file_open(wf, path, FSAM_WRITE, FSOM_OPEN_APPEND)) {
            char line[16];
            int len = snprintf(line, sizeof(line), "%s\n", key_hex);
            if(len > 0) {
                storage_file_write(wf, line, (size_t)len);
            }
        }
        storage_file_close(wf);
        storage_file_free(wf);
    }

    furi_record_close(RECORD_STORAGE);
}

// -----------------------------------------------------------------------------
// Progress callback (from worker thread)
// -----------------------------------------------------------------------------

static bool hitag2_bf_progress_cb(
    uint8_t level,
    const char* level_name,
    uint8_t progress,
    uint64_t keys_tested,
    void* context) {
    Hitag2BfCtx* ctx = context;
    if(ctx->cancel) return false;

    uint32_t now = furi_get_tick();
    uint32_t elapsed_ms = now - ctx->start_tick;
    uint32_t elapsed_sec = elapsed_ms / 1000U;
    uint32_t keys_per_sec = (elapsed_ms > 0) ?
                                (uint32_t)((keys_tested * 1000ULL) / elapsed_ms) :
                                0;
    // ETA is unreliable across levels; only estimate within current level
    // Assume worst-case remaining = 100% - progress% of the current level
    uint32_t eta_sec = 0;
    if(progress < 100 && keys_per_sec > 0) {
        // Rough estimate: assume linear time in this level
        uint32_t remaining_pct = (uint32_t)(100 - progress);
        eta_sec = (elapsed_sec * remaining_pct) / (progress > 0 ? progress : 1);
        // Cap at 24h
        if(eta_sec > 86400U) eta_sec = 86400U;
    }

    subghz_view_hitag2_bf_update_stats(
        ctx->subghz->subghz_hitag2_bf,
        level,
        level_name,
        progress,
        keys_tested,
        keys_per_sec,
        elapsed_sec,
        eta_sec,
        subghz_hitag2_bf_get_capture_count(ctx->bf));

    return true;
}

// -----------------------------------------------------------------------------
// BLE compute-offload (Flipper offloads the heavy attack to a connected phone)
// -----------------------------------------------------------------------------

static void hitag2_ble_data_received(uint8_t* data, uint16_t size, void* context) {
    Hitag2BfCtx* ctx = context;
    if(size < 1 || ctx->cancel) return;

    if(data[0] == HT_MSG_BF_PROGRESS && size >= 10) {
        uint8_t pct = data[1];
        uint64_t slots_done = 0;
        memcpy(&slots_done, data + 2, 8);

        uint32_t elapsed_sec = (furi_get_tick() - ctx->start_tick) / 1000U;

        subghz_view_hitag2_bf_update_stats(
            ctx->subghz->subghz_hitag2_bf,
            SubGhzHitag2BfLevelHitag2Hell,
            "H2H",
            pct,
            slots_done,
            0,
            elapsed_sec,
            0,
            subghz_hitag2_bf_get_capture_count(ctx->bf));

    } else if(data[0] == HT_MSG_BF_RESULT && size >= 12) {
        uint8_t found = data[1];

        if(found) {
            memcpy(ctx->found_key, data + 2, 6);
            memcpy(&ctx->found_epoch, data + 8, 4);
            ctx->found_level = SubGhzHitag2BfLevelHitag2Hell;
            ctx->success = true;

            // For Fiat V2 / Renault V1 the phone ALSO resolves the IV combo (and,
            // for RV1, the hop slice). Rather than trusting a value packed into
            // the epoch field over the wire, re-resolve it LOCALLY from the
            // returned key using the same verifier the local worker uses. This
            // is cheap (a 4-way or 12-way check) and stamps iv_combo/hop_slice
            // into ctx->bf's capture[0], which the DONE handler reads back to
            // write the .sub (Hitag2 IV / Hitag2 Slice). V2/RV1 always resolve
            // against epoch 0 (their Fiat epoch is fixed 0); the phone's epoch
            // field carries combo/slice metadata that we intentionally ignore.
            if(ctx->is_fiat_v2 || ctx->is_renault_v1) {
                if(subghz_hitag2_bf_verify_multi_resolve_key(ctx->bf, ctx->found_key, 0)) {
                    // Re-verified locally; force the persisted epoch to 0 so the
                    // packed slice/combo bits never leak into "Hitag2 Epoch".
                    ctx->found_epoch = 0;
                } else {
                    // The returned key does not validate our captures — reject
                    // it so we fall through to the "not found" UI instead of
                    // writing a bogus key to the .sub.
                    ctx->success = false;
                }
            }
        }

        view_dispatcher_send_custom_event(
            ctx->subghz->view_dispatcher, HITAG2_BF_EVENT_DONE);
    }
}

static void hitag2_ble_cleanup(Hitag2BfCtx* ctx) {
    if(!ctx->ble_offload) return;
    Bt* bt = furi_record_open(RECORD_BT);
    bt_set_custom_data_callback(bt, NULL, NULL);
    furi_record_close(RECORD_BT);
    ctx->ble_offload = false;
}

static bool hitag2_ble_start_offload(Hitag2BfCtx* ctx) {
    Bt* bt = furi_record_open(RECORD_BT);
    if(!bt_is_connected(bt)) {
        furi_record_close(RECORD_BT);
        return false;
    }

    // Register callback for incoming data (progress/result)
    bt_set_custom_data_callback(bt, hitag2_ble_data_received, ctx);

    // Determine the wire proto from the (already-parsed) primary-capture flags.
    // The firmware ALWAYS sends the combo/slice-aware 0x24 request; Fiat V1 is
    // just proto 0 (one code path for all three protocols).
    uint8_t proto;
    uint8_t cap_cap; // per-proto max captures that fit the 64-byte budget
    if(ctx->is_fiat_v2) {
        proto = HT_MSG_BF_PROTO_FIAT_V2;
        cap_cap = HT_MSG_BF_MAX_OFFLOAD_CAPTURES_V2;
    } else if(ctx->is_renault_v1) {
        proto = HT_MSG_BF_PROTO_RENAULT_V1;
        cap_cap = HT_MSG_BF_MAX_OFFLOAD_CAPTURES_RV1;
    } else {
        proto = HT_MSG_BF_PROTO_FIAT_V1;
        cap_cap = HT_MSG_BF_MAX_OFFLOAD_CAPTURES_V1;
    }

    // Build the BF request from the captures already loaded in ctx. Cap the
    // capture count to the per-proto maximum so the request stays within
    // BLE_SVC_SERIAL_CUSTOM_DATA_LEN_MAX (64).
    uint8_t cap_total = subghz_hitag2_bf_get_capture_count(ctx->bf);
    uint8_t cap_count = (cap_total > cap_cap) ? cap_cap : cap_total;

    uint32_t uid = subghz_hitag2_bf_get_uid(ctx->bf);
    uint32_t l0_start = 0;
    uint32_t l0_end = 0;

    uint8_t req[BLE_SVC_SERIAL_CUSTOM_DATA_LEN_MAX];
    uint16_t off = 0;
    req[off++] = HT_MSG_BF_REQUEST_V2; // [0]
    req[off++] = proto; // [1]
    memcpy(req + off, &uid, 4); // [2..5] uid LE
    off += 4;
    memcpy(req + off, &l0_start, 4); // [6..9] l0_start LE
    off += 4;
    memcpy(req + off, &l0_end, 4); // [10..13] l0_end LE
    off += 4;
    req[off++] = cap_count; // [14] capture_count
    // off == HT_MSG_BF_REQUEST_V2_HEADER_LEN (15) here.

    for(uint8_t i = 0; i < cap_count; i++) {
        if(proto == HT_MSG_BF_PROTO_FIAT_V2) {
            // raw[14]
            uint8_t raw[14];
            subghz_hitag2_bf_get_capture_raw(ctx->bf, i, raw);
            memcpy(req + off, raw, 14);
            off += 14;
        } else if(proto == HT_MSG_BF_PROTO_RENAULT_V1) {
            // payload42 (6 bytes LE) + button(1) + counter(1)
            uint64_t payload42 = 0;
            uint8_t button = 0;
            uint8_t counter = 0;
            subghz_hitag2_bf_get_capture_rv1(ctx->bf, i, &payload42, &button, &counter);
            for(uint8_t b = 0; b < 6; b++) {
                req[off++] = (uint8_t)((payload42 >> (8U * b)) & 0xFFU);
            }
            req[off++] = button;
            req[off++] = counter;
        } else {
            // Fiat V1: btn(1) + cnt(2 LE) + hop(4 LE)
            uint16_t control = 0;
            uint8_t button = 0;
            uint32_t hop = 0;
            subghz_hitag2_bf_get_capture(ctx->bf, i, NULL, &control, &button, &hop);
            req[off++] = button; // btn:1
            memcpy(req + off, &control, 2); // cnt:2 LE
            off += 2;
            memcpy(req + off, &hop, 4); // hop:4 LE
            off += 4;
        }
    }

    bt_custom_data_tx(bt, req, off);

    furi_record_close(RECORD_BT);
    ctx->ble_offload = true;
    return true;
}

// -----------------------------------------------------------------------------

static int32_t hitag2_bf_thread(void* context) {
    Hitag2BfCtx* ctx = context;

    ctx->success = subghz_hitag2_bf_run(ctx->bf, hitag2_bf_progress_cb, ctx);
    if(ctx->success) {
        subghz_hitag2_bf_get_result(
            ctx->bf, ctx->found_key, &ctx->found_epoch, &ctx->found_level);
    }

    view_dispatcher_send_custom_event(
        ctx->subghz->view_dispatcher, HITAG2_BF_EVENT_DONE);
    return 0;
}

static void hitag2_bf_view_callback(SubGhzCustomEvent event, void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

// -----------------------------------------------------------------------------
// Scene entrypoints
// -----------------------------------------------------------------------------

void subghz_scene_hitag2_bf_on_enter(void* context) {
    SubGhz* subghz = context;

    Hitag2BfCtx* ctx = malloc(sizeof(Hitag2BfCtx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->subghz = subghz;
    ctx->result = furi_string_alloc_set("No result");
    ctx->bf = subghz_hitag2_bf_alloc();

    // Parse primary capture from the currently-loaded fff
    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    uint32_t uid = 0;
    uint16_t control = 0;
    uint8_t button = 0;
    uint32_t hop = 0;
    uint8_t raw[14];
    bool is_v2 = false;
    bool is_rv1 = false;
    uint64_t payload42 = 0;
    uint8_t rv1_btn = 0;
    uint8_t rv1_cnt = 0;

    if(!hitag2_bf_extract_capture(
           fff,
           &uid,
           &control,
           &button,
           &hop,
           raw,
           &is_v2,
           &is_rv1,
           &payload42,
           &rv1_btn,
           &rv1_cnt)) {
        subghz_view_hitag2_bf_set_result(
            subghz->subghz_hitag2_bf, false, "Not a Fiat V1/V2/Renault V1 signal");
        // Still install a valid ctx so on_exit / on_event cleanup works
        scene_manager_set_scene_state(
            subghz->scene_manager, SubGhzSceneHitag2Bf, (uint32_t)(uintptr_t)ctx);
        subghz_view_hitag2_bf_set_callback(
            subghz->subghz_hitag2_bf, hitag2_bf_view_callback, subghz);
        view_dispatcher_switch_to_view(
            subghz->view_dispatcher, SubGhzViewIdHitag2Bf);
        return;
    }

    ctx->is_fiat_v2 = is_v2;
    ctx->is_renault_v1 = is_rv1;
    if(is_rv1) {
        ctx->rv1_payload42 = payload42;
        subghz_hitag2_bf_add_capture_renault_v1(ctx->bf, uid, payload42, rv1_btn, rv1_cnt);
    } else if(is_v2) {
        subghz_hitag2_bf_add_capture_v2(ctx->bf, uid, hop, raw);
    } else {
        subghz_hitag2_bf_add_capture(ctx->bf, uid, control, button, hop);
    }

    // Auto-scan same directory for additional captures with the same UID
    uint8_t added = hitag2_bf_scan_directory_for_captures(
        ctx->bf, furi_string_get_cstr(subghz->file_path), uid);
    FURI_LOG_I(
        TAG,
        "Total captures: %u (primary + %u auto-loaded)",
        subghz_hitag2_bf_get_capture_count(ctx->bf),
        added);

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneHitag2Bf, (uint32_t)(uintptr_t)ctx);

    subghz_view_hitag2_bf_reset(subghz->subghz_hitag2_bf);
    subghz_view_hitag2_bf_set_callback(
        subghz->subghz_hitag2_bf, hitag2_bf_view_callback, subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdHitag2Bf);

    ctx->start_tick = furi_get_tick();

    // Try BLE offload first, fall back to the local worker thread if no phone
    // is connected. All three protocols (Fiat V1, Fiat V2, Renault V1) are now
    // offloadable: the combo/slice-aware 0x24 request carries the raw frame
    // (Fiat V2) or the 42-bit payload + button + counter (Renault V1) so the
    // phone can perform the same combo/slice search the local worker would. On
    // an offloaded find the firmware locally re-resolves slice/combo from the
    // returned key before writing the .sub (see hitag2_ble_data_received).
    if(!hitag2_ble_start_offload(ctx)) {
        ctx->thread = furi_thread_alloc_ex("Hitag2BF", 4096, hitag2_bf_thread, ctx);
        // Run below the UI/input services (Normal=16) so the compute loop can
        // never starve them on the single-core M4 — BACK stays responsive even
        // during a long deep_search burst.
        furi_thread_set_priority(ctx->thread, FuriThreadPriorityLow);
        furi_thread_start(ctx->thread);
    }
}

bool subghz_scene_hitag2_bf_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    Hitag2BfCtx* ctx = (Hitag2BfCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneHitag2Bf);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == HITAG2_BF_EVENT_DONE) {
            hitag2_ble_cleanup(ctx);
            if(ctx->thread) {
                furi_thread_join(ctx->thread);
                furi_thread_free(ctx->thread);
                ctx->thread = NULL;
            }

            if(ctx->success) {
                // For Fiat V2, pull the IV combo that validated the found key
                // (resolved into the capture during verification) so it can be
                // persisted alongside the key. Whether the key came from the
                // local worker or from an offloaded (phone) find, bf holds the
                // resolved combo: the local worker resolves it during the attack,
                // and the offload RESULT handler re-resolves it from the returned
                // key before signalling DONE.
                if(ctx->is_fiat_v2 && ctx->bf) {
                    ctx->iv_combo = subghz_hitag2_bf_get_capture_iv_combo(ctx->bf, 0);
                }
                // For Renault V1, pull the resolved IV combo AND hop slice from
                // capture 0. As with V2, bf holds the resolved params for both
                // local and offloaded finds (offload re-resolves from the key).
                if(ctx->is_renault_v1 && ctx->bf) {
                    ctx->iv_combo = subghz_hitag2_bf_get_capture_iv_combo(ctx->bf, 0);
                    ctx->hop_slice = subghz_hitag2_bf_get_capture_hop_slice(ctx->bf, 0);
                }
                // Persist key into the .sub file
                hitag2_bf_write_key_to_fff(ctx);
                // ...and grow the known-keys dictionary so it's found instantly
                // next time. Fires for both local finds and offloaded (phone)
                // finds, since both converge on this DONE handler.
                hitag2_bf_append_key_to_dict(ctx);
                subghz_save_protocol_to_file(
                    subghz,
                    subghz_txrx_get_fff_data(subghz->txrx),
                    furi_string_get_cstr(subghz->file_path));

                char msg[128];
                const char* level_str = "?";
                switch(ctx->found_level) {
                case SubGhzHitag2BfLevelKnown: level_str = "Known"; break;
                case SubGhzHitag2BfLevelFlashDict: level_str = "Flash"; break;
                case SubGhzHitag2BfLevelSDDict: level_str = "SD"; break;
                case SubGhzHitag2BfLevelHeuristic: level_str = "Heur"; break;
                case SubGhzHitag2BfLevelHitag2Hell: level_str = "H2H"; break;
                default: break;
                }
                snprintf(
                    msg,
                    sizeof(msg),
                    "%02X %02X %02X %02X %02X %02X\n"
                    "[L%u:%s] epoch=%lu\n"
                    "Saved to .sub",
                    ctx->found_key[0],
                    ctx->found_key[1],
                    ctx->found_key[2],
                    ctx->found_key[3],
                    ctx->found_key[4],
                    ctx->found_key[5],
                    ctx->found_level,
                    level_str,
                    (unsigned long)ctx->found_epoch);
                subghz_view_hitag2_bf_set_result(
                    subghz->subghz_hitag2_bf, true, msg);
            } else if(!ctx->cancel) {
                char msg[96];
                snprintf(
                    msg,
                    sizeof(msg),
                    "Tried %lu keys.\n"
                    "%u capture%s used.\n"
                    "Try adding more captures\nor dict on SD.",
                    (unsigned long)subghz_hitag2_bf_get_total_keys_tested(ctx->bf),
                    subghz_hitag2_bf_get_capture_count(ctx->bf),
                    subghz_hitag2_bf_get_capture_count(ctx->bf) == 1 ? "" : "s");
                subghz_view_hitag2_bf_set_result(
                    subghz->subghz_hitag2_bf, false, msg);
            } else {
                subghz_view_hitag2_bf_set_result(
                    subghz->subghz_hitag2_bf, false, "Cancelled.");
            }
            return true;

        } else if(event.event == SubGhzCustomEventViewTransmitterBack) {
            if(ctx->ble_offload) {
                // Tell the phone to stop the offloaded attack
                Bt* bt = furi_record_open(RECORD_BT);
                uint8_t cancel_msg = HT_MSG_BF_CANCEL;
                bt_custom_data_tx(bt, &cancel_msg, 1);
                furi_record_close(RECORD_BT);
                hitag2_ble_cleanup(ctx);
            }
            if(ctx->thread) {
                ctx->cancel = true;
                furi_thread_join(ctx->thread);
                furi_thread_free(ctx->thread);
                ctx->thread = NULL;
            }
            if(ctx->bf) subghz_hitag2_bf_free(ctx->bf);
            furi_string_free(ctx->result);
            free(ctx);
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneHitag2Bf, 0);
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    }
    return false;
}

void subghz_scene_hitag2_bf_on_exit(void* context) {
    SubGhz* subghz = context;
    Hitag2BfCtx* ctx = (Hitag2BfCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneHitag2Bf);

    if(ctx) {
        hitag2_ble_cleanup(ctx);
        if(ctx->thread) {
            ctx->cancel = true;
            furi_thread_join(ctx->thread);
            furi_thread_free(ctx->thread);
            ctx->thread = NULL;
        }
        if(ctx->bf) subghz_hitag2_bf_free(ctx->bf);
        furi_string_free(ctx->result);
        free(ctx);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneHitag2Bf, 0);
    }
}

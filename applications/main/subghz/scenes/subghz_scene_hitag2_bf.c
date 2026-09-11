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
#include <furi.h>
#include <storage/storage.h>
#include <toolbox/path.h>
#include <bt/bt_service/bt.h>

#define TAG "SubGhzSceneHitag2Bf"

#define HITAG2_BF_EVENT_DONE (0xD2)

// Maximum files to scan in the same directory when auto-loading captures
#define HITAG2_BF_MAX_SCAN_FILES 64U

// --- BLE compute-offload wire protocol (must match qUnleashed exactly) ---
#define HT_MSG_BF_REQUEST  0x20 // Flipper -> phone
#define HT_MSG_BF_PROGRESS 0x21 // phone -> Flipper
#define HT_MSG_BF_RESULT   0x22 // phone -> Flipper
#define HT_MSG_BF_CANCEL   0x23 // Flipper -> phone

// Cap offloaded captures so the request fits BLE_SVC_SERIAL_CUSTOM_DATA_LEN_MAX
// (64). Request = 14-byte header + 7 bytes/capture -> 7 captures = 63 bytes.
#define HT_MSG_BF_MAX_OFFLOAD_CAPTURES 7U

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
} Hitag2BfCtx;

// -----------------------------------------------------------------------------
// Helpers to parse a Fiat V1 .sub file
// -----------------------------------------------------------------------------

static bool hitag2_bf_extract_capture(
    FlipperFormat* fff,
    uint32_t* uid_out,
    uint16_t* control_out,
    uint8_t* button_out,
    uint32_t* hop_out) {
    // Verify Protocol == "Fiat V1"
    FuriString* proto = furi_string_alloc();
    bool is_fiat_v1 = false;
    flipper_format_rewind(fff);
    if(flipper_format_read_string(fff, "Protocol", proto)) {
        is_fiat_v1 = furi_string_equal_str(proto, FIAT_V1_PROTOCOL_NAME);
    }
    furi_string_free(proto);
    if(!is_fiat_v1) return false;

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
                if(hitag2_bf_extract_capture(fff, &uid, &control, &button, &hop)) {
                    if(uid == target_uid) {
                        if(subghz_hitag2_bf_add_capture(bf, uid, control, button, hop)) {
                            added++;
                            FURI_LOG_I(
                                TAG,
                                "Added capture from %s: cnt=%u btn=%02X hop=%08lX",
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

    // Build the BF request from the captures already loaded in ctx.
    // Cap at HT_MSG_BF_MAX_OFFLOAD_CAPTURES to stay within the 64-byte limit.
    uint8_t cap_total = subghz_hitag2_bf_get_capture_count(ctx->bf);
    uint8_t cap_count = (cap_total > HT_MSG_BF_MAX_OFFLOAD_CAPTURES) ?
                            (uint8_t)HT_MSG_BF_MAX_OFFLOAD_CAPTURES :
                            cap_total;

    uint32_t uid = subghz_hitag2_bf_get_uid(ctx->bf);
    uint32_t l0_start = 0;
    uint32_t l0_end = 0;

    uint8_t req[BLE_SVC_SERIAL_CUSTOM_DATA_LEN_MAX];
    uint16_t off = 0;
    req[off++] = HT_MSG_BF_REQUEST; // [0]
    memcpy(req + off, &uid, 4); // [1..4] uid LE
    off += 4;
    memcpy(req + off, &l0_start, 4); // [5..8] l0_start LE
    off += 4;
    memcpy(req + off, &l0_end, 4); // [9..12] l0_end LE
    off += 4;
    req[off++] = cap_count; // [13] capture_count

    for(uint8_t i = 0; i < cap_count; i++) {
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

    if(!hitag2_bf_extract_capture(fff, &uid, &control, &button, &hop)) {
        subghz_view_hitag2_bf_set_result(
            subghz->subghz_hitag2_bf, false, "Not a Fiat V1 signal");
        // Still install a valid ctx so on_exit / on_event cleanup works
        scene_manager_set_scene_state(
            subghz->scene_manager, SubGhzSceneHitag2Bf, (uint32_t)(uintptr_t)ctx);
        subghz_view_hitag2_bf_set_callback(
            subghz->subghz_hitag2_bf, hitag2_bf_view_callback, subghz);
        view_dispatcher_switch_to_view(
            subghz->view_dispatcher, SubGhzViewIdHitag2Bf);
        return;
    }

    subghz_hitag2_bf_add_capture(ctx->bf, uid, control, button, hop);

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
    // is connected.
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

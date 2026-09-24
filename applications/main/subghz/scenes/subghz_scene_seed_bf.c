// [HITAG2_SEED] Scene: manual, on-demand classic-Hitag2 4-byte SEED brute force
// for a saved Renault V1 signal.
//
// This is a MANUAL action invoked ONLY from the saved-signal "Seed BF" menu.
// The heavy (~0x40000-candidate) classic Hitag2 brute force must NEVER run
// during live capture (it would stall the decoder and drop button presses), so
// it lives here, off the live path, and runs on a worker thread so the GUI
// stays responsive.
//
// Flow (heavily stripped vs. hitag2_bf.c: no BLE, no directory scan, one frame):
//   on_enter  - read `data` (64-bit "Key") + `key2` ("Key2") from the loaded
//               fff EXACTLY as renault_v1 deserialize does, show a "Running... 0%"
//               Popup (header + a STABLE heap body buffer), spawn a worker
//               running subghz_protocol_renault_v1_run_seed_bf_ex.
//   worker    - runs the BF; its progress_cb posts throttled SEED_BF_EVENT_PROGRESS
//               custom events (percentage packed in the high bits) and yields the
//               CPU; on completion posts SEED_BF_EVENT_DONE.
//   on_event  - on PROGRESS: rewrite the heap body buffer with "Running... X%" on
//               the GUI thread. On DONE: if recovered, write "Seed" (4-byte BE
//               hex) + "Recovered" (uint32 = 1) back into the fff and re-save the
//               .sub, then show a success Popup; on miss show "Seed not found".
//               Either way BACK returns to the saved menu.
//   on_exit   - stop/join the worker, free ctx, reset the Popup view.
//
// WHY custom events instead of SceneManagerEventTypeTick: the previous version
// relied on Tick to marshal progress AND pointed the popup at a stack buffer
// inside the tick handler. popup_set_text() stores the char* by POINTER (no
// copy), so once the handler returned the popup redrew freed stack memory and
// the body appeared blank ("only Seed BF"). Custom events are queued reliably to
// the GUI thread, and the body now lives in the heap ctx for a stable lifetime.

#include "../subghz_i.h"
#include "../helpers/subghz_custom_event.h"

#include <lib/subghz/protocols/renault_v1.h>
#include <furi.h>
#include <storage/storage.h>

#define TAG "SubGhzSceneSeedBf"

// Scene-local custom events (kept well clear of the SubGhzCustomEvent enum).
//
// DONE/BACK are plain events. PROGRESS is *not* a bare event: the worker packs
// the percentage into the high bits so a single reliable custom event carries
// both "there is new progress" and the value itself:
//
//   event = SEED_BF_EVENT_PROGRESS | (pct << 8)
//
// Custom events are delivered via the view_dispatcher's queue on the GUI thread
// (guaranteed delivery), unlike SceneManagerEventTypeTick whose cadence relative
// to a low-priority compute thread is not guaranteed. This is why the old
// tick-driven update never visibly ran.
#define SEED_BF_EVENT_DONE     (0xE0)
#define SEED_BF_EVENT_BACK     (0xE1)
#define SEED_BF_EVENT_PROGRESS (0xE2)

// Extract the percentage packed into a PROGRESS custom event.
#define SEED_BF_EVENT_PROGRESS_PCT(evt) ((uint8_t)(((evt) >> 8) & 0xFFU))

typedef struct {
    SubGhz* subghz;
    FuriThread* thread;
    volatile bool cancel;
    uint64_t data; // decoded 64-bit frame body (from "Key")
    uint32_t key2; // 18-bit key2 field (from "Key2")
    bool success; // BF recovered a SEED
    uint32_t seed; // recovered 4-byte SEED
    bool done_handled; // guards result handling from re-running
    volatile uint8_t progress; // 0..100, published by the worker
    uint8_t last_sent_progress; // last percentage the worker posted an event for
    uint32_t last_progress_tick; // furi_get_tick() of the last posted PROGRESS event
    bool running; // true while the worker is active
    // Heap-backed popup body text. CRITICAL: popup_set_text() stores the char*
    // by POINTER (it does not copy), so the buffer it points at MUST outlive
    // every redraw. The old code pointed the popup at a stack buffer inside the
    // tick handler; once that handler returned the stack was reused and the
    // popup redrew garbage (appearing blank). Keeping the buffer in the heap ctx
    // guarantees a stable lifetime for the life of the scene.
    char body[32];
} SeedBfCtx;

// -----------------------------------------------------------------------------
// Cooperative progress callback (mirrors PSA's psa_decrypt_progress_cb):
// yields the CPU so the GUI/idle/watchdog can run, posts a throttled progress
// event to the GUI thread, and lets a BACK press cancel the search. Returning
// false aborts.
// -----------------------------------------------------------------------------

static bool seed_bf_progress_cb(uint8_t progress, uint32_t cand_tested, void* context) {
    UNUSED(cand_tested);
    SeedBfCtx* ctx = context;
    if(ctx->cancel) return false;

    ctx->progress = progress;

    // Push progress to the GUI *reliably* by posting a custom event that carries
    // the percentage in its high bits. popup_set_* must only be touched from the
    // GUI thread, so we never draw here — the on_event handler (GUI thread) does.
    //
    // Throttle to ~200ms cadence (and always emit on a percentage change) so we
    // do not flood the view_dispatcher queue: the BF fires this callback every
    // 4096 candidates (64 times total), which is coarse enough already, but the
    // throttle keeps behavior stable if the yield step ever shrinks.
    uint32_t now = furi_get_tick();
    if(progress != ctx->last_sent_progress || (now - ctx->last_progress_tick) >= 200U) {
        ctx->last_sent_progress = progress;
        ctx->last_progress_tick = now;
        view_dispatcher_send_custom_event(
            ctx->subghz->view_dispatcher,
            SEED_BF_EVENT_PROGRESS | ((uint32_t)progress << 8));
    }

    // Yield the CPU: THIS is the actual freeze fix — it lets the GUI/idle/
    // watchdog run on the single-core M4 during the ~262k-iteration brute force.
    furi_delay_ms(1);
    return true;
}

// -----------------------------------------------------------------------------
// Worker thread: runs the classic-Hitag2 SEED brute force for the single frame.
// -----------------------------------------------------------------------------

static int32_t seed_bf_thread(void* context) {
    SeedBfCtx* ctx = context;

    FURI_LOG_I(
        TAG,
        "Seed BF start: data=%08lX%08lX key2=%05lX",
        (unsigned long)(ctx->data >> 32),
        (unsigned long)(ctx->data & 0xFFFFFFFF),
        (unsigned long)ctx->key2);

    uint32_t seed = 0;
    bool ok = subghz_protocol_renault_v1_run_seed_bf_ex(
        ctx->data, ctx->key2, &seed, seed_bf_progress_cb, ctx);
    if(!ctx->cancel) {
        ctx->success = ok;
        ctx->seed = seed;
    }

    FURI_LOG_I(
        TAG,
        "Seed BF done: ok=%d cancel=%d seed=%08lX",
        (int)ok,
        (int)ctx->cancel,
        (unsigned long)seed);

    view_dispatcher_send_custom_event(ctx->subghz->view_dispatcher, SEED_BF_EVENT_DONE);
    return 0;
}

// -----------------------------------------------------------------------------
// Popup helpers
// -----------------------------------------------------------------------------

static void seed_bf_popup_callback(void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, SEED_BF_EVENT_BACK);
}

// Write the recovered SEED + "Recovered" marker back into the current .sub and
// re-save it. Mirrors renault_v1 serialize: "Seed" = 4-byte big-endian hex,
// "Recovered" = uint32 (1). Uses the live fff + current file path, exactly like
// the neighboring hitag2_bf / psa_decrypt scenes.
static void seed_bf_write_seed_and_save(SeedBfCtx* ctx) {
    FlipperFormat* fff = subghz_txrx_get_fff_data(ctx->subghz->txrx);
    if(!fff) return;

    uint8_t seed_be[4] = {
        (uint8_t)(ctx->seed >> 24U),
        (uint8_t)(ctx->seed >> 16U),
        (uint8_t)(ctx->seed >> 8U),
        (uint8_t)ctx->seed,
    };
    flipper_format_rewind(fff);
    flipper_format_insert_or_update_hex(fff, RENAULT_V1_SEED_FIELD, seed_be, 4U);

    uint32_t recovered = 1U;
    flipper_format_rewind(fff);
    flipper_format_insert_or_update_uint32(fff, RENAULT_V1_RECOVERED_FIELD, &recovered, 1);

    subghz_save_protocol_to_file(
        ctx->subghz, fff, furi_string_get_cstr(ctx->subghz->file_path));
}

// Free the worker thread (stop it first if still running).
static void seed_bf_stop_thread(SeedBfCtx* ctx) {
    if(ctx->thread) {
        furi_thread_join(ctx->thread);
        furi_thread_free(ctx->thread);
        ctx->thread = NULL;
    }
}

// -----------------------------------------------------------------------------
// Scene entrypoints
// -----------------------------------------------------------------------------

void subghz_scene_seed_bf_on_enter(void* context) {
    SubGhz* subghz = context;

    SeedBfCtx* ctx = malloc(sizeof(SeedBfCtx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->subghz = subghz;

    // Read `data` and `key2` EXACTLY as renault_v1 deserialize does:
    //   - `data`: the generic 64-bit body serialized as the 8-byte big-endian
    //     "Key" hex field (subghz_block_generic_serialize writes it there;
    //     deserialize pulls it via subghz_block_generic_deserialize into
    //     instance->generic.data). We reconstruct it MSB-first from the 8 bytes.
    //   - `key2`: flipper_format_read_uint32(RENAULT_V1_KEY2_FIELD).
    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    bool fields_ok = false;
    if(fff) {
        uint8_t key_data[8] = {0};
        flipper_format_rewind(fff);
        if(flipper_format_read_hex(fff, "Key", key_data, sizeof(key_data))) {
            uint64_t data = 0;
            for(size_t i = 0; i < sizeof(key_data); i++) {
                data = (data << 8) | key_data[i];
            }
            ctx->data = data;

            uint32_t key2 = 0;
            flipper_format_rewind(fff);
            if(flipper_format_read_uint32(fff, RENAULT_V1_KEY2_FIELD, &key2, 1)) {
                ctx->key2 = key2;
                fields_ok = true;
            }
        }
    }

    FURI_LOG_I(
        TAG,
        "on_enter fields_ok=%d data=%08lX%08lX key2=%05lX",
        (int)fields_ok,
        (unsigned long)(ctx->data >> 32),
        (unsigned long)(ctx->data & 0xFFFFFFFF),
        (unsigned long)ctx->key2);

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneSeedBf, (uint32_t)(uintptr_t)ctx);

    Popup* popup = subghz->popup;
    popup_reset(popup);
    popup_set_context(popup, subghz);

    if(!fields_ok) {
        // Not a decodable Renault V1 frame; report and let BACK return. Only here
        // do we wire the OK->return callback (nothing is running to cancel).
        popup_set_callback(popup, seed_bf_popup_callback);
        popup_set_header(popup, "Seed BF", 64, 6, AlignCenter, AlignTop);
        popup_set_text(
            popup, "Missing Key/Key2\nfields.", 64, 30, AlignCenter, AlignTop);
        popup_disable_timeout(popup);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdPopup);
        return;
    }

    // Show progress and spawn the local worker (no BLE, single frame). Set BOTH
    // a header and a body text so the popup always has visible content from the
    // very first frame (a header-only popup can render as a near-blank screen).
    // The body is a stable heap buffer inside ctx (see the struct comment); the
    // worker-driven PROGRESS events rewrite it in place on the GUI thread.
    // NOTE: no popup OK-callback while running, so OK does nothing mid-search
    // (only hardware BACK cancels). The callback is wired on completion below.
    ctx->running = true;
    ctx->progress = 0;
    ctx->last_sent_progress = 0xFF; // force the first callback to post an event
    ctx->last_progress_tick = furi_get_tick();
    strncpy(ctx->body, "Running... 0%", sizeof(ctx->body) - 1);

    popup_set_callback(popup, NULL);
    popup_set_header(popup, "Seed BF", 64, 12, AlignCenter, AlignTop);
    popup_set_text(popup, ctx->body, 64, 34, AlignCenter, AlignTop);
    popup_disable_timeout(popup);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdPopup);

    ctx->thread = furi_thread_alloc_ex("SeedBF", 4096, seed_bf_thread, ctx);
    // Run below the UI/input services so the compute loop can never starve them
    // on the single-core M4 — BACK stays responsive.
    furi_thread_set_priority(ctx->thread, FuriThreadPriorityLow);
    furi_thread_start(ctx->thread);
}

bool subghz_scene_seed_bf_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    SeedBfCtx* ctx = (SeedBfCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneSeedBf);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if((event.event & 0xFFU) == SEED_BF_EVENT_PROGRESS) {
            // Runs on the GUI thread: reflect the worker's progress (carried in
            // the event's high bits) into the popup body. popup_* is only ever
            // touched here on the GUI thread. We rewrite ctx->body IN PLACE — the
            // popup already points at it (stable heap buffer), and popup_set_text
            // re-commits the model so the view repaints.
            if(ctx->running && !ctx->done_handled) {
                uint8_t p = SEED_BF_EVENT_PROGRESS_PCT(event.event);
                snprintf(ctx->body, sizeof(ctx->body), "Running... %u%%", (unsigned)p);
                popup_set_text(subghz->popup, ctx->body, 64, 34, AlignCenter, AlignTop);
            }
            return true;
        } else if(event.event == SEED_BF_EVENT_DONE) {
            ctx->running = false;
            if(ctx->done_handled) return true;
            ctx->done_handled = true;

            seed_bf_stop_thread(ctx);

            Popup* popup = subghz->popup;
            popup_reset(popup);
            popup_set_context(popup, subghz);
            popup_set_callback(popup, seed_bf_popup_callback);

            if(ctx->success) {
                seed_bf_write_seed_and_save(ctx);

                // Write the result into the stable heap buffer, NOT a stack
                // buffer: popup_set_text keeps the pointer, and this popup lives
                // until BACK — a stack buffer would dangle and render garbage.
                snprintf(
                    ctx->body, sizeof(ctx->body), "Seed: %08lX", (unsigned long)ctx->seed);
                popup_set_header(popup, "Seed found", 64, 6, AlignCenter, AlignTop);
                popup_set_text(popup, ctx->body, 64, 30, AlignCenter, AlignTop);
                FURI_LOG_I(TAG, "SEED recovered: %08lX", (unsigned long)ctx->seed);
            } else {
                popup_set_header(popup, "Seed BF", 64, 6, AlignCenter, AlignTop);
                popup_set_text(popup, "Seed not found", 64, 30, AlignCenter, AlignTop);
            }
            popup_disable_timeout(popup);
            view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdPopup);
            return true;

        } else if(event.event == SEED_BF_EVENT_BACK) {
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        // Hardware BACK: signal the worker to stop (on_exit joins it) and return
        // to the saved menu. Do NOT free ctx here — on_exit owns cleanup.
        ctx->cancel = true;
        scene_manager_previous_scene(subghz->scene_manager);
        return true;
    }
    return false;
}

void subghz_scene_seed_bf_on_exit(void* context) {
    SubGhz* subghz = context;
    SeedBfCtx* ctx = (SeedBfCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneSeedBf);

    if(ctx) {
        // Stop/join the worker before freeing anything it references.
        if(ctx->thread) {
            ctx->cancel = true;
            furi_thread_join(ctx->thread);
            furi_thread_free(ctx->thread);
            ctx->thread = NULL;
        }
        free(ctx);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneSeedBf, 0);
    }

    popup_reset(subghz->popup);
}

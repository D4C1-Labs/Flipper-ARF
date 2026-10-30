#include "../subghz_i.h"
#include "../helpers/subghz_txrx_i.h"
#include <lib/subghz/protocols/keeloq.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/subghz_keystore.h>
#include <furi.h>
#include <bt/bt_service/bt.h>

#define KL_DECRYPT_EVENT_DONE      (0xD2)
#define KL_DECRYPT_EVENT_CANDIDATE (0xD3)
#define KL_TOTAL_KEYS              0x100000000ULL

#define KL_MSG_BF_REQUEST  0x10
#define KL_MSG_BF_PROGRESS 0x11
#define KL_MSG_BF_RESULT   0x12
#define KL_MSG_BF_CANCEL   0x13

typedef struct {
    SubGhz* subghz;
    volatile bool cancel;
    uint32_t start_tick;
    bool success;
    FuriString* result;

    uint32_t fix;
    uint32_t hop;
    uint32_t serial;
    uint8_t btn;
    uint16_t disc;

    uint32_t hop2;

    uint32_t candidate_count;
    uint64_t recovered_mfkey;
    uint16_t recovered_type;
    uint32_t recovered_cnt;

    bool ble_offload;
} KlDecryptCtx;

static void kl_ble_data_received(uint8_t* data, uint16_t size, void* context) {
    KlDecryptCtx* ctx = context;
    if(size < 1 || ctx->cancel) return;

    if(data[0] == KL_MSG_BF_PROGRESS && size >= 10) {
        uint32_t keys_tested, keys_per_sec;
        memcpy(&keys_tested, data + 2, 4);
        memcpy(&keys_per_sec, data + 6, 4);

        uint32_t elapsed_sec = (furi_get_tick() - ctx->start_tick) / 1000;
        uint32_t remaining = (keys_tested > 0) ? (0xFFFFFFFFU - keys_tested) : 0xFFFFFFFFU;
        uint32_t eta_sec = (keys_per_sec > 0) ? (remaining / keys_per_sec) : 0;
        uint8_t pct = (uint8_t)((uint64_t)keys_tested * 100 / 0xFFFFFFFFULL);

        subghz_view_keeloq_decrypt_update_stats(
            ctx->subghz->subghz_keeloq_decrypt, pct, keys_tested, keys_per_sec, elapsed_sec, eta_sec);

    } else if(data[0] == KL_MSG_BF_RESULT && size >= 26) {
        uint8_t found = data[1];

        if(found == 1) {
            uint64_t mfkey = 0;
            uint32_t cnt = 0;
            memcpy(&mfkey, data + 2, 8);
            memcpy(&cnt, data + 18, 4);
            uint16_t learn_type = (size >= 27) ? data[26] : 6;

            ctx->candidate_count++;
            ctx->recovered_mfkey = mfkey;
            ctx->recovered_type = learn_type;
            ctx->recovered_cnt = cnt;

            subghz_view_keeloq_decrypt_update_candidates(
                ctx->subghz->subghz_keeloq_decrypt, ctx->candidate_count);

            view_dispatcher_send_custom_event(
                ctx->subghz->view_dispatcher, KL_DECRYPT_EVENT_CANDIDATE);

        } else if(found == 2) {
            ctx->success = (ctx->candidate_count > 0);
            view_dispatcher_send_custom_event(
                ctx->subghz->view_dispatcher, KL_DECRYPT_EVENT_DONE);
        }
    }
}

static void kl_ble_cleanup(KlDecryptCtx* ctx) {
    if(!ctx->ble_offload) return;
    Bt* bt = furi_record_open(RECORD_BT);
    bt_set_custom_data_callback(bt, NULL, NULL);
    furi_record_close(RECORD_BT);
    ctx->ble_offload = false;
}

static bool kl_ble_start_offload(KlDecryptCtx* ctx) {
    Bt* bt = furi_record_open(RECORD_BT);
    if(!bt_is_connected(bt)) {
        furi_record_close(RECORD_BT);
        return false;
    }

    bt_set_custom_data_callback(bt, kl_ble_data_received, ctx);

    uint8_t req[18];
    req[0] = KL_MSG_BF_REQUEST;
    req[1] = ctx->subghz->keeloq_bf2.learn_type;
    memcpy(req + 2, &ctx->fix, 4);
    memcpy(req + 6, &ctx->hop, 4);
    memcpy(req + 10, &ctx->hop2, 4);
    memcpy(req + 14, &ctx->serial, 4);
    bt_custom_data_tx(bt, req, sizeof(req));

    furi_record_close(RECORD_BT);
    ctx->ble_offload = true;

    subghz_view_keeloq_decrypt_set_status(
        ctx->subghz->subghz_keeloq_decrypt, "[BT] Offloading...");
    return true;
}

static void kl_decrypt_view_callback(SubGhzCustomEvent event, void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

void subghz_scene_keeloq_decrypt_on_enter(void* context) {
    SubGhz* subghz = context;

    // [ARF] Lazy-allocate this heavy view only now that its scene is entered.
    subghz_ensure_keeloq_decrypt_view(subghz);

    KlDecryptCtx* ctx = malloc(sizeof(KlDecryptCtx));
    memset(ctx, 0, sizeof(KlDecryptCtx));
    ctx->subghz = subghz;
    ctx->result = furi_string_alloc_set("No result");

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    flipper_format_rewind(fff);

    uint8_t key_data[8] = {0};
    if(flipper_format_read_hex(fff, "Key", key_data, 8)) {
        uint64_t raw = 0;
        for(uint8_t i = 0; i < 8; i++) {
            raw = (raw << 8) | key_data[i];
        }
        uint64_t reversed = subghz_protocol_blocks_reverse_key(raw, 64);
        ctx->fix = (uint32_t)(reversed >> 32);
        ctx->hop = (uint32_t)(reversed & 0xFFFFFFFF);
    }

    ctx->serial = ctx->fix & 0x0FFFFFFF;
    ctx->btn = ctx->fix >> 28;
    ctx->disc = ctx->serial & 0x3FF;
    ctx->hop2 = subghz->keeloq_bf2.sig2_loaded ? subghz->keeloq_bf2.hop2 : 0;

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneKeeloqDecrypt, (uint32_t)(uintptr_t)ctx);

    subghz_view_keeloq_decrypt_reset(subghz->subghz_keeloq_decrypt);
    subghz_view_keeloq_decrypt_set_callback(
        subghz->subghz_keeloq_decrypt, kl_decrypt_view_callback, subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdKeeloqDecrypt);

    ctx->start_tick = furi_get_tick();

    if(!kl_ble_start_offload(ctx)) {
        char msg[128];
        snprintf(msg, sizeof(msg),
            "No BLE connection!\n"
            "Connect companion app\n"
            "and try again.\n\n"
            "Fix:0x%08lX\nHop:0x%08lX",
            ctx->fix, ctx->hop);
        subghz_view_keeloq_decrypt_set_result(
            subghz->subghz_keeloq_decrypt, false, msg);
    }
}

bool subghz_scene_keeloq_decrypt_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    KlDecryptCtx* ctx = (KlDecryptCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneKeeloqDecrypt);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KL_DECRYPT_EVENT_CANDIDATE) {
            if(!subghz->keeloq_keys_manager) {
                subghz->keeloq_keys_manager = subghz_keeloq_keys_alloc();
            }
            uint16_t learning_type = ctx->recovered_type ?
                                         ctx->recovered_type :
                                         KEELOQ_LEARNING_SIMPLE;
            char key_name[24];
            snprintf(key_name, sizeof(key_name), "BF_%07lX", ctx->serial);
            subghz_keeloq_keys_add(
                subghz->keeloq_keys_manager,
                ctx->recovered_mfkey,
                learning_type,
                key_name);
            subghz_keeloq_keys_save(subghz->keeloq_keys_manager);

            SubGhzKeystore* env_ks = subghz_environment_get_keystore(
                subghz->txrx->environment);
            SubGhzKeyArray_t* env_arr = subghz_keystore_get_data(env_ks);
            SubGhzKey* entry = SubGhzKeyArray_push_raw(*env_arr);
            entry->name = furi_string_alloc_set(key_name);
            entry->key = ctx->recovered_mfkey;
            entry->type = learning_type;
            return true;

        } else if(event.event == KL_DECRYPT_EVENT_DONE) {
            kl_ble_cleanup(ctx);
            subghz->keeloq_bf2.sig1_loaded = false;
            subghz->keeloq_bf2.sig2_loaded = false;

            if(ctx->success) {
                furi_string_printf(
                    ctx->result,
                    "Found %lu candidate(s)\n"
                    "Last: %08lX%08lX\n"
                    "Type:%u Cnt:%04lX\n"
                    "Saved to user keys",
                    ctx->candidate_count,
                    (uint32_t)(ctx->recovered_mfkey >> 32),
                    (uint32_t)(ctx->recovered_mfkey & 0xFFFFFFFF),
                    ctx->recovered_type,
                    ctx->recovered_cnt);

                FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
                flipper_format_rewind(fff);

                char mf_str[20];
                snprintf(mf_str, sizeof(mf_str), "BF_%07lX", ctx->serial);
                flipper_format_insert_or_update_string_cstr(fff, "Manufacture", mf_str);

                uint32_t cnt_val = ctx->recovered_cnt;
                flipper_format_rewind(fff);
                flipper_format_insert_or_update_uint32(fff, "Cnt", &cnt_val, 1);

                if(ctx->hop2 != 0) {
                    flipper_format_rewind(fff);
                    flipper_format_insert_or_update_uint32(fff, "Hop2", &ctx->hop2, 1);
                }

                flipper_format_rewind(fff);
                subghz_protocol_decoder_base_deserialize(
                    subghz_txrx_get_decoder(subghz->txrx), fff);

                const char* save_path = NULL;
                if(subghz_path_is_file(subghz->file_path)) {
                    save_path = furi_string_get_cstr(subghz->file_path);
                } else if(subghz_path_is_file(subghz->keeloq_bf2.sig1_path)) {
                    save_path = furi_string_get_cstr(subghz->keeloq_bf2.sig1_path);
                }
                if(save_path) {
                    subghz_save_protocol_to_file(
                        subghz,
                        subghz_txrx_get_fff_data(subghz->txrx),
                        save_path);
                    furi_string_set_str(subghz->file_path, save_path);
                }

                subghz_view_keeloq_decrypt_set_result(
                    subghz->subghz_keeloq_decrypt, true, furi_string_get_cstr(ctx->result));
            } else if(!ctx->cancel) {
                subghz_view_keeloq_decrypt_set_result(
                    subghz->subghz_keeloq_decrypt, false,
                    "Key NOT found.\nNo matching key in\n2^32 search space.");
            } else {
                subghz_view_keeloq_decrypt_set_result(
                    subghz->subghz_keeloq_decrypt, false, "Cancelled.");
            }
            return true;

        } else if(event.event == SubGhzCustomEventViewTransmitterBack) {
            if(ctx->ble_offload) {
                Bt* bt = furi_record_open(RECORD_BT);
                uint8_t cancel_msg = KL_MSG_BF_CANCEL;
                bt_custom_data_tx(bt, &cancel_msg, 1);
                furi_record_close(RECORD_BT);
            }
            ctx->cancel = true;
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    }
    return false;
}

void subghz_scene_keeloq_decrypt_on_exit(void* context) {
    SubGhz* subghz = context;
    KlDecryptCtx* ctx = (KlDecryptCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneKeeloqDecrypt);

    if(ctx) {
        kl_ble_cleanup(ctx);
        ctx->cancel = true;
        furi_string_free(ctx->result);
        free(ctx);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneKeeloqDecrypt, 0);
    }

    // [ARF] BLE callback is torn down in kl_ble_cleanup above (no worker thread
    // here), so the view is safe to free now.
    subghz_release_keeloq_decrypt_view(subghz);
}

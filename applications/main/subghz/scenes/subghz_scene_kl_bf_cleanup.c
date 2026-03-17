#include "../subghz_i.h"
#include <lib/subghz/protocols/keeloq_common.h>

typedef struct {
    uint32_t serial;
    uint32_t hop;
    uint8_t btn;
    uint16_t disc;
    size_t bf_indices[32];
    size_t bf_count;
    size_t valid_indices[32];
    size_t valid_count;
} KlCleanupCtx;

static bool kl_cleanup_validate_key(uint64_t key, uint32_t hop, uint8_t btn, uint16_t disc) {
    uint32_t dec = subghz_protocol_keeloq_common_decrypt(hop, key);
    if((dec >> 28) != btn) return false;
    uint16_t dec_disc = (dec >> 16) & 0x3FF;
    if(dec_disc == disc) return true;
    if((dec_disc & 0xFF) == (disc & 0xFF)) return true;
    return false;
}

void subghz_scene_kl_bf_cleanup_on_enter(void* context) {
    SubGhz* subghz = context;

    KlCleanupCtx* ctx = malloc(sizeof(KlCleanupCtx));
    memset(ctx, 0, sizeof(KlCleanupCtx));

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    flipper_format_rewind(fff);

    uint8_t key_data[8] = {0};
    if(flipper_format_read_hex(fff, "Key", key_data, 8)) {
        uint32_t fix = ((uint32_t)key_data[0] << 24) | ((uint32_t)key_data[1] << 16) |
                       ((uint32_t)key_data[2] << 8) | key_data[3];
        ctx->hop = ((uint32_t)key_data[4] << 24) | ((uint32_t)key_data[5] << 16) |
                   ((uint32_t)key_data[6] << 8) | key_data[7];
        ctx->serial = fix & 0x0FFFFFFF;
        ctx->btn = fix >> 28;
        ctx->disc = ctx->serial & 0x3FF;
    }

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneKlBfCleanup, (uint32_t)(uintptr_t)ctx);

    if(!subghz->keeloq_keys_manager) {
        subghz->keeloq_keys_manager = subghz_keeloq_keys_alloc();
    }

    char prefix[16];
    snprintf(prefix, sizeof(prefix), "BF_%07lX_", ctx->serial);
    size_t prefix_len = strlen(prefix);

    size_t user_count = subghz_keeloq_keys_user_count(subghz->keeloq_keys_manager);
    ctx->bf_count = 0;
    ctx->valid_count = 0;

    for(size_t i = 0; i < user_count && ctx->bf_count < 32; i++) {
        SubGhzKey* k = subghz_keeloq_keys_get(subghz->keeloq_keys_manager, i);
        if(!k || !k->name) continue;
        const char* name = furi_string_get_cstr(k->name);
        if(strncmp(name, prefix, prefix_len) == 0) {
            ctx->bf_indices[ctx->bf_count] = i;
            if(kl_cleanup_validate_key(k->key, ctx->hop, ctx->btn, ctx->disc)) {
                ctx->valid_indices[ctx->valid_count++] = i;
            }
            ctx->bf_count++;
        }
    }

    FuriString* msg = furi_string_alloc();

    if(ctx->bf_count == 0) {
        furi_string_set_str(msg, "No BF candidate keys\nfound for this serial.");
    } else if(ctx->bf_count == 1) {
        furi_string_set_str(msg, "Only 1 BF key exists.\nNothing to clean up.");
    } else if(ctx->valid_count == 1) {
        size_t deleted = 0;
        for(int i = (int)ctx->bf_count - 1; i >= 0; i--) {
            if(ctx->bf_indices[i] != ctx->valid_indices[0]) {
                subghz_keeloq_keys_delete(subghz->keeloq_keys_manager, ctx->bf_indices[i]);
                deleted++;
            }
        }

        char new_name[24];
        snprintf(new_name, sizeof(new_name), "BF_%07lX", ctx->serial);

        size_t new_user_count = subghz_keeloq_keys_user_count(subghz->keeloq_keys_manager);
        for(size_t i = 0; i < new_user_count; i++) {
            SubGhzKey* k = subghz_keeloq_keys_get(subghz->keeloq_keys_manager, i);
            if(!k || !k->name) continue;
            const char* n = furi_string_get_cstr(k->name);
            if(strncmp(n, prefix, prefix_len) == 0) {
                subghz_keeloq_keys_set(
                    subghz->keeloq_keys_manager, i, k->key, k->type, new_name);
                break;
            }
        }
        subghz_keeloq_keys_save(subghz->keeloq_keys_manager);

        furi_string_printf(msg,
            "Cleaned %u keys.\nKept valid key:\n%s",
            deleted, new_name);
    } else if(ctx->valid_count == 0) {
        furi_string_printf(msg,
            "%u BF keys found\nbut none validates\nhop. Kept all.",
            ctx->bf_count);
    } else {
        furi_string_printf(msg,
            "%u BF keys, %u valid.\nCannot auto-select.\nKept all.",
            ctx->bf_count, ctx->valid_count);
    }

    widget_add_text_scroll_element(subghz->widget, 0, 0, 128, 64, furi_string_get_cstr(msg));
    furi_string_free(msg);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_kl_bf_cleanup_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subghz_scene_kl_bf_cleanup_on_exit(void* context) {
    SubGhz* subghz = context;

    KlCleanupCtx* ctx = (KlCleanupCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneKlBfCleanup);
    if(ctx) {
        free(ctx);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneKlBfCleanup, 0);
    }

    widget_reset(subghz->widget);
}

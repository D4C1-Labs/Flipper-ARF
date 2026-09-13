#include "../subghz_i.h"
#include <lib/subghz/subghz_protocol_registry.h>
#include <stdlib.h>

#define TAG "SubGhzSceneProtocolList"

typedef struct {
    SubGhz* subghz;
    const char* protocol_name;
} SubGhzProtocolListItemContext;

static SubGhzProtocolListItemContext* protocol_list_item_contexts = NULL;
// [PROTO_MASTER] Context for the master (All ON/OFF) row at the top of the list.
static SubGhzProtocolListItemContext master_item_context;

static void subghz_scene_protocol_list_free_contexts(void) {
    free(protocol_list_item_contexts);
    protocol_list_item_contexts = NULL;
}

// [PROTO_MASTER] Build (or rebuild) the whole protocol list, including the master
// row. Factored out so the master toggle can refresh every row after a bulk
// change, mirroring the on_enter build exactly.
static void subghz_scene_protocol_list_rebuild(SubGhz* subghz);

static void subghz_scene_protocol_list_item_changed(VariableItem* item) {
    SubGhzProtocolListItemContext* item_context = variable_item_get_context(item);
    if(!item_context || !item_context->subghz || !item_context->protocol_name) return;

    SubGhz* subghz = item_context->subghz;
    bool should_disable = variable_item_get_current_value_index(item) == 1;

    bool changed = subghz_last_settings_protocol_filter_set(
        subghz->last_settings, item_context->protocol_name, should_disable);
    bool is_disabled = subghz_last_settings_protocol_filter_contains(
        subghz->last_settings, item_context->protocol_name);

    variable_item_set_current_value_index(item, is_disabled ? 1 : 0);
    variable_item_set_current_value_text(item, is_disabled ? "OFF" : "ON");
    if(changed) subghz_last_settings_save(subghz->last_settings);
}

// [PROTO_MASTER] Master toggle: index 0 = enable ALL, index 1 = disable ALL.
// After the bulk change, the whole list is rebuilt so every row reflects the new
// state, then the change is persisted like an individual toggle.
static void subghz_scene_protocol_list_master_changed(VariableItem* item) {
    SubGhzProtocolListItemContext* item_context = variable_item_get_context(item);
    if(!item_context || !item_context->subghz) return;

    SubGhz* subghz = item_context->subghz;
    bool disable_all = variable_item_get_current_value_index(item) == 1;

    bool changed = false;
    if(disable_all) {
        // Add every registered protocol to the OFF list.
        size_t protocol_count = subghz_protocol_registry_count(&subghz_protocol_registry);
        for(size_t i = 0; i < protocol_count; i++) {
            const SubGhzProtocol* protocol =
                subghz_protocol_registry_get_by_index(&subghz_protocol_registry, i);
            if(!protocol) continue;
            if(subghz_last_settings_protocol_filter_set(
                   subghz->last_settings, protocol->name, true)) {
                changed = true;
            }
        }
    } else {
        // Clear the whole OFF list -> all protocols enabled.
        changed = subghz_last_settings_protocol_filter_clear(subghz->last_settings);
    }

    if(changed) subghz_last_settings_save(subghz->last_settings);

    // Rebuild so every protocol row shows the new ON/OFF and the master shows the
    // resolved state. Preserve the current selection (the master row, index 0).
    size_t selected = variable_item_list_get_selected_item_index(subghz->variable_item_list);
    subghz_scene_protocol_list_rebuild(subghz);
    variable_item_list_set_selected_item(subghz->variable_item_list, selected);
}

static void subghz_scene_protocol_list_rebuild(SubGhz* subghz) {
    VariableItemList* list = subghz->variable_item_list;
    variable_item_list_reset(list);
    subghz_scene_protocol_list_free_contexts();

    size_t protocol_count = subghz_protocol_registry_count(&subghz_protocol_registry);
    protocol_list_item_contexts =
        malloc(sizeof(SubGhzProtocolListItemContext) * protocol_count);
    furi_check(protocol_list_item_contexts);

    // [PROTO_MASTER] Master "All Protocols" row at the very top. 3-way display:
    //   0 disabled -> "ON"    (all enabled)
    //   count==all -> "OFF"   (all disabled)
    //   otherwise  -> "Mixed" (individually varied)
    // The item is a 2-value control (ON/OFF) so toggling it forces bulk on/off;
    // "Mixed" is display-only and shown when the real state is partial.
    size_t disabled_count = subghz_last_settings_protocol_filter_count(subghz->last_settings);
    master_item_context.subghz = subghz;
    master_item_context.protocol_name = NULL; // marks this as the master row
    VariableItem* master = variable_item_list_add(
        list,
        "All Protocols",
        2,
        subghz_scene_protocol_list_master_changed,
        &master_item_context);
    if(disabled_count == 0) {
        variable_item_set_current_value_index(master, 0);
        variable_item_set_current_value_text(master, "ON");
    } else if(disabled_count >= protocol_count) {
        variable_item_set_current_value_index(master, 1);
        variable_item_set_current_value_text(master, "OFF");
    } else {
        // Partial: show "Mixed"; keep index at 1 so the next toggle enables all.
        variable_item_set_current_value_index(master, 1);
        variable_item_set_current_value_text(master, "Mixed");
    }

    for(size_t i = 0; i < protocol_count; i++) {
        const SubGhzProtocol* protocol =
            subghz_protocol_registry_get_by_index(&subghz_protocol_registry, i);
        if(!protocol) continue;

        protocol_list_item_contexts[i].subghz = subghz;
        protocol_list_item_contexts[i].protocol_name = protocol->name;

        VariableItem* item = variable_item_list_add(
            list,
            protocol->name,
            2,
            subghz_scene_protocol_list_item_changed,
            &protocol_list_item_contexts[i]);

        bool is_disabled =
            subghz_last_settings_protocol_filter_contains(subghz->last_settings, protocol->name);
        variable_item_set_current_value_index(item, is_disabled ? 1 : 0);
        variable_item_set_current_value_text(item, is_disabled ? "OFF" : "ON");
    }
}

void subghz_scene_protocol_list_on_enter(void* context) {
    SubGhz* subghz = context;

    subghz_scene_protocol_list_rebuild(subghz);

    variable_item_list_set_selected_item(
        subghz->variable_item_list,
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneProtocolList));

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_protocol_list_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(
            subghz->scene_manager, SubGhzSceneProtocolList, event.event);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(subghz->scene_manager);
        consumed = true;
    }

    return consumed;
}

void subghz_scene_protocol_list_on_exit(void* context) {
    SubGhz* subghz = context;
    scene_manager_set_scene_state(
        subghz->scene_manager,
        SubGhzSceneProtocolList,
        variable_item_list_get_selected_item_index(subghz->variable_item_list));
    variable_item_list_reset(subghz->variable_item_list);
    subghz_scene_protocol_list_free_contexts();
}

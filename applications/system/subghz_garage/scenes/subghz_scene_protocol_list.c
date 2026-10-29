#include "../subghz_i.h"

static void protocol_list_group_toggled_cb(void* context, uint8_t group_index, bool enabled) {
    SubGhz* subghz = context;
    if(group_index >= SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT) return;
    subghz->last_settings->protocol_groups_enabled[group_index] = enabled ? 1 : 0;
}

void subghz_scene_protocol_list_on_enter(void* context) {
    FURI_LOG_I("SubGhzSceneProtocolList", "on_enter");
    SubGhz* subghz = context;

    subghz_ensure_protocol_groups(subghz);
    subghz_protocol_groups_set_enabled_all(
        subghz->protocol_groups, subghz->last_settings->protocol_groups_enabled);
    subghz_protocol_groups_set_callback(
        subghz->protocol_groups, protocol_list_group_toggled_cb, subghz);
    subghz_protocol_groups_resume_scroll(subghz->protocol_groups);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdProtocolGroups);
}

bool subghz_scene_protocol_list_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
    } else if(event.type == SceneManagerEventTypeBack) {
        subghz_save_all(subghz);
    }
    return consumed;
}

void subghz_scene_protocol_list_on_exit(void* context) {
    SubGhz* subghz = context;

    subghz_protocol_groups_pause_scroll(subghz->protocol_groups);
    subghz_save_all(subghz);
}

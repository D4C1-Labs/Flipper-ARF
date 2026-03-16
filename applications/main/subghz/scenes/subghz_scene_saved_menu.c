#include "../subghz_i.h" // IWYU pragma: keep

enum SubmenuIndex {
    SubmenuIndexEmulate,
    SubmenuIndexEdit,
    SubmenuIndexDelete,
    SubmenuIndexSignalSettings,
    SubmenuIndexPsaDecrypt,
    SubmenuIndexCounterBf
};

void subghz_scene_saved_menu_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

void subghz_scene_saved_menu_on_enter(void* context) {
    SubGhz* subghz = context;

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    bool is_psa_encrypted = false;
    bool has_counter = false;
    if(fff) {
        FuriString* proto = furi_string_alloc();
        flipper_format_rewind(fff);
        if(flipper_format_read_string(fff, "Protocol", proto)) {
            if(furi_string_equal_str(proto, "PSA GROUP")) {
                FuriString* type_str = furi_string_alloc();
                flipper_format_rewind(fff);
                if(!flipper_format_read_string(fff, "Type", type_str) ||
                   furi_string_equal_str(type_str, "00")) {
                    is_psa_encrypted = true;
                }
                furi_string_free(type_str);
            }
        }
        furi_string_free(proto);
    }

    if(fff) {
        uint32_t cnt_tmp = 0;
        flipper_format_rewind(fff);
        if(flipper_format_read_uint32(fff, "Cnt", &cnt_tmp, 1)) {
            has_counter = true;
        }
    }

    submenu_add_item(
        subghz->submenu,
        "Emulate",
        SubmenuIndexEmulate,
        subghz_scene_saved_menu_submenu_callback,
        subghz);

    submenu_add_item(
        subghz->submenu,
        "Rename",
        SubmenuIndexEdit,
        subghz_scene_saved_menu_submenu_callback,
        subghz);

    submenu_add_item(
        subghz->submenu,
        "Delete",
        SubmenuIndexDelete,
        subghz_scene_saved_menu_submenu_callback,
        subghz);

    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
        submenu_add_item(
            subghz->submenu,
            "Signal Settings",
            SubmenuIndexSignalSettings,
            subghz_scene_saved_menu_submenu_callback,
            subghz);
    };
    if(is_psa_encrypted) {
        submenu_add_item(
            subghz->submenu,
            "PSA Decrypt",
            SubmenuIndexPsaDecrypt,
            subghz_scene_saved_menu_submenu_callback,
            subghz);
    }
    if(has_counter) {
        submenu_add_item(
            subghz->submenu,
            "Counter BruteForce",
            SubmenuIndexCounterBf,
            subghz_scene_saved_menu_submenu_callback,
            subghz);
    }

    submenu_set_selected_item(
        subghz->submenu,
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneSavedMenu));

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
}

bool subghz_scene_saved_menu_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexEmulate) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneSavedMenu, SubmenuIndexEmulate);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
            return true;
        } else if(event.event == SubmenuIndexDelete) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneSavedMenu, SubmenuIndexDelete);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneDelete);
            return true;
        } else if(event.event == SubmenuIndexEdit) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneSavedMenu, SubmenuIndexEdit);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveName);
            return true;
        } else if(event.event == SubmenuIndexSignalSettings) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneSavedMenu, SubmenuIndexSignalSettings);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSignalSettings);
            return true;
        } else if(event.event == SubmenuIndexPsaDecrypt) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneSavedMenu, SubmenuIndexPsaDecrypt);
            scene_manager_next_scene(subghz->scene_manager, SubGhzScenePsaDecrypt);
            return true;
        } else if(event.event == SubmenuIndexCounterBf) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneSavedMenu, SubmenuIndexCounterBf);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneCounterBf);
            return true;
        }
    }
    return false;
}

void subghz_scene_saved_menu_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
}

#include "../subghz_i.h" // IWYU pragma: keep

enum SubmenuIndex {
    SubmenuIndexEmulate,
    SubmenuIndexSignalSettings,
    SubmenuIndexPsaDecrypt,
    SubmenuIndexEdit,
    SubmenuIndexDelete,
    SubmenuIndexCounterBf,                  /* <-- comma was missing here */
    SubmenuIndexCarEmulateSettings,
    SubmenuIndexHitag2Bf, // [HITAG2_BF]
};

void subghz_scene_saved_menu_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

void subghz_scene_saved_menu_on_enter(void* context) {
    SubGhz* subghz = context;

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    bool is_psa_encrypted = false;
    bool has_signal_editor = false;
    bool has_counter = false;
    bool is_fiat_bf_candidate = false; // [HITAG2_BF] Fiat V1 or V2 without a key
    if(fff) {
        FuriString* proto = furi_string_alloc();
        flipper_format_rewind(fff);
        if(flipper_format_read_string(fff, "Protocol", proto)) {
            has_signal_editor = !furi_string_equal_str(proto, "RAW");
            if(furi_string_equal_str(proto, "PSA GROUP")) {
                FuriString* type_str = furi_string_alloc();
                flipper_format_rewind(fff);
                if(!flipper_format_read_string(fff, "Type", type_str) ||
                   furi_string_equal_str(type_str, "00")) {
                    is_psa_encrypted = true;
                    has_signal_editor = false;
                }
                furi_string_free(type_str);
            }
            // [HITAG2_BF] Show Hitag2 BF button when protocol is Fiat V1,
            // Fiat V2 or Renault V1 and there is no "Hitag2 Key" field (i.e. not
            // cracked yet)
            if(furi_string_equal_str(proto, "Fiat V1") ||
               furi_string_equal_str(proto, "Fiat V2") ||
               furi_string_equal_str(proto, "Renault V1")) {
                uint8_t key_buf[6];
                flipper_format_rewind(fff);
                if(!flipper_format_read_hex(fff, "Hitag2 Key", key_buf, 6)) {
                    is_fiat_bf_candidate = true;
                }
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

    if(!is_psa_encrypted) {
        submenu_add_item(
            subghz->submenu,
            "Emulate",
            SubmenuIndexEmulate,
            subghz_scene_saved_menu_submenu_callback,
            subghz);

        if(has_signal_editor) {
            submenu_add_item(
                subghz->submenu,
                "Signal Editor",
                SubmenuIndexSignalSettings,
                subghz_scene_saved_menu_submenu_callback,
                subghz);
        }
    }

    if(is_psa_encrypted) {
        submenu_add_item(
            subghz->submenu,
            "PSA Decrypt",
            SubmenuIndexPsaDecrypt,
            subghz_scene_saved_menu_submenu_callback,
            subghz);
    }

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

    submenu_add_item(
        subghz->submenu,
        "Custom Emulate Settings",
        SubmenuIndexCarEmulateSettings,
        subghz_scene_saved_menu_submenu_callback,
        subghz);

    if(has_counter) {
        submenu_add_item(
            subghz->submenu,
            "Counter BruteForce",
            SubmenuIndexCounterBf,
            subghz_scene_saved_menu_submenu_callback,
            subghz);
    }

    // [HITAG2_BF] Show Hitag2 BF button for uncracked Fiat V1/V2 signals
    if(is_fiat_bf_candidate) {
        submenu_add_item(
            subghz->submenu,
            "Hitag2 BF",
            SubmenuIndexHitag2Bf,
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

            if(subghz->last_settings->custom_car_emulate) {
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneCarEmulate);
            } else {
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
            }
            return true;
        } else if(event.event == SubmenuIndexPsaDecrypt) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneSavedMenu, SubmenuIndexPsaDecrypt);
            scene_manager_next_scene(subghz->scene_manager, SubGhzScenePsaDecrypt);
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
        } else if(event.event == SubmenuIndexCounterBf) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneSavedMenu, SubmenuIndexCounterBf);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneCounterBf);
            return true;
        } else if(event.event == SubmenuIndexCarEmulateSettings) {
            /* <-- was outside the if block due to misplaced brace, now fixed */
            scene_manager_set_scene_state(
                subghz->scene_manager,
                SubGhzSceneSavedMenu,
                SubmenuIndexCarEmulateSettings);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneCarEmulateSettings);
            return true;
        } else if(event.event == SubmenuIndexHitag2Bf) {
            // [HITAG2_BF]
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneSavedMenu, SubmenuIndexHitag2Bf);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneHitag2Bf);
            return true;
        }
    }
    return false;
}

void subghz_scene_saved_menu_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
}

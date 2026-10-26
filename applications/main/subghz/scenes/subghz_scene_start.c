#include "../subghz_i.h"
#include "subghz_scene_start.h"
#include <dolphin/dolphin.h>
#include <loader/loader.h>
#include <storage/storage.h>

#include <lib/subghz/protocols/raw.h>

/* External decoder apps. Both are built from applications/system/ and carry
 * their own protocol catalog, so those protocols cost no internal flash.
 * They reuse this app's scenes/views, so the UI is identical. */
#define SUBGHZ_ARF_CARS_FAP_PATH   EXT_PATH("apps/Sub-GHz/arf_cars.fap")
#define SUBGHZ_ARF_GARAGE_FAP_PATH EXT_PATH("apps/Sub-GHz/arf_garage.fap")

static void subghz_scene_start_blank_draw_cb(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    canvas_clear(canvas);
}

/* Queue an external app and shut this one down. The blank full-screen viewport
 * covers the hand-off so the transition does not flash the desktop. */
static void
    subghz_scene_start_launch_and_exit(SubGhz* subghz, const char* fap_path, const char* args) {
    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(loader, fap_path, args, LoaderDeferredLaunchFlagNone);
    furi_record_close(RECORD_LOADER);

    if(!subghz->blank_transition_viewport) {
        subghz->blank_transition_viewport = view_port_alloc();
        view_port_draw_callback_set(
            subghz->blank_transition_viewport, subghz_scene_start_blank_draw_cb, NULL);
        gui_add_view_port(subghz->gui, subghz->blank_transition_viewport, GuiLayerFullscreen);
        view_port_update(subghz->blank_transition_viewport);
    }

    scene_manager_stop(subghz->scene_manager);
    view_dispatcher_stop(subghz->view_dispatcher);
}

void subghz_scene_start_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

void subghz_scene_start_on_enter(void* context) {
    SubGhz* subghz = context;
    if(subghz->state_notifications == SubGhzNotificationStateStarting) {
        subghz->state_notifications = SubGhzNotificationStateIDLE;
    }

    /* Only offer an external decoder if its .fap is actually installed. */
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool has_cars = storage_file_exists(storage, SUBGHZ_ARF_CARS_FAP_PATH);
    bool has_garage = storage_file_exists(storage, SUBGHZ_ARF_GARAGE_FAP_PATH);
    furi_record_close(RECORD_STORAGE);

    if(has_cars) {
        submenu_add_item(
            subghz->submenu,
            "Read Cars",
            SubmenuIndexReadCars,
            subghz_scene_start_submenu_callback,
            subghz);
    }
    if(has_garage) {
        submenu_add_item(
            subghz->submenu,
            "Read Garage Door",
            SubmenuIndexReadGarage,
            subghz_scene_start_submenu_callback,
            subghz);
    }
    submenu_add_item(
        subghz->submenu,
        "Read RAW",
        SubmenuIndexReadRAW,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Add Manually",
        SubmenuIndexAddManuallyAdvanced,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Frequency Analyzer",
        SubmenuIndexFrequencyAnalyzer,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Protocol List",
        SubmenuIndexProtocolList,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "Radio Settings",
        SubmenuIndexExtSettings,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "KeeLoq Keys",
        SubmenuIndexKeeloqKeys,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_add_item(
        subghz->submenu,
        "KeeLoq BF (2 Signals)",
        SubmenuIndexKeeloqBf2,
        subghz_scene_start_submenu_callback,
        subghz);
    submenu_set_selected_item(
        subghz->submenu, scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneStart));

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
}

bool subghz_scene_start_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeBack) {
        //exit app
        scene_manager_stop(subghz->scene_manager);
        view_dispatcher_stop(subghz->view_dispatcher);
        return true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexReadCars) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexReadCars);
            subghz_scene_start_launch_and_exit(subghz, SUBGHZ_ARF_CARS_FAP_PATH, "read");
            return true;
        } else if(event.event == SubmenuIndexReadGarage) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexReadGarage);
            subghz_scene_start_launch_and_exit(subghz, SUBGHZ_ARF_GARAGE_FAP_PATH, "read");
            return true;
        } else if(event.event == SubmenuIndexReadRAW) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexReadRAW);
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
            return true;
        } else if(event.event == SubmenuIndexAddManuallyAdvanced) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexAddManuallyAdvanced);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSetType);
            return true;
        } else if(event.event == SubmenuIndexFrequencyAnalyzer) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexFrequencyAnalyzer);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFrequencyAnalyzer);
            dolphin_deed(DolphinDeedSubGhzFrequencyAnalyzer);
            return true;
        } else if(event.event == SubmenuIndexProtocolList) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexProtocolList);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneProtocolList);
            return true;
        } else if(event.event == SubmenuIndexExtSettings) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexExtSettings);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneExtModuleSettings);
            return true;
        } else if(event.event == SubmenuIndexKeeloqKeys) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexKeeloqKeys);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneKeeloqKeys);
            return true;
        } else if(event.event == SubmenuIndexKeeloqBf2) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneStart, SubmenuIndexKeeloqBf2);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneKeeloqBf2);
            return true;
        }
    }
    return false;
}

void subghz_scene_start_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
}

#include "karr_poc_scene.h"
#include "../karr_poc_app.h"
#include <stdio.h>

static void karr_poc_scene_mayhem_menu_submenu_callback(void* context, uint32_t index) {
    KarrPocApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void karr_poc_scene_mayhem_menu_on_enter(void* context) {
    KarrPocApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Mayhem mode");

    char allowlist_label[48];
    snprintf(
        allowlist_label,
        sizeof(allowlist_label),
        "Allowlist: %s (%zu)",
        app->allowlist_enabled ? "ON" : "OFF",
        app->allowlist_count);

    submenu_add_item(
        app->submenu,
        allowlist_label,
        KarrPocEvent_MayhemToggleAllowlist,
        karr_poc_scene_mayhem_menu_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Unlock All (User)",
        KarrPocEvent_MayhemUnlockUser,
        karr_poc_scene_mayhem_menu_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Lock All (User)",
        KarrPocEvent_MayhemLockUser,
        karr_poc_scene_mayhem_menu_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Unlock All (Dealer)",
        KarrPocEvent_MayhemUnlockDealer,
        karr_poc_scene_mayhem_menu_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Lock All (Dealer)",
        KarrPocEvent_MayhemLockDealer,
        karr_poc_scene_mayhem_menu_submenu_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_Submenu);
}

bool karr_poc_scene_mayhem_menu_on_event(void* context, SceneManagerEvent event) {
    KarrPocApp* app = context;
    bool consumed = false;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == KarrPocEvent_MayhemToggleAllowlist) {
        app->allowlist_enabled = !app->allowlist_enabled;
        karr_poc_scene_mayhem_menu_on_enter(app);
        return true;
    }

    QtMode mode;
    QtCommand command;

    switch(event.event) {
    case KarrPocEvent_MayhemUnlockUser:
        mode = QtMode_User;
        command = QtCmd_UnlockDoors;
        break;
    case KarrPocEvent_MayhemLockUser:
        mode = QtMode_User;
        command = QtCmd_LockDoors;
        break;
    case KarrPocEvent_MayhemUnlockDealer:
        mode = QtMode_Dealer;
        command = QtCmd_UnlockDoors;
        break;
    case KarrPocEvent_MayhemLockDealer:
        mode = QtMode_Dealer;
        command = QtCmd_LockDoors;
        break;
    default:
        return false;
    }

    app->mayhem_mode = mode;
    app->mayhem_command = command;

    if(app->allowlist_enabled) {
        scene_manager_next_scene(app->scene_manager, KarrPocScene_MayhemScan);
    } else {
        scene_manager_next_scene(app->scene_manager, KarrPocScene_MayhemConfirm);
    }
    consumed = true;

    return consumed;
}

void karr_poc_scene_mayhem_menu_on_exit(void* context) {
    KarrPocApp* app = context;
    submenu_reset(app->submenu);
}

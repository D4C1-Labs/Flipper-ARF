#include "karr_poc_scene.h"
#include "../karr_poc_app.h"

typedef enum {
    KarrPocDeviceMenuIndex_UnlockUser,
    KarrPocDeviceMenuIndex_LockUser,
    KarrPocDeviceMenuIndex_UnlockDealer,
    KarrPocDeviceMenuIndex_LockDealer,
} KarrPocDeviceMenuIndex;

static void karr_poc_scene_device_menu_submenu_callback(void* context, uint32_t index) {
    KarrPocApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void karr_poc_scene_device_menu_on_enter(void* context) {
    KarrPocApp* app = context;
    KarrBleDevice* dev = &app->devices[app->selected_device];

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, dev->name);

    submenu_add_item(
        app->submenu,
        "Unlock (mode User)",
        KarrPocDeviceMenuIndex_UnlockUser,
        karr_poc_scene_device_menu_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Lock (mode User)",
        KarrPocDeviceMenuIndex_LockUser,
        karr_poc_scene_device_menu_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Unlock (mode Dealer)",
        KarrPocDeviceMenuIndex_UnlockDealer,
        karr_poc_scene_device_menu_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Lock (mode Dealer)",
        KarrPocDeviceMenuIndex_LockDealer,
        karr_poc_scene_device_menu_submenu_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_Submenu);
}

bool karr_poc_scene_device_menu_on_event(void* context, SceneManagerEvent event) {
    KarrPocApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case KarrPocDeviceMenuIndex_UnlockUser:
            app->mode = QtMode_User;
            app->pending_command = QtCmd_UnlockDoors;
            break;
        case KarrPocDeviceMenuIndex_LockUser:
            app->mode = QtMode_User;
            app->pending_command = QtCmd_LockDoors;
            break;
        case KarrPocDeviceMenuIndex_UnlockDealer:
            app->mode = QtMode_Dealer;
            app->pending_command = QtCmd_UnlockDoors;
            break;
        case KarrPocDeviceMenuIndex_LockDealer:
            app->mode = QtMode_Dealer;
            app->pending_command = QtCmd_LockDoors;
            break;
        default:
            return false;
        }
        scene_manager_next_scene(app->scene_manager, KarrPocScene_Auth);
        consumed = true;
    }

    return consumed;
}

void karr_poc_scene_device_menu_on_exit(void* context) {
    KarrPocApp* app = context;
    submenu_reset(app->submenu);
}

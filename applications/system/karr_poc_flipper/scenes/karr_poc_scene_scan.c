#include "karr_poc_scene.h"
#include "../karr_poc_app.h"
#include <string.h>

static void karr_poc_scene_scan_submenu_callback(void* context, uint32_t index) {
    KarrPocApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void karr_poc_scene_scan_ble_callback(KarrBleStatus status, const char* message, void* context) {
    KarrPocApp* app = context;

    if(status == KarrBleStatus_DeviceFound) {
        if(app->device_count < KARR_BLE_MAX_DEVICES) {
            KarrBleDevice* dev = &app->devices[app->device_count];
            strncpy(dev->name, message, KARR_BLE_NAME_MAX_LEN - 1);
            dev->name[KARR_BLE_NAME_MAX_LEN - 1] = '\0';
            memset(dev->address, 0, sizeof(dev->address));
            dev->rssi = 0;

            submenu_add_item(
                app->submenu,
                dev->name,
                app->device_count,
                karr_poc_scene_scan_submenu_callback,
                app);

            app->device_count++;
        }
    }
}

void karr_poc_scene_scan_on_enter(void* context) {
    KarrPocApp* app = context;

    app->device_count = 0;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Scanning...");

    view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_Submenu);

    karr_ble_scan_start(karr_poc_scene_scan_ble_callback, app);

    submenu_set_header(app->submenu, "Units found:");
}

bool karr_poc_scene_scan_on_event(void* context, SceneManagerEvent event) {
    KarrPocApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        uint32_t index = event.event;
        if(index < app->device_count) {
            app->selected_device = index;
            scene_manager_next_scene(app->scene_manager, KarrPocScene_DeviceMenu);
            consumed = true;
        }
    }

    return consumed;
}

void karr_poc_scene_scan_on_exit(void* context) {
    KarrPocApp* app = context;
    karr_ble_scan_stop();
    submenu_reset(app->submenu);
}

#include "karr_poc_scene.h"
#include "../karr_poc_app.h"
#include <string.h>

#define MAYHEM_SCAN_DURATION_MS 4000

static void karr_poc_scene_mayhem_scan_ble_callback(
    KarrBleStatus status,
    const char* message,
    void* context) {
    KarrPocApp* app = context;

    if(status == KarrBleStatus_DeviceFound) {
        if(app->device_count < KARR_BLE_MAX_DEVICES) {
            KarrBleDevice* dev = &app->devices[app->device_count];
            strncpy(dev->name, message, KARR_BLE_NAME_MAX_LEN - 1);
            dev->name[KARR_BLE_NAME_MAX_LEN - 1] = '\0';
            memset(dev->address, 0, sizeof(dev->address));
            dev->rssi = 0;
            app->device_count++;
        }
    }
}

void karr_poc_scene_mayhem_scan_on_enter(void* context) {
    KarrPocApp* app = context;

    app->device_count = 0;
    furi_string_reset(app->log_text);
    text_box_reset(app->text_box);
    furi_string_cat_str(app->log_text, "Scanning for devices...\n\nPlease wait...\n");
    text_box_set_text(app->text_box, furi_string_get_cstr(app->log_text));

    view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_TextBox);

    karr_ble_scan_start(karr_poc_scene_mayhem_scan_ble_callback, app);

    furi_delay_ms(MAYHEM_SCAN_DURATION_MS);

    karr_ble_scan_stop();

    furi_string_reset(app->log_text);
    furi_string_cat_printf(
        app->log_text, "Scan complete: %zu devices found\n\n", app->device_count);
    for(size_t i = 0; i < app->device_count; i++) {
        furi_string_cat_printf(
            app->log_text, "%zu. %s\n", i + 1, app->devices[i].name);
    }
    text_box_set_text(app->text_box, furi_string_get_cstr(app->log_text));

    view_dispatcher_send_custom_event(app->view_dispatcher, KarrPocEvent_MayhemScanDone);
}

bool karr_poc_scene_mayhem_scan_on_event(void* context, SceneManagerEvent event) {
    KarrPocApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KarrPocEvent_MayhemScanDone) {
            scene_manager_next_scene(app->scene_manager, KarrPocScene_MayhemRun);
            consumed = true;
        }
    }

    return consumed;
}

void karr_poc_scene_mayhem_scan_on_exit(void* context) {
    UNUSED(context);
}

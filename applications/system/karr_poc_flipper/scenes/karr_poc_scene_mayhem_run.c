#include "karr_poc_scene.h"
#include "../karr_poc_app.h"
#include <string.h>

static bool karr_poc_device_in_allowlist(KarrPocApp* app, const char* name) {
    for(size_t i = 0; i < app->allowlist_count; i++) {
        if(strcmp(app->allowlist[i], name) == 0) return true;
    }
    return false;
}

static void karr_poc_scene_mayhem_run_device_callback(
    const KarrBleDevice* device,
    KarrMayhemResult result,
    void* context) {
    KarrPocApp* app = context;

    const char* label;
    switch(result) {
    case KarrMayhemResult_Success:
        label = "OK";
        break;
    case KarrMayhemResult_AuthFailed:
        label = "AUTH FAILED";
        break;
    case KarrMayhemResult_ConnectFailed:
        label = "CONNECT FAILED";
        break;
    default:
        label = "?";
        break;
    }

    furi_string_cat_printf(app->log_text, "%s -> %s\n", device->name, label);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->log_text));
}

void karr_poc_scene_mayhem_run_on_enter(void* context) {
    KarrPocApp* app = context;

    text_box_reset(app->text_box);
    view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_TextBox);

    KarrBleDevice targets[KARR_BLE_MAX_DEVICES];
    size_t target_count = 0;

    for(size_t i = 0; i < app->device_count; i++) {
        bool allowed = !app->allowlist_enabled || karr_poc_device_in_allowlist(app, app->devices[i].name);
        if(allowed && target_count < KARR_BLE_MAX_DEVICES) {
            targets[target_count++] = app->devices[i];
        }
    }

    furi_string_cat_printf(
        app->log_text,
        "\nMayhem: %zu of %zu units%s\n\n",
        target_count,
        app->device_count,
        app->allowlist_enabled ? " (filtered by allowlist)" : "");
    text_box_set_text(app->text_box, furi_string_get_cstr(app->log_text));

    if(target_count == 0) {
        furi_string_cat_str(
            app->log_text, "No units found match the\nallowlist. Nothing to do.\n");
        text_box_set_text(app->text_box, furi_string_get_cstr(app->log_text));
        view_dispatcher_send_custom_event(app->view_dispatcher, KarrPocEvent_MayhemRunDone);
        return;
    }

    karr_ble_mayhem_run(
        targets,
        target_count,
        app->mayhem_mode,
        app->mayhem_command,
        karr_poc_scene_mayhem_run_device_callback,
        app);

    view_dispatcher_send_custom_event(app->view_dispatcher, KarrPocEvent_MayhemRunDone);
}

bool karr_poc_scene_mayhem_run_on_event(void* context, SceneManagerEvent event) {
    KarrPocApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KarrPocEvent_MayhemRunDone) {
            scene_manager_next_scene(app->scene_manager, KarrPocScene_Result);
            consumed = true;
        }
    }

    return consumed;
}

void karr_poc_scene_mayhem_run_on_exit(void* context) {
    UNUSED(context);
}

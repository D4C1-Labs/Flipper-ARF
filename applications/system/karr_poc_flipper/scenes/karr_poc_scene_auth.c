#include "karr_poc_scene.h"
#include "../karr_poc_app.h"

static const char* karr_ble_status_label(KarrBleStatus status) {
    switch(status) {
    case KarrBleStatus_ScanStarted:
        return "Scan started";
    case KarrBleStatus_DeviceFound:
        return "Device found";
    case KarrBleStatus_Connecting:
        return "Connecting";
    case KarrBleStatus_Connected:
        return "Connected";
    case KarrBleStatus_ServiceDiscovered:
        return "Service/char OK";
    case KarrBleStatus_AuthInitSent:
        return "TX 0x22 (init)";
    case KarrBleStatus_ChallengeReceived:
        return "RX 0x0E (challenge)";
    case KarrBleStatus_AuthResponseSent:
        return "TX 0x0F (hash)";
    case KarrBleStatus_AuthSuccess:
        return "RX 0x10 (success)";
    case KarrBleStatus_AuthFailed:
        return "AUTH FAILED";
    case KarrBleStatus_CommandSent:
        return "Command sent";
    case KarrBleStatus_Disconnected:
        return "Disconnected";
    case KarrBleStatus_Error:
        return "ERROR";
    default:
        return "?";
    }
}

static void karr_poc_scene_auth_ble_callback(KarrBleStatus status, const char* message, void* context) {
    KarrPocApp* app = context;

    furi_string_cat_printf(app->log_text, "%s: %s\n", karr_ble_status_label(status), message);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->log_text));

    if(status == KarrBleStatus_AuthSuccess) {
        view_dispatcher_send_custom_event(app->view_dispatcher, KarrPocEvent_AuthDone);
    } else if(status == KarrBleStatus_AuthFailed || status == KarrBleStatus_Error) {
        view_dispatcher_send_custom_event(app->view_dispatcher, KarrPocEvent_AuthFailed);
    }
}

void karr_poc_scene_auth_on_enter(void* context) {
    KarrPocApp* app = context;
    KarrBleDevice* dev = &app->devices[app->selected_device];

    furi_string_reset(app->log_text);
    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);

    view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_TextBox);

    bool ok = karr_ble_connect_and_auth(dev, app->mode, karr_poc_scene_auth_ble_callback, app);

    if(!ok) {
        view_dispatcher_send_custom_event(app->view_dispatcher, KarrPocEvent_AuthFailed);
    }
}

bool karr_poc_scene_auth_on_event(void* context, SceneManagerEvent event) {
    KarrPocApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KarrPocEvent_AuthDone) {
            bool sent = karr_ble_send_command(app->pending_command);
            furi_string_cat_printf(
                app->log_text, "\n%s\n", sent ? "Command sent OK" : "Failed to send command");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->log_text));

            scene_manager_next_scene(app->scene_manager, KarrPocScene_Result);
            consumed = true;
        } else if(event.event == KarrPocEvent_AuthFailed) {
            scene_manager_next_scene(app->scene_manager, KarrPocScene_Result);
            consumed = true;
        }
    }

    return consumed;
}

void karr_poc_scene_auth_on_exit(void* context) {
    UNUSED(context);
}

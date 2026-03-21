#include "rolljam_scene.h"
#include "../helpers/rolljam_receiver.h"

// ============================================================
// Phase 4 / Result: user chooses to SAVE or REPLAY 2nd code
// ============================================================

static void result_dialog_callback(DialogExResult result, void* context) {
    RollJamApp* app = context;

    if(result == DialogExResultLeft) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RollJamEventSaveSignal);
    } else if(result == DialogExResultRight) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RollJamEventReplayNow);
    }
}

void rolljam_scene_result_on_enter(void* context) {
    RollJamApp* app = context;

    dialog_ex_reset(app->dialog_ex);

    dialog_ex_set_header(
        app->dialog_ex, "Attack Complete!",
        64, 2, AlignCenter, AlignTop);

    dialog_ex_set_text(
        app->dialog_ex,
        "1st code: SENT to target\n"
        "2nd code: IN MEMORY\n\n"
        "What to do with 2nd?",
        64, 18, AlignCenter, AlignTop);

    dialog_ex_set_left_button_text(app->dialog_ex, "Save");
    dialog_ex_set_right_button_text(app->dialog_ex, "Send");

    dialog_ex_set_result_callback(app->dialog_ex, result_dialog_callback);
    dialog_ex_set_context(app->dialog_ex, app);

    view_dispatcher_switch_to_view(
        app->view_dispatcher, RollJamViewDialogEx);
}

bool rolljam_scene_result_on_event(void* context, SceneManagerEvent event) {
    RollJamApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RollJamEventSaveSignal) {

            rolljam_save_signal(app, &app->signal_second);

            popup_reset(app->popup);
            popup_set_header(
                app->popup, "Saved!",
                64, 20, AlignCenter, AlignCenter);
            popup_set_text(
                app->popup,
                "File saved to:\n/ext/subghz/rolljam_*.sub\n\nPress Back",
                64, 38, AlignCenter, AlignCenter);
            popup_set_timeout(app->popup, 5000);
            popup_enable_timeout(app->popup);
            view_dispatcher_switch_to_view(
                app->view_dispatcher, RollJamViewPopup);

            notification_message(app->notification, &sequence_success);
            return true;

        } else if(event.event == RollJamEventReplayNow) {

            popup_reset(app->popup);
            popup_set_header(
                app->popup, "Transmitting...",
                64, 20, AlignCenter, AlignCenter);
            popup_set_text(
                app->popup, "Sending 2nd code NOW",
                64, 38, AlignCenter, AlignCenter);
            view_dispatcher_switch_to_view(
                app->view_dispatcher, RollJamViewPopup);

            rolljam_transmit_signal(app, &app->signal_second);

            notification_message(app->notification, &sequence_success);

            popup_set_header(
                app->popup, "Done!",
                64, 20, AlignCenter, AlignCenter);
            popup_set_text(
                app->popup,
                "2nd code transmitted!\n\nPress Back",
                64, 38, AlignCenter, AlignCenter);
            popup_set_timeout(app->popup, 5000);
            popup_enable_timeout(app->popup);

            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_search_and_switch_to_another_scene(
            app->scene_manager, RollJamSceneMenu);
        return true;
    }
    return false;
}

void rolljam_scene_result_on_exit(void* context) {
    RollJamApp* app = context;
    dialog_ex_reset(app->dialog_ex);
    popup_reset(app->popup);
}

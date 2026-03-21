#include "rolljam.h"
#include "scenes/rolljam_scene.h"
#include "helpers/rolljam_cc1101_ext.h"
#include "helpers/rolljam_receiver.h"
#include "helpers/rolljam_cc1101_ext.h"

// ============================================================
// Frequency / modulation tables
// ============================================================

const uint32_t freq_values[] = {
    300000000,
    303875000,
    315000000,
    318000000,
    390000000,
    433075000,
    433920000,
    434420000,
    438900000,
    868350000,
    915000000,
};

const char* freq_names[] = {
    "300.00",
    "303.87",
    "315.00",
    "318.00",
    "390.00",
    "433.07",
    "433.92",
    "434.42",
    "438.90",
    "868.35",
    "915.00",
};

const char* mod_names[] = {
    "AM 650",
    "AM 270",
    "FM 238",
    "FM 476",
};

const uint32_t jam_offset_values[] = {
    300000,
    500000,
    700000,
    1000000,
};

const char* jam_offset_names[] = {
    "300 kHz",
    "500 kHz",
    "700 kHz",
    "1000 kHz",
};

const char* hw_names[] = {
    "CC1101",
    "Flux Cap",
};

// ============================================================
// Scene handlers table (extern declarations in scene header)
// ============================================================

void (*const rolljam_scene_on_enter_handlers[])(void*) = {
    rolljam_scene_menu_on_enter,
    rolljam_scene_attack_phase1_on_enter,
    rolljam_scene_attack_phase2_on_enter,
    rolljam_scene_attack_phase3_on_enter,
    rolljam_scene_result_on_enter,
};

bool (*const rolljam_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
    rolljam_scene_menu_on_event,
    rolljam_scene_attack_phase1_on_event,
    rolljam_scene_attack_phase2_on_event,
    rolljam_scene_attack_phase3_on_event,
    rolljam_scene_result_on_event,
};

void (*const rolljam_scene_on_exit_handlers[])(void*) = {
    rolljam_scene_menu_on_exit,
    rolljam_scene_attack_phase1_on_exit,
    rolljam_scene_attack_phase2_on_exit,
    rolljam_scene_attack_phase3_on_exit,
    rolljam_scene_result_on_exit,
};

const SceneManagerHandlers rolljam_scene_handlers = {
    .on_enter_handlers = rolljam_scene_on_enter_handlers,
    .on_event_handlers = rolljam_scene_on_event_handlers,
    .on_exit_handlers = rolljam_scene_on_exit_handlers,
    .scene_num = RollJamSceneCount,
};

// ============================================================
// Navigation callbacks
// ============================================================

static bool rolljam_navigation_callback(void* context) {
    RollJamApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static bool rolljam_custom_event_callback(void* context, uint32_t event) {
    RollJamApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

// ============================================================
// App alloc
// ============================================================

static RollJamApp* rolljam_app_alloc(void) {
    RollJamApp* app = malloc(sizeof(RollJamApp));
    memset(app, 0, sizeof(RollJamApp));

    app->freq_index = FreqIndex_433_92;
    app->frequency = freq_values[FreqIndex_433_92];
    app->mod_index = ModIndex_AM650;
    app->jam_offset_index = JamOffIndex_700k;
    app->jam_offset_hz = jam_offset_values[JamOffIndex_700k];
    app->hw_index = HwIndex_CC1101;

    // Services
    app->gui = furi_record_open(RECORD_GUI);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);

    // Scene manager
    app->scene_manager = scene_manager_alloc(&rolljam_scene_handlers, app);

    // View dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, rolljam_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, rolljam_navigation_callback);
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Variable item list
    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        RollJamViewVarItemList,
        variable_item_list_get_view(app->var_item_list));

    // Widget
    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        RollJamViewWidget,
        widget_get_view(app->widget));

    // Dialog
    app->dialog_ex = dialog_ex_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        RollJamViewDialogEx,
        dialog_ex_get_view(app->dialog_ex));

    // Popup
    app->popup = popup_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        RollJamViewPopup,
        popup_get_view(app->popup));

    return app;
}

// ============================================================
// App free
// ============================================================

static void rolljam_app_free(RollJamApp* app) {
    if(app->jamming_active) {
        rolljam_jammer_stop(app);
    }
    if(app->raw_capture_active) {
        rolljam_capture_stop(app);
    }

    view_dispatcher_remove_view(app->view_dispatcher, RollJamViewVarItemList);
    variable_item_list_free(app->var_item_list);

    view_dispatcher_remove_view(app->view_dispatcher, RollJamViewWidget);
    widget_free(app->widget);

    view_dispatcher_remove_view(app->view_dispatcher, RollJamViewDialogEx);
    dialog_ex_free(app->dialog_ex);

    view_dispatcher_remove_view(app->view_dispatcher, RollJamViewPopup);
    popup_free(app->popup);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);

    free(app);
}

// ============================================================
// Entry point
// ============================================================

int32_t rolljam_app(void* p) {
    UNUSED(p);

    RollJamApp* app = rolljam_app_alloc();

    FURI_LOG_I(TAG, "=== RollJam Started ===");
    FURI_LOG_I(TAG, "Internal CC1101 = RX capture (narrow BW)");
    FURI_LOG_I(TAG, "External CC1101 = TX jam (offset +%lu Hz)", app->jam_offset_hz);

    scene_manager_next_scene(app->scene_manager, RollJamSceneMenu);
    view_dispatcher_run(app->view_dispatcher);

    rolljam_app_free(app);

    FURI_LOG_I(TAG, "=== RollJam Stopped ===");
    return 0;
}

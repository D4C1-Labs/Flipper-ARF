#include "karr_poc_app.h"
#include "scenes/karr_poc_scene.h"

extern const SceneManagerHandlers karr_poc_scene_handlers;

const char* const karr_default_allowlist[] = {
    "PLACEHOLDER-SERIAL-1",
    "PLACEHOLDER-SERIAL-2",
};
const size_t karr_default_allowlist_count =
    sizeof(karr_default_allowlist) / sizeof(karr_default_allowlist[0]);

static bool karr_poc_app_custom_event_callback(void* context, uint32_t event) {
    KarrPocApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool karr_poc_app_back_event_callback(void* context) {
    KarrPocApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static KarrPocApp* karr_poc_app_alloc(void) {
    KarrPocApp* app = malloc(sizeof(KarrPocApp));
    memset(app, 0, sizeof(KarrPocApp));

    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&karr_poc_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, karr_poc_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, karr_poc_app_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, KarrPocView_Submenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, KarrPocView_Widget, widget_get_view(app->widget));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, KarrPocView_TextBox, text_box_get_view(app->text_box));

    app->log_text = furi_string_alloc();

    app->mode = QtMode_User;
    app->pending_command = QtCmd_UnlockDoors;

    app->allowlist_enabled = true;
    app->allowlist_count = karr_default_allowlist_count < KARR_ALLOWLIST_MAX
                                ? karr_default_allowlist_count
                                : KARR_ALLOWLIST_MAX;
    for(size_t i = 0; i < app->allowlist_count; i++) {
        strncpy(app->allowlist[i], karr_default_allowlist[i], KARR_BLE_NAME_MAX_LEN - 1);
        app->allowlist[i][KARR_BLE_NAME_MAX_LEN - 1] = '\0';
    }
    app->mayhem_mode = QtMode_User;
    app->mayhem_command = QtCmd_UnlockDoors;

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void karr_poc_app_free(KarrPocApp* app) {
    furi_string_free(app->log_text);

    view_dispatcher_remove_view(app->view_dispatcher, KarrPocView_Submenu);
    submenu_free(app->submenu);

    view_dispatcher_remove_view(app->view_dispatcher, KarrPocView_Widget);
    widget_free(app->widget);

    view_dispatcher_remove_view(app->view_dispatcher, KarrPocView_TextBox);
    text_box_free(app->text_box);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t karr_poc_app(void* p) {
    UNUSED(p);

    KarrPocApp* app = karr_poc_app_alloc();

    scene_manager_next_scene(app->scene_manager, KarrPocScene_Start);
    view_dispatcher_run(app->view_dispatcher);

    karr_poc_app_free(app);

    return 0;
}

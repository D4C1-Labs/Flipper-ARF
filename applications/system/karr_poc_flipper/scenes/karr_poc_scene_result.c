#include "karr_poc_scene.h"
#include "../karr_poc_app.h"

static void karr_poc_scene_result_widget_callback(GuiButtonType result, InputType type, void* context) {
    KarrPocApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void karr_poc_scene_result_on_enter(void* context) {
    KarrPocApp* app = context;

    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget, 0, 0, 128, 50, furi_string_get_cstr(app->log_text));
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Back to start", karr_poc_scene_result_widget_callback, app);

    karr_ble_disconnect();

    view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_Widget);
}

bool karr_poc_scene_result_on_event(void* context, SceneManagerEvent event) {
    KarrPocApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeCenter) {
            scene_manager_search_and_switch_to_previous_scene(app->scene_manager, KarrPocScene_Start);
            consumed = true;
        }
    }

    return consumed;
}

void karr_poc_scene_result_on_exit(void* context) {
    KarrPocApp* app = context;
    widget_reset(app->widget);
}

#include "karr_poc_scene.h"
#include "../karr_poc_app.h"

static void karr_poc_scene_mayhem_confirm_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    KarrPocApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void karr_poc_scene_mayhem_confirm_on_enter(void* context) {
    KarrPocApp* app = context;

    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget,
        0,
        0,
        128,
        44,
        "ALLOWLIST DISABLED\n\n"
        "This will send the command\n"
        "to ALL units that respond\n"
        "nearby, without filtering.\n\n"
        "Are you sure?");
    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Cancel", karr_poc_scene_mayhem_confirm_widget_callback, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Yes, do it", karr_poc_scene_mayhem_confirm_widget_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_Widget);
}

bool karr_poc_scene_mayhem_confirm_on_event(void* context, SceneManagerEvent event) {
    KarrPocApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeRight) {
            scene_manager_next_scene(app->scene_manager, KarrPocScene_MayhemScan);
            consumed = true;
        } else if(event.event == GuiButtonTypeLeft) {
            scene_manager_search_and_switch_to_previous_scene(app->scene_manager, KarrPocScene_MayhemMenu);
            consumed = true;
        }
    }

    return consumed;
}

void karr_poc_scene_mayhem_confirm_on_exit(void* context) {
    KarrPocApp* app = context;
    widget_reset(app->widget);
}

#include "karr_poc_scene.h"
#include "../karr_poc_app.h"

typedef enum {
    KarrPocStartIndex_Scan,
    KarrPocStartIndex_Mayhem,
    KarrPocStartIndex_About,
} KarrPocStartIndex;

static void karr_poc_scene_start_submenu_callback(void* context, uint32_t index) {
    KarrPocApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void karr_poc_scene_start_on_enter(void* context) {
    KarrPocApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "KARR BLE PoC");
    submenu_add_item(
        app->submenu, "Scan for units", KarrPocStartIndex_Scan, karr_poc_scene_start_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Mayhem mode", KarrPocStartIndex_Mayhem, karr_poc_scene_start_submenu_callback, app);
    submenu_add_item(
        app->submenu, "About", KarrPocStartIndex_About, karr_poc_scene_start_submenu_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_Submenu);
}

bool karr_poc_scene_start_on_event(void* context, SceneManagerEvent event) {
    KarrPocApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KarrPocStartIndex_Scan) {
            scene_manager_next_scene(app->scene_manager, KarrPocScene_Scan);
            consumed = true;
        } else if(event.event == KarrPocStartIndex_Mayhem) {
            scene_manager_next_scene(app->scene_manager, KarrPocScene_MayhemMenu);
            consumed = true;
        } else if(event.event == KarrPocStartIndex_About) {
            widget_reset(app->widget);
            widget_add_text_scroll_element(
                app->widget,
                0,
                0,
                128,
                64,
                "KARR/QTAP BLE PoC\n\n"
                "BLE remote keyless system\n"
                "research project.\n\n"
                "Protocol and cryptography\n"
                "analysis of the KARR/QTAP\n"
                "automotive system.\n\n"
                "See project documentation\n"
                "for technical details.");
            view_dispatcher_switch_to_view(app->view_dispatcher, KarrPocView_Widget);
            consumed = true;
        }
    }

    return consumed;
}

void karr_poc_scene_start_on_exit(void* context) {
    KarrPocApp* app = context;
    submenu_reset(app->submenu);
}

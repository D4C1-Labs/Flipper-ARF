#include "../subghz_i.h"
#include "../helpers/subghz_custom_event.h"
#include <storage/storage.h>

#define SUBGHZ_GARAGE_SELF_FAP_PATH EXT_PATH("apps/Sub-GHz/subghz_garage.fap")

static void subghz_scene_protocol_load_error_widget_cb(
    GuiButtonType result,
    InputType type,
    void* context) {
    SubGhz* subghz = context;
    if(type != InputTypeShort) return;

    if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventProtocolLoadErrorRetry);
    } else if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventProtocolLoadErrorExit);
    }
}

void subghz_scene_protocol_load_error_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz_ensure_widget(subghz);
    Widget* widget = subghz->widget;

    widget_add_string_multiline_element(
        widget, 64, 8, AlignCenter, AlignTop, FontPrimary, "Loading Error");
    widget_add_string_multiline_element(
        widget,
        64,
        20,
        AlignCenter,
        AlignTop,
        FontSecondary,
        "An error occurred while\nloading Protocols");
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Exit", subghz_scene_protocol_load_error_widget_cb, subghz);
    widget_add_button_element(
        widget, GuiButtonTypeRight, "Retry", subghz_scene_protocol_load_error_widget_cb, subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_protocol_load_error_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeBack) {

        scene_manager_stop(subghz->scene_manager);
        view_dispatcher_stop(subghz->view_dispatcher);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventProtocolLoadErrorExit) {
            scene_manager_stop(subghz->scene_manager);
            view_dispatcher_stop(subghz->view_dispatcher);
            return true;
        }
        if(event.event == SubGhzCustomEventProtocolLoadErrorRetry) {

            subghz_scene_start_launch_and_exit(
                subghz,
                SUBGHZ_GARAGE_SELF_FAP_PATH,
                subghz->launched_from_mode_picker ? "frommode" : NULL);
            return true;
        }
    }

    return false;
}

void subghz_scene_protocol_load_error_on_exit(void* context) {
    SubGhz* subghz = context;
    if(subghz->widget) {
        widget_reset(subghz->widget);
    }
}

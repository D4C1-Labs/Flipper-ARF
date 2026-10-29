#include "../subghz_i.h"
#include "../helpers/subghz_custom_event.h"
#include <furi/core/memmgr.h>

static void subghz_scene_shared_ram_warning_widget_cb(
    GuiButtonType result,
    InputType type,
    void* context) {
    SubGhz* subghz = context;
    if(type != InputTypeShort) return;

    if(result == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSharedRamWarningContinue);
    }
}

void subghz_scene_shared_ram_warning_on_enter(void* context) {
    SubGhz* subghz = context;

    subghz_ensure_widget(subghz);
    Widget* widget = subghz->widget;

    widget_add_string_element(
        widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Flipper RAM");

    const char* body = "\ecSub-GHz Garage App\n"
                        "\ecrequires the use of\n"
                        "\ecadditional RAM.\n"
                        "\ecFox will Disconnect\n"
                        "\ecqFlipper during Read and\n"
                        "\ecDecode and Reconnect\n"
                        "\ecautomatically on its own.";
    widget_add_text_scroll_element(widget, 0, 14, 128, 34, body);

    widget_add_button_element(
        widget,
        GuiButtonTypeCenter,
        "Continue",
        subghz_scene_shared_ram_warning_widget_cb,
        subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_shared_ram_warning_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeBack) {

        scene_manager_stop(subghz->scene_manager);
        view_dispatcher_stop(subghz->view_dispatcher);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSharedRamWarningContinue) {
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    }

    return false;
}

void subghz_scene_shared_ram_warning_on_exit(void* context) {
    SubGhz* subghz = context;
    if(subghz->widget) {
        widget_reset(subghz->widget);
    }
}

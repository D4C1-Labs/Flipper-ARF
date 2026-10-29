#include "../subghz_i.h"
#include <string.h>

void subghz_scene_details_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz_ensure_widget(subghz);
    widget_reset(subghz->widget);

    const char* full = furi_string_get_cstr(subghz->error_str);

    char proto[48] = "Details";
    const char* body = full;
    const char* nl = strchr(full, '\n');
    if(nl) {
        size_t len = (size_t)(nl - full);
        if(len >= sizeof(proto)) len = sizeof(proto) - 1;
        memcpy(proto, full, len);
        proto[len] = '\0';
        body = nl + 1;
    }

    widget_add_string_element(
        subghz->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, proto);

    widget_add_text_scroll_element(
        subghz->widget, 0, 14, 128, 50, body);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_details_on_event(void* context, SceneManagerEvent event) {

    UNUSED(context);
    UNUSED(event);
    return false;
}

void subghz_scene_details_on_exit(void* context) {
    SubGhz* subghz = context;
    widget_reset(subghz->widget);
}

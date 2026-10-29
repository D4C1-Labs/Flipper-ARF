#include "../subghz_i.h"
#include "../views/subghz_signal_visualizer.h"

#define TAG "SubGhzSceneSignalVisualizer"

static void subghz_scene_signal_visualizer_view_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

void subghz_scene_signal_visualizer_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz_ensure_signal_visualizer(subghz);

    subghz_signal_visualizer_set_callback(
        subghz->subghz_signal_visualizer,
        subghz_scene_signal_visualizer_view_callback,
        subghz);

    view_dispatcher_switch_to_view(
        subghz->view_dispatcher, SubGhzViewIdSignalVisualizer);

    subghz_signal_visualizer_set_mode(
        subghz->subghz_signal_visualizer,
        subghz->last_settings->visualizer_display_mode);

    subghz_signal_visualizer_start(subghz->subghz_signal_visualizer);
}

bool subghz_scene_signal_visualizer_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventViewSignalVisualizerBack) {
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    }

    return false;
}

void subghz_scene_signal_visualizer_on_exit(void* context) {
    SubGhz* subghz = context;
    subghz_signal_visualizer_stop(subghz->subghz_signal_visualizer);

    subghz->last_settings->visualizer_display_mode =
        subghz_signal_visualizer_get_mode(subghz->subghz_signal_visualizer);
    subghz_garage_last_settings_save(subghz->last_settings);
}

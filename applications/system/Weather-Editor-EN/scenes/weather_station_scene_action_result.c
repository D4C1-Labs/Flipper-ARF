#include "../weather_station_app_i.h"

void weather_station_scene_action_result_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!weather_station_ensure_widget(app)) return;

    widget_reset(app->widget);
    widget_add_text_box_element(
        app->widget,
        0,
        2,
        128,
        14,
        AlignCenter,
        AlignCenter,
        furi_string_get_cstr(app->editor_result_title),
        false);
    widget_add_text_scroll_element(
        app->widget,
        2,
        18,
        124,
        44,
        furi_string_get_cstr(app->editor_result_text));

    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewWidget);
    weather_station_release_inactive_gui_views(app, WeatherStationViewWidget);
}

bool weather_station_scene_action_result_on_event(
    void* context,
    SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void weather_station_scene_action_result_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(app->widget) widget_reset(app->widget);
}

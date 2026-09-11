#include "../weather_station_app_i.h"

#define WEATHER_LAB_PIN 7777

static void weather_lab_pin_done(void* context, int32_t value) {
    WeatherStationApp* app = context;
    if(!app) return;
    if(value == WEATHER_LAB_PIN) {
        app->lab_unlocked = true;
        app->lab_target_temperature_tenths = 0;
        app->lab_target_humidity = 0;
        scene_manager_next_scene(app->scene_manager, WeatherStationSceneLabRun);
    } else {
        app->lab_unlocked = false;
        scene_manager_previous_scene(app->scene_manager);
    }
}

void weather_station_scene_lab_pin_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !weather_station_ensure_number_input(app)) return;
    number_input_set_header_text(app->number_input, "Code");
    number_input_set_result_callback(app->number_input, weather_lab_pin_done, app, 0, 0, 9999);
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewNumberInput);
    weather_station_release_inactive_gui_views(app, WeatherStationViewNumberInput);
}

bool weather_station_scene_lab_pin_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}
void weather_station_scene_lab_pin_on_exit(void* context) { UNUSED(context); }

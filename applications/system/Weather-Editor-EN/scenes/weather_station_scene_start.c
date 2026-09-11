#include "../weather_station_app_i.h"

typedef enum {
    SubmenuIndexWeatherStationReceiver,
    SubmenuIndexWeatherStationSaved,
    SubmenuIndexWeatherStationSettings,
    SubmenuIndexWeatherStationSimulation,
    SubmenuIndexWeatherStationAbout,
} SubmenuIndex;

void weather_station_scene_start_submenu_callback(void* context, uint32_t index) {
    WeatherStationApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void weather_station_scene_start_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app) return;

    /* The main menu does not decode anything. Keep the selected radio device,
       but release worker/environment/receiver memory before building menus. */
    weather_station_release_rx_core(app);
    if(!weather_station_ensure_submenu(app)) return;
    Submenu* submenu = app->submenu;

    submenu_set_header(submenu, "Weather Editor");

    submenu_add_item(
        submenu,
        "Receiver",
        SubmenuIndexWeatherStationReceiver,
        weather_station_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Load saved",
        SubmenuIndexWeatherStationSaved,
        weather_station_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Settings",
        SubmenuIndexWeatherStationSettings,
        weather_station_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Simulation",
        SubmenuIndexWeatherStationSimulation,
        weather_station_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Info",
        SubmenuIndexWeatherStationAbout,
        weather_station_scene_start_submenu_callback,
        app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, WeatherStationSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewSubmenu);

    /* The submenu is already active. Only now may the previous GUI module be
       detached and freed without invalidating ViewDispatcher internals. */
    weather_station_release_inactive_gui_views(app, WeatherStationViewSubmenu);
}

bool weather_station_scene_start_on_event(void* context, SceneManagerEvent event) {
    WeatherStationApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexWeatherStationAbout) {
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneAbout);
            consumed = true;
        } else if(event.event == SubmenuIndexWeatherStationSaved) {
            app->editor_simulation_mode = false;
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneSavedProfiles);
            consumed = true;
        } else if(event.event == SubmenuIndexWeatherStationSettings) {
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneSettings);
            consumed = true;
        } else if(event.event == SubmenuIndexWeatherStationSimulation) {
            app->editor_simulation_mode = false;
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneSimulation);
            consumed = true;
        } else if(event.event == SubmenuIndexWeatherStationReceiver) {
            app->editor_simulation_mode = false;
            if(app->loaded_profile_path) furi_string_reset(app->loaded_profile_path);
            if(app->loaded_profile_preset_data) {
                free(app->loaded_profile_preset_data);
                app->loaded_profile_preset_data = NULL;
                app->txrx->preset->data = NULL;
                app->txrx->preset->data_size = 0;
            }
            app->editor_loaded_from_profile = false;
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneReceiver);
            consumed = true;
        }
        scene_manager_set_scene_state(app->scene_manager, WeatherStationSceneStart, event.event);
    }

    return consumed;
}

void weather_station_scene_start_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(app->submenu) submenu_reset(app->submenu);
}

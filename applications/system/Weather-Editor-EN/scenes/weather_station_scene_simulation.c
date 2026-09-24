#include "../weather_station_app_i.h"
#include "../weather_editor_test.h"
#include "../protocols/ws_generic.h"

#define WEATHER_SIM_DEFAULT_ID 0x42U

static void weather_station_simulation_callback(void* context, uint32_t index) {
    WeatherStationApp* app = context;
    if(app && app->view_dispatcher) view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void weather_station_scene_simulation_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !weather_station_ensure_submenu(app)) return;

    weather_station_release_rx_core(app);
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Simulation - protocol");
    for(uint8_t i = 0U; i < WEATHER_EDITOR_PROTOCOL_TEST_COUNT; i++) {
        submenu_add_item(
            app->submenu,
            weather_editor_protocol_test_name(i),
            i,
            weather_station_simulation_callback,
            app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewSubmenu);
    weather_station_release_inactive_gui_views(app, WeatherStationViewSubmenu);
}

bool weather_station_scene_simulation_on_event(void* context, SceneManagerEvent event) {
    WeatherStationApp* app = context;
    if(!app || event.type != SceneManagerEventTypeCustom ||
       event.event >= WEATHER_EDITOR_PROTOCOL_TEST_COUNT) {
        return false;
    }

    if(!weather_station_ensure_radio_core(app)) {
        furi_string_set(app->editor_result_title, "No radio");
        furi_string_set(app->editor_result_text, "Cannot start Sub-GHz.");
        scene_manager_next_scene(app->scene_manager, WeatherStationSceneActionResult);
        return true;
    }

    app->editor_simulation_mode = true;
    app->editor_simulation_protocol_index = (uint8_t)event.event;
    app->editor_loaded_from_profile = false;
    if(app->loaded_profile_path) furi_string_reset(app->loaded_profile_path);
    free(app->loaded_profile_preset_data);
    app->loaded_profile_preset_data = NULL;

    furi_string_set(app->txrx->preset->name, WEATHER_EDITOR_PROTOCOL_TEST_PRESET);
    app->txrx->preset->frequency = WEATHER_EDITOR_PROTOCOL_TEST_FREQUENCY;
    app->txrx->preset->data = NULL;
    app->txrx->preset->data_size = 0U;
    app->editor_frequency_hz = WEATHER_EDITOR_PROTOCOL_TEST_FREQUENCY;
    app->editor_channel_auto = false;
    app->editor_received_channel = WS_NO_CHANNEL;

    uint32_t default_id = WEATHER_SIM_DEFAULT_ID;
    const uint32_t max_id = weather_editor_simulation_id_max(app->editor_simulation_protocol_index);
    if(max_id > 0xFFU) default_id = 0x1234U & max_id;
    if(!weather_editor_simulation_prepare(
           app->editor, app->editor_simulation_protocol_index, default_id)) {
        furi_string_set(app->editor_result_title, "Simulation error");
        furi_string_set(app->editor_result_text, "Cannot prepare protocol.");
        scene_manager_next_scene(app->scene_manager, WeatherStationSceneActionResult);
        return true;
    }

    scene_manager_set_scene_state(app->scene_manager, WeatherStationSceneActions, 0U);
    scene_manager_next_scene(app->scene_manager, WeatherStationSceneActions);
    return true;
}

void weather_station_scene_simulation_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(app && app->submenu) submenu_reset(app->submenu);
}

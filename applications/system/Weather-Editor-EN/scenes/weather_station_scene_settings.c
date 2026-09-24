#include "../weather_station_app_i.h"

static const char* const weather_editor_unit_labels[] = {"C", "F"};

static void weather_station_settings_unit_changed(VariableItem* item) {
    if(!item) return;
    WeatherStationApp* app = variable_item_get_context(item);
    if(!app) return;
    uint8_t index = variable_item_get_current_value_index(item);
    if(index > 1U) index = 0U;
    app->temperature_unit = index == 1 ? WeatherEditorTemperatureUnitFahrenheit :
                                        WeatherEditorTemperatureUnitCelsius;
    app->editor_settings.display_fahrenheit = index == 1;
    variable_item_set_current_value_text(item, weather_editor_unit_labels[index]);
    weather_editor_settings_save(&app->editor_settings);
}


void weather_station_scene_settings_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->view_dispatcher || !weather_station_ensure_variable_item_list(app)) return;
    VariableItemList* list = app->variable_item_list;
    variable_item_list_reset(list);

    VariableItem* unit = variable_item_list_add(
        list, "Temp. unit", 2, weather_station_settings_unit_changed, app);
    const uint8_t unit_index =
        app->temperature_unit == WeatherEditorTemperatureUnitFahrenheit ? 1 : 0;
    variable_item_set_current_value_index(unit, unit_index);
    variable_item_set_current_value_text(unit, weather_editor_unit_labels[unit_index]);


    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewVariableItemList);
    weather_station_release_inactive_gui_views(app, WeatherStationViewVariableItemList);
}

bool weather_station_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void weather_station_scene_settings_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(app && app->variable_item_list) {
        variable_item_list_set_selected_item(app->variable_item_list, 0);
        variable_item_list_reset(app->variable_item_list);
    }
}

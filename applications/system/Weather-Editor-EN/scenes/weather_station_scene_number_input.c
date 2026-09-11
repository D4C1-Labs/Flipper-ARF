#include "../weather_station_app_i.h"
#include "../weather_editor_test.h"
#include <string.h>


static int8_t weather_editor_hex_value(char c) {
    if(c >= '0' && c <= '9') return (int8_t)(c - '0');
    if(c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    if(c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    return -1;
}

static bool weather_editor_parse_hex_id(const char* text, uint32_t* value) {
    if(!text || !value) return false;
    if(text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    const size_t len = strlen(text);
    if(len == 0U || len > 8U) return false;
    uint32_t parsed = 0U;
    for(size_t i = 0; i < len; i++) {
        const int8_t nibble = weather_editor_hex_value(text[i]);
        if(nibble < 0) return false;
        parsed = (parsed << 4U) | (uint32_t)nibble;
    }
    *value = parsed;
    return true;
}

static bool weather_editor_sensor_id_validator(const char* text, FuriString* error, void* context) {
    WeatherStationApp* app = context;
    uint32_t value = 0U;
    if(!app || !weather_editor_parse_hex_id(text, &value)) {
        furi_string_set(error, "Enter HEX: 0-9, A-F");
        return false;
    }
    const uint32_t max_id = weather_editor_simulation_id_max(app->editor_simulation_protocol_index);
    if(value > max_id) {
        furi_string_printf(error, "ID too large (max 0x%lX)", (unsigned long)max_id);
        return false;
    }
    return true;
}

static void weather_editor_sensor_id_text_done(void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->editor || !app->editor_simulation_mode) return;
    uint32_t value = app->editor->id;
    if(weather_editor_parse_hex_id(app->editor_text_input_buffer, &value)) {
        weather_editor_simulation_set_id(
            app->editor, app->editor_simulation_protocol_index, value);
    }
    if(app->scene_manager) scene_manager_previous_scene(app->scene_manager);
}

static void weather_editor_open_sensor_id_text_input(WeatherStationApp* app) {
    if(!app || !app->editor || !weather_station_ensure_text_input(app)) return;
    snprintf(
        app->editor_text_input_buffer,
        sizeof(app->editor_text_input_buffer),
        "%lX",
        (unsigned long)app->editor->id);
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Sensor ID HEX (0-9 A-F)");
    text_input_set_minimum_length(app->text_input, 1U);
    text_input_set_validator(app->text_input, weather_editor_sensor_id_validator, app);
    text_input_set_result_callback(
        app->text_input,
        weather_editor_sensor_id_text_done,
        app,
        app->editor_text_input_buffer,
        sizeof(app->editor_text_input_buffer),
        false);
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewTextInput);
    weather_station_release_inactive_gui_views(app, WeatherStationViewTextInput);
}

static int32_t weather_editor_display_temperature_tenths(const WeatherStationApp* app) {
    if(!app || !app->editor) return 0;
    int32_t value = app->editor->temperature_tenths;
    if(app->temperature_unit == WeatherEditorTemperatureUnitFahrenheit) {
        value = weather_editor_temperature_to_fahrenheit_tenths(value);
    }
    return value;
}


static void weather_editor_number_input_done(void* context, int32_t number) {
    WeatherStationApp* app = context;
    if(!app) return;
    switch(app->editor_input_target) {
    case WeatherEditorInputTemperature: {
        if(!app->editor) break;
        if(number < 0) number = 0;
        if(number > 9990) number = 9990;
        int32_t signed_value = app->temperature_negative ? -number : number;
        if(app->temperature_unit == WeatherEditorTemperatureUnitFahrenheit) {
            app->editor->temperature_tenths =
                weather_editor_temperature_to_celsius_tenths(signed_value);
        } else {
            app->editor->temperature_tenths = signed_value;
        }
        break;
    }
    case WeatherEditorInputHumidity:
        if(!app->editor) break;
        /* Wilgotnosc wzgledna nie ma znaku minus. Ochrona pozostaje takze
           na wypadek wywolania callbacku z wartoscia spoza zakresu UI. */
        app->editor->humidity = number < 0 ? 0 : (number > 900 ? 900 : number);
        break;
    case WeatherEditorInputBattery:
        if(!app->editor) break;
        app->editor->battery = number < 0 ? 0 : (number > 100 ? 100 : number);
        break;
    case WeatherEditorInputChannel:
        if(!app->editor) break;
        app->editor->channel = weather_editor_sanitize_channel(app->editor, number);
        break;
    case WeatherEditorInputSensorId:
        if(!app->editor || !app->editor_simulation_mode) break;
        if(number < 0) number = 0;
        weather_editor_simulation_set_id(
            app->editor, app->editor_simulation_protocol_index, (uint32_t)number);
        break;
    case WeatherEditorInputTxInterval:
        if(number < 1) number = 1;
        if(number > 3600) number = 3600;
        app->editor_auto_tx_interval_s = (uint16_t)number;
        app->editor_settings.auto_tx_interval_s = app->editor_auto_tx_interval_s;
        break;
    case WeatherEditorInputEditorFrequencyKHz:
        if(number < 1) number = 1;
        if(number > 999999) number = 999999;
        app->editor_frequency_hz = (uint32_t)number * 1000UL;
        break;
    }
    if(app->scene_manager) scene_manager_previous_scene(app->scene_manager);
}

void weather_station_scene_number_input_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->view_dispatcher) return;
    if(app->editor_input_target == WeatherEditorInputSensorId) {
        if(!app->editor || !app->editor_simulation_mode) return;
        weather_editor_open_sensor_id_text_input(app);
        return;
    }
    if(!weather_station_ensure_number_input(app)) return;
    int32_t current = 0;
    int32_t min = -9999999;
    int32_t max = 9999999;
    const char* header = "Value";

    switch(app->editor_input_target) {
    case WeatherEditorInputTemperature:
        if(!app->editor) return;
        current = weather_editor_abs_saturated(weather_editor_display_temperature_tenths(app));
        header = app->temperature_unit == WeatherEditorTemperatureUnitFahrenheit ?
                     "Temperature x0.1 F" :
                     "Temperature x0.1 C";
        min = 0;
        max = 9990;
        break;
    case WeatherEditorInputHumidity:
        if(!app->editor) return;
        current = app->editor->humidity < 0 ? 0 : app->editor->humidity;
        header = "Humidity %";
        min = 0;
        max = 900;
        break;
    case WeatherEditorInputBattery:
        if(!app->editor) return;
        current = app->editor->battery;
        header = app->editor->battery_kind == WeatherEditorBatteryPercent ?
                     "Battery 0-100%" :
                     "Battery: 0=OK 1=LOW";
        min = 0;
        max = app->editor->battery_kind == WeatherEditorBatteryPercent ? 100 : 1;
        break;
    case WeatherEditorInputChannel:
        if(!app->editor) return;
        weather_editor_get_channel_limits(app->editor, &min, &max);
        current = weather_editor_sanitize_channel(app->editor, app->editor->channel);
        header = "Manual channel";
        break;
    case WeatherEditorInputSensorId:
        if(!app->editor || !app->editor_simulation_mode) return;
        current = (int32_t)app->editor->id;
        header = "Sensor ID (dec)";
        min = 0;
        max = (int32_t)weather_editor_simulation_id_max(
            app->editor_simulation_protocol_index);
        break;
    case WeatherEditorInputTxInterval:
        current = app->editor_auto_tx_interval_s;
        header = "TX interval 1-3600 s";
        min = 1;
        max = 3600;
        break;
    case WeatherEditorInputEditorFrequencyKHz:
        current = (int32_t)(app->editor_frequency_hz / 1000UL);
        header = "TX frequency in kHz";
        min = 1;
        max = 999999;
        break;
    }

    number_input_set_header_text(app->number_input, header);
    number_input_set_result_callback(
        app->number_input,
        weather_editor_number_input_done,
        app,
        current,
        min,
        max);
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewNumberInput);
    weather_station_release_inactive_gui_views(app, WeatherStationViewNumberInput);
}

bool weather_station_scene_number_input_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void weather_station_scene_number_input_on_exit(void* context) {
    UNUSED(context);
}

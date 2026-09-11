#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool display_fahrenheit;
    uint16_t auto_tx_interval_s;
    uint16_t protocol_test_interval_s;
    uint32_t protocol_test_frequency_hz;
} WeatherEditorSettings;

void weather_editor_settings_set_defaults(WeatherEditorSettings* settings);
bool weather_editor_settings_load(WeatherEditorSettings* settings);
bool weather_editor_settings_save(const WeatherEditorSettings* settings);

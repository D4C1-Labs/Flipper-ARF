#pragma once

#include "weather_station_app_i.h"
#include "weather_editor_engine.h"

void weather_editor_pair_callback(void* context, bool level, uint32_t duration);
bool weather_editor_validate_raw(
    WeatherStationApp* app,
    const WeatherEditorRaw* raw,
    const WeatherEditorState* expected_state,
    FuriString* status);
bool weather_editor_send_raw(
    WeatherStationApp* app,
    const WeatherEditorRaw* raw,
    const SubGhzRadioPreset* preset,
    FuriString* status);

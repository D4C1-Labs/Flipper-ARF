#pragma once

#include "weather_editor_engine.h"

#define WEATHER_EDITOR_PROTOCOL_TEST_COUNT 25U
#define WEATHER_EDITOR_PROTOCOL_TEST_FREQUENCY 433920000UL
#define WEATHER_EDITOR_PROTOCOL_TEST_PRESET "AM650"

const char* weather_editor_protocol_test_name(uint8_t index);
bool weather_editor_protocol_test_prepare(
    WeatherEditorState* state,
    uint8_t index,
    uint32_t random_value);

/* Reuses the proven 25 TX templates for manual sensor simulation. */
bool weather_editor_simulation_prepare(
    WeatherEditorState* state, uint8_t index, uint32_t sensor_id);
bool weather_editor_simulation_set_id(
    WeatherEditorState* state, uint8_t index, uint32_t sensor_id);
bool weather_editor_simulation_id_editable(uint8_t index);
uint32_t weather_editor_simulation_id_max(uint8_t index);

#pragma once

#include "weather_editor_engine.h"

/**
 * Encode protocols added in Weather Lab 0.4.
 * handled is set when the protocol name belongs to this module, even if a
 * specific sensor variant cannot be edited.
 */
bool weather_editor_encode_extended(
    const WeatherEditorState* state,
    WeatherEditorRaw* out_raw,
    uint64_t* edited_data,
    bool* handled,
    FuriString* status);

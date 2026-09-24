#pragma once

#include <furi.h>
#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/types.h>

#define WEATHER_EDITOR_MAX_PULSES 1536
#define WEATHER_EDITOR_PROFILE_FOLDER EXT_PATH("apps_data/weather_editor/profiles")
#define WEATHER_EDITOR_RX_PROFILE_FOLDER EXT_PATH("apps_data/weather_editor/profiles/rx")
#define WEATHER_EDITOR_EDITED_PROFILE_FOLDER EXT_PATH("apps_data/weather_editor/profiles/edited")
#define WEATHER_EDITOR_SUB_FOLDER EXT_PATH("subghz/weather_editor")
#define WEATHER_EDITOR_MAX_PRESET_DATA 512U

typedef enum {
    WeatherEditorBatteryNone = 0,
    WeatherEditorBatteryFlag,
    WeatherEditorBatteryPercent,
} WeatherEditorBatteryKind;

typedef struct {
    FuriString* protocol_name;
    uint64_t original_data;
    uint64_t edited_data;
    uint8_t bit_count;
    uint8_t var_bits;
    uint64_t var_data;
    uint8_t frame_bits;
    uint64_t frame_upper;
    uint64_t frame_lower;
    uint32_t id;
    int32_t temperature_tenths;
    int32_t humidity;
    int32_t battery;
    uint8_t channel;
    uint8_t button;
    WeatherEditorBatteryKind battery_kind;
    bool has_temperature;
    bool has_humidity;
    bool has_channel;
    bool has_button;
    bool encoder_available;
    bool overflow_wrapped;
} WeatherEditorState;

typedef struct {
    int32_t* values;
    size_t count;
    size_t capacity;
    bool overflowed;
} WeatherEditorRaw;

WeatherEditorState* weather_editor_state_alloc(void);
void weather_editor_state_free(WeatherEditorState* state);
bool weather_editor_state_load(WeatherEditorState* state, FlipperFormat* format);
bool weather_editor_state_copy(WeatherEditorState* dst, const WeatherEditorState* src);


/** Return safe manual-channel limits for the current protocol. */
void weather_editor_get_channel_limits(
    const WeatherEditorState* state, int32_t* min_channel, int32_t* max_channel);

/** Clamp a manual channel to the protocol-specific supported range. */
uint8_t weather_editor_sanitize_channel(const WeatherEditorState* state, int32_t channel);

WeatherEditorRaw* weather_editor_raw_alloc(size_t capacity);
void weather_editor_raw_free(WeatherEditorRaw* raw);
void weather_editor_raw_reset(WeatherEditorRaw* raw);
bool weather_editor_raw_push(WeatherEditorRaw* raw, int32_t duration);

bool weather_editor_encode(const WeatherEditorState* state, WeatherEditorRaw* out_raw, FuriString* status);
bool weather_editor_save_profile(
    const char* path,
    const SubGhzRadioPreset* preset,
    const WeatherEditorState* state,
    FuriString* status);

bool weather_editor_save_key_sub(
    const char* path,
    const SubGhzRadioPreset* preset,
    const WeatherEditorState* state,
    bool use_edited_data,
    FuriString* status);

bool weather_editor_save_raw_sub(
    const char* path,
    const SubGhzRadioPreset* preset,
    const WeatherEditorRaw* raw,
    FuriString* status);

bool weather_editor_load_profile(
    const char* path,
    SubGhzRadioPreset* preset,
    WeatherEditorState* state,
    uint8_t** custom_preset_data,
    FuriString* status);

const char* weather_editor_battery_label(const WeatherEditorState* state);

int32_t weather_editor_temperature_to_fahrenheit_tenths(int32_t celsius_tenths);
int32_t weather_editor_temperature_to_celsius_tenths(int32_t fahrenheit_tenths);
int32_t weather_editor_abs_saturated(int32_t value);

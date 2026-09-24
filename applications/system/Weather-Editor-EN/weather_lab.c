#include "weather_station_app_i.h"
#include <flipper_format/flipper_format_i.h>
#include <lib/toolbox/stream/stream.h>
#include <storage/storage.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define WEATHER_LAB_MAX_SENSORS 10U

static void weather_lab_slot_path(uint8_t slot, char* path, size_t size) {
    snprintf(path, size, EXT_PATH("apps_data/weather_editor/.lab_session_%02u.ws"), slot);
}

void weather_lab_clear_session_files(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) return;
    char path[96];
    for(uint8_t i = 0U; i < WEATHER_LAB_MAX_SENSORS; i++) {
        weather_lab_slot_path(i, path, sizeof(path));
        if(storage_file_exists(storage, path)) (void)storage_common_remove(storage, path);
    }
    furi_record_close(RECORD_STORAGE);
}

static uint32_t weather_lab_hash(const WeatherEditorState* state) {
    uint32_t h = 2166136261UL;
    const char* p = state && state->protocol_name ? furi_string_get_cstr(state->protocol_name) : "";
    while(*p) {
        h ^= (uint8_t)*p++;
        h *= 16777619UL;
    }
    h ^= state ? state->id : 0U;
    h *= 16777619UL;
    h ^= state ? state->channel : 0U;
    h *= 16777619UL;
    return h ? h : 1U;
}

static int8_t weather_lab_find_key(const WeatherStationApp* app, uint32_t key) {
    for(uint8_t i = 0U; i < app->lab_sensor_count; i++) {
        if(app->lab_sensor_keys[i] == key) return (int8_t)i;
    }
    return -1;
}

static void weather_lab_rx_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    WeatherStationApp* app = context;
    if(!app || !receiver || !decoder_base || !app->lab_state || !app->lab_format ||
       !app->txrx || !app->txrx->preset) return;

    Stream* stream = flipper_format_get_raw_stream(app->lab_format);
    if(stream) stream_clean(stream);
    if(subghz_protocol_decoder_base_serialize(decoder_base, app->lab_format, app->txrx->preset) !=
       SubGhzProtocolStatusOk) {
        subghz_receiver_reset(receiver);
        return;
    }
    if(!weather_editor_state_load(app->lab_state, app->lab_format) ||
       !app->lab_state->encoder_available ||
       (!app->lab_state->has_temperature && !app->lab_state->has_humidity)) {
        subghz_receiver_reset(receiver);
        return;
    }

    const uint32_t key = weather_lab_hash(app->lab_state);
    int8_t existing = weather_lab_find_key(app, key);
    uint8_t slot = 0U;
    if(existing >= 0) {
        slot = (uint8_t)existing;
    } else {
        if(app->lab_sensor_count >= WEATHER_LAB_MAX_SENSORS) {
            subghz_receiver_reset(receiver);
            return;
        }
        slot = app->lab_sensor_count;
        app->lab_sensor_keys[slot] = key;
        app->lab_sensor_count++;
    }

    char path[96];
    weather_lab_slot_path(slot, path, sizeof(path));
    (void)weather_editor_save_profile(path, app->txrx->preset, app->lab_state, app->editor_status);
    subghz_receiver_reset(receiver);
}

bool weather_lab_prepare(WeatherStationApp* app) {
    if(!app || !app->editor) return false;
    weather_lab_release(app, false);
    memset(app->lab_sensor_keys, 0, sizeof(app->lab_sensor_keys));
    app->lab_sensor_count = 0U;
    app->lab_round = 0U;
    app->lab_last_tx_ok = 0U;
    weather_lab_clear_session_files();

    /* Reuse the editor state which is already allocated by Weather Editor.
       The previous test build allocated a third WeatherEditorState only for
       LAB, which was unnecessary and could tip the app into OOM after PIN. */
    app->lab_state = app->editor;
    app->lab_format = flipper_format_string_alloc();
    app->lab_preset = calloc(1, sizeof(SubGhzRadioPreset));
    if(app->lab_preset) app->lab_preset->name = furi_string_alloc();
    if(!app->lab_format || !app->lab_preset || !app->lab_preset->name) {
        weather_lab_release(app, true);
        return false;
    }
    return true;
}

void weather_lab_stop_rx(WeatherStationApp* app) {
    if(!app || !app->txrx) return;
    if(app->txrx->receiver) subghz_receiver_set_rx_callback(app->txrx->receiver, NULL, NULL);
    weather_station_release_rx_core(app);
}

void weather_lab_release(WeatherStationApp* app, bool clear_files) {
    if(!app) return;
    weather_lab_stop_rx(app);
    if(app->lab_loaded_preset_data) {
        free(app->lab_loaded_preset_data);
        app->lab_loaded_preset_data = NULL;
    }
    if(app->lab_preset) {
        if(app->lab_preset->name) furi_string_free(app->lab_preset->name);
        free(app->lab_preset);
        app->lab_preset = NULL;
    }
    if(app->lab_format) {
        flipper_format_free(app->lab_format);
        app->lab_format = NULL;
    }
    /* lab_state aliases app->editor; never free it here. */
    app->lab_state = NULL;
    if(clear_files) weather_lab_clear_session_files();
}

bool weather_lab_start_rx(WeatherStationApp* app) {
    if(!app || !weather_station_ensure_radio_core(app) || !app->txrx->receiver) return false;
    subghz_receiver_set_rx_callback(app->txrx->receiver, weather_lab_rx_callback, app);
    if(app->txrx->txrx_state == WSTxRxStateRx) ws_rx_end(app);
    ws_begin(
        app,
        subghz_setting_get_preset_data_by_name(
            app->setting, furi_string_get_cstr(app->txrx->preset->name)));
    ws_rx(app, app->txrx->preset->frequency);
    return app->txrx->txrx_state == WSTxRxStateRx;
}

uint8_t weather_lab_send_burst(WeatherStationApp* app) {
    if(!app || !app->lab_state || !app->lab_preset) return 0U;
    weather_lab_stop_rx(app);
    notification_message(app->notifications, &sequence_set_only_blue_255);

    uint8_t ok_count = 0U;
    char path[96];
    for(uint8_t i = 0U; i < app->lab_sensor_count; i++) {
        if(app->lab_loaded_preset_data) {
            free(app->lab_loaded_preset_data);
            app->lab_loaded_preset_data = NULL;
            app->lab_preset->data = NULL;
            app->lab_preset->data_size = 0U;
        }
        weather_lab_slot_path(i, path, sizeof(path));
        if(!weather_editor_load_profile(
               path,
               app->lab_preset,
               app->lab_state,
               &app->lab_loaded_preset_data,
               app->editor_status)) continue;
        if(app->lab_state->has_temperature)
            app->lab_state->temperature_tenths = app->lab_target_temperature_tenths;
        if(app->lab_state->has_humidity)
            app->lab_state->humidity = app->lab_target_humidity;
        if(weather_editor_send_state(app, app->lab_state, app->lab_preset)) ok_count++;
    }
    if(app->lab_loaded_preset_data) {
        free(app->lab_loaded_preset_data);
        app->lab_loaded_preset_data = NULL;
        app->lab_preset->data = NULL;
        app->lab_preset->data_size = 0U;
    }
    notification_message(app->notifications, &sequence_reset_rgb);
    app->lab_last_tx_ok = ok_count;
    app->lab_round++;
    return ok_count;
}

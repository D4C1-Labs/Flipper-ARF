#include "weather_station_app_i.h"

#define TAG "WeatherStation"
#include <flipper_format/flipper_format_i.h>

void ws_preset_init(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size) {
    WeatherStationApp* app = context;
    if(!app || !app->txrx || !app->txrx->preset || !app->txrx->preset->name || !preset_name) {
        return;
    }
    furi_string_set(app->txrx->preset->name, preset_name);
    app->txrx->preset->frequency = frequency;
    app->txrx->preset->data = preset_data;
    app->txrx->preset->data_size = preset_data_size;
}

bool ws_set_preset(WeatherStationApp* app, const char* preset) {
    if(!app || !app->txrx || !app->txrx->preset || !app->txrx->preset->name || !preset) {
        return false;
    }
    if(!strcmp(preset, "FuriHalSubGhzPresetOok270Async")) {
        furi_string_set(app->txrx->preset->name, "AM270");
    } else if(!strcmp(preset, "FuriHalSubGhzPresetOok650Async")) {
        furi_string_set(app->txrx->preset->name, "AM650");
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev238Async")) {
        furi_string_set(app->txrx->preset->name, "FM238");
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev12KAsync")) {
        furi_string_set(app->txrx->preset->name, "FM12K");
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev476Async")) {
        furi_string_set(app->txrx->preset->name, "FM476");
    } else if(!strcmp(preset, "FuriHalSubGhzPresetCustom")) {
        furi_string_set(app->txrx->preset->name, "CUSTOM");
    } else {
        FURI_LOG_E(TAG, "Unknown preset");
        return false;
    }
    return true;
}

void ws_get_frequency_modulation(
    WeatherStationApp* app,
    FuriString* frequency,
    FuriString* modulation) {
    if(!app || !app->txrx || !app->txrx->preset || !app->txrx->preset->name) {
        if(frequency) furi_string_set(frequency, "---.--");
        if(modulation) furi_string_set(modulation, "--");
        return;
    }
    if(frequency != NULL) {
        furi_string_printf(
            frequency,
            "%03ld.%02ld",
            app->txrx->preset->frequency / 1000000 % 1000,
            app->txrx->preset->frequency / 10000 % 100);
    }
    if(modulation != NULL) {
        furi_string_printf(modulation, "%.2s", furi_string_get_cstr(app->txrx->preset->name));
    }
}

void ws_begin(WeatherStationApp* app, uint8_t* preset_data) {
    if(!app || !app->txrx || !app->txrx->radio_device || !preset_data) return;
    subghz_devices_reset(app->txrx->radio_device);
    subghz_devices_idle(app->txrx->radio_device);
    subghz_devices_load_preset(app->txrx->radio_device, FuriHalSubGhzPresetCustom, preset_data);
    app->txrx->txrx_state = WSTxRxStateIDLE;
}

uint32_t ws_rx(WeatherStationApp* app, uint32_t frequency) {
    if(!app || !app->txrx || !app->txrx->radio_device || !app->txrx->worker) return 0;
    if(!subghz_devices_is_frequency_valid(app->txrx->radio_device, frequency)) {
        FURI_LOG_E(TAG, "Incorrect RX frequency: %lu", (unsigned long)frequency);
        return 0;
    }
    if(app->txrx->txrx_state == WSTxRxStateRx ||
       app->txrx->txrx_state == WSTxRxStateSleep) {
        FURI_LOG_W(TAG, "RX start requested in invalid state");
        return 0;
    }

    subghz_devices_idle(app->txrx->radio_device);
    uint32_t value = subghz_devices_set_frequency(app->txrx->radio_device, frequency);
    subghz_devices_flush_rx(app->txrx->radio_device);
    subghz_devices_set_rx(app->txrx->radio_device);

    subghz_devices_start_async_rx(
        app->txrx->radio_device, subghz_worker_rx_callback, app->txrx->worker);

    subghz_worker_start(app->txrx->worker);
    app->txrx->txrx_state = WSTxRxStateRx;
    return value;
}

void ws_idle(WeatherStationApp* app) {
    if(!app || !app->txrx || !app->txrx->radio_device ||
       app->txrx->txrx_state == WSTxRxStateSleep) return;
    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = WSTxRxStateIDLE;
}

void ws_rx_end(WeatherStationApp* app) {
    if(!app || !app->txrx || !app->txrx->radio_device) return;
    if(app->txrx->txrx_state != WSTxRxStateRx) return;

    /* First stop the radio ISR, then join the worker thread. The old order left
       a short window in which the ISR could still write into a worker that was
       being stopped/freed, producing UsageFault or NULL-pointer crashes later
       on the editor screen. */
    subghz_devices_stop_async_rx(app->txrx->radio_device);
    if(app->txrx->worker && subghz_worker_is_running(app->txrx->worker)) {
        subghz_worker_stop(app->txrx->worker);
    }
    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = WSTxRxStateIDLE;
}

void ws_sleep(WeatherStationApp* app) {
    if(!app || !app->txrx || !app->txrx->radio_device) return;
    subghz_devices_sleep(app->txrx->radio_device);
    app->txrx->txrx_state = WSTxRxStateSleep;
}

void ws_hopper_update(WeatherStationApp* app) {
    if(!app || !app->txrx || !app->txrx->radio_device || !app->setting ||
       !app->txrx->preset) return;

    switch(app->txrx->hopper_state) {
    case WSHopperStateOFF:
    case WSHopperStatePause:
        return;
    case WSHopperStateRSSITimeOut:
        if(app->txrx->hopper_timeout != 0) {
            app->txrx->hopper_timeout--;
            return;
        }
        break;
    default:
        break;
    }
    float rssi = -127.0f;
    if(app->txrx->hopper_state != WSHopperStateRSSITimeOut) {
        // See RSSI Calculation timings in CC1101 17.3 RSSI
        rssi = subghz_devices_get_rssi(app->txrx->radio_device);

        // Stay if RSSI is high enough
        if(rssi > -90.0f) {
            app->txrx->hopper_timeout = 10;
            app->txrx->hopper_state = WSHopperStateRSSITimeOut;
            return;
        }
    } else {
        app->txrx->hopper_state = WSHopperStateRunnig;
    }
    // Select next frequency
    if(app->txrx->hopper_idx_frequency <
       subghz_setting_get_hopper_frequency_count(app->setting) - 1) {
        app->txrx->hopper_idx_frequency++;
    } else {
        app->txrx->hopper_idx_frequency = 0;
    }

    if(app->txrx->txrx_state == WSTxRxStateRx) {
        ws_rx_end(app);
    };
    if(app->txrx->txrx_state == WSTxRxStateIDLE && app->txrx->receiver && app->txrx->worker) {
        subghz_receiver_reset(app->txrx->receiver);
        app->txrx->preset->frequency =
            subghz_setting_get_hopper_frequency(app->setting, app->txrx->hopper_idx_frequency);
        ws_rx(app, app->txrx->preset->frequency);
    }
}

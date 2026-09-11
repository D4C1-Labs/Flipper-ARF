#include "weather_editor_radio.h"
#include "protocols/protocol_items.h"
#include "protocols/ws_generic.h"
#include <lib/subghz/blocks/decoder.h>
#include <string.h>

#define TAG "WeatherEditorRadio"

typedef struct {
    const WeatherEditorRaw* raw;
    size_t index;
} WeatherEditorTxCtx;

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    WSBlockGeneric generic;
} WeatherEditorDecoderProxy;

typedef struct {
    const char* expected_protocol;
    uint64_t expected_data;
    uint8_t expected_bits;
    uint32_t expected_id;
    int32_t expected_temp_tenths;
    int32_t expected_humidity;
    uint8_t expected_channel;
    uint8_t expected_battery_low;
    bool check_id;
    bool check_temperature;
    bool check_humidity;
    bool check_channel;
    bool check_battery;
    bool decoded;
    bool payload_match;
    bool semantic_match;
    bool complete_match;
    uint64_t decoded_data;
    uint8_t decoded_bits;
    uint32_t decoded_id;
    int32_t decoded_temp_tenths;
    int32_t decoded_humidity;
    uint8_t decoded_channel;
    uint8_t decoded_battery_low;
    uint16_t decoded_frames;
} WeatherEditorLoopbackContext;

static int32_t weather_editor_float_to_tenths(float value) {
    const float scaled = value * 10.0f;
    return (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static int32_t weather_editor_temperature_tolerance_tenths(const char* protocol_name) {
    /* Acurite-986 stores whole Fahrenheit degrees, so its unavoidable
       quantisation is wider than the 0.1 C protocols. */
    return strcmp(protocol_name, "Acurite-986") == 0 ? 4 : 2;
}

static int32_t weather_editor_expected_humidity(
    const char* protocol_name,
    int32_t humidity) {
    /* Match the published/local decoder presentation rules. The payload is
       still checked bit-for-bit independently. */
    if(strcmp(protocol_name, "Nexus-TH") == 0) {
        if(humidity < 20) return 20;
        if(humidity > 95) return 95;
    } else if(strcmp(protocol_name, "GT-WT02") == 0) {
        if(humidity <= 10) return 0;
        if(humidity > 90) return 100;
    } else if(strcmp(protocol_name, "GT-WT03") == 0) {
        if(humidity <= 10) return 0;
        if(humidity > 95) return 100;
    }
    return humidity;
}

static void weather_editor_loopback_process(
    SubGhzProtocolDecoderBase* decoder_base,
    WeatherEditorLoopbackContext* loopback) {
    if(!decoder_base || !decoder_base->protocol || !loopback) return;
    if(strcmp(decoder_base->protocol->name, loopback->expected_protocol) != 0) return;

    const WeatherEditorDecoderProxy* proxy = (const WeatherEditorDecoderProxy*)decoder_base;
    const WSBlockGeneric* generic = &proxy->generic;
    loopback->decoded = true;
    loopback->decoded_frames++;
    loopback->decoded_data = generic->data;
    loopback->decoded_bits = generic->data_count_bit;
    loopback->decoded_id = generic->id;
    loopback->decoded_temp_tenths = weather_editor_float_to_tenths(generic->temp);
    loopback->decoded_humidity = generic->humidity;
    loopback->decoded_channel = generic->channel;
    loopback->decoded_battery_low = generic->battery_low;

    const bool payload_match =
        (generic->data == loopback->expected_data) &&
        ((!loopback->expected_bits) || (generic->data_count_bit == loopback->expected_bits));

    bool semantic_match = true;
    if(loopback->check_id && generic->id != loopback->expected_id) semantic_match = false;
    if(loopback->check_temperature) {
        if(generic->temp == WS_NO_TEMPERATURE) {
            semantic_match = false;
        } else {
            const int32_t diff =
                loopback->decoded_temp_tenths - loopback->expected_temp_tenths;
            const int32_t tolerance =
                weather_editor_temperature_tolerance_tenths(loopback->expected_protocol);
            if(diff < -tolerance || diff > tolerance) semantic_match = false;
        }
    }
    if(loopback->check_humidity &&
       (generic->humidity == WS_NO_HUMIDITY ||
        generic->humidity != (uint8_t)loopback->expected_humidity)) {
        semantic_match = false;
    }
    if(loopback->check_channel &&
       (generic->channel == WS_NO_CHANNEL || generic->channel != loopback->expected_channel)) {
        semantic_match = false;
    }
    if(loopback->check_battery &&
       (generic->battery_low == WS_NO_BATT ||
        generic->battery_low != loopback->expected_battery_low)) {
        semantic_match = false;
    }

    loopback->payload_match = payload_match;
    loopback->semantic_match = semantic_match;
    if(payload_match && semantic_match) loopback->complete_match = true;
}

static void weather_editor_loopback_receiver_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    UNUSED(receiver);
    weather_editor_loopback_process(decoder_base, context);
}

bool weather_editor_validate_raw(
    WeatherStationApp* app,
    const WeatherEditorRaw* raw,
    const WeatherEditorState* expected_state,
    FuriString* status) {
    if(!app || !app->txrx || !app->txrx->receiver || !raw || !raw->values ||
       !expected_state || !expected_state->protocol_name) {
        if(status) furi_string_set(status, "TX test skipped: no RX core");
        return false;
    }

    const char* protocol_name = furi_string_get_cstr(expected_state->protocol_name);
    WeatherEditorLoopbackContext loopback = {
        .expected_protocol = protocol_name,
        .expected_data = expected_state->edited_data,
        .expected_bits = expected_state->bit_count,
        .expected_id = expected_state->id,
        .expected_temp_tenths = expected_state->temperature_tenths,
        .expected_humidity = weather_editor_expected_humidity(protocol_name, expected_state->humidity),
        .expected_channel = expected_state->channel,
        .expected_battery_low = (uint8_t)expected_state->battery,
        .check_id = expected_state->id != WS_NO_ID,
        .check_temperature = expected_state->has_temperature && !expected_state->overflow_wrapped,
        .check_humidity = expected_state->has_humidity && !expected_state->overflow_wrapped,
        .check_channel = expected_state->has_channel && !expected_state->overflow_wrapped,
        .check_battery = expected_state->battery_kind != WeatherEditorBatteryNone,
        .decoded = false,
        .payload_match = false,
        .semantic_match = false,
        .complete_match = false,
        .decoded_data = 0,
        .decoded_bits = 0,
        .decoded_id = 0,
        .decoded_temp_tenths = 0,
        .decoded_humidity = WS_NO_HUMIDITY,
        .decoded_channel = WS_NO_CHANNEL,
        .decoded_battery_low = WS_NO_BATT,
        .decoded_frames = 0,
    };

    /* Reuse the receiver's already allocated decoder registry. The previous
       implementation allocated a second protocol decoder for every TX, which
       could trigger furi_check/out-of-memory on a fragmented Flipper heap. */
    if(app->txrx->txrx_state == WSTxRxStateRx) ws_rx_end(app);
    subghz_receiver_set_rx_callback(
        app->txrx->receiver, weather_editor_loopback_receiver_callback, &loopback);
    subghz_receiver_reset(app->txrx->receiver);

    for(size_t i = 0; i < raw->count && !loopback.complete_match; i++) {
        const int32_t value = raw->values[i];
        if(value == 0) continue;
        subghz_receiver_decode(
            app->txrx->receiver, value > 0, (uint32_t)(value > 0 ? value : -value));
    }

    subghz_receiver_set_rx_callback(app->txrx->receiver, NULL, NULL);
    subghz_receiver_reset(app->txrx->receiver);

    if(!loopback.decoded) {
        if(status) furi_string_set(status, "TX test: no decoded frame");
        return false;
    }
    if(!loopback.payload_match) {
        if(status) {
            furi_string_printf(
                status,
                "TX test: bit mismatch\nExp:%lX%08lX/%u\nGot:%lX%08lX/%u",
                (uint32_t)(loopback.expected_data >> 32),
                (uint32_t)loopback.expected_data,
                loopback.expected_bits,
                (uint32_t)(loopback.decoded_data >> 32),
                (uint32_t)loopback.decoded_data,
                loopback.decoded_bits);
        }
        return false;
    }
    if(!loopback.semantic_match) {
        if(status) {
            furi_string_printf(
                status,
                "TX test: field mismatch\nT:%ld/%ld H:%ld/%ld\nCh:%u/%u B:%u/%u",
                (long)loopback.expected_temp_tenths,
                (long)loopback.decoded_temp_tenths,
                (long)loopback.expected_humidity,
                (long)loopback.decoded_humidity,
                loopback.expected_channel,
                loopback.decoded_channel,
                loopback.expected_battery_low,
                loopback.decoded_battery_low);
        }
        return false;
    }

    if(status) {
        furi_string_printf(
            status, "TX test OK (%u frames)\nBits and fields match", loopback.decoded_frames);
    }
    return true;
}

static LevelDuration weather_editor_tx_yield(void* context) {
    WeatherEditorTxCtx* tx = context;
    if(!tx || !tx->raw || !tx->raw->values || tx->index >= tx->raw->count) {
        return level_duration_reset();
    }
    int32_t value = tx->raw->values[tx->index++];
    if(value > 0) return level_duration_make(true, (uint32_t)value);
    if(value < 0) return level_duration_make(false, (uint32_t)(-value));
    return level_duration_reset();
}

void weather_editor_pair_callback(void* context, bool level, uint32_t duration) {
    WeatherStationApp* app = context;
    if(!app || !app->txrx || !app->txrx->receiver ||
       app->txrx->txrx_state != WSTxRxStateRx) {
        return;
    }

    /* Low-RAM mode: decode pulses directly. RX history stores only decoded
       sensor fields, never a copy of the full pulse stream. */
    subghz_receiver_decode(app->txrx->receiver, level, duration);
}

static uint8_t* weather_editor_get_preset_data(
    WeatherStationApp* app,
    const SubGhzRadioPreset* preset) {
    if(!app || !app->setting || !preset || !preset->name) return NULL;
    uint8_t* data = subghz_setting_get_preset_data_by_name(
        app->setting, furi_string_get_cstr(preset->name));
    if(!data && preset->data && preset->data_size) data = preset->data;
    return data;
}

static void weather_editor_restart_rx_if_needed(WeatherStationApp* app, bool restart_rx) {
    if(!restart_rx || !app || !app->txrx || !app->txrx->radio_device ||
       !app->txrx->worker || !app->txrx->receiver || !app->txrx->preset) {
        return;
    }

    uint8_t* preset_data = weather_editor_get_preset_data(app, app->txrx->preset);
    if(!preset_data) {
        FURI_LOG_E(TAG, "Cannot restart RX: preset data unavailable");
        return;
    }

    ws_begin(app, preset_data);
    ws_rx(app, app->txrx->preset->frequency);
}

bool weather_editor_send_raw(
    WeatherStationApp* app,
    const WeatherEditorRaw* raw,
    const SubGhzRadioPreset* preset,
    FuriString* status) {
    if(status) furi_string_reset(status);
    if(!app || !app->txrx || !raw || !raw->values || !preset || !preset->name) {
        if(status) furi_string_set(status, "No TX data");
        return false;
    }

    if(!app->txrx->radio_device) {
        if(status) furi_string_set(status, "No Sub-GHz radio");
        return false;
    }

    if(raw->count == 0) {
        if(status) furi_string_set(status, "No RAW data");
        return false;
    }

    if(!subghz_devices_is_frequency_valid(app->txrx->radio_device, preset->frequency)) {
        if(status) furi_string_set(status, "Frequency not allowed");
        return false;
    }

    uint8_t* preset_data = weather_editor_get_preset_data(app, preset);
    if(!preset_data) {
        if(status) furi_string_set(status, "No preset data");
        return false;
    }

    const bool restart_rx = app->txrx->txrx_state == WSTxRxStateRx;
    if(restart_rx) ws_rx_end(app);

    bool ok = false;
    do {
        subghz_devices_idle(app->txrx->radio_device);
        subghz_devices_reset(app->txrx->radio_device);
        subghz_devices_load_preset(
            app->txrx->radio_device, FuriHalSubGhzPresetCustom, preset_data);
        subghz_devices_set_frequency(app->txrx->radio_device, preset->frequency);

        /* set_tx performs the regional TX permission check and switches the
           CC1101 into TX mode. Starting async TX without it is not reliable. */
        if(!subghz_devices_set_tx(app->txrx->radio_device)) {
            if(status) furi_string_set(status, "TX blocked for this band");
            break;
        }

        subghz_devices_flush_tx(app->txrx->radio_device);

        WeatherEditorTxCtx tx = {.raw = raw, .index = 0};
        if(!subghz_devices_start_async_tx(
               app->txrx->radio_device, weather_editor_tx_yield, &tx)) {
            if(status) furi_string_set(status, "Cannot start TX");
            break;
        }

        uint64_t total_us = 0;
        for(size_t i = 0; i < raw->count; i++) {
            const int64_t value = raw->values[i];
            total_us += (uint64_t)(value < 0 ? -value : value);
        }

        uint32_t timeout_ms = (uint32_t)(total_us / 1000ULL) + 2000U;
        if(timeout_ms < 3000U) timeout_ms = 3000U;
        if(timeout_ms > 60000U) timeout_ms = 60000U;

        uint32_t elapsed_ms = 0;
        bool complete = subghz_devices_is_async_complete_tx(app->txrx->radio_device);
        while(!complete && elapsed_ms < timeout_ms) {
            furi_delay_ms(1);
            elapsed_ms++;
            complete = subghz_devices_is_async_complete_tx(app->txrx->radio_device);
        }

        subghz_devices_stop_async_tx(app->txrx->radio_device);
        ok = complete;
        if(status) {
            furi_string_set(
                status, ok ? "Transmission complete" : "Transmission timeout");
        }
    } while(false);

    subghz_devices_idle(app->txrx->radio_device);
    weather_editor_restart_rx_if_needed(app, restart_rx);

    if(status && !ok && furi_string_size(status) == 0) {
        furi_string_set(status, "TX error");
    }
    return ok;
}

#include "../weather_station_app_i.h"
#include "../weather_editor_radio.h"
#include "../weather_editor_tx_variant.h"
#include "../weather_editor_test.h"
#include "../protocols/ws_generic.h"

#include <storage/storage.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define TAG "WeatherEditorActions"

static const char* const weather_editor_off_on[] = {"NO", "YES"};
static const char* const weather_editor_battery_flag[] = {"OK", "LOW"};
static const char* const weather_editor_channel_modes[] = {"AUTO", "MANUAL"};

typedef enum {
    WeatherEditorActionSaveReceived = 0,
    WeatherEditorActionSendReceived,
    WeatherEditorActionTemperature,
    WeatherEditorActionMinus,
    WeatherEditorActionHumidity,
    WeatherEditorActionBattery,
    WeatherEditorActionButton,
    WeatherEditorActionChannelMode,
    WeatherEditorActionChannel,
    WeatherEditorActionSensorId,
    WeatherEditorActionFrequency,
    WeatherEditorActionSaveEdited,
    WeatherEditorActionDeleteLoadedProfile,
    WeatherEditorActionSendEdited,
    WeatherEditorActionTxInterval,
    WeatherEditorActionAutoTxToggle,
    WeatherEditorActionNone = 255,
} WeatherEditorAction;

#define WEATHER_EDITOR_CUSTOM_EVENT_REFRESH_ACTIONS (0x57455246UL)

static void weather_editor_request_actions_refresh(WeatherStationApp* app) {
    /* Do not reset/rebuild the active VariableItemList from inside
       a VariableItem callback. On ARF this can trip furi_check because the
       list model is still locked by the GUI callback. The edited values are
       already stored in app->editor; the screen is rebuilt only after a
       normal scene transition. */
    UNUSED(app);
}

static int32_t weather_editor_display_temperature_tenths(const WeatherStationApp* app) {
    if(!app || !app->editor) return 0;
    int32_t value = app->editor->temperature_tenths;
    if(app->temperature_unit == WeatherEditorTemperatureUnitFahrenheit) {
        value = weather_editor_temperature_to_fahrenheit_tenths(value);
    }
    return value;
}

/* VariableItemList przechowuje elementy w dynamicznej tablicy. Wskaznik
   zwrocony przez variable_item_list_add() moze zostac uniewazniony przez
   kazde kolejne dodanie elementu. Dlatego wskazniki sa uzywane tylko lokalnie,
   zanim do listy zostanie dodany nastepny element. */
static void weather_editor_set_temperature_text(
    WeatherStationApp* app,
    VariableItem* item) {
    if(!app || !app->editor || !item) return;
    const int32_t value = weather_editor_display_temperature_tenths(app);
    const int32_t magnitude = weather_editor_abs_saturated(value);
    char text[24];
    snprintf(
        text,
        sizeof(text),
        "%s%ld.%01ld %s",
        value < 0 ? "-" : "+",
        (long)(magnitude / 10),
        (long)(magnitude % 10),
        app->temperature_unit == WeatherEditorTemperatureUnitFahrenheit ? "F" : "C");
    variable_item_set_current_value_text(item, text);
}

static void weather_editor_set_humidity_text(
    const WeatherStationApp* app,
    VariableItem* item) {
    if(!app || !app->editor || !item) return;
    char text[24];
    snprintf(text, sizeof(text), "%ld %%", (long)app->editor->humidity);
    variable_item_set_current_value_text(item, text);
}

static void weather_editor_set_channel_text(
    const WeatherStationApp* app,
    VariableItem* item) {
    if(!app || !app->editor || !item) return;
    char text[16];
    if(app->editor_channel_auto && app->editor_received_channel != WS_NO_CHANNEL) {
        snprintf(text, sizeof(text), "RX %u", app->editor_received_channel);
    } else {
        snprintf(text, sizeof(text), "%u", app->editor->channel);
    }
    variable_item_set_current_value_text(item, text);
}

static void weather_editor_set_frequency_text(
    const WeatherStationApp* app,
    VariableItem* item) {
    if(!app || !item) return;
    char text[24];
    const uint32_t hz = app->editor_frequency_hz;
    snprintf(
        text,
        sizeof(text),
        "%lu.%03lu MHz",
        (unsigned long)(hz / 1000000UL),
        (unsigned long)((hz % 1000000UL) / 1000UL));
    variable_item_set_current_value_text(item, text);
}

static const SubGhzRadioPreset* weather_editor_get_active_preset(WeatherStationApp* app) {
    if(!app || !app->txrx) return NULL;
    if(app->editor_simulation_mode || app->editor_loaded_from_profile) return app->txrx->preset;
    if(!app->txrx->history ||
       !ws_history_is_valid_index(app->txrx->history, app->txrx->idx_menu_chosen)) {
        return NULL;
    }
    return ws_history_get_radio_preset(app->txrx->history, app->txrx->idx_menu_chosen);
}

static void weather_editor_set_auto_tx_interval_text(
    const WeatherStationApp* app,
    VariableItem* item) {
    if(!app || !item) return;
    char text[20];
    snprintf(text, sizeof(text), "%u s", app->editor_auto_tx_interval_s);
    variable_item_set_current_value_text(item, text);
}

static void weather_editor_set_toggle_text(VariableItem* item, bool enabled) {
    if(!item) return;
    const uint8_t index = enabled ? 1U : 0U;
    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, weather_editor_off_on[index]);
}

static void weather_editor_stop_auto_tx(WeatherStationApp* app) {
    if(!app) return;
    app->editor_auto_tx_enabled = false;
    app->editor_auto_tx_elapsed_ds = 0;
}

static void weather_editor_show_result(
    WeatherStationApp* app,
    const char* header,
    const char* text) {
    if(!app || !app->editor_result_title || !app->editor_result_text || !app->scene_manager) return;
    furi_string_set(app->editor_result_title, header ? header : "Weather Editor");
    furi_string_set(app->editor_result_text, text ? text : "");
    scene_manager_next_scene(app->scene_manager, WeatherStationSceneActionResult);
}

static void weather_editor_make_profile_filename(
    const WeatherEditorState* state,
    bool edited,
    FuriString* path) {
    if(!state || !path) return;
    const char* folder =
        edited ? WEATHER_EDITOR_EDITED_PROFILE_FOLDER : WEATHER_EDITOR_RX_PROFILE_FOLDER;
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        furi_string_reset(path);
        return;
    }

    furi_string_printf(
        path,
        "%s/%04u-%02u-%02u_%02u-%02u-%02u.ws",
        folder,
        (unsigned)dt.year,
        (unsigned)dt.month,
        (unsigned)dt.day,
        (unsigned)dt.hour,
        (unsigned)dt.minute,
        (unsigned)dt.second);

    for(uint16_t suffix = 2U; storage_file_exists(storage, furi_string_get_cstr(path)); suffix++) {
        furi_string_printf(
            path,
            "%s/%04u-%02u-%02u_%02u-%02u-%02u_%u.ws",
            folder,
            (unsigned)dt.year,
            (unsigned)dt.month,
            (unsigned)dt.day,
            (unsigned)dt.hour,
            (unsigned)dt.minute,
            (unsigned)dt.second,
            suffix);
        if(suffix == UINT16_MAX) break;
    }

    furi_record_close(RECORD_STORAGE);
}

static bool weather_editor_rtl433_external_protocol(const WeatherEditorState* state) {
    if(!state || !state->protocol_name) return false;
    const char* p = furi_string_get_cstr(state->protocol_name);
    return !strcmp(p, "ThermoPRO-TX4") || !strcmp(p, "Solight TE44") ||
           !strcmp(p, "GT-WT03") || !strcmp(p, "Ambient_Weather") ||
           !strcmp(p, "Auriol AHFL") || !strcmp(p, "EMOS E601x") ||
           !strcmp(p, "Oregon2") || !strcmp(p, "Oregon3") ||
           !strcmp(p, "LaCrosse_TX");
}

static void weather_editor_format_tenths(char* out, size_t out_size, int32_t value) {
    const int32_t magnitude = weather_editor_abs_saturated(value);
    snprintf(
        out,
        out_size,
        "%s%ld.%01ld",
        value < 0 ? "-" : "",
        (long)(magnitude / 10),
        (long)(magnitude % 10));
}

static void weather_editor_suspend_rx_core(WeatherStationApp* app) {
    weather_station_release_rx_core(app);
}

static bool weather_editor_ensure_tx_raw(WeatherStationApp* app) {
    if(!app) return false;
    if(app->editor_tx_raw) {
        weather_editor_raw_reset(app->editor_tx_raw);
        return true;
    }

    weather_editor_suspend_rx_core(app);
    app->editor_tx_raw = weather_editor_raw_alloc(WEATHER_EDITOR_MAX_PULSES);
    if(!app->editor_tx_raw) {
        if(app->editor_status) furi_string_set(app->editor_status, "No memory for TX buffer");
        return false;
    }
    return true;
}

bool weather_editor_send_state(
    WeatherStationApp* app,
    const WeatherEditorState* state,
    const SubGhzRadioPreset* preset) {
    if(!app || !app->txrx || !state || !state->protocol_name ||
       !app->txrx->radio_device || !app->setting || !preset || !preset->name) {
        if(app && app->editor_status) furi_string_set(app->editor_status, "No radio or TX preset");
        return false;
    }
    if(!weather_editor_ensure_tx_raw(app)) return false;
    WeatherEditorRaw* raw = app->editor_tx_raw;

    bool ok = weather_editor_encode(state, raw, app->editor_status);
    bool bresser =
        strcmp(furi_string_get_cstr(state->protocol_name), "Bresser-3CH") == 0;
    uint32_t bresser_raw = 0;
    int32_t bresser_decoded_c_tenths = 0;

    if(ok && bresser) {
        bresser_raw = (uint32_t)((state->edited_data >> 16) & 0x0FFFU);
        const int32_t decoded_f_tenths = (int32_t)bresser_raw - 900;
        bresser_decoded_c_tenths =
            weather_editor_temperature_to_celsius_tenths(decoded_f_tenths);

        /* For a non-wrapped Bresser value, encoding and decoding the 12-bit
           field must return the requested Celsius value within 0.1 C. */
        const int32_t diff = bresser_decoded_c_tenths - state->temperature_tenths;
        if(!state->overflow_wrapped && (diff < -1 || diff > 1)) {
            char requested[20];
            char decoded[20];
            weather_editor_format_tenths(requested, sizeof(requested), state->temperature_tenths);
            weather_editor_format_tenths(decoded, sizeof(decoded), bresser_decoded_c_tenths);
            furi_string_printf(
                app->editor_status,
                "Bresser temp error\nInput:%sC RAW:%03lX\nDec:%sC",
                requested,
                (unsigned long)bresser_raw,
                decoded);
            ok = false;
        }
    }

    if(ok && app->txrx->receiver) {
#if WEATHER_EDITOR_TX_VARIANT == WEATHER_EDITOR_TX_VARIANT_SDR_RTL433
        /* Walidacja lokalna jest wykonywana tylko wtedy, gdy rdzen RX jest
           dostepny. W trybie edytora dekodery sa celowo zwolnione, aby TX i
           Auto TX nie konczyly sie brakiem pamieci. LaCrosse_TX nadal wysyla
           temperature i wilgotnosc jako dwa oddzielne typy wiadomosci. */
        if(!weather_editor_rtl433_external_protocol(state)) {
            ok = weather_editor_validate_raw(app, raw, state, app->editor_status);
        }
#else
        ok = weather_editor_validate_raw(app, raw, state, app->editor_status);
#endif
    }
    if(ok) {
        ok = weather_editor_send_raw(app, raw, preset, app->editor_status);
    }

    if(ok && bresser) {
        char requested[20];
        char decoded[20];
        weather_editor_format_tenths(requested, sizeof(requested), state->temperature_tenths);
        weather_editor_format_tenths(decoded, sizeof(decoded), bresser_decoded_c_tenths);
        furi_string_printf(
            app->editor_status,
            "TX OK\nInput:%sC RAW:%03lX\nDec:%sC",
            requested,
            (unsigned long)bresser_raw,
            decoded);
    } else if(ok) {
        furi_string_set(app->editor_status, "TX OK\nFrame encoded");
    }

    /* Bufor pozostaje przydzielony do ponownego SEND i Auto TX. Zostanie
       zwolniony automatycznie przed ponownym uruchomieniem odbiornika. */
    return ok;
}

static void weather_editor_set_temperature_from_display(
    WeatherStationApp* app,
    int32_t display_tenths) {
    if(!app || !app->editor) return;
    if(app->temperature_unit == WeatherEditorTemperatureUnitFahrenheit) {
        app->editor->temperature_tenths =
            weather_editor_temperature_to_celsius_tenths(display_tenths);
    } else {
        app->editor->temperature_tenths = display_tenths;
    }
}

static void weather_editor_minus_changed(VariableItem* item) {
    if(!item) return;
    WeatherStationApp* app = variable_item_get_context(item);
    if(!app || !app->editor || !app->editor->has_temperature) return;
    uint8_t index = variable_item_get_current_value_index(item);
    if(index > 1U) index = 0U;
    const int32_t current = weather_editor_display_temperature_tenths(app);
    const int32_t magnitude = weather_editor_abs_saturated(current);
    app->temperature_negative = index == 1U;
    weather_editor_set_temperature_from_display(
        app, app->temperature_negative ? -magnitude : magnitude);
    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, weather_editor_off_on[index]);
    /* Odswiez Temperature dopiero po wyjsciu z callbacku VariableItemList.
       Resetowanie lub dotykanie innych elementow w tym callbacku jest
       niebezpieczne, bo lista trzyma elementy w relokowanej tablicy. */
    weather_editor_request_actions_refresh(app);
}

static void weather_editor_battery_changed(VariableItem* item) {
    if(!item) return;
    WeatherStationApp* app = variable_item_get_context(item);
    if(!app || !app->editor || app->editor->battery_kind != WeatherEditorBatteryFlag) return;
    uint8_t index = variable_item_get_current_value_index(item);
    if(index > 1U) index = 0U;
    app->editor->battery = index ? 1 : 0;
    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, weather_editor_battery_flag[index]);
}

static void weather_editor_button_changed(VariableItem* item) {
    if(!item) return;
    WeatherStationApp* app = variable_item_get_context(item);
    if(!app || !app->editor || !app->editor->has_button) return;
    uint8_t index = variable_item_get_current_value_index(item);
    if(index > 1U) index = 0U;
    app->editor->button = index ? 1U : 0U;
    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, weather_editor_off_on[index]);
}

static void weather_editor_channel_mode_changed(VariableItem* item) {
    if(!item) return;
    WeatherStationApp* app = variable_item_get_context(item);
    if(!app || !app->editor || !app->editor->has_channel) return;

    uint8_t index = variable_item_get_current_value_index(item);
    if(index > 1U) index = 0U;
    app->editor_channel_auto = index == 0U;

    if(app->editor_channel_auto) {
        if(app->editor_received_channel != WS_NO_CHANNEL) {
            app->editor->channel = app->editor_received_channel;
        }
    } else {
        int32_t candidate = app->editor->channel;
        if(candidate == WS_NO_CHANNEL) candidate = app->editor_received_channel;
        if(candidate == WS_NO_CHANNEL) {
            int32_t min_channel = 0;
            int32_t max_channel = 0;
            weather_editor_get_channel_limits(app->editor, &min_channel, &max_channel);
            UNUSED(max_channel);
            candidate = min_channel;
        }
        app->editor->channel = weather_editor_sanitize_channel(app->editor, candidate);
    }

    variable_item_set_current_value_index(item, index);
    variable_item_set_current_value_text(item, weather_editor_channel_modes[index]);
    weather_editor_request_actions_refresh(app);
}

static void weather_editor_auto_tx_changed(VariableItem* item) {
    if(!item) return;
    WeatherStationApp* app = variable_item_get_context(item);
    if(!app || !app->editor || !app->editor->encoder_available) return;
    uint8_t index = variable_item_get_current_value_index(item);
    if(index > 1U) index = 0U;

    app->editor_auto_tx_enabled = index == 1U;
    /* Pierwszy TX nastapi przy najblizszym ticku, potem po wpisanym odstepie. */
    app->editor_auto_tx_elapsed_ds = app->editor_auto_tx_enabled ?
                                         (uint32_t)app->editor_auto_tx_interval_s * 10U :
                                         0U;
    weather_editor_set_toggle_text(item, app->editor_auto_tx_enabled);
}

static void weather_editor_execute_action(WeatherStationApp* app, uint32_t index) {
    if(!app || !app->editor || !app->txrx || !app->rx_menu_text ||
       !app->editor_status || !app->scene_manager) return;
    /* RX is paused in this scene, so its persistent scratch string can safely
       serve as the profile path without another heap allocation. */
    FuriString* path = app->rx_menu_text;
    furi_string_reset(path);
    const SubGhzRadioPreset* received_preset = weather_editor_get_active_preset(app);
    SubGhzRadioPreset edited_preset = {0};
    const SubGhzRadioPreset* preset = received_preset;
    if(received_preset) {
        edited_preset = *received_preset;
        edited_preset.frequency = app->editor_frequency_hz;
        preset = &edited_preset;
    }
    bool ok = false;

    switch(index) {
    case WeatherEditorActionSaveReceived: {
        const WeatherEditorState* received = app->editor_rx_snapshot;
        if(received && received_preset && furi_string_size(received->protocol_name) > 0U) {
            weather_editor_make_profile_filename(received, false, path);
            ok = furi_string_size(path) > 0 && weather_editor_save_key_sub(
                furi_string_get_cstr(path), received_preset, received, false, app->editor_status);
            weather_editor_show_result(
                app,
                ok ? "RX saved" : "Save error",
                ok ? furi_string_get_cstr(path) : furi_string_get_cstr(app->editor_status));
        } else {
            weather_editor_show_result(app, "No data", "Receive the station again");
        }
        break;
    }

    case WeatherEditorActionSendReceived: {
        const WeatherEditorState* received = app->editor_rx_snapshot;
        if(received && furi_string_size(received->protocol_name) > 0U) {
            if(received->encoder_available && preset) {
                ok = weather_editor_send_state(app, received, preset);
                if(ok) {
                    notification_message(app->notifications, &sequence_blink_green_10);
                } else {
                    weather_editor_show_result(
                        app, "TX error", furi_string_get_cstr(app->editor_status));
                }
            } else {
                weather_editor_show_result(
                    app,
                    preset ? "No encoder" : "No preset",
                    preset ? "RX can only be saved" : "Receive the station again");
            }
        } else {
            weather_editor_show_result(app, "No data", "Receive the station again");
        }
        break;
    }

    case WeatherEditorActionTemperature:
        app->editor_input_target = WeatherEditorInputTemperature;
        scene_manager_next_scene(app->scene_manager, WeatherStationSceneNumberInput);
        break;

    case WeatherEditorActionHumidity:
        app->editor_input_target = WeatherEditorInputHumidity;
        scene_manager_next_scene(app->scene_manager, WeatherStationSceneNumberInput);
        break;

    case WeatherEditorActionBattery:
        if(app->editor->battery_kind == WeatherEditorBatteryPercent) {
            app->editor_input_target = WeatherEditorInputBattery;
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneNumberInput);
        }
        break;

    case WeatherEditorActionChannel:
        if(app->editor_channel_auto) {
            weather_editor_show_result(app, "AUTO channel", "Using the received channel");
        } else {
            app->editor_input_target = WeatherEditorInputChannel;
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneNumberInput);
        }
        break;

    case WeatherEditorActionSensorId:
        if(app->editor_simulation_mode &&
           weather_editor_simulation_id_editable(app->editor_simulation_protocol_index)) {
            weather_editor_stop_auto_tx(app);
            app->editor_input_target = WeatherEditorInputSensorId;
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneNumberInput);
        }
        break;

    case WeatherEditorActionFrequency:
        weather_editor_stop_auto_tx(app);
        app->editor_input_target = WeatherEditorInputEditorFrequencyKHz;
        scene_manager_next_scene(app->scene_manager, WeatherStationSceneNumberInput);
        break;

    case WeatherEditorActionSaveEdited:
        if(!preset) {
            weather_editor_show_result(app, "No preset", "Receive the station again");
            break;
        }
        if(app->editor_channel_auto && app->editor_received_channel != WS_NO_CHANNEL)
            app->editor->channel = app->editor_received_channel;
        if(!weather_editor_ensure_tx_raw(app) ||
           !weather_editor_encode(app->editor, app->editor_tx_raw, app->editor_status)) {
            weather_editor_show_result(app, "Encoding error", furi_string_get_cstr(app->editor_status));
            break;
        }
        weather_editor_make_profile_filename(app->editor, true, path);
        ok = furi_string_size(path) > 0 && weather_editor_save_key_sub(
            furi_string_get_cstr(path), preset, app->editor, true, app->editor_status);
        weather_editor_show_result(
            app,
            ok ? "Edit saved" : "Save error",
            ok ? furi_string_get_cstr(path) : furi_string_get_cstr(app->editor_status));
        break;

    case WeatherEditorActionDeleteLoadedProfile: {
        if(!app->editor_loaded_from_profile || !app->loaded_profile_path ||
           furi_string_size(app->loaded_profile_path) == 0U) {
            weather_editor_show_result(app, "No saved file", "The file was already deleted.");
            break;
        }
        Storage* storage = furi_record_open(RECORD_STORAGE);
        bool removed = false;
        if(storage) {
            removed = storage_common_remove(
                          storage, furi_string_get_cstr(app->loaded_profile_path)) == FSE_OK;
            furi_record_close(RECORD_STORAGE);
        }
        if(removed) furi_string_reset(app->loaded_profile_path);
        weather_editor_show_result(
            app, removed ? "Saved file deleted" : "Delete error",
            removed ? "The saved file was deleted." : "Cannot delete the file.");
        break;
    }

    case WeatherEditorActionSendEdited:
        if(!preset) {
            weather_editor_show_result(app, "No preset", "Receive the station again");
            break;
        }
        if(app->editor_channel_auto && app->editor_received_channel != WS_NO_CHANNEL)
            app->editor->channel = app->editor_received_channel;
        ok = weather_editor_send_state(app, app->editor, preset);
        /* After a successful TX, keep "Send edit" selected so the same signal
           can be sent again immediately. Show the result screen only on error
           because a failed transmission should not go unnoticed. */
        if(ok) {
            notification_message(app->notifications, &sequence_blink_green_10);
        } else {
            weather_editor_show_result(
                app, "TX error", furi_string_get_cstr(app->editor_status));
        }
        break;

    case WeatherEditorActionTxInterval:
        weather_editor_stop_auto_tx(app);
        app->editor_input_target = WeatherEditorInputTxInterval;
        scene_manager_next_scene(app->scene_manager, WeatherStationSceneNumberInput);
        break;

    case WeatherEditorActionAutoTxToggle:
        break;

    case WeatherEditorActionMinus:
    case WeatherEditorActionButton:
    case WeatherEditorActionChannelMode:
    case WeatherEditorActionNone:
    default:
        break;
    }

}

static void weather_editor_enter_callback(void* context, uint32_t index) {
    WeatherStationApp* app = context;
    if(!app || !app->scene_manager || !app->editor) return;
    scene_manager_set_scene_state(app->scene_manager, WeatherStationSceneActions, index);
    if(index < app->editor_action_count) {
        weather_editor_execute_action(app, app->editor_action_map[index]);
    }
}

static void weather_editor_map_action(WeatherStationApp* app, uint32_t action) {
    if(app->editor_action_count < COUNT_OF(app->editor_action_map)) {
        app->editor_action_map[app->editor_action_count++] = (uint8_t)action;
    }
}

static VariableItem* weather_editor_add_readonly_item(
    VariableItemList* list,
    const char* label,
    uint32_t action,
    WeatherStationApp* app) {
    if(!list || !label || !app) return NULL;
    VariableItem* item = variable_item_list_add(list, label, 1, NULL, app);
    if(item) weather_editor_map_action(app, action);
    return item;
}

void weather_station_scene_actions_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->editor || !app->editor->protocol_name ||
       !weather_station_ensure_variable_item_list(app)) {
        return;
    }

    /* Najpierw zwolnij RX, dopiero potem przebuduj liste. To daje zapas heapu
       przy zmianie temperatury/minusa, powrocie z NumberInput i Auto TX. */
    weather_editor_suspend_rx_core(app);

    VariableItemList* list = app->variable_item_list;
    variable_item_list_reset(list);
    app->editor_action_count = 0;

    if(app->editor && app->editor->has_channel) {
        if(app->editor_channel_auto && app->editor_received_channel != WS_NO_CHANNEL) {
            app->editor->channel = app->editor_received_channel;
        } else if(!app->editor_channel_auto) {
            app->editor->channel =
                weather_editor_sanitize_channel(app->editor, app->editor->channel);
        }
    }

    VariableItem* protocol_item = weather_editor_add_readonly_item(
        list, "Protocol", WeatherEditorActionNone, app);
    if(protocol_item) {
        variable_item_set_current_value_text(
            protocol_item, furi_string_get_cstr(app->editor->protocol_name));
    }

    const uint32_t id_action =
        app->editor_simulation_mode &&
                weather_editor_simulation_id_editable(app->editor_simulation_protocol_index) ?
            WeatherEditorActionSensorId : WeatherEditorActionNone;
    VariableItem* id_item = weather_editor_add_readonly_item(
        list, "Sensor ID", id_action, app);
    char id_text[20];
    snprintf(id_text, sizeof(id_text), "0x%08lX", (unsigned long)app->editor->id);
    if(id_item) variable_item_set_current_value_text(id_item, id_text);

    VariableItem* bit_item = weather_editor_add_readonly_item(
        list, "Frame bits", WeatherEditorActionNone, app);
    char bit_text[8];
    snprintf(bit_text, sizeof(bit_text), "%u", app->editor->bit_count);
    if(bit_item) variable_item_set_current_value_text(bit_item, bit_text);

    VariableItem* frequency_item = weather_editor_add_readonly_item(
        list, "TX frequency", WeatherEditorActionFrequency, app);
    weather_editor_set_frequency_text(app, frequency_item);

    if(!app->editor_loaded_from_profile && !app->editor_simulation_mode) {
        weather_editor_add_readonly_item(
            list, "Save RX", WeatherEditorActionSaveReceived, app);
        if(app->editor->encoder_available) {
            weather_editor_add_readonly_item(
                list, "Send RX data", WeatherEditorActionSendReceived, app);
        }
    }

    if(app->editor->has_temperature) {
        const int32_t display = weather_editor_display_temperature_tenths(app);
        app->temperature_negative = display < 0;

        VariableItem* temperature_item = weather_editor_add_readonly_item(
            list, "Temperature", WeatherEditorActionTemperature, app);
        weather_editor_set_temperature_text(app, temperature_item);

        VariableItem* minus_item = variable_item_list_add(
            list, "Minus", 2, weather_editor_minus_changed, app);
        if(minus_item) {
            weather_editor_map_action(app, WeatherEditorActionMinus);
            weather_editor_set_toggle_text(minus_item, app->temperature_negative);
        }
    }

    if(app->editor->has_humidity) {
        VariableItem* humidity_item = weather_editor_add_readonly_item(
            list, "Humidity", WeatherEditorActionHumidity, app);
        weather_editor_set_humidity_text(app, humidity_item);
    }

    if(app->editor->battery_kind == WeatherEditorBatteryFlag) {
        VariableItem* battery_item = variable_item_list_add(
            list, "Battery", 2, weather_editor_battery_changed, app);
        if(battery_item) {
            weather_editor_map_action(app, WeatherEditorActionBattery);
            const uint8_t index = app->editor->battery ? 1U : 0U;
            variable_item_set_current_value_index(battery_item, index);
            variable_item_set_current_value_text(
                battery_item, weather_editor_battery_flag[index]);
        }
    } else if(app->editor->battery_kind == WeatherEditorBatteryPercent) {
        VariableItem* battery_item = weather_editor_add_readonly_item(
            list, "Battery", WeatherEditorActionBattery, app);
        char text[16];
        snprintf(text, sizeof(text), "%ld %%", (long)app->editor->battery);
        if(battery_item) variable_item_set_current_value_text(battery_item, text);
    }

    if(app->editor->has_button) {
        VariableItem* button_item = variable_item_list_add(
            list, "Button", 2, weather_editor_button_changed, app);
        if(button_item) {
            weather_editor_map_action(app, WeatherEditorActionButton);
            const uint8_t index = app->editor->button ? 1U : 0U;
            variable_item_set_current_value_index(button_item, index);
            variable_item_set_current_value_text(button_item, weather_editor_off_on[index]);
        }
    }

    if(app->editor->has_channel) {
        VariableItem* channel_mode_item = variable_item_list_add(
            list, "Channel mode", 2, weather_editor_channel_mode_changed, app);
        if(channel_mode_item) {
            weather_editor_map_action(app, WeatherEditorActionChannelMode);
            const uint8_t mode_index = app->editor_channel_auto ? 0U : 1U;
            variable_item_set_current_value_index(channel_mode_item, mode_index);
            variable_item_set_current_value_text(
                channel_mode_item, weather_editor_channel_modes[mode_index]);
        }

        VariableItem* channel_item = weather_editor_add_readonly_item(
            list, "Channel", WeatherEditorActionChannel, app);
        weather_editor_set_channel_text(app, channel_item);
    }

    if(app->editor->encoder_available) {
        weather_editor_add_readonly_item(
            list, app->editor_simulation_mode ? "Save" : "Save edit",
            WeatherEditorActionSaveEdited, app);
        if(app->editor_loaded_from_profile && app->loaded_profile_path &&
           furi_string_size(app->loaded_profile_path) > 0U) {
            weather_editor_add_readonly_item(
                list, "Delete saved", WeatherEditorActionDeleteLoadedProfile, app);
        }
        weather_editor_add_readonly_item(
            list, app->editor_simulation_mode ? "Send" : "Send edit",
            WeatherEditorActionSendEdited, app);

        VariableItem* interval_item = weather_editor_add_readonly_item(
            list, "Auto TX interval", WeatherEditorActionTxInterval, app);
        weather_editor_set_auto_tx_interval_text(app, interval_item);

        VariableItem* auto_tx_item = variable_item_list_add(
            list, "Auto TX", 2, weather_editor_auto_tx_changed, app);
        if(auto_tx_item) {
            weather_editor_map_action(app, WeatherEditorActionAutoTxToggle);
            weather_editor_set_toggle_text(auto_tx_item, app->editor_auto_tx_enabled);
        }

    }

    variable_item_list_set_enter_callback(list, weather_editor_enter_callback, app);
    const uint32_t selected =
        scene_manager_get_scene_state(app->scene_manager, WeatherStationSceneActions);
    if(selected < app->editor_action_count) {
        variable_item_list_set_selected_item(list, (uint8_t)selected);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewVariableItemList);
    weather_station_release_inactive_gui_views(app, WeatherStationViewVariableItemList);
}

static void weather_editor_refresh_actions_scene(WeatherStationApp* app) {
    if(!app || !app->scene_manager || !app->variable_item_list) return;
    const uint8_t selected =
        variable_item_list_get_selected_item_index(app->variable_item_list);
    scene_manager_set_scene_state(
        app->scene_manager, WeatherStationSceneActions, selected);
    weather_station_scene_actions_on_enter(app);
}

bool weather_station_scene_actions_on_event(void* context, SceneManagerEvent event) {
    WeatherStationApp* app = context;
    if(!app || !app->editor) return false;

    if(event.type == SceneManagerEventTypeCustom &&
       event.event == WEATHER_EDITOR_CUSTOM_EVENT_REFRESH_ACTIONS) {
        weather_editor_refresh_actions_scene(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeBack && app->editor_auto_tx_enabled) {
        /* Pierwszy BACK zatrzymuje automat i pozostawia ekran edycji. */
        weather_editor_stop_auto_tx(app);
        weather_editor_request_actions_refresh(app);
        return true;
    }

    if(event.type == SceneManagerEventTypeTick && app->editor_auto_tx_enabled) {
        const uint32_t interval_ds = (uint32_t)app->editor_auto_tx_interval_s * 10U;
        if(app->editor_auto_tx_elapsed_ds < interval_ds) {
            app->editor_auto_tx_elapsed_ds++;
        }

        if(app->editor_auto_tx_elapsed_ds >= interval_ds) {
            app->editor_auto_tx_elapsed_ds = 0;
            if(app->editor_channel_auto && app->editor_received_channel != WS_NO_CHANNEL) {
                app->editor->channel = app->editor_received_channel;
            }
            const SubGhzRadioPreset* base_preset = weather_editor_get_active_preset(app);
            SubGhzRadioPreset tx_preset = {0};
            const SubGhzRadioPreset* active_preset = base_preset;
            if(base_preset) {
                tx_preset = *base_preset;
                tx_preset.frequency = app->editor_frequency_hz;
                active_preset = &tx_preset;
            }
            const bool ok = active_preset &&
                            weather_editor_send_state(app, app->editor, active_preset);
            if(!active_preset) furi_string_set(app->editor_status, "No TX preset");
            if(ok) {
                notification_message(app->notifications, &sequence_blink_green_10);
            } else {
                weather_editor_stop_auto_tx(app);
                weather_editor_show_result(
                    app, "Auto TX stopped", furi_string_get_cstr(app->editor_status));
            }
        }
        return true;
    }

    return false;
}

void weather_station_scene_actions_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(!app) return;
    /* Do not free the active VariableItemList here. The destination scene
       switches to its own view first and then releases this inactive module. */
    app->editor_auto_tx_enabled = false;
    app->editor_auto_tx_elapsed_ds = 0;
    app->editor_action_count = 0;
}

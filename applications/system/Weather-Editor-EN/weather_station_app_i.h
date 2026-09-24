#pragma once

#include "helpers/weather_station_types.h"

#include "scenes/weather_station_scene.h"
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/number_input.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>
#include "views/weather_station_receiver.h"
#include "views/weather_station_receiver_info.h"
#include "weather_station_history.h"
#include "weather_editor_engine.h"
#include "weather_editor_settings.h"

#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/registry.h>

#include "helpers/radio_device_loader.h"

typedef struct WeatherStationApp WeatherStationApp;

#define WEATHER_EDITOR_MAX_SAVED_PROFILES 64U

typedef enum {
    WeatherEditorInputTemperature = 0,
    WeatherEditorInputHumidity,
    WeatherEditorInputBattery,
    WeatherEditorInputChannel,
    WeatherEditorInputSensorId,
    WeatherEditorInputTxInterval,
    WeatherEditorInputEditorFrequencyKHz,
} WeatherEditorInputTarget;

typedef enum {
    WeatherEditorTemperatureUnitCelsius = 0,
    WeatherEditorTemperatureUnitFahrenheit,
} WeatherEditorTemperatureUnit;

struct WeatherStationTxRx {
    SubGhzWorker* worker;

    const SubGhzDevice* radio_device;
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzRadioPreset* preset;
    WSHistory* history;
    uint16_t idx_menu_chosen;
    WSTxRxState txrx_state;
    WSHopperState hopper_state;
    uint8_t hopper_timeout;
    uint8_t hopper_idx_frequency;
    WSRxKeyState rx_key_state;
};

typedef struct WeatherStationTxRx WeatherStationTxRx;

struct WeatherStationApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    WeatherStationTxRx* txrx;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    VariableItemList* variable_item_list;
    Submenu* submenu;
    Widget* widget;
    NumberInput* number_input;
    TextInput* text_input;
    char editor_text_input_buffer[12];
    WSReceiver* ws_receiver;
    WSReceiverInfo* ws_receiver_info;
    WeatherStationView gui_cleanup_active_view;
    bool gui_cleanup_pending;
    uint8_t gui_cleanup_delay_ticks;
    WSLock lock;
    SubGhzSetting* setting;
    WeatherEditorState* editor;
    WeatherEditorState* editor_rx_snapshot;
    /* Jeden bufor TX uzywany ponownie w edytorze i Auto TX.
       Powstaje dopiero po zwolnieniu dekoderow RX. */
    WeatherEditorRaw* editor_tx_raw;
    FuriString* editor_status;
    FuriString* editor_result_title;
    FuriString* editor_result_text;
    /* Persistent RX UI scratch strings: avoid heap churn on every decoded frame. */
    FuriString* rx_menu_text;
    FuriString* rx_frequency_text;
    FuriString* rx_modulation_text;
    FuriString* rx_history_text;
    WeatherEditorInputTarget editor_input_target;
    WeatherEditorTemperatureUnit temperature_unit;
    bool temperature_negative;
    WeatherEditorSettings editor_settings;
    bool editor_channel_auto;
    uint8_t editor_received_channel;
    uint8_t editor_action_map[24];
    uint8_t editor_action_count;
    bool editor_loaded_from_profile;
    bool editor_simulation_mode;
    uint8_t editor_simulation_protocol_index;
    uint8_t* loaded_profile_preset_data;
    FuriString* loaded_profile_path;
    FuriString* saved_profile_paths[WEATHER_EDITOR_MAX_SAVED_PROFILES];
    uint8_t saved_profile_count;
    uint16_t saved_profile_total_count;
    uint16_t saved_profile_page;
    uint8_t saved_profile_source; /* 0=wybor, 1=ORG (RX), 2=MOD (edycja) */
    uint32_t editor_frequency_hz;
    uint16_t editor_auto_tx_interval_s;
    uint32_t editor_auto_tx_elapsed_ds;
    bool editor_auto_tx_enabled;
    bool subghz_devices_initialized;

    bool lab_unlocked;
    uint8_t lab_about_taps;
    uint32_t lab_about_last_tick;
    int32_t lab_target_temperature_tenths;
    int32_t lab_target_humidity;
    uint8_t lab_sensor_count;
    uint32_t lab_sensor_keys[10];
    uint32_t lab_phase_started_tick;
    uint32_t lab_round;
    uint8_t lab_last_tx_ok;
    WeatherEditorState* lab_state;
    FlipperFormat* lab_format;
    SubGhzRadioPreset* lab_preset;
    uint8_t* lab_loaded_preset_data;

};

/* GUI modules are allocated lazily. Inactive modules are released only after
   a delayed dispatcher tick, never while an input/model callback is active. */
bool weather_station_ensure_submenu(WeatherStationApp* app);
void weather_station_release_submenu(WeatherStationApp* app);
bool weather_station_ensure_variable_item_list(WeatherStationApp* app);
void weather_station_release_variable_item_list(WeatherStationApp* app);
bool weather_station_ensure_widget(WeatherStationApp* app);
void weather_station_release_widget(WeatherStationApp* app);
bool weather_station_ensure_number_input(WeatherStationApp* app);
void weather_station_release_number_input(WeatherStationApp* app);
bool weather_station_ensure_text_input(WeatherStationApp* app);
void weather_station_release_text_input(WeatherStationApp* app);
bool weather_station_ensure_receiver_view(WeatherStationApp* app);
void weather_station_release_receiver_view(WeatherStationApp* app);
bool weather_station_ensure_receiver_info_view(WeatherStationApp* app);
void weather_station_release_receiver_info_view(WeatherStationApp* app);
void weather_station_release_inactive_gui_views(
    WeatherStationApp* app,
    WeatherStationView active_view);
bool weather_station_ensure_radio_core(WeatherStationApp* app);
void weather_station_release_rx_core(WeatherStationApp* app);

void weather_lab_clear_session_files(void);
bool weather_lab_prepare(WeatherStationApp* app);
void weather_lab_release(WeatherStationApp* app, bool clear_files);
bool weather_lab_start_rx(WeatherStationApp* app);
void weather_lab_stop_rx(WeatherStationApp* app);
uint8_t weather_lab_send_burst(WeatherStationApp* app);

void ws_preset_init(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size);
bool ws_set_preset(WeatherStationApp* app, const char* preset);
void ws_get_frequency_modulation(
    WeatherStationApp* app,
    FuriString* frequency,
    FuriString* modulation);
void ws_begin(WeatherStationApp* app, uint8_t* preset_data);
uint32_t ws_rx(WeatherStationApp* app, uint32_t frequency);
void ws_idle(WeatherStationApp* app);
void ws_rx_end(WeatherStationApp* app);
void ws_sleep(WeatherStationApp* app);
void ws_hopper_update(WeatherStationApp* app);

/* Shared TX helper used by the editor and protocol test. */
bool weather_editor_send_state(
    WeatherStationApp* app,
    const WeatherEditorState* state,
    const SubGhzRadioPreset* preset);

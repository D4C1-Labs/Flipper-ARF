#include "weather_station_app_i.h"

#include <furi.h>
#include <furi_hal.h>
#include <string.h>
#include "protocols/protocol_items.h"
#include "weather_editor_radio.h"

static void weather_station_release_inactive_gui_views_now(
    WeatherStationApp* app,
    WeatherStationView active_view);

static bool weather_station_app_custom_event_callback(void* context, uint32_t event) {
    WeatherStationApp* app = context;
    return app && app->scene_manager ?
               scene_manager_handle_custom_event(app->scene_manager, event) :
               false;
}

static bool weather_station_app_back_event_callback(void* context) {
    WeatherStationApp* app = context;
    return app && app->scene_manager ? scene_manager_handle_back_event(app->scene_manager) : false;
}

static void weather_station_app_tick_event_callback(void* context) {
    WeatherStationApp* app = context;
    if(!app) return;

    /* A VariableItemList changes scene from inside a locked model callback.
       Also, ViewDispatcher may still deliver the key-release event to the
       previous view. Keep that previous GUI object alive for one second and
       release it only from a later dispatcher tick. This avoids both freeing
       a locked model and sending KeyRelease to freed memory. */
    if(app->gui_cleanup_pending) {
        if(app->gui_cleanup_delay_ticks > 0U) {
            app->gui_cleanup_delay_ticks--;
        } else {
            const WeatherStationView active_view = app->gui_cleanup_active_view;
            app->gui_cleanup_pending = false;
            weather_station_release_inactive_gui_views_now(app, active_view);
        }
    }

    if(app->scene_manager) scene_manager_handle_tick_event(app->scene_manager);
}

static void weather_station_worker_overrun_callback(void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->txrx || !app->txrx->receiver) return;
    subghz_receiver_reset(app->txrx->receiver);
}

bool weather_station_ensure_submenu(WeatherStationApp* app) {
    if(!app) return false;
    if(app->submenu) return true;
    app->submenu = submenu_alloc();
    if(!app->submenu) return false;
    view_dispatcher_add_view(
        app->view_dispatcher, WeatherStationViewSubmenu, submenu_get_view(app->submenu));
    return true;
}

void weather_station_release_submenu(WeatherStationApp* app) {
    if(!app || !app->submenu) return;
    view_dispatcher_remove_view(app->view_dispatcher, WeatherStationViewSubmenu);
    submenu_free(app->submenu);
    app->submenu = NULL;
}

bool weather_station_ensure_variable_item_list(WeatherStationApp* app) {
    if(!app) return false;
    if(app->variable_item_list) return true;
    app->variable_item_list = variable_item_list_alloc();
    if(!app->variable_item_list) return false;
    view_dispatcher_add_view(
        app->view_dispatcher,
        WeatherStationViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));
    return true;
}

void weather_station_release_variable_item_list(WeatherStationApp* app) {
    if(!app || !app->variable_item_list) return;
    view_dispatcher_remove_view(app->view_dispatcher, WeatherStationViewVariableItemList);
    variable_item_list_free(app->variable_item_list);
    app->variable_item_list = NULL;
}

bool weather_station_ensure_widget(WeatherStationApp* app) {
    if(!app) return false;
    if(app->widget) return true;
    app->widget = widget_alloc();
    if(!app->widget) return false;
    view_dispatcher_add_view(
        app->view_dispatcher, WeatherStationViewWidget, widget_get_view(app->widget));
    return true;
}

void weather_station_release_widget(WeatherStationApp* app) {
    if(!app || !app->widget) return;
    view_dispatcher_remove_view(app->view_dispatcher, WeatherStationViewWidget);
    widget_free(app->widget);
    app->widget = NULL;
}

bool weather_station_ensure_number_input(WeatherStationApp* app) {
    if(!app) return false;
    if(app->number_input) return true;
    app->number_input = number_input_alloc();
    if(!app->number_input) return false;
    view_dispatcher_add_view(
        app->view_dispatcher,
        WeatherStationViewNumberInput,
        number_input_get_view(app->number_input));
    return true;
}

void weather_station_release_number_input(WeatherStationApp* app) {
    if(!app || !app->number_input) return;
    view_dispatcher_remove_view(app->view_dispatcher, WeatherStationViewNumberInput);
    number_input_free(app->number_input);
    app->number_input = NULL;
}


bool weather_station_ensure_text_input(WeatherStationApp* app) {
    if(!app) return false;
    if(app->text_input) return true;
    app->text_input = text_input_alloc();
    if(!app->text_input) return false;
    view_dispatcher_add_view(
        app->view_dispatcher,
        WeatherStationViewTextInput,
        text_input_get_view(app->text_input));
    return true;
}

void weather_station_release_text_input(WeatherStationApp* app) {
    if(!app || !app->text_input) return;
    view_dispatcher_remove_view(app->view_dispatcher, WeatherStationViewTextInput);
    text_input_free(app->text_input);
    app->text_input = NULL;
}

bool weather_station_ensure_receiver_view(WeatherStationApp* app) {
    if(!app) return false;
    if(app->ws_receiver) return true;
    app->ws_receiver = ws_view_receiver_alloc();
    if(!app->ws_receiver) return false;
    view_dispatcher_add_view(
        app->view_dispatcher,
        WeatherStationViewReceiver,
        ws_view_receiver_get_view(app->ws_receiver));
    return true;
}

void weather_station_release_receiver_view(WeatherStationApp* app) {
    if(!app || !app->ws_receiver) return;
    view_dispatcher_remove_view(app->view_dispatcher, WeatherStationViewReceiver);
    ws_view_receiver_free(app->ws_receiver);
    app->ws_receiver = NULL;
}

bool weather_station_ensure_receiver_info_view(WeatherStationApp* app) {
    if(!app) return false;
    if(app->ws_receiver_info) return true;
    app->ws_receiver_info = ws_view_receiver_info_alloc();
    if(!app->ws_receiver_info) return false;
    view_dispatcher_add_view(
        app->view_dispatcher,
        WeatherStationViewReceiverInfo,
        ws_view_receiver_info_get_view(app->ws_receiver_info));
    return true;
}

void weather_station_release_receiver_info_view(WeatherStationApp* app) {
    if(!app || !app->ws_receiver_info) return;
    view_dispatcher_remove_view(app->view_dispatcher, WeatherStationViewReceiverInfo);
    ws_view_receiver_info_free(app->ws_receiver_info);
    app->ws_receiver_info = NULL;
}

static void weather_station_release_inactive_gui_views_now(
    WeatherStationApp* app,
    WeatherStationView active_view) {
    if(!app || !app->view_dispatcher) return;

    if(active_view != WeatherStationViewSubmenu) weather_station_release_submenu(app);
    if(active_view != WeatherStationViewVariableItemList)
        weather_station_release_variable_item_list(app);
    if(active_view != WeatherStationViewWidget) weather_station_release_widget(app);
    if(active_view != WeatherStationViewNumberInput) weather_station_release_number_input(app);
    if(active_view != WeatherStationViewTextInput) weather_station_release_text_input(app);
    if(active_view != WeatherStationViewReceiver) weather_station_release_receiver_view(app);
    if(active_view != WeatherStationViewReceiverInfo)
        weather_station_release_receiver_info_view(app);
}

void weather_station_release_inactive_gui_views(
    WeatherStationApp* app,
    WeatherStationView active_view) {
    if(!app || !app->view_dispatcher) return;

    /* Never remove the previous view directly from an input callback. The
       dispatcher can still reference it until the matching key-release event.
       Ten 100-ms ticks provide a safe hand-off window; a newer scene change
       simply replaces the requested active view and restarts the delay. */
    app->gui_cleanup_active_view = active_view;
    app->gui_cleanup_pending = true;
    app->gui_cleanup_delay_ticks = 10U;
}

bool weather_station_ensure_radio_core(WeatherStationApp* app) {
    if(!app || !app->txrx) return false;

    /* Powrot do odbiornika zawsze oddaje duzy bufor edytora. Inaczej
       25 dekoderow RX i bufor 1536 impulsow moglyby nie zmiescic sie razem. */
    if(app->editor_tx_raw) {
        weather_editor_raw_free(app->editor_tx_raw);
        app->editor_tx_raw = NULL;
    }

    if(!app->setting) {
        app->setting = subghz_setting_alloc();
        if(!app->setting) return false;
        subghz_setting_load(app->setting, EXT_PATH("subghz/assets/setting_user"));
        if(app->txrx->preset && app->txrx->preset->frequency == 0U) {
            app->txrx->preset->frequency = subghz_setting_get_default_frequency(app->setting);
        }
    }

    if(!app->txrx->history) app->txrx->history = ws_history_alloc();
    if(!app->txrx->worker) app->txrx->worker = subghz_worker_alloc();
    if(!app->txrx->environment) {
        app->txrx->environment = subghz_environment_alloc();
        if(app->txrx->environment) {
            subghz_environment_set_protocol_registry(
                app->txrx->environment, (void*)&weather_station_protocol_registry);
        }
    }
    if(!app->txrx->receiver && app->txrx->environment) {
        app->txrx->receiver = subghz_receiver_alloc_init(app->txrx->environment);
    }
    if(!app->txrx->history || !app->txrx->worker || !app->txrx->environment ||
       !app->txrx->receiver) {
        return false;
    }

    if(!app->subghz_devices_initialized) {
        subghz_devices_init();
        app->subghz_devices_initialized = true;
    }

    if(!app->txrx->radio_device) {
        app->txrx->radio_device =
            radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);
        if(!app->txrx->radio_device) {
            app->txrx->radio_device =
                radio_device_loader_set(NULL, SubGhzRadioDeviceTypeInternal);
        }
        if(app->txrx->radio_device) {
            subghz_devices_reset(app->txrx->radio_device);
            subghz_devices_idle(app->txrx->radio_device);
        }
    }

    subghz_receiver_set_filter(app->txrx->receiver, SubGhzProtocolFlag_Decodable);
    subghz_worker_set_overrun_callback(
        app->txrx->worker, weather_station_worker_overrun_callback);
    subghz_worker_set_pair_callback(app->txrx->worker, weather_editor_pair_callback);
    subghz_worker_set_context(app->txrx->worker, app);

    return app->txrx->radio_device != NULL;
}

void weather_station_release_rx_core(WeatherStationApp* app) {
    if(!app || !app->txrx) return;

    /* Stop the hardware callback before touching worker memory. This helper is
       shared by the editor and simulation so every protocol follows exactly the
       same safe shutdown path. */
    /* Do not trust only txrx_state here: a failed transition can leave the
       worker running while the state already says IDLE. Stop the hardware ISR
       first whenever RX may be active, then always join a running worker. */
    if(app->txrx->radio_device &&
       (app->txrx->txrx_state == WSTxRxStateRx ||
        (app->txrx->worker && subghz_worker_is_running(app->txrx->worker)))) {
        subghz_devices_stop_async_rx(app->txrx->radio_device);
    }
    if(app->txrx->worker && subghz_worker_is_running(app->txrx->worker)) {
        subghz_worker_stop(app->txrx->worker);
    }
    if(app->txrx->radio_device) subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = WSTxRxStateIDLE;

    if(app->txrx->receiver) {
        subghz_receiver_set_rx_callback(app->txrx->receiver, NULL, NULL);
        subghz_receiver_reset(app->txrx->receiver);
    }
    if(app->txrx->worker) {
        subghz_worker_set_pair_callback(app->txrx->worker, NULL);
        subghz_worker_set_overrun_callback(app->txrx->worker, NULL);
        subghz_worker_set_context(app->txrx->worker, NULL);
    }

    if(app->txrx->receiver) {
        subghz_receiver_free(app->txrx->receiver);
        app->txrx->receiver = NULL;
    }
    if(app->txrx->environment) {
        subghz_environment_free(app->txrx->environment);
        app->txrx->environment = NULL;
    }
    if(app->txrx->worker) {
        subghz_worker_free(app->txrx->worker);
        app->txrx->worker = NULL;
    }
    app->txrx->txrx_state = WSTxRxStateIDLE;
}

WeatherStationApp* weather_station_app_alloc() {
    WeatherStationApp* app = malloc(sizeof(WeatherStationApp));
    furi_check(app);
    memset(app, 0, sizeof(WeatherStationApp));

    // GUI
    app->gui = furi_record_open(RECORD_GUI);

    // View Dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&weather_station_scene_handlers, app);
    

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, weather_station_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, weather_station_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, weather_station_app_tick_event_callback, 100);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Open Notification record
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    /* GUI modules are allocated only by the scene that currently needs them.
       Keeping every module alive consumed about 5 KB too much when qFlipper
       was connected and left less than 2 KB during RX-to-editor transition. */
    app->variable_item_list = NULL;
    app->submenu = NULL;
    app->widget = NULL;
    app->number_input = NULL;
    app->text_input = NULL;
    app->ws_receiver = NULL;
    app->ws_receiver_info = NULL;
    app->gui_cleanup_active_view = WeatherStationViewSubmenu;
    app->gui_cleanup_pending = false;
    app->gui_cleanup_delay_ticks = 0U;
    app->setting = NULL;

    //init Worker & Protocol & History
    app->lock = WSLockOff;
    app->txrx = malloc(sizeof(WeatherStationTxRx));
    furi_check(app->txrx);
    memset(app->txrx, 0, sizeof(WeatherStationTxRx));
    app->txrx->preset = malloc(sizeof(SubGhzRadioPreset));
    furi_check(app->txrx->preset);
    memset(app->txrx->preset, 0, sizeof(SubGhzRadioPreset));
    app->txrx->preset->name = furi_string_alloc();
    ws_preset_init(app, "AM650", 433920000UL, NULL, 0);

    app->txrx->hopper_state = WSHopperStateOFF;
    app->txrx->history = NULL;
    app->txrx->worker = NULL;
    app->txrx->environment = NULL;
    app->txrx->receiver = NULL;
    app->txrx->radio_device = NULL;

    app->editor = weather_editor_state_alloc();
    app->editor_rx_snapshot = weather_editor_state_alloc();
    furi_check(app->editor);
    furi_check(app->editor_rx_snapshot);
    app->editor_tx_raw = NULL;
    app->editor_status = furi_string_alloc();
    app->editor_result_title = furi_string_alloc();
    app->editor_result_text = furi_string_alloc();
    app->rx_menu_text = furi_string_alloc();
    app->rx_frequency_text = furi_string_alloc();
    app->rx_modulation_text = furi_string_alloc();
    app->rx_history_text = furi_string_alloc();
    app->editor_input_target = WeatherEditorInputTemperature;
    weather_editor_settings_set_defaults(&app->editor_settings);
    weather_editor_settings_load(&app->editor_settings);
    app->temperature_unit = app->editor_settings.display_fahrenheit ?
                                WeatherEditorTemperatureUnitFahrenheit :
                                WeatherEditorTemperatureUnitCelsius;
    app->temperature_negative = false;
    app->editor_channel_auto = true;
    app->editor_received_channel = 0xFF;
    app->editor_action_count = 0;
    app->editor_loaded_from_profile = false;
    app->editor_simulation_mode = false;
    app->editor_simulation_protocol_index = 0U;
    app->editor_frequency_hz = app->txrx->preset->frequency;
    app->loaded_profile_preset_data = NULL;
    app->loaded_profile_path = furi_string_alloc();
    app->saved_profile_count = 0;
    app->saved_profile_total_count = 0U;
    app->saved_profile_page = 0U;
    app->saved_profile_source = 0U;
    for(size_t i = 0; i < WEATHER_EDITOR_MAX_SAVED_PROFILES; i++) {
        app->saved_profile_paths[i] = NULL;
    }
    app->editor_auto_tx_interval_s = app->editor_settings.auto_tx_interval_s;
    if(app->editor_auto_tx_interval_s < 1U || app->editor_auto_tx_interval_s > 3600U)
        app->editor_auto_tx_interval_s = 5U;
    app->editor_auto_tx_elapsed_ds = 0;
    app->editor_auto_tx_enabled = false;
    app->subghz_devices_initialized = false;

    app->lab_unlocked = false;
    app->lab_about_taps = 0U;
    app->lab_about_last_tick = 0U;
    app->lab_target_temperature_tenths = 0;
    app->lab_target_humidity = 0;
    app->lab_sensor_count = 0U;
    app->lab_phase_started_tick = 0U;
    app->lab_round = 0U;
    app->lab_last_tx_ok = 0U;
    app->lab_state = NULL;
    app->lab_format = NULL;
    app->lab_preset = NULL;
    app->lab_loaded_preset_data = NULL;
    weather_lab_clear_session_files();

    furi_hal_power_suppress_charge_enter();

    /* Do not allocate the 25-decoder RX core while only the main menu is
       visible. The receiver scene starts internal/external CC1101 lazily. This
       also avoids an immediate OOM when qFlipper is connected at app start. */

    scene_manager_next_scene(app->scene_manager, WeatherStationSceneStart);

    return app;
}

void weather_station_app_free(WeatherStationApp* app) {
    weather_lab_release(app, true);
    if(!app) return;

    app->editor_settings.display_fahrenheit =
        app->temperature_unit == WeatherEditorTemperatureUnitFahrenheit;
    app->editor_settings.auto_tx_interval_s = app->editor_auto_tx_interval_s;
    weather_editor_settings_save(&app->editor_settings);

    /* Stop RX ISR and join the worker before shutting down the radio/device.
       This is the same path used by every editor and simulation transition. */
    weather_station_release_rx_core(app);

    if(app->txrx && app->txrx->radio_device) {
        subghz_devices_sleep(app->txrx->radio_device);
        radio_device_loader_end(app->txrx->radio_device);
        app->txrx->radio_device = NULL;
    }

    if(app->subghz_devices_initialized) {
        subghz_devices_deinit();
        app->subghz_devices_initialized = false;
    }

    weather_station_release_submenu(app);
    weather_station_release_variable_item_list(app);
    weather_station_release_widget(app);
    weather_station_release_number_input(app);
    weather_station_release_text_input(app);
    weather_station_release_receiver_view(app);
    weather_station_release_receiver_info_view(app);

    if(app->setting) subghz_setting_free(app->setting);

    //Worker & Protocol & History
    if(app->txrx) {
        if(app->txrx->history) ws_history_free(app->txrx->history);
        if(app->txrx->preset) {
            if(app->txrx->preset->name) furi_string_free(app->txrx->preset->name);
            free(app->txrx->preset);
        }
        free(app->txrx);
        app->txrx = NULL;
    }

    weather_editor_raw_free(app->editor_tx_raw);
    app->editor_tx_raw = NULL;
    weather_editor_state_free(app->editor);
    weather_editor_state_free(app->editor_rx_snapshot);
    free(app->loaded_profile_preset_data);
    app->loaded_profile_preset_data = NULL;
    furi_string_free(app->loaded_profile_path);
    app->loaded_profile_path = NULL;
    for(size_t i = 0; i < WEATHER_EDITOR_MAX_SAVED_PROFILES; i++) {
        if(app->saved_profile_paths[i]) furi_string_free(app->saved_profile_paths[i]);
    }
    furi_string_free(app->editor_status);
    furi_string_free(app->editor_result_title);
    furi_string_free(app->editor_result_text);
    furi_string_free(app->rx_menu_text);
    furi_string_free(app->rx_frequency_text);
    furi_string_free(app->rx_modulation_text);
    furi_string_free(app->rx_history_text);

    // View dispatcher
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    // Notifications
    furi_record_close(RECORD_NOTIFICATION);
    app->notifications = NULL;

    // Close records
    furi_record_close(RECORD_GUI);

    furi_hal_power_suppress_charge_exit();

    free(app);
}

int32_t weather_station_app(void* p) {
    UNUSED(p);
    WeatherStationApp* weather_station_app = weather_station_app_alloc();

    view_dispatcher_run(weather_station_app->view_dispatcher);

    weather_station_app_free(weather_station_app);

    return 0;
}

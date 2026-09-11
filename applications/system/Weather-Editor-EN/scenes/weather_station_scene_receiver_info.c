#include "../weather_station_app_i.h"
#include "../views/weather_station_receiver.h"
#include "../weather_editor_radio.h"

void weather_station_scene_receiver_info_callback(WSCustomEvent event, void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->view_dispatcher) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void weather_station_receiver_pause_for_editor(WeatherStationApp* app) {
    /* Details and editing use a frozen history entry. Stop the worker before
       the selected station can be updated/replaced behind the UI. */
    if(!app || !app->txrx) return;
    if(app->txrx->receiver) subghz_receiver_set_rx_callback(app->txrx->receiver, NULL, NULL);
    if(app->txrx->txrx_state == WSTxRxStateRx) ws_rx_end(app);
    if(app->txrx->receiver) subghz_receiver_reset(app->txrx->receiver);
}

void weather_station_scene_receiver_info_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->txrx || !app->scene_manager || !app->view_dispatcher ||
       !weather_station_ensure_receiver_info_view(app)) {
        return;
    }

    if(!app->txrx->history ||
       !ws_history_is_valid_index(app->txrx->history, app->txrx->idx_menu_chosen)) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }
    FlipperFormat* decoded =
        ws_history_get_decoded_data(app->txrx->history, app->txrx->idx_menu_chosen);
    if(!decoded) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    /* Pause immediately after selecting a station, not only after opening the
       action list. RX is restarted by WeatherStationSceneReceiver on return. */
    weather_station_receiver_pause_for_editor(app);

    ws_view_receiver_info_set_callback(
        app->ws_receiver_info, weather_station_scene_receiver_info_callback, app);
    ws_view_receiver_info_update(
        app->ws_receiver_info,
        decoded,
        app->temperature_unit == WeatherEditorTemperatureUnitFahrenheit);
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewReceiverInfo);
    weather_station_release_inactive_gui_views(app, WeatherStationViewReceiverInfo);
}

bool weather_station_scene_receiver_info_on_event(void* context, SceneManagerEvent event) {
    WeatherStationApp* app = context;
    if(!app || !app->txrx || !app->editor || !app->scene_manager) return false;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == WSCustomEventViewReceiverInfoActions) {
        if(!app->txrx->history ||
           !ws_history_is_valid_index(app->txrx->history, app->txrx->idx_menu_chosen)) {
            return true;
        }
        FlipperFormat* decoded =
            ws_history_get_decoded_data(app->txrx->history, app->txrx->idx_menu_chosen);
        if(!decoded) return true;
        weather_station_receiver_pause_for_editor(app);
        const bool state_loaded = weather_editor_state_load(app->editor, decoded);
        if(state_loaded) {
            if(app->editor_rx_snapshot) weather_editor_state_copy(app->editor_rx_snapshot, app->editor);
            app->editor_received_channel = app->editor->channel;
            app->editor_channel_auto = app->editor->has_channel;
            app->editor_loaded_from_profile = false;
            const SubGhzRadioPreset* received_preset = ws_history_get_radio_preset(
                app->txrx->history, app->txrx->idx_menu_chosen);
            app->editor_frequency_hz = received_preset ? received_preset->frequency : 433920000UL;
            scene_manager_set_scene_state(app->scene_manager, WeatherStationSceneActions, 0);
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneActions);
        }
        consumed = true;
    }
    return consumed;
}

void weather_station_scene_receiver_info_on_exit(void* context) {
    UNUSED(context);
}

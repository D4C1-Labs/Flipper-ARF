#include "../weather_station_app_i.h"
#include <storage/storage.h>
#include "../views/weather_station_receiver.h"
#include "../weather_editor_radio.h"

static const NotificationSequence subghs_sequence_rx = {
    &message_green_255,

    &message_vibro_on,
    &message_note_c6,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,

    &message_delay_50,
    NULL,
};

static const NotificationSequence subghs_sequence_rx_locked = {
    &message_green_255,

    &message_display_backlight_on,

    &message_vibro_on,
    &message_note_c6,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,

    &message_delay_500,

    &message_display_backlight_off,
    NULL,
};

static void weather_station_scene_receiver_update_statusbar(void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->txrx || !app->txrx->history || !app->txrx->preset ||
       !app->ws_receiver || !app->rx_history_text || !app->rx_frequency_text ||
       !app->rx_modulation_text) return;

    furi_string_reset(app->rx_history_text);
    furi_string_reset(app->rx_frequency_text);
    furi_string_reset(app->rx_modulation_text);

    if(!ws_history_get_text_space_left(app->txrx->history, app->rx_history_text)) {
        ws_get_frequency_modulation(
            app, app->rx_frequency_text, app->rx_modulation_text);
        ws_view_receiver_add_data_statusbar(
            app->ws_receiver,
            furi_string_get_cstr(app->rx_frequency_text),
            furi_string_get_cstr(app->rx_modulation_text),
            furi_string_get_cstr(app->rx_history_text),
            radio_device_loader_is_external(app->txrx->radio_device));
    } else {
        ws_view_receiver_add_data_statusbar(
            app->ws_receiver,
            furi_string_get_cstr(app->rx_history_text),
            "",
            "",
            radio_device_loader_is_external(app->txrx->radio_device));
    }
}


void weather_station_scene_receiver_callback(WSCustomEvent event, void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->view_dispatcher) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void weather_station_scene_receiver_add_to_history_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    WeatherStationApp* app = context;
    if(!receiver || !decoder_base || !app || !app->txrx || !app->txrx->history ||
       !app->txrx->preset || !app->ws_receiver || !app->rx_menu_text) return;

    const WSHistoryStateAddKey add_state =
        ws_history_add_to_history(app->txrx->history, decoder_base, app->txrx->preset);
    const uint16_t idx = ws_history_get_last_touched_index(app->txrx->history);

    if(add_state == WSHistoryStateAddKeyNewDada) {
        furi_string_reset(app->rx_menu_text);
        ws_history_get_text_item_menu(app->txrx->history, app->rx_menu_text, idx);
        ws_view_receiver_add_item_to_menu(
            app->ws_receiver,
            furi_string_get_cstr(app->rx_menu_text),
            ws_history_get_type_protocol(app->txrx->history, idx));
        weather_station_scene_receiver_update_statusbar(app);
        notification_message(app->notifications, &sequence_blink_green_10);
        if(app->lock != WSLockOn) {
            notification_message(app->notifications, &subghs_sequence_rx);
        } else {
            notification_message(app->notifications, &subghs_sequence_rx_locked);
        }
    } else if(
        add_state == WSHistoryStateAddKeyUpdateData ||
        add_state == WSHistoryStateAddKeyReplaceData) {
        /* Single-slot mode: refresh or replace the only visible RAM entry. */
        furi_string_reset(app->rx_menu_text);
        ws_history_get_text_item_menu(app->txrx->history, app->rx_menu_text, idx);
        ws_view_receiver_update_item(
            app->ws_receiver,
            idx,
            furi_string_get_cstr(app->rx_menu_text),
            ws_history_get_type_protocol(app->txrx->history, idx));
        if(add_state == WSHistoryStateAddKeyReplaceData) {
            weather_station_scene_receiver_update_statusbar(app);
            notification_message(app->notifications, &sequence_blink_green_10);
        }
    }

    subghz_receiver_reset(receiver);
    app->txrx->rx_key_state = WSRxKeyStateAddKey;
}

void weather_station_scene_receiver_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->txrx || !app->scene_manager || !app->view_dispatcher ||
       !weather_station_ensure_radio_core(app) ||
       !weather_station_ensure_receiver_view(app) ||
       !app->txrx->radio_device) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    FuriString* str_buff = app->rx_menu_text;
    furi_string_reset(str_buff);

    if(app->txrx->rx_key_state == WSRxKeyStateIDLE) {
        ws_preset_init(app, "AM650", subghz_setting_get_default_frequency(app->setting), NULL, 0);
        ws_history_reset(app->txrx->history);
        app->txrx->rx_key_state = WSRxKeyStateStart;
    }

    ws_view_receiver_set_lock(app->ws_receiver, app->lock);

    //Load history to receiver
    ws_view_receiver_exit(app->ws_receiver);
    for(uint8_t i = 0; i < ws_history_get_item(app->txrx->history); i++) {
        furi_string_reset(str_buff);
        ws_history_get_text_item_menu(app->txrx->history, str_buff, i);
        ws_view_receiver_add_item_to_menu(
            app->ws_receiver,
            furi_string_get_cstr(str_buff),
            ws_history_get_type_protocol(app->txrx->history, i));
        app->txrx->rx_key_state = WSRxKeyStateAddKey;
    }
    furi_string_reset(str_buff);
    weather_station_scene_receiver_update_statusbar(app);

    ws_view_receiver_set_callback(app->ws_receiver, weather_station_scene_receiver_callback, app);
    subghz_receiver_set_rx_callback(
        app->txrx->receiver, weather_station_scene_receiver_add_to_history_callback, app);

    if(app->txrx->txrx_state == WSTxRxStateRx) {
        ws_rx_end(app);
    };
    if((app->txrx->txrx_state == WSTxRxStateIDLE) || (app->txrx->txrx_state == WSTxRxStateSleep)) {
        ws_begin(
            app,
            subghz_setting_get_preset_data_by_name(
                app->setting, furi_string_get_cstr(app->txrx->preset->name)));

        ws_rx(app, app->txrx->preset->frequency);
    }

    ws_view_receiver_set_idx_menu(app->ws_receiver, app->txrx->idx_menu_chosen);
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewReceiver);
    weather_station_release_inactive_gui_views(app, WeatherStationViewReceiver);
}

bool weather_station_scene_receiver_on_event(void* context, SceneManagerEvent event) {
    WeatherStationApp* app = context;
    if(!app || !app->txrx || !app->scene_manager) return false;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case WSCustomEventViewReceiverBack:
            // Stop CC1101 Rx
            if(app->txrx->txrx_state == WSTxRxStateRx) {
                ws_rx_end(app);
                ws_sleep(app);
            };
            app->txrx->hopper_state = WSHopperStateOFF;
            app->txrx->idx_menu_chosen = 0;
            if(app->txrx->receiver) {
                subghz_receiver_set_rx_callback(app->txrx->receiver, NULL, NULL);
            }

            app->txrx->rx_key_state = WSRxKeyStateIDLE;
            ws_preset_init(
                app, "AM650", subghz_setting_get_default_frequency(app->setting), NULL, 0);
            weather_station_release_rx_core(app);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, WeatherStationSceneStart);
            consumed = true;
            break;
        case WSCustomEventViewReceiverOK:
            if(app->ws_receiver && app->txrx->history) {
                const uint16_t selected = ws_view_receiver_get_idx_menu(app->ws_receiver);
                if(ws_history_is_valid_index(app->txrx->history, selected)) {
                    app->txrx->idx_menu_chosen = selected;
                    scene_manager_next_scene(app->scene_manager, WeatherStationSceneReceiverInfo);
                }
            }
            consumed = true;
            break;
        case WSCustomEventViewReceiverConfig:
            app->txrx->idx_menu_chosen = ws_view_receiver_get_idx_menu(app->ws_receiver);
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneReceiverConfig);
            consumed = true;
            break;
        case WSCustomEventViewReceiverOffDisplay:
            notification_message(app->notifications, &sequence_display_backlight_off);
            consumed = true;
            break;
        case WSCustomEventViewReceiverUnlock:
            app->lock = WSLockOff;
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(app->txrx->hopper_state != WSHopperStateOFF) {
            ws_hopper_update(app);
            weather_station_scene_receiver_update_statusbar(app);
        }
        // Get current RSSI
        if(app->txrx->radio_device) {
            float rssi = subghz_devices_get_rssi(app->txrx->radio_device);
            ws_view_receiver_set_rssi(app->ws_receiver, rssi);
        }

        if(app->txrx->txrx_state == WSTxRxStateRx) {
            notification_message(app->notifications, &sequence_blink_cyan_10);
        }
    }
    return consumed;
}

void weather_station_scene_receiver_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(!app) return;
    /* Detach the decoder callback and stop RX before another persistent view
       becomes active. RX is restarted when the receiver scene returns. */
    if(app->txrx && app->txrx->receiver) {
        subghz_receiver_set_rx_callback(app->txrx->receiver, NULL, NULL);
        if(app->txrx->txrx_state == WSTxRxStateRx) ws_rx_end(app);
        subghz_receiver_reset(app->txrx->receiver);
    }
}

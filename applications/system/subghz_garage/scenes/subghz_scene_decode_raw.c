#include "../subghz_i.h"
#include "../subghz_protocol_filter.h"
#include "../helpers/subghz_debug_log.h"
#include "../helpers/subghz_lib_ext_compat.h"
#include <furi/core/memmgr.h>

#define TAG "SubGhzDecodeRaw"

#define SAMPLES_TO_READ_PER_TICK 400

static void subghz_scene_receiver_update_statusbar(void* context) {
    SubGhz* subghz = context;
    FuriString* history_stat_str = furi_string_alloc();
    if(!subghz_history_get_text_space_left(
           subghz->history, history_stat_str, SUBGHZ_LOW_RAM_FREE_HEAP_READ)) {
        FuriString* frequency_str = furi_string_alloc();
        FuriString* modulation_str = furi_string_alloc();

        subghz_txrx_get_frequency_and_modulation(
            subghz->txrx, frequency_str, modulation_str, false);

        subghz_view_receiver_add_data_statusbar(
            subghz->subghz_receiver,
            furi_string_get_cstr(frequency_str),
            furi_string_get_cstr(modulation_str),
            furi_string_get_cstr(history_stat_str),
            subghz_txrx_hopper_get_state(subghz->txrx) != SubGhzHopperStateOFF,
            READ_BIT(subghz->filter, SubGhzProtocolFlag_BinRAW) > 0);

        furi_string_free(frequency_str);
        furi_string_free(modulation_str);
    } else {
        subghz_view_receiver_add_data_statusbar(
            subghz->subghz_receiver,
            furi_string_get_cstr(history_stat_str),
            "",
            "",
            subghz_txrx_hopper_get_state(subghz->txrx) != SubGhzHopperStateOFF,
            READ_BIT(subghz->filter, SubGhzProtocolFlag_BinRAW) > 0);
    }
    furi_string_free(history_stat_str);
}

void subghz_scene_decode_raw_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static void subghz_scene_add_to_history_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    FuriString* item_name = furi_string_alloc();
    FuriString* item_time = furi_string_alloc();
    uint16_t idx = subghz_history_get_item(subghz->history);
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);

    if(subghz_history_add_to_history(
           subghz->history, decoder_base, &preset, SUBGHZ_LOW_RAM_FREE_HEAP_READ)) {
        furi_string_reset(item_name);
        furi_string_reset(item_time);

        subghz->state_notifications = SubGhzNotificationStateRxDone;

        subghz_history_get_text_item_menu(subghz->history, item_name, idx);
        subghz_history_get_time_item_menu(subghz->history, item_time, idx);
        subghz_view_receiver_add_item_to_menu(
            subghz->subghz_receiver,
            furi_string_get_cstr(item_name),
            furi_string_get_cstr(item_time),
            subghz_history_get_type_protocol(subghz->history, idx));

        subghz_scene_receiver_update_statusbar(subghz);
    }
    subghz_receiver_reset(receiver);
    furi_string_free(item_name);
    furi_string_free(item_time);
}

bool subghz_scene_decode_raw_start(SubGhz* subghz) {
    FuriString* file_name = furi_string_alloc();
    bool success = false;
    do {
        if(!flipper_format_rewind(subghz_txrx_get_fff_data(subghz->txrx))) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        if(!flipper_format_read_string(
               subghz_txrx_get_fff_data(subghz->txrx), "File_name", file_name)) {
            FURI_LOG_E(TAG, "Missing File_name");
            break;
        }

        success = true;
    } while(false);

    if(success) {

        subghz->decode_raw_file_worker_encoder = subghz_file_encoder_worker_alloc();
        if(subghz_file_encoder_worker_start(
               subghz->decode_raw_file_worker_encoder,
               furi_string_get_cstr(file_name),
               subghz_txrx_radio_device_get_name(subghz->txrx))) {

            furi_delay_ms(100);
        } else {
            success = false;
        }

        if(!success) {
            subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
        }
    }

    furi_string_free(file_name);
    return success;
}

bool subghz_scene_decode_raw_next(SubGhz* subghz) {
    LevelDuration level_duration;
    SubGhzReceiver* receiver = subghz_txrx_get_receiver(subghz->txrx);
    for(uint32_t read = SAMPLES_TO_READ_PER_TICK; read > 0; --read) {
        level_duration =
            subghz_file_encoder_worker_get_level_duration(subghz->decode_raw_file_worker_encoder);
        if(!level_duration_is_reset(level_duration)) {
            if(level_duration_is_wait(level_duration)) {
                FURI_LOG_W(TAG, "LD tells wait!");
                return true;
            }
            bool level = level_duration_get_level(level_duration);
            uint32_t duration = level_duration_get_duration(level_duration);
            if(duration > 1000000) {
                FURI_LOG_E(TAG, "LD came with overflow: %ld", duration);
                return true;
            }
            subghz_receiver_decode(receiver, level, duration);
        } else {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateLoaded);
            subghz->state_notifications = SubGhzNotificationStateIDLE;

            if(subghz_history_get_item(subghz->history) == 0) {

                extern void subghz_scene_decode_raw_failed_set_context(bool all_protocols_enabled);
                size_t total = subghz_protocol_registry_count(
                    subghz_txrx_get_protocol_registry(subghz->txrx));
                size_t enabled = subghz_garage_protocol_filter_enabled_count(subghz->protocol_filter, total);
                subghz_scene_decode_raw_failed_set_context(enabled == total);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneDecodeRawFailed);
            } else {
                subghz_view_receiver_add_data_progress(subghz->subghz_receiver, "Done!");
            }
            return false;
        }
    }

    FuriString* progress_str = furi_string_alloc();
    subghz_garage_encoder_get_text_progress(
        subghz->decode_raw_file_worker_encoder, progress_str);

    subghz_view_receiver_add_data_progress(
        subghz->subghz_receiver, furi_string_get_cstr(progress_str));

    furi_string_free(progress_str);

    return true;
}

void subghz_scene_decode_raw_on_enter(void* context) {
    SubGhz* subghz = context;

    rpc_gui_screen_stream_set_suppressed(true);

    bool decoding_new_file = scene_manager_get_scene_state(
                                  subghz->scene_manager, SubGhzSceneDecodeRAW) ==
                              SubGhzDecodeRawStateStart;

    if(decoding_new_file) {

        if(subghz_low_ram_mitigate(subghz, SUBGHZ_LOW_RAM_FREE_HEAP_READ)) {
            FURI_LOG_W(
                TAG,
                "on_enter: free heap %zu still low after mitigation, bailing to warning",
                memmgr_get_free_heap());
            subghz_debug_log_write(
                "on_enter: free heap %zu still low after mitigation, bailing to warning",
                memmgr_get_free_heap());

            subghz_txrx_release_protocol_group(subghz->txrx);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneLowRamWarning);
            return;
        }
    }

    subghz_ensure_history(subghz);

    subghz_ensure_receiver_view(subghz);

    FuriString* item_name = furi_string_alloc();
    FuriString* item_time = furi_string_alloc();

    subghz_view_receiver_set_mode(subghz->subghz_receiver, SubGhzViewReceiverModeFile);
    subghz_view_receiver_set_callback(
        subghz->subghz_receiver, subghz_scene_decode_raw_callback, subghz);

    subghz_txrx_set_rx_callback(subghz->txrx, subghz_scene_add_to_history_callback, subghz);

    subghz_txrx_receiver_set_filter(subghz->txrx, SubGhzProtocolFlag_Decodable);

    if(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneDecodeRAW) ==
       SubGhzDecodeRawStateStart) {

        subghz_history_reset(subghz->history);
        if(subghz_scene_decode_raw_start(subghz)) {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateLoading);
            subghz->state_notifications = SubGhzNotificationStateRx;
        }
    } else {

        subghz_view_receiver_exit(subghz->subghz_receiver);
        for(uint16_t i = 0; i < subghz_history_get_item(subghz->history); i++) {
            furi_string_reset(item_name);
            furi_string_reset(item_time);
            subghz_history_get_text_item_menu(subghz->history, item_name, i);
            subghz_history_get_time_item_menu(subghz->history, item_time, i);
            subghz_view_receiver_add_item_to_menu(
                subghz->subghz_receiver,
                furi_string_get_cstr(item_name),
                furi_string_get_cstr(item_time),
                subghz_history_get_type_protocol(subghz->history, i));
        }
        subghz_view_receiver_set_idx_menu(subghz->subghz_receiver, subghz->idx_menu_chosen);
    }

    furi_string_free(item_name);
    furi_string_free(item_time);

    subghz_scene_receiver_update_statusbar(subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReceiver);
}

bool subghz_scene_decode_raw_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SubGhzCustomEventViewReceiverBack:
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateStart);
            subghz->idx_menu_chosen = 0;

            subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);

            if(subghz_file_encoder_worker_is_running(subghz->decode_raw_file_worker_encoder)) {
                subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
            }
            subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);

            subghz->state_notifications = SubGhzNotificationStateIDLE;
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerNoSet);
            scene_manager_search_and_switch_to_previous_scene(
                subghz->scene_manager, SubGhzSceneMoreRAW);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverOK:
            subghz->idx_menu_chosen = subghz_view_receiver_get_idx_menu(subghz->subghz_receiver);
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverInfo);
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverConfig:
            FURI_LOG_W(TAG, "No config options");
            consumed = true;
            break;
        case SubGhzCustomEventViewReceiverOffDisplay:
            notification_message(subghz->notifications, &sequence_display_backlight_off);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        switch(subghz->state_notifications) {
        case SubGhzNotificationStateRx:
            notification_message(subghz->notifications, &sequence_blink_cyan_10);
            break;
        case SubGhzNotificationStateRxDone:
            notification_message(subghz->notifications, &subghz_sequence_rx);
            subghz->state_notifications = SubGhzNotificationStateRx;
            break;
        default:
            break;
        }

        switch(scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneDecodeRAW)) {
        case SubGhzDecodeRawStateLoading:
            subghz_scene_decode_raw_next(subghz);
            break;
        default:
            break;
        }
    }
    return consumed;
}

void subghz_scene_decode_raw_on_exit(void* context) {
    SubGhz* subghz = context;
    rpc_gui_screen_stream_set_suppressed(false);
    subghz_cli_soft_unlock(subghz);
}

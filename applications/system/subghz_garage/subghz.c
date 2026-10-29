#include <furi/core/log.h>
#include <furi/core/memmgr.h>
#include <furi/core/memmgr_heap.h>
#include <subghz/types.h>
#include <lib/toolbox/path.h>
#include <float_tools.h>
#include "subghz_i.h"
#include "helpers/subghz_debug_log.h"
#include "helpers/subghz_lib_ext_compat.h"
#include "helpers/subghz_cli_vcp_compat.h"
#include "helpers/subghz_memmgr_pool_compat.h"
#include "helpers/rpc_gui_screen_suppress_compat.h"
#include <storage/storage.h>
#include <furi_hal_usb.h>

#define TAG "SubGhzApp"

bool subghz_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    SubGhz* subghz = context;
    return scene_manager_handle_custom_event(subghz->scene_manager, event);
}

bool subghz_back_event_callback(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    return scene_manager_handle_back_event(subghz->scene_manager);
}

void subghz_tick_event_callback(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    scene_manager_handle_tick_event(subghz->scene_manager);
}

static void subghz_rpc_command_callback(const RpcAppSystemEvent* event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;

    furi_assert(subghz->rpc_ctx);

    if(event->type == RpcAppEventTypeSessionClose) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneRpcSessionClose);
        rpc_system_app_set_callback(subghz->rpc_ctx, NULL, NULL);
        subghz->rpc_ctx = NULL;
    } else if(event->type == RpcAppEventTypeAppExit) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventSceneExit);
    } else if(event->type == RpcAppEventTypeLoadFile) {
        furi_assert(event->data.type == RpcAppSystemEventDataTypeString);
        furi_string_set(subghz->file_path, event->data.string);
        view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventSceneRpcLoad);
    } else if(event->type == RpcAppEventTypeButtonPress) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneRpcButtonPress);
    } else if(event->type == RpcAppEventTypeButtonRelease) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneRpcButtonRelease);
    } else if(event->type == RpcAppEventTypeButtonPressRelease) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventSceneRpcButtonPressRelease);
    } else {
        rpc_system_app_confirm(subghz->rpc_ctx, false);
    }
}

void subghz_save_all(SubGhz* subghz) {
    furi_assert(subghz);

    subghz_garage_protocol_filter_get_raw(
        subghz->protocol_filter,
        subghz->last_settings->protocol_filter_data,
        sizeof(subghz->last_settings->protocol_filter_data));
    subghz->last_settings->protocol_filter_present = true;
    subghz_modulation_filter_get_raw(
        subghz->modulation_filter,
        subghz->last_settings->mod_filter_data,
        sizeof(subghz->last_settings->mod_filter_data));
    subghz->last_settings->mod_filter_present = true;

    subghz_garage_last_settings_save(subghz->last_settings);
}

void subghz_lock_cli_sessions(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->cli_sessions_locked_after_recovery) return;

    FURI_LOG_W(TAG, "Locking CLI/RPC sessions for the rest of this app run (see subghz_i.h)");
    subghz_debug_log_write("lock_cli_sessions: enter");

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    subghz_debug_log_write("lock_cli_sessions: RECORD_CLI_VCP open, %p, calling session_lock", (void*)cli_vcp);
    subghz_garage_cli_vcp_session_lock(cli_vcp);
    subghz_debug_log_write("lock_cli_sessions: session_lock returned");
    furi_record_close(RECORD_CLI_VCP);
    subghz->cli_sessions_locked_after_recovery = true;
    subghz_debug_log_write("lock_cli_sessions: done");
}

void subghz_unlock_cli_sessions_after_recovery(SubGhz* subghz) {
    furi_assert(subghz);
    if(!subghz->cli_sessions_locked_after_recovery) return;

    FURI_LOG_I(TAG, "Unlocking CLI/RPC sessions (back at Garage's Start menu, safe to allow new ones again)");
    subghz_debug_log_write("unlock_cli_sessions_after_recovery: enter");
    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    subghz_garage_cli_vcp_session_unlock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);
    subghz->cli_sessions_locked_after_recovery = false;
    subghz_debug_log_write("unlock_cli_sessions_after_recovery: done");
}

void subghz_cli_soft_lock(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->cli_sessions_soft_locked) return;

    FURI_LOG_I(TAG, "Soft-locking CLI/RPC sessions (proactive Read/Read RAW/Decode RAW entry)");
    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    subghz_garage_cli_vcp_session_lock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);

    FURI_LOG_I(TAG, "Hard-disconnecting USB/CLI (proactive Read/Read RAW/Decode RAW entry)");
    subghz->cli_hard_disconnect_usb_config = furi_hal_usb_get_config();
    furi_hal_usb_unlock();
    furi_hal_usb_set_config(NULL, NULL);
    subghz->cli_sessions_soft_locked = true;
}

void subghz_cli_soft_unlock(SubGhz* subghz) {
    furi_assert(subghz);
    if(!subghz->cli_sessions_soft_locked) return;

    FURI_LOG_I(TAG, "Restoring USB/CLI (leaving Read/Read RAW/Decode RAW)");
    furi_hal_usb_set_config((FuriHalUsbInterface*)subghz->cli_hard_disconnect_usb_config, NULL);
    subghz->cli_hard_disconnect_usb_config = NULL;

    CliVcp* cli_vcp = furi_record_open(RECORD_CLI_VCP);
    subghz_garage_cli_vcp_session_unlock(cli_vcp);
    furi_record_close(RECORD_CLI_VCP);
    subghz->cli_sessions_soft_locked = false;
}

#define SUBGHZ_LOW_RAM_MITIGATE_WAIT_MS 300
#define SUBGHZ_LOW_RAM_MITIGATE_RECHECK_WAIT_MS 50

bool subghz_low_ram_mitigate(SubGhz* subghz, size_t threshold) {
    furi_assert(subghz);

    if(!subghz->cli_sessions_soft_locked) {

        size_t free_heap_before_lock = memmgr_get_free_heap();
        size_t max_block_before_lock = memmgr_heap_get_max_free_block();
        FURI_LOG_I(
            TAG,
            "low_ram_mitigate: soft-locking CLI/RPC before heavy allocation (free heap %zu, max free block %zu)",
            free_heap_before_lock,
            max_block_before_lock);
        subghz_debug_log_write(
            "low_ram_mitigate: soft-locking CLI/RPC before heavy allocation (free heap %zu, max free block %zu)",
            free_heap_before_lock,
            max_block_before_lock);
        subghz_cli_soft_lock(subghz);
        furi_delay_ms(SUBGHZ_LOW_RAM_MITIGATE_WAIT_MS);
        size_t free_heap_after_lock = memmgr_get_free_heap();
        size_t max_block_after_lock = memmgr_heap_get_max_free_block();
        FURI_LOG_I(
            TAG,
            "low_ram_mitigate: free heap %zu, max free block %zu after %dms wait",
            free_heap_after_lock,
            max_block_after_lock,
            SUBGHZ_LOW_RAM_MITIGATE_WAIT_MS);
        subghz_debug_log_write(
            "low_ram_mitigate: free heap %zu, max free block %zu after %dms wait",
            free_heap_after_lock,
            max_block_after_lock,
            SUBGHZ_LOW_RAM_MITIGATE_WAIT_MS);
        if(free_heap_after_lock >= threshold) {

            if(free_heap_before_lock < threshold && !rpc_gui_screen_stream_is_active()) {
                subghz_lock_cli_sessions(subghz);
            }
            return false;
        }
        return true;
    }

    if(memmgr_get_free_heap() >= threshold) {
        return false;
    }
    furi_delay_ms(SUBGHZ_LOW_RAM_MITIGATE_RECHECK_WAIT_MS);
    return memmgr_get_free_heap() < threshold;
}

void subghz_ensure_submenu(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->submenu) return;
    subghz->submenu = submenu_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdMenu, submenu_get_view(subghz->submenu));
}

void subghz_ensure_text_input(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->text_input) return;
    subghz->text_input = text_input_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdTextInput, text_input_get_view(subghz->text_input));
}

void subghz_ensure_byte_input(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->byte_input) return;
    subghz->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdByteInput, byte_input_get_view(subghz->byte_input));
}

void subghz_ensure_widget(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->widget) return;
    subghz->widget = widget_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdWidget, widget_get_view(subghz->widget));
}

void subghz_ensure_variable_item_list(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->variable_item_list) return;
    subghz->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdVariableItemList,
        variable_item_list_get_view(subghz->variable_item_list));
}

void subghz_ensure_signal_visualizer(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->subghz_signal_visualizer) return;
    subghz->subghz_signal_visualizer = subghz_signal_visualizer_alloc(subghz->txrx);
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdSignalVisualizer,
        subghz_signal_visualizer_get_view(subghz->subghz_signal_visualizer));
}

void subghz_ensure_receiver_view(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->subghz_receiver) return;
    FURI_LOG_I(TAG, "Allocating receiver view: free heap %zu", memmgr_get_free_heap());
    subghz->subghz_receiver = subghz_view_receiver_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdReceiver,
        subghz_view_receiver_get_view(subghz->subghz_receiver));
    FURI_LOG_I(TAG, "Receiver view added: free heap %zu", memmgr_get_free_heap());
}

void subghz_ensure_popup(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->popup) return;
    subghz->popup = popup_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdPopup, popup_get_view(subghz->popup));
}

void subghz_ensure_transmitter_view(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->subghz_transmitter) return;
    subghz->subghz_transmitter = subghz_view_transmitter_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdTransmitter,
        subghz_view_transmitter_get_view(subghz->subghz_transmitter));
}

void subghz_ensure_history(SubGhz* subghz) {
    furi_assert(subghz);
    if(subghz->history) return;
    subghz->history = subghz_history_alloc();
}

SubGhz* subghz_alloc(bool alloc_for_tx_only) {

    FURI_LOG_I(
        TAG,
        "subghz_alloc: enter, free heap %zu, pool free %zu, pool max block %zu",
        memmgr_get_free_heap(),
        subghz_garage_pool_get_free(),
        subghz_garage_pool_get_max_block());
    subghz_debug_log_write(
        "subghz_alloc: enter, free heap %zu, pool free %zu, pool max block %zu",
        memmgr_get_free_heap(),
        subghz_garage_pool_get_free(),
        subghz_garage_pool_get_max_block());

    SubGhz* subghz = calloc(1, sizeof(SubGhz));

    subghz->blank_transition_viewport = NULL;

    subghz->file_path = furi_string_alloc();
    subghz->file_path_tmp = furi_string_alloc();
    subghz->decoded_preview_orig_path = furi_string_alloc();
    subghz->decoded_preview_active    = false;

    subghz->gui = furi_record_open(RECORD_GUI);

    if(!alloc_for_tx_only) {
        subghz->startup_loading = loading_alloc();
        subghz->startup_holder  = view_holder_alloc();
        view_holder_attach_to_gui(subghz->startup_holder, subghz->gui);
        view_holder_set_view(
            subghz->startup_holder, loading_get_view(subghz->startup_loading));
    }

    subghz->view_dispatcher = view_dispatcher_alloc();

    subghz->scene_manager = scene_manager_alloc(&subghz_scene_handlers, subghz);
    view_dispatcher_set_event_callback_context(subghz->view_dispatcher, subghz);
    view_dispatcher_set_custom_event_callback(
        subghz->view_dispatcher, subghz_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        subghz->view_dispatcher, subghz_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        subghz->view_dispatcher, subghz_tick_event_callback, 100);

    subghz->notifications = furi_record_open(RECORD_NOTIFICATION);
#if SUBGHZ_MEASURE_LOADING
    uint32_t load_ticks = furi_get_tick();
#endif
    subghz->txrx = subghz_txrx_alloc();

    subghz->subghz_receiver = NULL;

    subghz->popup = NULL;

    subghz->submenu = NULL;
    subghz->text_input = NULL;
    subghz->byte_input = NULL;
    subghz->widget = NULL;
    subghz->variable_item_list = NULL;
    subghz->subghz_signal_visualizer = NULL;

    subghz->dialogs = furi_record_open(RECORD_DIALOGS);

    subghz->subghz_transmitter = NULL;
    FURI_LOG_I(TAG, "Boot: core views added, free heap %zu", memmgr_get_free_heap());

    subghz->subghz_read_raw = subghz_read_raw_alloc(alloc_for_tx_only);
    FURI_LOG_I(TAG, "Boot: read raw alloc'd, free heap %zu", memmgr_get_free_heap());
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdReadRAW,
        subghz_read_raw_get_view(subghz->subghz_read_raw));
    FURI_LOG_I(TAG, "Boot: read raw view added");

    subghz->threshold_rssi = subghz_threshold_rssi_alloc();
    FURI_LOG_I(TAG, "Boot: threshold rssi alloc'd, free heap %zu", memmgr_get_free_heap());

    subghz->last_settings = subghz_garage_last_settings_alloc();
    subghz->protocol_filter = subghz_garage_protocol_filter_alloc();
    subghz->modulation_filter = subghz_modulation_filter_alloc();
    FURI_LOG_I(TAG, "Boot: filters alloc'd, free heap %zu", memmgr_get_free_heap());

    subghz_garage_last_settings_load(subghz->last_settings, 0);
    FURI_LOG_I(TAG, "Boot: last settings loaded");

    if(subghz->last_settings->protocol_filter_present) {
        subghz_garage_protocol_filter_set_raw(
            subghz->protocol_filter,
            subghz->last_settings->protocol_filter_data,
            sizeof(subghz->last_settings->protocol_filter_data));
    }
    if(subghz->last_settings->mod_filter_present) {
        subghz_modulation_filter_set_raw(
            subghz->modulation_filter,
            subghz->last_settings->mod_filter_data,
            sizeof(subghz->last_settings->mod_filter_data));
    }

    subghz_garage_set_ext_leds_and_amp(subghz->last_settings->leds_and_amp);
    FURI_LOG_I(TAG, "Boot: leds/amp set");

    subghz_txrx_set_protocol_group(
        subghz->txrx, (SubGhzGarageProtocolGroup)subghz->last_settings->protocol_group);
    FURI_LOG_I(TAG, "Boot: protocol group restored");

    if(!alloc_for_tx_only) {
        subghz_txrx_set_preset_internal(
            subghz->txrx,
            subghz->last_settings->frequency,
            subghz->last_settings->preset_index,
            subghz->tx_power);
        FURI_LOG_I(TAG, "Boot: preset set");

        subghz->history = NULL;
    }

    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);

    if(!alloc_for_tx_only) {
        subghz->filter = subghz->last_settings->filter;
        subghz->tx_power = subghz->last_settings->tx_power;
    } else {
        subghz->filter = SubGhzProtocolFlag_Decodable;
        subghz->tx_power = 0;
    }

    subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);
    subghz_txrx_set_need_save_callback(subghz->txrx, subghz_save_to_file, subghz);

    if(!alloc_for_tx_only) {
        if(!float_is_equal(subghz->last_settings->rssi, 0)) {
            subghz_threshold_rssi_set(subghz->threshold_rssi, subghz->last_settings->rssi);
        } else {
            subghz->last_settings->rssi = SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_TRIGGER;
        }
    }
#if SUBGHZ_MEASURE_LOADING
    load_ticks = furi_get_tick() - load_ticks;
    FURI_LOG_I(TAG, "Loaded: %ld ms.", load_ticks);
#endif

    subghz->error_str = furi_string_alloc();

    subghz->cli_sessions_locked_after_recovery = false;
    subghz->cli_sessions_soft_locked = false;
    subghz->cli_hard_disconnect_usb_config = NULL;
    subghz->shared_ram_warning_shown = false;

    FURI_LOG_I(TAG, "Boot: subghz_alloc complete, free heap %zu", memmgr_get_free_heap());
    return subghz;
}

void subghz_free(SubGhz* subghz, bool alloc_for_tx_only) {
    furi_assert(subghz);
    FURI_LOG_I(TAG, "subghz_free: enter, free heap %zu", memmgr_get_free_heap());

    rpc_gui_screen_stream_set_suppressed(false);

    subghz_unlock_cli_sessions_after_recovery(subghz);

    subghz_cli_soft_unlock(subghz);

    if(subghz->rpc_ctx) {
        rpc_system_app_set_callback(subghz->rpc_ctx, NULL, NULL);
        rpc_system_app_send_exited(subghz->rpc_ctx);
        subghz_blink_stop(subghz);
        subghz->rpc_ctx = NULL;
    }

    subghz_txrx_speaker_off(subghz->txrx);
    subghz_txrx_stop(subghz->txrx);
    subghz_txrx_sleep(subghz->txrx);
    FURI_LOG_I(TAG, "subghz_free: txrx stopped/slept, free heap %zu", memmgr_get_free_heap());

    if(subghz->subghz_receiver) {
        FURI_LOG_I(TAG, "subghz_free: freeing receiver view, free heap %zu", memmgr_get_free_heap());
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdReceiver);
        subghz_view_receiver_free(subghz->subghz_receiver);
        FURI_LOG_I(TAG, "subghz_free: receiver view freed, free heap %zu", memmgr_get_free_heap());
    }
    if(subghz->text_input) {
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdTextInput);
        text_input_free(subghz->text_input);
    }
    if(subghz->byte_input) {
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdByteInput);
        byte_input_free(subghz->byte_input);
    }
    if(subghz->widget) {
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdWidget);
        widget_free(subghz->widget);
    }

    furi_record_close(RECORD_DIALOGS);

    if(subghz->subghz_transmitter) {
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdTransmitter);
        subghz_view_transmitter_free(subghz->subghz_transmitter);
    }
    if(subghz->variable_item_list) {
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
        variable_item_list_free(subghz->variable_item_list);
    }
    if(subghz->subghz_signal_visualizer) {
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdSignalVisualizer);
        subghz_signal_visualizer_free(subghz->subghz_signal_visualizer);
    }
    FURI_LOG_I(TAG, "subghz_free: freeing read_raw, free heap %zu", memmgr_get_free_heap());
    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdReadRAW);
    subghz_read_raw_free(subghz->subghz_read_raw);
    FURI_LOG_I(TAG, "subghz_free: read_raw freed, free heap %zu", memmgr_get_free_heap());
    if(subghz->submenu) {
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdMenu);
        submenu_free(subghz->submenu);
    }

    if(subghz->popup) {
        view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdPopup);
        popup_free(subghz->popup);
    }

    FURI_LOG_I(TAG, "subghz_free: freeing scene_manager, free heap %zu", memmgr_get_free_heap());
    scene_manager_free(subghz->scene_manager);
    FURI_LOG_I(TAG, "subghz_free: scene_manager freed, free heap %zu", memmgr_get_free_heap());

    view_dispatcher_free(subghz->view_dispatcher);
    FURI_LOG_I(TAG, "subghz_free: view_dispatcher freed, free heap %zu", memmgr_get_free_heap());

    if(subghz->blank_transition_viewport) {
        gui_remove_view_port(subghz->gui, subghz->blank_transition_viewport);
        view_port_free(subghz->blank_transition_viewport);
        subghz->blank_transition_viewport = NULL;
    }

    if(subghz->startup_holder) {
        view_holder_set_view(subghz->startup_holder, NULL);
        view_holder_free(subghz->startup_holder);
        subghz->startup_holder = NULL;
    }
    if(subghz->startup_loading) {
        loading_free(subghz->startup_loading);
        subghz->startup_loading = NULL;
    }
    furi_record_close(RECORD_GUI);
    subghz->gui = NULL;

    subghz_save_all(subghz);
    subghz_garage_protocol_filter_free(subghz->protocol_filter);
    subghz_modulation_filter_free(subghz->modulation_filter);
    subghz_garage_last_settings_free(subghz->last_settings);

    subghz_threshold_rssi_free(subghz->threshold_rssi);

    if(!alloc_for_tx_only && subghz->history) {
        subghz_history_free(subghz->history);
    }

    FURI_LOG_I(TAG, "subghz_free: freeing txrx, free heap %zu", memmgr_get_free_heap());
    subghz_txrx_free(subghz->txrx);
    FURI_LOG_I(TAG, "subghz_free: txrx freed, free heap %zu", memmgr_get_free_heap());

    furi_string_free(subghz->error_str);

    furi_record_close(RECORD_NOTIFICATION);
    subghz->notifications = NULL;

    furi_string_free(subghz->file_path);
    furi_string_free(subghz->file_path_tmp);
    furi_string_free(subghz->decoded_preview_orig_path);

    FURI_LOG_I(
        TAG,
        "subghz_free: complete, free heap %zu, pool free %zu, pool max block %zu",
        memmgr_get_free_heap(),
        subghz_garage_pool_get_free(),
        subghz_garage_pool_get_max_block());
    subghz_debug_log_write(
        "subghz_free: complete, free heap %zu, pool free %zu, pool max block %zu",
        memmgr_get_free_heap(),
        subghz_garage_pool_get_free(),
        subghz_garage_pool_get_max_block());
    free(subghz);
}

int32_t subghz_app(void* p) {
    bool open_receiver = (p && strcmp((const char*)p, "read") == 0);
    bool open_readraw = (p && strcmp((const char*)p, "readraw") == 0);
    bool from_mode_picker = (p && strcmp((const char*)p, "frommode") == 0);
    bool savedmenu = (p && strncmp((const char*)p, "savedmenu:", 10) == 0);

    FURI_LOG_I(
        TAG,
        "Launch routing: p=\"%s\" open_receiver=%d open_readraw=%d savedmenu=%d from_mode_picker=%d",
        p ? (const char*)p : "(null)",
        (int)open_receiver,
        (int)open_readraw,
        (int)savedmenu,
        (int)from_mode_picker);

    bool alloc_for_tx;
    if(p && strlen((const char*)p) && !open_receiver && !open_readraw && !savedmenu &&
       !from_mode_picker) {
        alloc_for_tx = true;
    } else {
        alloc_for_tx = false;
    }

    SubGhz* subghz = subghz_alloc(alloc_for_tx);

    if(alloc_for_tx) {
        subghz->raw_send_only = true;
    } else {
        subghz->raw_send_only = false;
    }
    subghz->launched_from_mode_picker =
        from_mode_picker || open_receiver || open_readraw || savedmenu;
    subghz->savedmenu_return = savedmenu;

    if(open_receiver) {
        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        if(subghz_txrx_is_database_loaded(subghz->txrx)) {
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiver);
        } else {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneShowError, SubGhzCustomEventManagerSet);
            furi_string_set(
                subghz->error_str,
                "No SD card or\ndatabase found.\nSome app function\nmay be reduced.");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
        }
    } else if(open_readraw) {
        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        if(subghz_txrx_is_database_loaded(subghz->txrx)) {
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
        } else {
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneShowError, SubGhzCustomEventManagerSet);
            furi_string_set(
                subghz->error_str,
                "No SD card or\ndatabase found.\nSome app function\nmay be reduced.");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
        }
    } else if(savedmenu) {
        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
        const char* saved_path = (const char*)p + 10;
        if(memmgr_get_free_heap() < SUBGHZ_LOW_RAM_FREE_HEAP_SAVED_LOAD &&
           subghz_low_ram_mitigate(subghz, SUBGHZ_LOW_RAM_FREE_HEAP_SAVED_LOAD)) {
            dialog_message_show_storage_error(
                subghz->dialogs, "Low memory.\nClose qFlipper or\nUSB, then retry.");
            subghz_return_to_launcher(subghz);
        } else {
            bool gar_kl = subghz_key_load(subghz, saved_path, true);
            if(gar_kl) {
                furi_string_set(subghz->file_path, saved_path);
                if(subghz_get_load_type_file(subghz) == SubGhzLoadTypeFileRaw) {
                    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
                } else {
                    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSavedMenu);
                }
            } else {
                subghz_return_to_launcher(subghz);
            }
        }
    } else if(p && strlen((const char*)p) && !from_mode_picker) {
        uint32_t rpc_ctx = 0;

        if(sscanf(p, "RPC %lX", &rpc_ctx) == 1) {
            subghz->rpc_ctx = (void*)rpc_ctx;
            rpc_system_app_set_callback(subghz->rpc_ctx, subghz_rpc_command_callback, subghz);
            rpc_system_app_send_started(subghz->rpc_ctx);
            view_dispatcher_attach_to_gui(
                subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeDesktop);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneRpc);
        } else {
            view_dispatcher_attach_to_gui(
                subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
            if(memmgr_get_free_heap() < SUBGHZ_LOW_RAM_FREE_HEAP_SAVED_LOAD &&
               subghz_low_ram_mitigate(subghz, SUBGHZ_LOW_RAM_FREE_HEAP_SAVED_LOAD)) {
                dialog_message_show_storage_error(
                    subghz->dialogs, "Low memory.\nClose qFlipper or\nUSB, then retry.");
                scene_manager_stop(subghz->scene_manager);
                view_dispatcher_stop(subghz->view_dispatcher);
            } else if(subghz_key_load(subghz, p, true)) {
                furi_string_set(subghz->file_path, (const char*)p);

                if(subghz_get_load_type_file(subghz) == SubGhzLoadTypeFileRaw) {
                    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
                } else {
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneTransmitter);
                }
            } else {
                scene_manager_stop(subghz->scene_manager);
                view_dispatcher_stop(subghz->view_dispatcher);
            }
        }
    } else {
        // Launched with no args (e.g. from the main Sub-GHz "Garage / Gate"
        // button or the Apps menu): show our own start menu instead of
        // bouncing back to the main app like the upstream FoxFW build did.
        view_dispatcher_attach_to_gui(
            subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
    }

    furi_hal_power_suppress_charge_enter();

    view_dispatcher_run(subghz->view_dispatcher);

    furi_hal_power_suppress_charge_exit();

    subghz_free(subghz, alloc_for_tx);

    return 0;
}

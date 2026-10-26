/*
 * ARF-Garage — garage door / gate Sub-GHz decoder.
 *
 * Same UI as the main Sub-GHz app (the scenes/views are copies of
 * applications/main/subghz), but shipped as an external FAP carrying its own
 * garage/gate protocol catalog, so those protocols do not consume internal flash.
 *
 * Trimmed vs the main app: no RPC, no "Add Manually" (SetType family), no
 * frequency analyzer, no KeeLoq/PSA/Hitag2 tooling, no car-emulate view.
 */

#include <furi/core/log.h>
#include <lib/subghz/types.h>
#include <lib/toolbox/path.h>
#include <float_tools.h>
#include "subghz_i.h"

#define TAG "ARFGarage"

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

static SubGhz* arf_garage_alloc(void) {
    SubGhz* subghz = malloc(sizeof(SubGhz));

    subghz->file_path = furi_string_alloc();
    subghz->file_path_tmp = furi_string_alloc();

    // GUI
    subghz->gui = furi_record_open(RECORD_GUI);

    // View Dispatcher
    subghz->view_dispatcher = view_dispatcher_alloc();

    subghz->scene_manager = scene_manager_alloc(&subghz_scene_handlers, subghz);
    view_dispatcher_set_event_callback_context(subghz->view_dispatcher, subghz);
    view_dispatcher_set_custom_event_callback(
        subghz->view_dispatcher, subghz_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        subghz->view_dispatcher, subghz_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        subghz->view_dispatcher, subghz_tick_event_callback, 100);

    // Notifications
    subghz->notifications = furi_record_open(RECORD_NOTIFICATION);

    subghz->txrx = subghz_txrx_alloc();

    // SubMenu
    subghz->submenu = submenu_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdMenu, submenu_get_view(subghz->submenu));

    // Receiver
    subghz->subghz_receiver = subghz_view_receiver_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdReceiver,
        subghz_view_receiver_get_view(subghz->subghz_receiver));

    // Popup
    subghz->popup = popup_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdPopup, popup_get_view(subghz->popup));

    // Text Input
    subghz->text_input = text_input_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdTextInput, text_input_get_view(subghz->text_input));

    // Byte Input
    subghz->byte_input = byte_input_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdByteInput, byte_input_get_view(subghz->byte_input));

    // Custom Widget
    subghz->widget = widget_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher, SubGhzViewIdWidget, widget_get_view(subghz->widget));

    // Dialog
    subghz->dialogs = furi_record_open(RECORD_DIALOGS);

    // Transmitter
    subghz->subghz_transmitter = subghz_view_transmitter_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdTransmitter,
        subghz_view_transmitter_get_view(subghz->subghz_transmitter));

    // Variable Item List
    subghz->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdVariableItemList,
        variable_item_list_get_view(subghz->variable_item_list));

    // Read RAW
    subghz->subghz_read_raw = subghz_read_raw_alloc(false);
    view_dispatcher_add_view(
        subghz->view_dispatcher,
        SubGhzViewIdReadRAW,
        subghz_read_raw_get_view(subghz->subghz_read_raw));

    // Threshold RSSI
    subghz->threshold_rssi = subghz_threshold_rssi_alloc();

    subghz_unlock(subghz);

    subghz->last_settings = subghz_last_settings_alloc();
    subghz_last_settings_load(subghz->last_settings, 0);

    furi_hal_subghz_set_ext_leds_and_amp(subghz->last_settings->leds_and_amp);

    subghz_txrx_set_preset_internal(
        subghz->txrx,
        subghz->last_settings->frequency,
        subghz->last_settings->preset_index,
        subghz->tx_power);
    subghz->history = subghz_history_alloc();

    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);

    subghz->ignore_filter = subghz->last_settings->ignore_filter;
    subghz->filter = subghz->last_settings->filter;
    subghz->tx_power = subghz->last_settings->tx_power;

    subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);
    subghz_txrx_set_need_save_callback(subghz->txrx, subghz_save_to_file, subghz);

    if(!float_is_equal(subghz->last_settings->rssi, 0)) {
        subghz_threshold_rssi_set(subghz->threshold_rssi, subghz->last_settings->rssi);
    } else {
        subghz->last_settings->rssi = SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_TRIGGER;
    }

    subghz->raw_send_only = false;
    subghz->error_str = furi_string_alloc();

    return subghz;
}

static void arf_garage_free(SubGhz* subghz) {
    furi_assert(subghz);

    subghz_txrx_speaker_off(subghz->txrx);
    subghz_txrx_stop(subghz->txrx);
    subghz_txrx_sleep(subghz->txrx);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdReceiver);
    subghz_view_receiver_free(subghz->subghz_receiver);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdTextInput);
    text_input_free(subghz->text_input);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdByteInput);
    byte_input_free(subghz->byte_input);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdWidget);
    widget_free(subghz->widget);

    furi_record_close(RECORD_DIALOGS);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdTransmitter);
    subghz_view_transmitter_free(subghz->subghz_transmitter);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
    variable_item_list_free(subghz->variable_item_list);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdReadRAW);
    subghz_read_raw_free(subghz->subghz_read_raw);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdMenu);
    submenu_free(subghz->submenu);

    view_dispatcher_remove_view(subghz->view_dispatcher, SubGhzViewIdPopup);
    popup_free(subghz->popup);

    scene_manager_free(subghz->scene_manager);
    view_dispatcher_free(subghz->view_dispatcher);

    furi_record_close(RECORD_GUI);
    subghz->gui = NULL;

    subghz_last_settings_free(subghz->last_settings);
    subghz_threshold_rssi_free(subghz->threshold_rssi);
    subghz_history_free(subghz->history);
    subghz_txrx_free(subghz->txrx);

    furi_string_free(subghz->error_str);

    furi_record_close(RECORD_NOTIFICATION);
    subghz->notifications = NULL;

    furi_string_free(subghz->file_path);
    furi_string_free(subghz->file_path_tmp);

    free(subghz);
}

int32_t arf_garage_app(void* p) {
    // Accepted arguments:
    //   "read"        -> jump straight into the receiver
    //   "<file.sub>"  -> load that file and open the transmitter / read-raw view
    //   NULL / ""     -> start menu
    const char* args = (const char*)p;
    bool open_receiver = (args && strcmp(args, "read") == 0);

    SubGhz* subghz = arf_garage_alloc();

    view_dispatcher_attach_to_gui(
        subghz->view_dispatcher, subghz->gui, ViewDispatcherTypeFullscreen);

    if(open_receiver) {
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiver);
    } else if(args && strlen(args)) {
        if(subghz_key_load(subghz, args, true)) {
            furi_string_set(subghz->file_path, args);
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
    } else {
        furi_string_set(subghz->file_path, SUBGHZ_APP_FOLDER);
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneStart);
    }

    furi_hal_power_suppress_charge_enter();

    view_dispatcher_run(subghz->view_dispatcher);

    furi_hal_power_suppress_charge_exit();

    arf_garage_free(subghz);

    return 0;
}

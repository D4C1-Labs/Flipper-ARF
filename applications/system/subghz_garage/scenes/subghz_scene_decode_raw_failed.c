#include "../subghz_i.h"
#include "../subghz_protocol_filter.h"
#include "../helpers/subghz_custom_event.h"
#include <lib/subghz/subghz_protocol_registry.h>

#define TAG "SubGhzSceneDecodeRawFailed"
#define DECODE_FAILED_POPUP_TIMEOUT_MS 3000

static bool g_all_protocols_were_enabled = true;

void subghz_scene_decode_raw_failed_set_context(bool all_protocols_enabled) {
    g_all_protocols_were_enabled = all_protocols_enabled;
}

static void decode_raw_failed_cleanup_and_return(SubGhz* subghz) {
    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateStart);

    subghz->idx_menu_chosen = 0;
    subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);

    if(subghz_file_encoder_worker_is_running(subghz->decode_raw_file_worker_encoder)) {
        subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
    }
    subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);

    subghz->state_notifications = SubGhzNotificationStateIDLE;
    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerNoSet);

    if(!scene_manager_search_and_switch_to_previous_scene(
           subghz->scene_manager, SubGhzSceneMoreRAW)) {
        subghz_return_to_launcher(subghz);
    }
}

static void decode_raw_failed_popup_cb(void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventDecodeRawFailedCancel);
}

static void decode_raw_failed_widget_cb(GuiButtonType result, InputType type, void* context) {
    SubGhz* subghz = context;
    if(type != InputTypeShort) return;

    if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventDecodeRawFailedRetry);
    } else if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventDecodeRawFailedCancel);
    }
}

void subghz_scene_decode_raw_failed_on_enter(void* context) {
    SubGhz* subghz = context;

    if(g_all_protocols_were_enabled) {
        subghz_ensure_popup(subghz);
        Popup* popup = subghz->popup;
        popup_set_header(popup, "No match found", 64, 6, AlignCenter, AlignTop);
        popup_set_text(
            popup,
            "Failed to decode with\navailable protocols\n(at used modulation)",
            64, 22, AlignCenter, AlignTop);
        popup_set_timeout(popup, DECODE_FAILED_POPUP_TIMEOUT_MS);
        popup_set_context(popup, subghz);
        popup_set_callback(popup, decode_raw_failed_popup_cb);
        popup_enable_timeout(popup);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdPopup);
    } else {
        subghz_ensure_widget(subghz);
        Widget* widget = subghz->widget;
        widget_add_string_multiline_element(
            widget, 64, 8, AlignCenter, AlignTop, FontPrimary, "Decode Failed");

        widget_add_string_multiline_element(
            widget, 64, 18, AlignCenter, AlignTop, FontSecondary,
            "Raw sub failed to decode\nwith the available\nprotocols.");
        widget_add_button_element(
            widget, GuiButtonTypeLeft, "Cancel", decode_raw_failed_widget_cb, subghz);
        widget_add_button_element(
            widget, GuiButtonTypeRight, "Re-Run All", decode_raw_failed_widget_cb, subghz);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
    }
}

bool subghz_scene_decode_raw_failed_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeBack) {
        decode_raw_failed_cleanup_and_return(subghz);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventDecodeRawFailedCancel) {
            decode_raw_failed_cleanup_and_return(subghz);
            return true;
        }
        if(event.event == SubGhzCustomEventDecodeRawFailedRetry) {

            subghz_garage_protocol_filter_reset(subghz->protocol_filter);
            subghz_garage_protocol_filter_save(subghz->protocol_filter);

            if(subghz->decode_raw_file_worker_encoder &&
               subghz_file_encoder_worker_is_running(subghz->decode_raw_file_worker_encoder)) {
                subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
            }

            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneDecodeRAW, SubGhzDecodeRawStateStart);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneDecodeRAW);
            return true;
        }
    }

    return false;
}

void subghz_scene_decode_raw_failed_on_exit(void* context) {
    SubGhz* subghz = context;
    if(subghz->popup) {
        popup_reset(subghz->popup);
    }
    if(subghz->widget) {
        widget_reset(subghz->widget);
    }
}

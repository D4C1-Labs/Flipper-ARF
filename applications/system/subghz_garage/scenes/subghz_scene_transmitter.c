#include "../subghz_i.h"
#include "../views/transmitter.h"

#include "../helpers/subghz_custom_btn_compat.h"
#include "../helpers/subghz_lib_ext_compat.h"
#include <string.h>

#include <lib/subghz/devices/devices.h>
#include "../helpers/subghz_txrx_i.h"
#include "lib/subghz/blocks/generic.h"
#include "../helpers/subghz_generic_global_compat.h"
#define TAG "SubGhzSceneTransmitter"

void subghz_scene_transmitter_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static bool txsc_btn_label(const char* s, char* d, size_t n) {
    const char* btn = strstr(s, "Btn:");
    const char* p   = btn ? strstr(btn, "[") : NULL;
    if(!p) { d[0]=0; return false; }
    p++; size_t i=0;
    while(p[i] && p[i]!=']' && i<n-1) { d[i]=p[i]; i++; }
    d[i]=0;
    return i>0;
}

bool subghz_scene_transmitter_update_data_show(void* context) {

    SubGhz* subghz = context;
    bool ret = false;
    SubGhzProtocolDecoderBase* decoder = subghz_txrx_get_decoder(subghz->txrx);

    if(!decoder) {
        subghz_view_transmitter_set_radio_device_type(
            subghz->subghz_transmitter, subghz_txrx_radio_device_get(subghz->txrx));
        return false;
    }

    FuriString* key_str        = furi_string_alloc();
    FuriString* frequency_str  = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();
    FlipperFormat* fff         = subghz_txrx_get_fff_data(subghz->txrx);

    uint8_t active_btn = subghz_custom_btn_get();

    subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_OK);
    if(subghz_protocol_decoder_base_deserialize(decoder, fff) == SubGhzProtocolStatusOk) {
        subghz_protocol_decoder_base_get_string(decoder, key_str);

        subghz_txrx_get_frequency_and_modulation(
            subghz->txrx, frequency_str, modulation_str, true);

        subghz_view_transmitter_add_data_to_show(
            subghz->subghz_transmitter,
            furi_string_get_cstr(key_str),
            furi_string_get_cstr(frequency_str),
            furi_string_get_cstr(modulation_str),
            subghz_txrx_protocol_is_transmittable(subghz->txrx, false));

        if(subghz_custom_btn_is_allowed()) {

            if(active_btn == SUBGHZ_CUSTOM_BTN_OK ||
               !subghz_view_transmitter_is_labels_ready(subghz->subghz_transmitter)) {

                char clabel[14] = {0};
                txsc_btn_label(furi_string_get_cstr(key_str), clabel, sizeof(clabel));
                if(!clabel[0]) snprintf(clabel, sizeof(clabel), "SEND");

                const char* ul = NULL;
                const char* dl = NULL;
                const char* ll = NULL;
                const char* rl = NULL;

                if(!ul) ul = "^";
                if(!dl) dl = "v";
                if(!ll) ll = "<";
                if(!rl) rl = ">";

                bool u_vis = (strcmp(ul, clabel) != 0);
                bool d_vis = (strcmp(dl, clabel) != 0);
                bool l_vis = (strcmp(ll, clabel) != 0);
                bool r_vis = (strcmp(rl, clabel) != 0);

                subghz_view_transmitter_set_btn_labels(
                    subghz->subghz_transmitter,
                    clabel,
                    ul, u_vis,
                    dl, d_vis,
                    ll, l_vis,
                    rl, r_vis);
            }

        }

        ret = true;
    }
    furi_string_free(frequency_str);
    furi_string_free(modulation_str);
    furi_string_free(key_str);

    subghz_view_transmitter_set_radio_device_type(
        subghz->subghz_transmitter, subghz_txrx_radio_device_get(subghz->txrx));
    return ret;
}

void subghz_scene_transmitter_on_enter(void* context) {
    SubGhz* subghz = context;

    subghz_ensure_transmitter_view(subghz);
    subghz_custom_btns_reset();

    subghz_view_transmitter_reset_labels(subghz->subghz_transmitter);

    if(!subghz_scene_transmitter_update_data_show(subghz)) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventViewTransmitterError);
    }

    subghz_view_transmitter_set_callback(
        subghz->subghz_transmitter, subghz_scene_transmitter_callback, subghz);

    subghz->state_notifications = SubGhzNotificationStateIDLE;
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdTransmitter);
}

bool subghz_scene_transmitter_on_event(void* context, SceneManagerEvent event) {

    SubGhz* subghz = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventViewTransmitterSendStart) {

            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_block_generic_global.endless_tx = true;
            if(subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx))) {
                subghz->state_notifications = SubGhzNotificationStateTx;
                subghz_scene_transmitter_update_data_show(subghz);
            }
            return true;
        } else if(event.event == SubGhzCustomEventViewTransmitterSendStop) {

            subghz_block_generic_global.endless_tx = false;
            return true;
        } else if(event.event == SubGhzCustomEventViewTransmitterBack) {
            subghz_return_to_launcher(subghz);
            return true;
        } else if(event.event == SubGhzCustomEventViewTransmitterPageChange) {

            subghz_scene_transmitter_update_data_show(subghz);
            return true;
        } else if(event.event == SubGhzCustomEventViewTransmitterError) {
            furi_string_set(subghz->error_str, "Protocol not\nfound!");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(subghz->state_notifications == SubGhzNotificationStateTx) {

            if(!subghz_devices_is_async_complete_tx(subghz->txrx->radio_device)) {
                notification_message(subghz->notifications, &sequence_blink_magenta_10);
                return true;

            } else {
                subghz->state_notifications = SubGhzNotificationStateIDLE;
                subghz_txrx_stop(subghz->txrx);
                if(subghz_custom_btn_get() != SUBGHZ_CUSTOM_BTN_OK) {
                    subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_OK);
                    int32_t tmp_counter = subghz_garage_get_rolling_counter_mult();
                    subghz_garage_set_rolling_counter_mult(0);

                    subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx));
                    subghz_txrx_stop(subghz->txrx);

                    subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx));
                    subghz_txrx_stop(subghz->txrx);
                    subghz_garage_set_rolling_counter_mult(tmp_counter);
                }
            }
        }
        return true;
    }
    return false;
}

void subghz_scene_transmitter_on_exit(void* context) {
    SubGhz* subghz = context;
    subghz->state_notifications = SubGhzNotificationStateIDLE;

    subghz_txrx_reset_dynamic_and_custom_btns(subghz->txrx);
}

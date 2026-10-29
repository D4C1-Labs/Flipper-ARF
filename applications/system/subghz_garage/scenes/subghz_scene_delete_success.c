#include "../subghz_i.h"
#include "../helpers/subghz_custom_event.h"

void subghz_scene_delete_success_popup_callback(void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(
        subghz->view_dispatcher, SubGhzCustomEventSceneDeleteSuccess);
}

void subghz_scene_delete_success_on_enter(void* context) {
    SubGhz* subghz = context;

    subghz_ensure_popup(subghz);
    Popup* popup = subghz->popup;

    popup_set_header(popup, "Deleted", 80, 19, AlignLeft, AlignBottom);
    popup_set_timeout(popup, 1500);
    popup_set_context(popup, subghz);
    popup_set_callback(popup, subghz_scene_delete_success_popup_callback);
    popup_enable_timeout(popup);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdPopup);
}

bool subghz_scene_delete_success_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneDeleteSuccess) {
            if(subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateRAWLoad) {
                if(scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneSaved)) {
                } else {
                    subghz_return_to_launcher(subghz);
                }
            } else {
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);

                if(scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneReadRAW)) {
                } else {
                    subghz_return_to_launcher(subghz);
                }
            }

            return true;
        }
    }
    return false;
}

void subghz_scene_delete_success_on_exit(void* context) {
    SubGhz* subghz = context;
    Popup* popup = subghz->popup;

    popup_reset(popup);
}

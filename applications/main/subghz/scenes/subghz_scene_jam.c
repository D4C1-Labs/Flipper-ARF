#include "../subghz_i.h"

#define TAG "SubGhzSceneJam"

// [ARF] JAM! scene: pick one of the pre-recorded RAW jamming captures from
// /ext/subghz/jam and play it back through the existing Read RAW TX flow. This
// reuses ARF's proven RAW TX path (no custom view, no extra txrx APIs), so the
// jammer is just "load a RAW .sub and transmit it".
void subghz_scene_jam_on_enter(void* context) {
    SubGhz* subghz = context;

    if(subghz_load_jam_from_file(subghz)) {
        if(subghz_get_load_type_file(subghz) == SubGhzLoadTypeFileRaw) {
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReadRAW);
        } else {
            // A non-RAW file in the jam folder is not a valid jammer capture;
            // fall back to the saved menu so the user still sees its info.
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWLoad);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSavedMenu);
        }
    } else {
        scene_manager_search_and_switch_to_previous_scene(subghz->scene_manager, SubGhzSceneStart);
    }
}

bool subghz_scene_jam_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subghz_scene_jam_on_exit(void* context) {
    UNUSED(context);
}

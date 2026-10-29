#include "../subghz_i.h"
#include "../helpers/subghz_custom_event.h"

void subghz_scene_file_prefix_text_input_callback(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(
        subghz->view_dispatcher, SubGhzCustomEventSceneFilePrefixDone);
}

void subghz_scene_file_prefix_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz_ensure_text_input(subghz);

    TextInput* text_input = subghz->text_input;

    strncpy(
        subghz->file_name_tmp,
        subghz->last_settings->file_prefix,
        sizeof(subghz->last_settings->file_prefix) - 1);
    subghz->file_name_tmp[sizeof(subghz->last_settings->file_prefix) - 1] = '\0';

    text_input_set_header_text(text_input, "File Naming Prefix");
    text_input_set_result_callback(
        text_input,
        subghz_scene_file_prefix_text_input_callback,
        subghz,
        subghz->file_name_tmp,
        sizeof(subghz->last_settings->file_prefix),
        true);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdTextInput);
}

bool subghz_scene_file_prefix_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventSceneFilePrefixDone) {
            strncpy(
                subghz->last_settings->file_prefix,
                subghz->file_name_tmp,
                sizeof(subghz->last_settings->file_prefix) - 1);
            subghz->last_settings->file_prefix[sizeof(subghz->last_settings->file_prefix) - 1] =
                '\0';
            subghz_save_all(subghz);
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    }

    return false;
}

void subghz_scene_file_prefix_on_exit(void* context) {
    SubGhz* subghz = context;
    text_input_reset(subghz->text_input);
}

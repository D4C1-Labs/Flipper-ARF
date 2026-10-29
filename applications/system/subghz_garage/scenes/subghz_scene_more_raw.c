#include "../subghz_i.h"
#include <loader/loader.h>
#include <storage/storage.h>

void subghz_blank_transition_draw_cb(Canvas* canvas, void* ctx);

#define SUBGHZ_RAW_EDIT_FAP_PATH EXT_PATH("apps/Sub-GHz/subghz_raw_edit.fap")

enum SubmenuIndex {
    SubmenuIndexDecode,
    SubmenuIndexRawEdit,
    SubmenuIndexEdit,
    SubmenuIndexDelete,
};

void subghz_scene_more_raw_submenu_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, index);
}

void subghz_scene_more_raw_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz_ensure_submenu(subghz);

    submenu_reset(subghz->submenu);

    submenu_add_item(
        subghz->submenu,
        "Decode",
        SubmenuIndexDecode,
        subghz_scene_more_raw_submenu_callback,
        subghz);

    {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        bool has_raw_edit = storage_file_exists(storage, SUBGHZ_RAW_EDIT_FAP_PATH);
        furi_record_close(RECORD_STORAGE);
        if(has_raw_edit) {
            submenu_add_item(
                subghz->submenu,
                "Edit RAW",
                SubmenuIndexRawEdit,
                subghz_scene_more_raw_submenu_callback,
                subghz);
        }
    }

    submenu_add_item(
        subghz->submenu,
        "Rename",
        SubmenuIndexEdit,
        subghz_scene_more_raw_submenu_callback,
        subghz);

    submenu_add_item(
        subghz->submenu,
        "Delete",
        SubmenuIndexDelete,
        subghz_scene_more_raw_submenu_callback,
        subghz);

    submenu_set_selected_item(
        subghz->submenu, scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneMoreRAW));

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
}

bool subghz_scene_more_raw_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubmenuIndexRawEdit) {
            if(subghz_file_available(subghz)) {

                {
                    Storage* raw_storage = furi_record_open(RECORD_STORAGE);
                    File* raw_f = storage_file_alloc(raw_storage);
                    if(storage_file_open(raw_f, "/ext/subghz/.focus_file",
                                         FSAM_WRITE, FSOM_CREATE_ALWAYS)) {

                    const char* fp = furi_string_get_cstr(subghz->file_path);
                    const char* prefix = "rawreturn:";
                    storage_file_write(raw_f, prefix, strlen(prefix));
                    storage_file_write(raw_f, fp, strlen(fp));
                    }
                    storage_file_close(raw_f);
                    storage_file_free(raw_f);
                    furi_record_close(RECORD_STORAGE);
                }

                if(!subghz->blank_transition_viewport) {
                    subghz->blank_transition_viewport = view_port_alloc();
                    view_port_draw_callback_set(
                        subghz->blank_transition_viewport,
                        subghz_blank_transition_draw_cb, NULL);
                    gui_add_view_port(
                        subghz->gui,
                        subghz->blank_transition_viewport,
                        GuiLayerFullscreen);
                    view_port_update(subghz->blank_transition_viewport);
                }

                Loader* loader = furi_record_open(RECORD_LOADER);
                loader_enqueue_launch(
                    loader,
                    SUBGHZ_RAW_EDIT_FAP_PATH,
                    furi_string_get_cstr(subghz->file_path),
                    LoaderDeferredLaunchFlagNone);
                furi_record_close(RECORD_LOADER);

                scene_manager_stop(subghz->scene_manager);
                view_dispatcher_stop(subghz->view_dispatcher);
                return true;
            } else {
                subghz_return_to_launcher(subghz);
            }
        } else if(event.event == SubmenuIndexDelete) {
            if(subghz_file_available(subghz)) {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerNoSet);
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneMoreRAW, SubmenuIndexDelete);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneDeleteRAW);
                return true;
            } else {
                subghz_return_to_launcher(subghz);
            }
        } else if(event.event == SubmenuIndexEdit) {
            if(subghz_file_available(subghz)) {
                furi_string_reset(subghz->file_path_tmp);
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneMoreRAW, SubmenuIndexEdit);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveName);
                return true;
            } else {
                subghz_return_to_launcher(subghz);
            }
        } else if(event.event == SubmenuIndexDecode) {
            if(subghz_file_available(subghz)) {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneMoreRAW, SubmenuIndexDecode);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneDecodeRAW);
                return true;
            } else {
                subghz_return_to_launcher(subghz);
            }
        }
    }
    return false;
}

void subghz_scene_more_raw_on_exit(void* context) {
    SubGhz* subghz = context;
    submenu_reset(subghz->submenu);
}

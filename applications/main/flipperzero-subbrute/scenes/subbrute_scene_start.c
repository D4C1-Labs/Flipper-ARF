#include "../subbrute_i.h"
#include "subbrute_scene.h"

#define TAG "SubBruteSceneStart"

void subbrute_scene_start_callback(SubBruteCustomEvent event, void* context) {
    furi_assert(context);

    SubBruteState* instance = (SubBruteState*)context;
    view_dispatcher_send_custom_event(instance->view_dispatcher, event);
}

void subbrute_scene_start_on_enter(void* context) {
    furi_assert(context);
    FURI_LOG_I(TAG, "on_enter");
    SubBruteState* instance = (SubBruteState*)context;
    SubBruteMainView* view = instance->view_main;

    instance->current_view = SubBruteViewMain;
    subbrute_main_view_set_callback(view, subbrute_scene_start_callback, instance);

    instance->device->attack = instance->settings->last_index;

    subbrute_main_view_set_index(
        view,
        instance->settings->last_index,
        instance->settings->repeat_values,
        false,
        instance->device->two_bytes,
        0);

    view_dispatcher_switch_to_view(instance->view_dispatcher, instance->current_view);
}

void subbrute_scene_start_on_exit(void* context) {
    furi_assert(context);
    FURI_LOG_I(TAG, "on_exit");
}

static void subbrute_scene_start_exit_app(SubBruteState* instance) {
    FURI_LOG_I(TAG, "exit_app: capture settings");
    instance->settings->last_index = subbrute_main_view_get_index(instance->view_main);
    subbrute_settings_set_repeats(
        instance->settings, subbrute_main_view_get_repeats(instance->view_main));

    FURI_LOG_I(TAG, "exit_app: scene_manager_stop");
    scene_manager_stop(instance->scene_manager);
    FURI_LOG_I(TAG, "exit_app: view_dispatcher_stop");
    view_dispatcher_stop(instance->view_dispatcher);
    FURI_LOG_I(TAG, "exit_app: dispatcher stopped");
}

bool subbrute_scene_start_on_event(void* context, SceneManagerEvent event) {
    SubBruteState* instance = (SubBruteState*)context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubBruteCustomEventTypeMenuSelected) {
            instance->settings->last_index = subbrute_main_view_get_index(instance->view_main);
            subbrute_settings_set_repeats(
                instance->settings, subbrute_main_view_get_repeats(instance->view_main));
            uint8_t total_repeats = subbrute_settings_get_current_repeats(instance->settings);

            if((subbrute_device_attack_set(
                    instance->device, instance->settings->last_index, total_repeats) !=
                SubBruteFileResultOk) ||
               (!subbrute_worker_init_default_attack(
                   instance->worker,
                   instance->settings->last_index,
                   instance->device->current_step,
                   instance->device->protocol_info,
                   instance->device->extra_repeats))) {
                furi_crash("Invalid attack set!");
            }
            subbrute_settings_save(instance->settings);
            scene_manager_next_scene(instance->scene_manager, SubBruteSceneSetupAttack);

            consumed = true;
        } else if(event.event == SubBruteCustomEventTypeLoadFile) {
            scene_manager_next_scene(instance->scene_manager, SubBruteSceneLoadFile);
            consumed = true;
        } else if(event.event == SubBruteCustomEventTypeBackPressed) {
            FURI_LOG_I(TAG, "event: BackPressed");
            subbrute_scene_start_exit_app(instance);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        FURI_LOG_I(TAG, "event: SceneManager Back");
        subbrute_scene_start_exit_app(instance);
        consumed = true;
    }

    return consumed;
}

#include "subbrute_i.h"
#include "scenes/subbrute_scene.h"

#include <string.h>

#define TAG "SubBruteApp"

static bool subbrute_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    SubBruteState* instance = context;
    return scene_manager_handle_custom_event(instance->scene_manager, event);
}

static bool subbrute_back_event_callback(void* context) {
    furi_assert(context);
    SubBruteState* instance = context;
    return scene_manager_handle_back_event(instance->scene_manager);
}

static void subbrute_tick_event_callback(void* context) {
    furi_assert(context);
    SubBruteState* instance = context;
    scene_manager_handle_tick_event(instance->scene_manager);
}

SubBruteState* subbrute_alloc() {
    FURI_LOG_I(TAG, "alloc");
    SubBruteState* instance = malloc(sizeof(SubBruteState));
    memset(instance, 0, sizeof(SubBruteState));

    instance->file_path = furi_string_alloc();

    instance->scene_manager = scene_manager_alloc(&subbrute_scene_handlers, instance);
    instance->view_dispatcher = view_dispatcher_alloc();

    instance->gui = furi_record_open(RECORD_GUI);

    view_dispatcher_set_event_callback_context(instance->view_dispatcher, instance);
    view_dispatcher_set_custom_event_callback(
        instance->view_dispatcher, subbrute_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        instance->view_dispatcher, subbrute_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        instance->view_dispatcher, subbrute_tick_event_callback, 100);

    instance->dialogs = furi_record_open(RECORD_DIALOGS);
    instance->notifications = furi_record_open(RECORD_NOTIFICATION);

    subghz_devices_init();

    instance->radio_device =
        subbrute_radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);
    FURI_LOG_I(
        TAG,
        "radio=%s external=%d",
        instance->radio_device ? instance->radio_device->name : "NULL",
        instance->radio_device ? radio_device_loader_is_external(instance->radio_device) : -1);

    subghz_devices_reset(instance->radio_device);
    subghz_devices_idle(instance->radio_device);

    instance->device = subbrute_device_alloc(instance->radio_device);
    instance->worker = subbrute_worker_alloc(instance->radio_device);

    instance->text_input = text_input_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher,
        SubBruteViewTextInput,
        text_input_get_view(instance->text_input));

    instance->widget = widget_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, SubBruteViewWidget, widget_get_view(instance->widget));

    instance->var_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher,
        SubBruteViewVarList,
        variable_item_list_get_view(instance->var_list));

    instance->popup = popup_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, SubBruteViewPopup, popup_get_view(instance->popup));

    instance->view_stack = view_stack_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher, SubBruteViewStack, view_stack_get_view(instance->view_stack));

    instance->view_main = subbrute_main_view_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher,
        SubBruteViewMain,
        subbrute_main_view_get_view(instance->view_main));

    instance->view_attack = subbrute_attack_view_alloc();
    view_dispatcher_add_view(
        instance->view_dispatcher,
        SubBruteViewAttack,
        subbrute_attack_view_get_view(instance->view_attack));

    instance->settings = subbrute_settings_alloc();
    subbrute_settings_load(instance->settings);

    return instance;
}

void subbrute_free(SubBruteState* instance) {
    furi_assert(instance);
    FURI_LOG_I(TAG, "free: begin");

    subbrute_worker_free(instance->worker);
    subbrute_device_free(instance->device);
    subghz_devices_deinit();
    subbrute_settings_free(instance->settings);

    notification_message(instance->notifications, &sequence_blink_stop);
    furi_record_close(RECORD_NOTIFICATION);
    instance->notifications = NULL;

    view_dispatcher_remove_view(instance->view_dispatcher, SubBruteViewMain);
    subbrute_main_view_free(instance->view_main);

    view_dispatcher_remove_view(instance->view_dispatcher, SubBruteViewAttack);
    subbrute_attack_view_free(instance->view_attack);

    view_dispatcher_remove_view(instance->view_dispatcher, SubBruteViewTextInput);
    text_input_free(instance->text_input);

    view_dispatcher_remove_view(instance->view_dispatcher, SubBruteViewWidget);
    widget_free(instance->widget);

    view_dispatcher_remove_view(instance->view_dispatcher, SubBruteViewVarList);
    variable_item_list_free(instance->var_list);

    view_dispatcher_remove_view(instance->view_dispatcher, SubBruteViewPopup);
    popup_free(instance->popup);

    view_dispatcher_remove_view(instance->view_dispatcher, SubBruteViewStack);
    view_stack_free(instance->view_stack);

    furi_record_close(RECORD_DIALOGS);
    instance->dialogs = NULL;

    scene_manager_free(instance->scene_manager);
    view_dispatcher_free(instance->view_dispatcher);

    furi_record_close(RECORD_GUI);
    instance->gui = NULL;

    furi_string_free(instance->file_path);

    free(instance);
    FURI_LOG_I(TAG, "free: done");
}

void subbrute_text_input_callback(void* context) {
    furi_assert(context);
    SubBruteState* instance = context;
    view_dispatcher_send_custom_event(
        instance->view_dispatcher, SubBruteCustomEventTypeTextEditDone);
}

void subbrute_popup_closed_callback(void* context) {
    furi_assert(context);
    SubBruteState* instance = context;
    view_dispatcher_send_custom_event(
        instance->view_dispatcher, SubBruteCustomEventTypePopupClosed);
}

int32_t subbrute_app(void* p) {
    UNUSED(p);
    FURI_LOG_I(TAG, "app start");
    furi_hal_power_suppress_charge_enter();

    SubBruteState* instance = subbrute_alloc();
    view_dispatcher_attach_to_gui(
        instance->view_dispatcher, instance->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(instance->scene_manager, SubBruteSceneStart);

    notification_message(instance->notifications, &sequence_display_backlight_on);
    view_dispatcher_run(instance->view_dispatcher);
    FURI_LOG_I(TAG, "dispatcher exited");

    subbrute_settings_save(instance->settings);
    subbrute_free(instance);

    furi_hal_power_suppress_charge_exit();
    FURI_LOG_I(TAG, "app exit");

    return 0;
}

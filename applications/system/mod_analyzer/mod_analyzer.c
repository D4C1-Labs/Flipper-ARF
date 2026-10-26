#include "mod_analyzer_view.h"
#include "helpers/radio_device_loader.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <dialogs/dialogs.h>
#include <loader/loader.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/subghz_setting.h>

#define TAG "ModAnalyzer"

#define MOD_ANALYZER_SETTING_FILE EXT_PATH("subghz/assets/setting_user")
#define MOD_ANALYZER_LAST_PATH    EXT_PATH("subghz/assets/last_subghz.settings")

typedef enum {
    ModAnalyzerViewId,
} ModAnalyzerViewIndex;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SubGhzSetting* setting;
    const SubGhzDevice* device;
    ModAnalyzerView* view;
} ModAnalyzer;

static void mod_analyzer_last_load_frequency(uint32_t* frequency) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_existing(file, MOD_ANALYZER_LAST_PATH)) {
        uint32_t freq = 0;
        if(flipper_format_read_uint32(file, "Frequency", &freq, 1) && freq > 0) {
            *frequency = freq;
        }
    }

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void mod_analyzer_last_store_frequency(uint32_t frequency) {
    if(frequency == 0) return;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_existing(file, MOD_ANALYZER_LAST_PATH)) {
        flipper_format_insert_or_update_uint32(file, "Frequency", &frequency, 1);
    }

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void mod_analyzer_show_radio_error(void) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Mod Analyzer", 64, 2, AlignCenter, AlignTop);
    dialog_message_set_text(
        message, "This radio has no\nRSSI burst path.", 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, NULL, NULL, "OK");
    dialog_message_show(dialogs, message);
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
}

static void mod_analyzer_view_event_callback(ModAnalyzerViewEvent event, void* context) {
    ModAnalyzer* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, (uint32_t)event);
}

static bool mod_analyzer_custom_event_callback(void* context, uint32_t event) {
    ModAnalyzer* app = context;
    if((ModAnalyzerViewEvent)event != ModAnalyzerViewEventOpenReceiver) return false;

    uint32_t freq = mod_analyzer_view_get_frequency(app->view);
    if(freq == 0) return true;

    mod_analyzer_last_store_frequency(freq);

    char args[24];
    snprintf(args, sizeof(args), "rx %lu", (unsigned long)freq);
    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(loader, "subghz", args, LoaderDeferredLaunchFlagGui);
    furi_record_close(RECORD_LOADER);
    view_dispatcher_switch_to_view(app->view_dispatcher, VIEW_NONE);
    return true;
}

static uint32_t mod_analyzer_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static ModAnalyzer* mod_analyzer_alloc(void) {
    ModAnalyzer* app = malloc(sizeof(ModAnalyzer));

    subghz_devices_init();
    app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);

    app->setting = subghz_setting_alloc();
    subghz_setting_load(app->setting, MOD_ANALYZER_SETTING_FILE);

    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, mod_analyzer_custom_event_callback);

    app->view = mod_analyzer_view_alloc(app->device, app->setting);
    mod_analyzer_view_set_callback(app->view, mod_analyzer_view_event_callback, app);

    uint32_t last_freq = 0;
    mod_analyzer_last_load_frequency(&last_freq);
    if(last_freq > 0 && subghz_devices_is_frequency_valid(app->device, last_freq)) {
        mod_analyzer_view_set_frequency(app->view, last_freq);
    }

    View* view = mod_analyzer_view_get_view(app->view);
    view_set_previous_callback(view, mod_analyzer_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, ModAnalyzerViewId, view);

    return app;
}

static void mod_analyzer_free(ModAnalyzer* app) {
    furi_assert(app);

    mod_analyzer_last_store_frequency(mod_analyzer_view_get_frequency(app->view));

    view_dispatcher_remove_view(app->view_dispatcher, ModAnalyzerViewId);
    mod_analyzer_view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    subghz_setting_free(app->setting);

    if(app->device) radio_device_loader_end(app->device);
    subghz_devices_deinit();

    free(app);
}

int32_t mod_analyzer_app(void* p) {
    UNUSED(p);
    ModAnalyzer* app = mod_analyzer_alloc();

    if(!app->device || !subghz_devices_rssi_burst_is_supported(app->device)) {
        FURI_LOG_E(TAG, "radio has no RSSI burst path");
        mod_analyzer_show_radio_error();
        mod_analyzer_free(app);
        return 0;
    }

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, ModAnalyzerViewId);

    view_dispatcher_run(app->view_dispatcher);

    mod_analyzer_free(app);
    return 0;
}

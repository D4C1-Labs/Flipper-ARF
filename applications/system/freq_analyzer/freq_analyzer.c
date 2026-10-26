#include "freq_analyzer_view.h"
#include "helpers/radio_device_loader.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <notification/notification_messages.h>
#include <loader/loader.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/subghz_setting.h>

#define TAG "FreqAnalyzer"

#define FREQ_ANALYZER_DEFAULT_TRIGGER (-93.0f)
#define FREQ_ANALYZER_SETTING_FILE    EXT_PATH("subghz/assets/setting_user")
#define FREQ_ANALYZER_LAST_PATH       EXT_PATH("subghz/assets/last_subghz.settings")

typedef enum {
    FreqAnalyzerViewId,
} FreqAnalyzerViewIndex;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    NotificationApp* notifications;
    SubGhzSetting* setting;
    const SubGhzDevice* device;
    FreqAnalyzerView* view;
} FreqAnalyzer;

static void freq_analyzer_last_load(float* trigger, uint32_t* feedback) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_existing(file, FREQ_ANALYZER_LAST_PATH)) {
        uint32_t level = 0;
        if(flipper_format_read_uint32(file, "FeedbackLevel", &level, 1)) {
            if(level <= FreqAnalyzerFeedbackLevelMute) *feedback = level;
        } else {
            flipper_format_rewind(file);
        }
        float fa_trigger = 0.0f;
        if(flipper_format_read_float(file, "FATrigger", &fa_trigger, 1)) {
            *trigger = fa_trigger;
        }
    }

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void freq_analyzer_last_store(uint32_t frequency, uint32_t feedback, float trigger) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_existing(file, FREQ_ANALYZER_LAST_PATH)) {
        if(frequency > 0) {
            flipper_format_insert_or_update_uint32(file, "Frequency", &frequency, 1);
        }
        flipper_format_insert_or_update_uint32(file, "FeedbackLevel", &feedback, 1);
        flipper_format_insert_or_update_float(file, "FATrigger", &trigger, 1);
    }

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void freq_analyzer_view_event_callback(FreqAnalyzerViewEvent event, void* context) {
    FreqAnalyzer* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, (uint32_t)event);
}

static bool freq_analyzer_custom_event_callback(void* context, uint32_t event) {
    FreqAnalyzer* app = context;
    switch((FreqAnalyzerViewEvent)event) {
    case FreqAnalyzerViewEventLock:
        notification_message(app->notifications, &sequence_set_green_255);
        switch(freq_analyzer_view_get_feedback_level(app->view)) {
        case FreqAnalyzerFeedbackLevelAll:
            notification_message(app->notifications, &sequence_success);
            break;
        case FreqAnalyzerFeedbackLevelVibro:
            notification_message(app->notifications, &sequence_single_vibro);
            break;
        case FreqAnalyzerFeedbackLevelMute:
            break;
        }
        notification_message(app->notifications, &sequence_display_backlight_on);
        return true;
    case FreqAnalyzerViewEventUnlock:
        notification_message(app->notifications, &sequence_reset_rgb);
        return true;
    case FreqAnalyzerViewEventOpenReceiver: {
        uint32_t freq = freq_analyzer_view_get_handoff_frequency(app->view);
        if(freq > 0) {
            freq_analyzer_last_store(
                freq,
                freq_analyzer_view_get_feedback_level(app->view),
                freq_analyzer_view_get_trigger_level(app->view));
            char args[24];
            snprintf(args, sizeof(args), "rx %lu", (unsigned long)freq);
            Loader* loader = furi_record_open(RECORD_LOADER);
            loader_enqueue_launch(loader, "subghz", args, LoaderDeferredLaunchFlagGui);
            furi_record_close(RECORD_LOADER);
            view_dispatcher_switch_to_view(app->view_dispatcher, VIEW_NONE);
        }
        return true;
    }
    }
    return false;
}

static uint32_t freq_analyzer_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static FreqAnalyzer* freq_analyzer_alloc(void) {
    FreqAnalyzer* app = malloc(sizeof(FreqAnalyzer));

    subghz_devices_init();
    app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);

    app->setting = subghz_setting_alloc();
    subghz_setting_load(app->setting, FREQ_ANALYZER_SETTING_FILE);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, freq_analyzer_custom_event_callback);

    float trigger = FREQ_ANALYZER_DEFAULT_TRIGGER;
    uint32_t feedback = FreqAnalyzerFeedbackLevelMute;
    freq_analyzer_last_load(&trigger, &feedback);
    if(trigger < -97.0f) trigger = -97.0f;
    if(trigger > -60.0f) trigger = -60.0f;

    app->view = freq_analyzer_view_alloc(
        app->device,
        app->setting,
        trigger,
        (FreqAnalyzerFeedbackLevel)feedback,
        radio_device_loader_is_external(app->device));
    freq_analyzer_view_set_callback(app->view, freq_analyzer_view_event_callback, app);

    View* view = freq_analyzer_view_get_view(app->view);
    view_set_previous_callback(view, freq_analyzer_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, FreqAnalyzerViewId, view);

    return app;
}

static void freq_analyzer_free(FreqAnalyzer* app) {
    furi_assert(app);

    freq_analyzer_last_store(
        0,
        freq_analyzer_view_get_feedback_level(app->view),
        freq_analyzer_view_get_trigger_level(app->view));

    view_dispatcher_remove_view(app->view_dispatcher, FreqAnalyzerViewId);
    freq_analyzer_view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);

    notification_message(app->notifications, &sequence_reset_rgb);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    subghz_setting_free(app->setting);

    if(app->device) {
        radio_device_loader_end(app->device);
    }
    subghz_devices_deinit();

    free(app);
}

int32_t freq_analyzer_app(void* p) {
    UNUSED(p);
    FreqAnalyzer* app = freq_analyzer_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, FreqAnalyzerViewId);

    view_dispatcher_run(app->view_dispatcher);

    freq_analyzer_free(app);
    return 0;
}

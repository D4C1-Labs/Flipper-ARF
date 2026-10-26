#include "nfc_sniffer_trace.h"
#include "nfc_sniffer_worker.h"
#include "views/nfc_sniffer_monitor.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#define TAG "NfcSniffer"

typedef enum {
    NfcSnifferViewMonitor,
} NfcSnifferViewId;

typedef enum {
    NfcSnifferEventRefresh = 0x100,
} NfcSnifferCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    NotificationApp* notifications;

    NfcSnifferTrace* trace;
    NfcSnifferWorker* worker;
    NfcSnifferMonitor* monitor;
} NfcSnifferApp;

static void nfc_sniffer_make_session_tag(char* tag, size_t cap, const char* suffix) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    snprintf(
        tag,
        cap,
        "%04u%02u%02u_%02u%02u%02u%s",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second,
        suffix ? suffix : "");
}

static void nfc_sniffer_worker_cb(void* context) {
    NfcSnifferApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, NfcSnifferEventRefresh);
}

static void nfc_sniffer_monitor_cb(NfcSnifferMonitorEvent event, void* context) {
    NfcSnifferApp* app = context;

    switch(event) {
    case NfcSnifferMonitorEventToggleRun: {
        if(nfc_sniffer_worker_is_running(app->worker)) {
            nfc_sniffer_worker_stop(app->worker);
            notification_message(app->notifications, &sequence_blink_stop);
        } else {
            nfc_sniffer_trace_reset(app->trace);
            nfc_sniffer_trace_sd_close(app->trace);

            char tag[40];
            nfc_sniffer_make_session_tag(tag, sizeof(tag), "");
            if(!nfc_sniffer_trace_sd_open(app->trace, tag)) {
                FURI_LOG_W(TAG, "SD open failed");
                notification_message(app->notifications, &sequence_error);
            }

            nfc_sniffer_worker_start(app->worker);
            notification_message(app->notifications, &sequence_blink_start_cyan);
        }
        nfc_sniffer_monitor_refresh(app->monitor);
        break;
    }
    }
}

static bool nfc_sniffer_custom_event_callback(void* context, uint32_t event) {
    NfcSnifferApp* app = context;
    if(event == NfcSnifferEventRefresh) {
        nfc_sniffer_monitor_refresh(app->monitor);
        return true;
    }
    return false;
}

static uint32_t nfc_sniffer_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static NfcSnifferApp* nfc_sniffer_app_alloc(void) {
    NfcSnifferApp* app = malloc(sizeof(NfcSnifferApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("nfc_sniffer"));
    furi_record_close(RECORD_STORAGE);

    app->trace = nfc_sniffer_trace_alloc();
    app->worker = nfc_sniffer_worker_alloc(app->trace);
    nfc_sniffer_worker_set_callback(app->worker, nfc_sniffer_worker_cb, app);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, nfc_sniffer_custom_event_callback);

    app->monitor = nfc_sniffer_monitor_alloc(app->trace, app->worker);
    nfc_sniffer_monitor_set_callback(app->monitor, nfc_sniffer_monitor_cb, app);

    View* view = nfc_sniffer_monitor_get_view(app->monitor);
    view_set_previous_callback(view, nfc_sniffer_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, NfcSnifferViewMonitor, view);

    return app;
}

static void nfc_sniffer_app_free(NfcSnifferApp* app) {
    furi_assert(app);

    nfc_sniffer_worker_stop(app->worker);

    view_dispatcher_remove_view(app->view_dispatcher, NfcSnifferViewMonitor);
    nfc_sniffer_monitor_free(app->monitor);
    view_dispatcher_free(app->view_dispatcher);

    nfc_sniffer_worker_free(app->worker);
    nfc_sniffer_trace_sd_close(app->trace);
    nfc_sniffer_trace_free(app->trace);

    notification_message(app->notifications, &sequence_blink_stop);
    notification_message(app->notifications, &sequence_reset_rgb);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t nfc_sniffer_app(void* p) {
    UNUSED(p);

    NfcSnifferApp* app = nfc_sniffer_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, NfcSnifferViewMonitor);
    view_dispatcher_run(app->view_dispatcher);

    nfc_sniffer_app_free(app);
    return 0;
}

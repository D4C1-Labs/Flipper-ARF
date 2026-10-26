#pragma once

#include "../nfc_sniffer_trace.h"
#include "../nfc_sniffer_worker.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NfcSnifferMonitor NfcSnifferMonitor;

typedef enum {
    NfcSnifferMonitorEventToggleRun,
} NfcSnifferMonitorEvent;

typedef void (*NfcSnifferMonitorCallback)(NfcSnifferMonitorEvent event, void* context);

NfcSnifferMonitor* nfc_sniffer_monitor_alloc(NfcSnifferTrace* trace, NfcSnifferWorker* worker);
void nfc_sniffer_monitor_free(NfcSnifferMonitor* monitor);

View* nfc_sniffer_monitor_get_view(NfcSnifferMonitor* monitor);

void nfc_sniffer_monitor_set_callback(
    NfcSnifferMonitor* monitor,
    NfcSnifferMonitorCallback callback,
    void* context);

void nfc_sniffer_monitor_refresh(NfcSnifferMonitor* monitor);

#ifdef __cplusplus
}
#endif

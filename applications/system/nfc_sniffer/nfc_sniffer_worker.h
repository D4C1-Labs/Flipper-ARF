#pragma once

#include "nfc_sniffer_trace.h"

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NfcSnifferWorker NfcSnifferWorker;

typedef enum {
    NfcSnifferStateStopped,
    NfcSnifferStateWaitField,
    NfcSnifferStateSniffing,
    NfcSnifferStateError,
} NfcSnifferState;

typedef void (*NfcSnifferWorkerCallback)(void* context);

NfcSnifferWorker* nfc_sniffer_worker_alloc(NfcSnifferTrace* trace);
void nfc_sniffer_worker_free(NfcSnifferWorker* worker);

void nfc_sniffer_worker_set_callback(
    NfcSnifferWorker* worker,
    NfcSnifferWorkerCallback callback,
    void* context);

void nfc_sniffer_worker_start(NfcSnifferWorker* worker);
void nfc_sniffer_worker_stop(NfcSnifferWorker* worker);
bool nfc_sniffer_worker_is_running(NfcSnifferWorker* worker);

NfcSnifferState nfc_sniffer_worker_get_state(NfcSnifferWorker* worker);
uint8_t nfc_sniffer_worker_coupling(NfcSnifferWorker* worker);

#ifdef __cplusplus
}
#endif

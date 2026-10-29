#include "nfc_sniffer_worker.h"
#include "nfc_sniffer_hf.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_nfc.h>

#include <string.h>

#define TAG "NfcSnifferWorker"

#define NFC_SNIFFER_WORKER_STACK (12U * 1024U)
#define NFC_SNIFFER_QUIET_MS     (250U)
#define NFC_SNIFFER_MAX_MS       (1500U)
#define NFC_SNIFFER_IDLE_GAP_MS  (100U)
#define NFC_SNIFFER_CARD_K_MIN   (90)

struct NfcSnifferWorker {
    FuriThread* thread;
    NfcSnifferTrace* trace;

    volatile bool running;
    volatile NfcSnifferState state;
    volatile uint8_t coupling;
    bool joinable;

    NfcSnifferWorkerCallback callback;
    void* callback_context;

    char* capture_buf;
};

static void nfc_sniffer_worker_notify(NfcSnifferWorker* worker) {
    if(worker->callback) worker->callback(worker->callback_context);
}

static void nfc_sniffer_worker_quality_filter(char* buf) {
    int k = -1;
    for(const char* p = buf; *p && *p != '\n'; p++) {
        if(p[0] == ' ' && p[1] == 'k') {
            k = 0;
            p += 2;
            while(*p >= '0' && *p <= '9')
                k = k * 10 + (*p++ - '0');
            break;
        }
    }
    if(k < 0 || k >= NFC_SNIFFER_CARD_K_MIN) return;
    char* w = buf;
    for(const char* r = buf; *r;) {
        const char* e = r;
        while(*e && *e != '\n')
            e++;
        if(*e == '\n') e++;
        if(r[0] != 'C') {
            size_t n = (size_t)(e - r);
            memmove(w, r, n);
            w += n;
        }
        r = e;
    }
    *w = '\0';
}

static void nfc_sniffer_worker_log_wire(const char* buf) {
    const char* p = buf;
    char line[112];
    while(*p) {
        size_t n = 0;
        while(p[n] && p[n] != '\n' && n < sizeof(line) - 1)
            n++;
        memcpy(line, p, n);
        line[n] = '\0';
        if(n) FURI_LOG_I(TAG, "%s", line);
        p += n;
        if(*p == '\n') p++;
    }
}

static void nfc_sniffer_worker_apply_coupling(NfcSnifferWorker* worker) {
    if(worker->capture_buf[0] == 'L' && worker->capture_buf[1] >= '0' &&
       worker->capture_buf[1] <= '2') {
        worker->coupling = (uint8_t)(worker->capture_buf[1] - '0');
    }
}

static int32_t nfc_sniffer_worker_thread(void* context) {
    NfcSnifferWorker* worker = context;

    FuriHalNfcError err = furi_hal_nfc_acquire();
    if(err != FuriHalNfcErrorNone) {
        FURI_LOG_E(TAG, "nfc acquire %d", err);
        worker->state = NfcSnifferStateError;
        worker->running = false;
        nfc_sniffer_worker_notify(worker);
        return 0;
    }

    furi_hal_nfc_low_power_mode_stop();

    worker->state = NfcSnifferStateWaitField;
    worker->coupling = 3;
    nfc_sniffer_worker_notify(worker);

    while(worker->running) {
        worker->state = NfcSnifferStateWaitField;
        nfc_sniffer_worker_notify(worker);

        int rc = nfc_sniffer_hf_capture(
            worker->capture_buf, NFC_SNIFFER_HF_TXT_CAP, NFC_SNIFFER_QUIET_MS, NFC_SNIFFER_MAX_MS);

        if(!worker->running) break;

        if(worker->capture_buf[0]) nfc_sniffer_worker_quality_filter(worker->capture_buf);

        if(rc == NFC_SNIFFER_HF_TIMEOUT) {
            if(worker->capture_buf[0]) {
                worker->state = NfcSnifferStateSniffing;
                nfc_sniffer_worker_log_wire(worker->capture_buf);
                nfc_sniffer_trace_add_wire_text(worker->trace, worker->capture_buf);
                nfc_sniffer_worker_apply_coupling(worker);
            } else {
                nfc_sniffer_trace_add_event(worker->trace, 0, "wait field");
                worker->state = NfcSnifferStateWaitField;
            }
            nfc_sniffer_trace_sd_sync(worker->trace);
            nfc_sniffer_worker_notify(worker);
            for(uint32_t i = 0; i < NFC_SNIFFER_IDLE_GAP_MS && worker->running; i++) {
                furi_delay_ms(1);
            }
            continue;
        }

        if(rc == NFC_SNIFFER_HF_ERR) {
            FURI_LOG_W(TAG, "hf sniff err %d", rc);
            if(worker->capture_buf[0]) {
                nfc_sniffer_trace_add_wire_text(worker->trace, worker->capture_buf);
            } else {
                nfc_sniffer_trace_add_event(worker->trace, 0, "sniff oom/err");
            }
            worker->state = NfcSnifferStateError;
            nfc_sniffer_trace_sd_sync(worker->trace);
            nfc_sniffer_worker_notify(worker);
            furi_delay_ms(200);
            if(worker->running) worker->state = NfcSnifferStateWaitField;
            continue;
        }

        worker->state = NfcSnifferStateSniffing;
        nfc_sniffer_worker_log_wire(worker->capture_buf);
        nfc_sniffer_trace_add_wire_text(worker->trace, worker->capture_buf);
        nfc_sniffer_worker_apply_coupling(worker);
        nfc_sniffer_trace_sd_sync(worker->trace);
        nfc_sniffer_worker_notify(worker);

        for(uint32_t i = 0; i < NFC_SNIFFER_IDLE_GAP_MS && worker->running; i++) {
            furi_delay_ms(1);
        }
    }

    nfc_sniffer_hf_request_stop();
    furi_hal_nfc_low_power_mode_start();
    furi_hal_nfc_release();

    worker->state = NfcSnifferStateStopped;
    nfc_sniffer_worker_notify(worker);
    return 0;
}

NfcSnifferWorker* nfc_sniffer_worker_alloc(NfcSnifferTrace* trace) {
    NfcSnifferWorker* worker = malloc(sizeof(NfcSnifferWorker));
    worker->trace = trace;
    worker->running = false;
    worker->state = NfcSnifferStateStopped;
    worker->coupling = 3;
    worker->joinable = false;
    worker->callback = NULL;
    worker->callback_context = NULL;
    worker->capture_buf = malloc(NFC_SNIFFER_HF_TXT_CAP);
    worker->thread = furi_thread_alloc_ex(
        "NfcSnifferWorker", NFC_SNIFFER_WORKER_STACK, nfc_sniffer_worker_thread, worker);
    return worker;
}

void nfc_sniffer_worker_free(NfcSnifferWorker* worker) {
    furi_assert(worker);
    nfc_sniffer_worker_stop(worker);
    furi_thread_free(worker->thread);
    free(worker->capture_buf);
    free(worker);
}

void nfc_sniffer_worker_set_callback(
    NfcSnifferWorker* worker,
    NfcSnifferWorkerCallback callback,
    void* context) {
    furi_assert(worker);
    worker->callback = callback;
    worker->callback_context = context;
}

static void nfc_sniffer_worker_ensure_joined(NfcSnifferWorker* worker) {
    if(worker->joinable) {
        furi_thread_join(worker->thread);
        worker->joinable = false;
    }
}

void nfc_sniffer_worker_start(NfcSnifferWorker* worker) {
    furi_assert(worker);
    if(worker->running) return;
    nfc_sniffer_worker_ensure_joined(worker);
    worker->running = true;
    worker->joinable = true;
    furi_thread_start(worker->thread);
}

void nfc_sniffer_worker_stop(NfcSnifferWorker* worker) {
    furi_assert(worker);
    worker->running = false;
    nfc_sniffer_hf_request_stop();
    nfc_sniffer_worker_ensure_joined(worker);
}

bool nfc_sniffer_worker_is_running(NfcSnifferWorker* worker) {
    furi_assert(worker);
    return worker->running;
}

NfcSnifferState nfc_sniffer_worker_get_state(NfcSnifferWorker* worker) {
    furi_assert(worker);
    return worker->state;
}

uint8_t nfc_sniffer_worker_coupling(NfcSnifferWorker* worker) {
    furi_assert(worker);
    return worker->coupling;
}

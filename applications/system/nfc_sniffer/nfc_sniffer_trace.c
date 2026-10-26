#include "nfc_sniffer_trace.h"

#include <furi.h>
#include <furi_hal.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define NFC_SNIFFER_DIR_PATH EXT_PATH("nfc_sniffer")

struct NfcSnifferTrace {
    FuriMutex* mutex;

    NfcSnifferFrame ring[NFC_SNIFFER_RING_SIZE];
    size_t head;
    size_t count;

    uint32_t total;
    uint32_t total_pcd;
    uint32_t total_picc;
    uint32_t period_ns;

    Storage* storage;
    File* log_file;
    bool sd_open;
};

NfcSnifferTrace* nfc_sniffer_trace_alloc(void) {
    NfcSnifferTrace* trace = malloc(sizeof(NfcSnifferTrace));
    memset(trace, 0, sizeof(*trace));
    trace->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    return trace;
}

void nfc_sniffer_trace_free(NfcSnifferTrace* trace) {
    furi_assert(trace);
    nfc_sniffer_trace_sd_close(trace);
    furi_mutex_free(trace->mutex);
    free(trace);
}

void nfc_sniffer_trace_reset(NfcSnifferTrace* trace) {
    furi_assert(trace);
    furi_mutex_acquire(trace->mutex, FuriWaitForever);
    trace->head = 0;
    trace->count = 0;
    trace->total = 0;
    trace->total_pcd = 0;
    trace->total_picc = 0;
    furi_mutex_release(trace->mutex);
}

static void nfc_sniffer_trace_push(NfcSnifferTrace* trace, const NfcSnifferFrame* f) {
    trace->ring[trace->head] = *f;
    trace->head = (trace->head + 1) % NFC_SNIFFER_RING_SIZE;
    if(trace->count < NFC_SNIFFER_RING_SIZE) trace->count++;

    trace->total++;
    if(f->dir == NfcSnifferDirPcd)
        trace->total_pcd++;
    else if(f->dir == NfcSnifferDirPicc)
        trace->total_picc++;
}

void nfc_sniffer_trace_add_event(NfcSnifferTrace* trace, uint32_t timestamp_us, const char* text) {
    furi_assert(trace);

    NfcSnifferFrame f;
    memset(&f, 0, sizeof(f));
    f.timestamp_us = timestamp_us;
    f.dir = NfcSnifferDirEvent;
    size_t len = text ? strlen(text) : 0;
    if(len > NFC_SNIFFER_FRAME_MAX_BYTES) len = NFC_SNIFFER_FRAME_MAX_BYTES;
    f.len = (uint16_t)len;
    if(len) memcpy(f.data, text, len);

    furi_mutex_acquire(trace->mutex, FuriWaitForever);
    nfc_sniffer_trace_push(trace, &f);
    furi_mutex_release(trace->mutex);
}

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void
    nfc_sniffer_trace_parse_line(NfcSnifferTrace* trace, const char* line, uint32_t period_ns) {
    if(!line || !line[0]) return;

    if(line[0] == 'L') {
        const char* p = strchr(line, 'p');
        if(p && p[1] >= '0' && p[1] <= '9') {
            trace->period_ns = (uint32_t)strtoul(p + 1, NULL, 10);
            period_ns = trace->period_ns;
        }
        NfcSnifferFrame f;
        memset(&f, 0, sizeof(f));
        f.dir = NfcSnifferDirEvent;
        size_t len = strlen(line);
        if(len > NFC_SNIFFER_FRAME_MAX_BYTES) len = NFC_SNIFFER_FRAME_MAX_BYTES;
        f.len = (uint16_t)len;
        memcpy(f.data, line, len);
        nfc_sniffer_trace_push(trace, &f);
        return;
    }

    if(line[0] != 'R' && line[0] != 'C') return;
    if(line[1] != ' ') return;

    NfcSnifferFrame f;
    memset(&f, 0, sizeof(f));
    f.dir = (line[0] == 'R') ? NfcSnifferDirPcd : NfcSnifferDirPicc;

    const char* s = line + 2;
    char* end = NULL;
    uint32_t t0 = (uint32_t)strtoul(s, &end, 10);
    if(!end || *end != ' ') return;
    s = end + 1;
    uint32_t t1 = (uint32_t)strtoul(s, &end, 10);
    UNUSED(t1);
    if(!end) return;
    s = end;

    if(period_ns) {
        f.timestamp_us = (uint32_t)(((uint64_t)t0 * period_ns) / 1000ULL);
    } else {
        f.timestamp_us = t0;
    }

    size_t nbytes = 0;
    while(*s) {
        while(*s == ' ')
            s++;
        if(!*s) break;
        int hi = hex_nibble(s[0]);
        int lo = (s[1]) ? hex_nibble(s[1]) : -1;
        if(hi < 0 || lo < 0) break;
        if(nbytes < NFC_SNIFFER_FRAME_MAX_BYTES) {
            f.data[nbytes++] = (uint8_t)((hi << 4) | lo);
        } else {
            f.truncated = true;
        }
        s += 2;
        if(*s == '!') {
            f.parity_err = true;
            s++;
        }
    }
    f.len = (uint16_t)nbytes;

    if(nbytes == 1 && (f.data[0] == 0x26 || f.data[0] == 0x52)) {
        f.bits = 7;
    } else {
        f.bits = (uint16_t)(nbytes * 8);
    }
    f.crc_ok = !f.parity_err;
    nfc_sniffer_trace_push(trace, &f);
}

void nfc_sniffer_trace_add_wire_text(NfcSnifferTrace* trace, const char* text) {
    furi_assert(trace);
    if(!text) return;

    furi_mutex_acquire(trace->mutex, FuriWaitForever);

    if(trace->sd_open && trace->log_file) {
        size_t n = strlen(text);
        if(n) {
            storage_file_write(trace->log_file, text, n);
            if(text[n - 1] != '\n') storage_file_write(trace->log_file, "\n", 1);
            storage_file_write(trace->log_file, "----\n", 5);
        }
    }

    uint32_t period = trace->period_ns ? trace->period_ns : 406;
    const char* p = text;
    while(*p) {
        const char* nl = strchr(p, '\n');
        char line[192];
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if(len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        if(len && line[len - 1] == '\r') line[len - 1] = '\0';
        nfc_sniffer_trace_parse_line(trace, line, period);
        if(!nl) break;
        p = nl + 1;
    }

    furi_mutex_release(trace->mutex);
}

void nfc_sniffer_trace_sd_raw(NfcSnifferTrace* trace, const char* text) {
    furi_assert(trace);
    if(!text) return;
    furi_mutex_acquire(trace->mutex, FuriWaitForever);
    if(trace->sd_open && trace->log_file) {
        storage_file_write(trace->log_file, text, strlen(text));
    }
    furi_mutex_release(trace->mutex);
}

uint32_t nfc_sniffer_trace_count_dir(NfcSnifferTrace* trace, NfcSnifferDir dir) {
    furi_assert(trace);
    furi_mutex_acquire(trace->mutex, FuriWaitForever);
    uint32_t v = (dir == NfcSnifferDirPcd) ? trace->total_pcd :
                 (dir == NfcSnifferDirPicc) ?
                                             trace->total_picc :
                                             (trace->total - trace->total_pcd - trace->total_picc);
    furi_mutex_release(trace->mutex);
    return v;
}

size_t nfc_sniffer_trace_snapshot(NfcSnifferTrace* trace, NfcSnifferFrame* out, size_t max) {
    furi_assert(trace);
    furi_assert(out);

    furi_mutex_acquire(trace->mutex, FuriWaitForever);
    size_t n = trace->count < max ? trace->count : max;
    for(size_t i = 0; i < n; i++) {
        size_t age = trace->count - n + i;
        size_t idx =
            (trace->head + NFC_SNIFFER_RING_SIZE - trace->count + age) % NFC_SNIFFER_RING_SIZE;
        out[i] = trace->ring[idx];
    }
    furi_mutex_release(trace->mutex);
    return n;
}

bool nfc_sniffer_trace_sd_open(NfcSnifferTrace* trace, const char* session_tag) {
    furi_assert(trace);
    if(trace->sd_open) return true;

    trace->storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(trace->storage, NFC_SNIFFER_DIR_PATH);

    char path[128];
    trace->log_file = storage_file_alloc(trace->storage);
    snprintf(path, sizeof(path), "%s/%s.txt", NFC_SNIFFER_DIR_PATH, session_tag);
    bool ok = storage_file_open(trace->log_file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);

    if(!ok) {
        storage_file_free(trace->log_file);
        trace->log_file = NULL;
        furi_record_close(RECORD_STORAGE);
        trace->storage = NULL;
        return false;
    }

    const char* hdr = "# NFC sniffer trace\n# L-header then R/C sample-timestamped frames\n";
    storage_file_write(trace->log_file, hdr, strlen(hdr));
    trace->sd_open = true;
    return true;
}

void nfc_sniffer_trace_sd_close(NfcSnifferTrace* trace) {
    furi_assert(trace);
    if(!trace->sd_open) return;

    furi_mutex_acquire(trace->mutex, FuriWaitForever);
    trace->sd_open = false;
    if(trace->log_file) {
        storage_file_close(trace->log_file);
        storage_file_free(trace->log_file);
        trace->log_file = NULL;
    }
    furi_mutex_release(trace->mutex);

    if(trace->storage) {
        furi_record_close(RECORD_STORAGE);
        trace->storage = NULL;
    }
}

void nfc_sniffer_trace_sd_sync(NfcSnifferTrace* trace) {
    furi_assert(trace);
    furi_mutex_acquire(trace->mutex, FuriWaitForever);
    if(trace->sd_open && trace->log_file) storage_file_sync(trace->log_file);
    furi_mutex_release(trace->mutex);
}

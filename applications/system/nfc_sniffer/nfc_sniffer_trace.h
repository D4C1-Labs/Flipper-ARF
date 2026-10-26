#pragma once

#include <furi.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_SNIFFER_FRAME_MAX_BYTES (130U)
#define NFC_SNIFFER_RING_SIZE       (64U)

typedef enum {
    NfcSnifferDirPcd,
    NfcSnifferDirPicc,
    NfcSnifferDirEvent,
} NfcSnifferDir;

typedef struct {
    uint32_t timestamp_us;
    NfcSnifferDir dir;
    uint16_t bits;
    uint16_t len;
    bool crc_ok;
    bool truncated;
    bool parity_err;
    uint8_t data[NFC_SNIFFER_FRAME_MAX_BYTES];
} NfcSnifferFrame;

typedef struct NfcSnifferTrace NfcSnifferTrace;

NfcSnifferTrace* nfc_sniffer_trace_alloc(void);
void nfc_sniffer_trace_free(NfcSnifferTrace* trace);
void nfc_sniffer_trace_reset(NfcSnifferTrace* trace);

void nfc_sniffer_trace_add_event(NfcSnifferTrace* trace, uint32_t timestamp_us, const char* text);
void nfc_sniffer_trace_add_wire_text(NfcSnifferTrace* trace, const char* text);

void nfc_sniffer_trace_sd_raw(NfcSnifferTrace* trace, const char* text);

uint32_t nfc_sniffer_trace_count_dir(NfcSnifferTrace* trace, NfcSnifferDir dir);

size_t nfc_sniffer_trace_snapshot(NfcSnifferTrace* trace, NfcSnifferFrame* out, size_t max);

bool nfc_sniffer_trace_sd_open(NfcSnifferTrace* trace, const char* session_tag);
void nfc_sniffer_trace_sd_close(NfcSnifferTrace* trace);
void nfc_sniffer_trace_sd_sync(NfcSnifferTrace* trace);

#ifdef __cplusplus
}
#endif

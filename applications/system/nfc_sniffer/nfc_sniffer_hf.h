#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_SNIFFER_HF_TIMEOUT (-1)
#define NFC_SNIFFER_HF_ERR     (-6)

#define NFC_SNIFFER_HF_TXT_CAP (4096U)

int nfc_sniffer_hf_capture(char* out_buf, size_t out_cap, uint32_t quiet_ms, uint32_t max_ms);

void nfc_sniffer_hf_request_stop(void);

#ifdef __cplusplus
}
#endif

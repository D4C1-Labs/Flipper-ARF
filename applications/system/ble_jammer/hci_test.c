#include "hci_test.h"
#include <string.h>

struct hci_request {
    uint16_t ogf;
    uint16_t ocf;
    int event;
    void* cparam;
    int clen;
    void* rparam;
    int rlen;
};

extern int hci_send_req(struct hci_request* req, uint8_t async);

#define HCI_OGF_LE                    0x08
#define HCI_OCF_LE_TRANSMITTER_TEST   0x001E
#define HCI_OCF_LE_TEST_END           0x001F

int hci_test_tx_start(uint8_t rf_channel, uint8_t packet_type) {
    struct hci_request req;
    uint8_t params[3];
    uint8_t status;

    memset(&req, 0, sizeof(req));
    params[0] = rf_channel;
    params[1] = 37;
    params[2] = packet_type;

    req.ogf = HCI_OGF_LE;
    req.ocf = HCI_OCF_LE_TRANSMITTER_TEST;
    req.cparam = params;
    req.clen = sizeof(params);
    req.rparam = &status;
    req.rlen = 1;

    if(hci_send_req(&req, 0) < 0) return -1;
    return (status == 0) ? 0 : -1;
}

int hci_test_stop(void) {
    struct hci_request req;
    uint8_t resp[3];

    memset(&req, 0, sizeof(req));
    req.ogf = HCI_OGF_LE;
    req.ocf = HCI_OCF_LE_TEST_END;
    req.cparam = NULL;
    req.clen = 0;
    req.rparam = resp;
    req.rlen = sizeof(resp);

    if(hci_send_req(&req, 0) < 0) return -1;
    return (resp[0] == 0) ? 0 : -1;
}

int hci_test_nrf24_to_ble_ch(uint8_t nrf24_ch, int* ble_ch) {
    if(nrf24_ch < 2) return -1;
    int freq_mhz = 2400 + nrf24_ch;
    if(freq_mhz < 2402 || freq_mhz > 2480) return -1;
    *ble_ch = (nrf24_ch - 2) / 2;
    return 0;
}

#pragma once
#include <stdint.h>

int hci_test_tx_start(uint8_t rf_channel, uint8_t packet_type);
int hci_test_stop(void);
int hci_test_nrf24_to_ble_ch(uint8_t nrf24_ch, int* ble_ch);

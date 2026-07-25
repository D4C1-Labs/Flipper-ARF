#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t aci_gap_start_general_discovery_proc(uint16_t LE_Scan_Interval, uint16_t LE_Scan_Window, uint8_t Own_Address_Type, uint8_t Filter_Duplicates);
uint8_t aci_gap_terminate_gap_proc(uint8_t Procedure_Code);
uint8_t aci_gap_create_connection(uint16_t LE_Scan_Interval, uint16_t LE_Scan_Window, uint8_t Peer_Address_Type, const uint8_t* Peer_Address, uint8_t Own_Address_Type, uint16_t Conn_Interval_Min, uint16_t Conn_Interval_Max, uint16_t Conn_Latency, uint16_t Supervision_Timeout, uint16_t Minimum_CE_Length, uint16_t Maximum_CE_Length);
uint8_t hci_disconnect(uint16_t Connection_Handle, uint8_t Reason);
uint8_t aci_gatt_disc_all_primary_services(uint16_t Connection_Handle);
uint8_t aci_gatt_write_char_value(uint16_t Connection_Handle, uint16_t Attr_Handle, uint8_t Attribute_Val_Length, const uint8_t* Attribute_Val);
uint8_t aci_gatt_write_without_resp(uint16_t Connection_Handle, uint16_t Attr_Handle, uint8_t Attribute_Val_Length, const uint8_t* Attribute_Val);

#ifdef __cplusplus
}
#endif

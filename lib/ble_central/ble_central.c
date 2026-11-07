#include "ble_central.h"

#include <ble/ble.h>
#include <ble_glue.h>
#include <furi_ble/event_dispatcher.h>
#include <interface/patterns/ble_thread/tl/hci_tl.h>
#include <furi.h>

#define TAG "BleCentral"
#define BLE_CENTRAL_SCAN_INTERVAL 0x100
#define BLE_CENTRAL_SCAN_WINDOW  0x50

typedef struct {
    BleCentralEventCallback callback;
    void* context;
    GapSvcEventHandler* handler_ref;
    uint16_t connection_handle;
    bool connected;
    bool scanning;
} BleCentral;

static BleCentral* ble_central = NULL;

static const char* ble_central_status_str(uint8_t status) {
    switch(status) {
    case 0x00: return "SUCCESS";
    case 0x01: return "UNKNOWN_HCI_CMD";
    case 0x02: return "UNKNOWN_CONN_ID";
    case 0x03: return "HW_FAILURE";
    case 0x04: return "PAGE_TIMEOUT";
    case 0x05: return "AUTH_FAILURE";
    case 0x06: return "PIN_MISSING";
    case 0x07: return "MEM_CAP_EXCEEDED";
    case 0x08: return "CONN_TIMEOUT";
    case 0x09: return "CONN_LIMIT_EXCEEDED";
    case 0x0A: return "SYNC_CONN_LIMIT_EXCEEDED";
    case 0x0B: return "ACL_CONN_ALREADY_EXISTS";
    case 0x0C: return "CMD_DISALLOWED";
    case 0x0D: return "CONN_REJ_LIMITED_RESOURCES";
    case 0x0E: return "CONN_REJ_SECURITY_REASONS";
    case 0x0F: return "CONN_REJ_UNACCEPTABLE_BDADDR";
    case 0x10: return "CONN_ACCEPT_TIMEOUT";
    case 0x11: return "UNSUPPORTED_FEATURE";
    case 0x12: return "INVALID_HCI_CMD_PARAMS";
    case 0x13: return "REMOTE_USER_TERM_CONN";
    case 0x14: return "REMOTE_DEV_TERM_CONN_LOW_RESOURCES";
    case 0x15: return "REMOTE_DEV_TERM_CONN_POWER_OFF";
    case 0x16: return "CONN_TERM_BY_LOCAL_HOST";
    case 0x92: return "INVALID_PARAMS";
    case 0x97: return "ERROR";
    default: return "UNKNOWN";
    }
}

static BleCentralAdvertisedDevice* ble_central_parse_advert_report(void* data) {
    hci_event_pckt* event_pckt = (hci_event_pckt*)(((hci_uart_pckt*)data)->data);
    if(event_pckt->evt != HCI_LE_META_EVT_CODE) return NULL;

    evt_le_meta_event* meta_evt = (evt_le_meta_event*)event_pckt->data;
    if(meta_evt->subevent != HCI_LE_ADVERTISING_REPORT_SUBEVT_CODE) return NULL;

    hci_le_advertising_report_event_rp0* adv_rpt =
        (hci_le_advertising_report_event_rp0*)meta_evt->data;

    if(adv_rpt->Num_Reports == 0) return NULL;

    uint8_t data_len = adv_rpt->Advertising_Report[0].Length_Data;
    uint8_t* data_ptr = &adv_rpt->Advertising_Report[0].Length_Data + 1;
    int8_t rssi = *(int8_t*)(data_ptr + data_len);

    BleCentralAdvertisedDevice* device = malloc(sizeof(BleCentralAdvertisedDevice));
    device->address_type = adv_rpt->Advertising_Report[0].Address_Type;
    memcpy(device->address, adv_rpt->Advertising_Report[0].Address, 6);
    device->rssi = rssi;
    device->name[0] = '\0';

    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t field_len = data_ptr[pos];
        if(field_len == 0) break;
        uint8_t field_type = data_ptr[pos + 1];
        if(field_type == AD_TYPE_COMPLETE_LOCAL_NAME ||
           field_type == AD_TYPE_SHORTENED_LOCAL_NAME) {
            uint8_t name_len = field_len - 1;
            if(name_len > 31) name_len = 31;
            memcpy(device->name, &data_ptr[pos + 2], name_len);
            device->name[name_len] = '\0';
            break;
        }
        pos += field_len + 1;
    }

    return device;
}

static BleEventAckStatus ble_central_event_handler(void* event, void* context) {
    UNUSED(context);
    if(!ble_central || !ble_central->callback) return BleEventNotAck;

    hci_event_pckt* event_pckt = (hci_event_pckt*)(((hci_uart_pckt*)event)->data);

    FURI_LOG_I(TAG, "HCI event: evt=0x%02X", event_pckt->evt);

    if(event_pckt->evt == HCI_LE_META_EVT_CODE) {
        evt_le_meta_event* meta_evt = (evt_le_meta_event*)event_pckt->data;
        FURI_LOG_I(TAG, "LE Meta event: subevent=0x%02X", meta_evt->subevent);

        if(meta_evt->subevent == HCI_LE_ADVERTISING_REPORT_SUBEVT_CODE) {
            BleCentralAdvertisedDevice* device = ble_central_parse_advert_report(event);
            if(device) {
                FURI_LOG_I(TAG, "Device found: %s [%02X:%02X:%02X:%02X:%02X:%02X] rssi=%d",
                    device->name,
                    device->address[0], device->address[1], device->address[2],
                    device->address[3], device->address[4], device->address[5],
                    device->rssi);
                ble_central->callback(BleCentralEventDeviceFound, device, ble_central->context);
                free(device);
                return BleEventAckFlowEnable;
            }
        }
        return BleEventNotAck;
    }

    if(event_pckt->evt == HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) {
        evt_blecore_aci* blue_evt = (evt_blecore_aci*)event_pckt->data;
        FURI_LOG_I(TAG, "Vendor event: ecode=0x%04X", blue_evt->ecode);
        if(blue_evt->ecode == ACI_GAP_PROC_COMPLETE_VSEVT_CODE) {
            FURI_LOG_I(TAG, "GAP procedure complete");
            ble_central->scanning = false;
            ble_central->callback(BleCentralEventGattProcComplete, NULL, ble_central->context);
            return BleEventAckFlowEnable;
        }
        return BleEventNotAck;
    }

    if(event_pckt->evt == HCI_DISCONNECTION_COMPLETE_EVT_CODE) {
        FURI_LOG_W(TAG, "Disconnection complete (handle=0x%04X)", ble_central->connection_handle);
        ble_central->connected = false;
        ble_central->connection_handle = 0;
        ble_central->callback(BleCentralEventDisconnected, NULL, ble_central->context);
        return BleEventAckFlowEnable;
    }

    return BleEventNotAck;
}

static void ble_central_free(void) {
    if(!ble_central) return;
    if(ble_central->handler_ref) {
        ble_event_dispatcher_unregister_svc_handler(ble_central->handler_ref);
    }
    free(ble_central);
    ble_central = NULL;
}

bool ble_central_scan_start(BleCentralEventCallback callback, void* context) {
    furi_check(callback != NULL);
    furi_check(context != NULL);

    if(ble_central) {
        FURI_LOG_W(TAG, "scan_start: already initialized, cleaning up previous instance");
        ble_central_free();
    }

    FURI_LOG_I(TAG, "ble_central_scan_start: callback=%p ctx=%p", callback, context);

    ble_central = malloc(sizeof(BleCentral));
    ble_central->callback = callback;
    ble_central->context = context;
    ble_central->connected = false;
    ble_central->connection_handle = 0;
    ble_central->scanning = false;

    ble_central->handler_ref =
        ble_event_dispatcher_register_svc_handler(ble_central_event_handler, ble_central);
    if(!ble_central->handler_ref) {
        FURI_LOG_E(TAG, "ble_event_dispatcher_register_svc_handler FAILED");
        ble_central_free();
        return false;
    }
    FURI_LOG_I(TAG, "Event handler registered: %p", ble_central->handler_ref);

    FURI_LOG_I(TAG,
        "Calling aci_gap_start_general_discovery_proc(interval=0x%04X, window=0x%04X, addr_type=0x%02X, filter=0x%02X)",
        BLE_CENTRAL_SCAN_INTERVAL, BLE_CENTRAL_SCAN_WINDOW, 0x00, 0x00);

    tBleStatus status = aci_gap_start_general_discovery_proc(
        BLE_CENTRAL_SCAN_INTERVAL,
        BLE_CENTRAL_SCAN_WINDOW,
        0x00,
        0x00);

    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "aci_gap_start_general_discovery_proc FAILED: 0x%02X (%s)",
            status, ble_central_status_str(status));
        ble_central_free();
        return false;
    }

    ble_central->scanning = true;
    FURI_LOG_I(TAG, "Scan started successfully");
    return true;
}

bool ble_central_scan_stop(void) {
    if(!ble_central) {
        FURI_LOG_W(TAG, "scan_stop: not initialized");
        return false;
    }

    if(ble_central->scanning) {
        FURI_LOG_I(TAG, "Stopping scan (terminating GAP discovery proc)");
        tBleStatus status = aci_gap_terminate_gap_proc(GAP_GENERAL_DISCOVERY_PROC);
        if(status != BLE_STATUS_SUCCESS) {
            FURI_LOG_W(TAG, "aci_gap_terminate_gap_proc returned 0x%02X (%s) — may have already completed",
                status, ble_central_status_str(status));
        }
        ble_central->scanning = false;
    } else {
        FURI_LOG_I(TAG, "scan_stop: GAP proc already completed, cleaning up");
    }

    ble_central_free();
    FURI_LOG_I(TAG, "Scan stopped");
    return true;
}

bool ble_central_connect(const uint8_t* address, uint8_t address_type) {
    if(!ble_central) {
        FURI_LOG_E(TAG, "connect: ble_central is NULL (not initialized)");
        return false;
    }
    if(ble_central->connected) {
        FURI_LOG_W(TAG, "connect: already connected (handle=0x%04X)", ble_central->connection_handle);
        return false;
    }

    FURI_LOG_I(TAG, "Connecting to %02X:%02X:%02X:%02X:%02X:%02X type=%d",
        address[0], address[1], address[2], address[3], address[4], address[5], address_type);

    tBleStatus status = aci_gap_create_connection(
        0x100, 0x50,
        address_type,
        address,
        0x00,
        0x28, 0x38,
        0x00, 0x100,
        0x0010, 0x0010);

    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "aci_gap_create_connection FAILED: 0x%02X (%s)",
            status, ble_central_status_str(status));
        return false;
    }

    FURI_LOG_I(TAG, "Connection initiated, waiting for HCI_LE_CONNECTION_COMPLETE");
    return true;
}

bool ble_central_disconnect(void) {
    if(!ble_central) {
        FURI_LOG_E(TAG, "disconnect: ble_central is NULL");
        return false;
    }
    if(!ble_central->connected) {
        FURI_LOG_W(TAG, "disconnect: not connected");
        return false;
    }

    FURI_LOG_I(TAG, "Disconnecting handle=0x%04X reason=0x13", ble_central->connection_handle);
    tBleStatus status = hci_disconnect(ble_central->connection_handle, 0x13);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "hci_disconnect FAILED: 0x%02X (%s)",
            status, ble_central_status_str(status));
        return false;
    }

    ble_central->connected = false;
    ble_central->connection_handle = 0;
    return true;
}

bool ble_central_discover_services(void) {
    if(!ble_central) {
        FURI_LOG_E(TAG, "discover_services: ble_central is NULL");
        return false;
    }
    if(!ble_central->connected) {
        FURI_LOG_E(TAG, "discover_services: not connected");
        return false;
    }

    FURI_LOG_I(TAG, "Discovering primary services on handle=0x%04X",
        ble_central->connection_handle);
    tBleStatus status = aci_gatt_disc_all_primary_services(ble_central->connection_handle);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "aci_gatt_disc_all_primary_services FAILED: 0x%02X (%s)",
            status, ble_central_status_str(status));
        return false;
    }

    return true;
}

bool ble_central_write_command(uint16_t handle, const uint8_t* data, uint16_t len) {
    if(!ble_central) {
        FURI_LOG_E(TAG, "write_command: ble_central is NULL");
        return false;
    }
    if(!ble_central->connected) {
        FURI_LOG_E(TAG, "write_command: not connected");
        return false;
    }

    FURI_LOG_I(TAG, "Write without response: handle=0x%04X len=%u", handle, len);
    tBleStatus status = aci_gatt_write_without_resp(
        ble_central->connection_handle, handle, len, data);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "aci_gatt_write_without_resp FAILED: 0x%02X (%s)",
            status, ble_central_status_str(status));
        return false;
    }

    return true;
}

bool ble_central_write_request(uint16_t handle, const uint8_t* data, uint16_t len) {
    if(!ble_central) {
        FURI_LOG_E(TAG, "write_request: ble_central is NULL");
        return false;
    }
    if(!ble_central->connected) {
        FURI_LOG_E(TAG, "write_request: not connected");
        return false;
    }

    FURI_LOG_I(TAG, "Write with response: handle=0x%04X len=%u", handle, len);
    tBleStatus status = aci_gatt_write_char_value(
        ble_central->connection_handle, handle, len, data);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "aci_gatt_write_char_value FAILED: 0x%02X (%s)",
            status, ble_central_status_str(status));
        return false;
    }

    return true;
}

uint16_t ble_central_get_connection_handle(void) {
    if(!ble_central) return 0;
    return ble_central->connection_handle;
}

bool ble_central_is_connected(void) {
    if(!ble_central) return false;
    return ble_central->connected;
}

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BleCentralEventDeviceFound,
    BleCentralEventConnected,
    BleCentralEventDisconnected,
    BleCentralEventGattProcComplete,
    BleCentralEventDataReceived,
} BleCentralEventType;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    char name[32];
    int8_t rssi;
} BleCentralAdvertisedDevice;

typedef struct {
    uint16_t handle;
    uint8_t data[256];
    uint16_t len;
} BleCentralGattData;

typedef void (*BleCentralEventCallback)(BleCentralEventType event, void* data, void* context);

bool ble_central_scan_start(BleCentralEventCallback callback, void* context);
bool ble_central_scan_stop(void);
bool ble_central_connect(const uint8_t* address, uint8_t address_type);
bool ble_central_disconnect(void);
bool ble_central_discover_services(void);
bool ble_central_write_command(uint16_t handle, const uint8_t* data, uint16_t len);
bool ble_central_write_request(uint16_t handle, const uint8_t* data, uint16_t len);
uint16_t ble_central_get_connection_handle(void);
bool ble_central_is_connected(void);

#ifdef __cplusplus
}
#endif

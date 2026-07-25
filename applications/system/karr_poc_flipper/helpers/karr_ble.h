#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "qt_auth.h"
#include "qt_protocol.h"

#define KARR_BLE_SERVICE_UUID "49535343-FE7D-4AE5-8FA9-9FAFD205E455"
#define KARR_BLE_CHARACTERISTIC_UUID "49535343-1E4D-4BD9-BA61-23C647249616"

#define KARR_BLE_MAX_DEVICES 16
#define KARR_BLE_NAME_MAX_LEN 32

typedef struct {
    char name[KARR_BLE_NAME_MAX_LEN];
    uint8_t address[6];
    int8_t rssi;
} KarrBleDevice;

typedef enum {
    KarrBleStatus_ScanStarted,
    KarrBleStatus_DeviceFound,
    KarrBleStatus_Connecting,
    KarrBleStatus_Connected,
    KarrBleStatus_ServiceDiscovered,
    KarrBleStatus_AuthInitSent,
    KarrBleStatus_ChallengeReceived,
    KarrBleStatus_AuthResponseSent,
    KarrBleStatus_AuthSuccess,
    KarrBleStatus_AuthFailed,
    KarrBleStatus_CommandSent,
    KarrBleStatus_Disconnected,
    KarrBleStatus_Error,
} KarrBleStatus;

typedef void (*KarrBleStatusCallback)(KarrBleStatus status, const char* message, void* context);

bool karr_ble_scan_start(KarrBleStatusCallback callback, void* context);
void karr_ble_scan_stop(void);

bool karr_ble_connect_and_auth(
    const KarrBleDevice* device,
    QtMode mode,
    KarrBleStatusCallback callback,
    void* context);

bool karr_ble_send_command(QtCommand command);

void karr_ble_disconnect(void);

typedef enum {
    KarrMayhemResult_Success,
    KarrMayhemResult_AuthFailed,
    KarrMayhemResult_ConnectFailed,
} KarrMayhemResult;

typedef void (*KarrMayhemDeviceCallback)(
    const KarrBleDevice* device,
    KarrMayhemResult result,
    void* context);

void karr_ble_mayhem_run(
    const KarrBleDevice* devices,
    size_t device_count,
    QtMode mode,
    QtCommand command,
    KarrMayhemDeviceCallback callback,
    void* context);

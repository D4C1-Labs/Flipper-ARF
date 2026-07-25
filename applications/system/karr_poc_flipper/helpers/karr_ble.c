#include "karr_ble.h"
#include <furi.h>
#include <string.h>
#include "ble_central.h"

#define TAG "KarrBle"

typedef struct {
    KarrBleStatusCallback scan_callback;
    void* scan_context;
    bool scanning;
    bool connected;
    char unit_serial[KARR_BLE_NAME_MAX_LEN];
    uint8_t hash[QT_HASH_OUT_LEN];
} KarrBle;

static KarrBle* karr_ble = NULL;

static void karr_ble_on_central_event(BleCentralEventType event, void* data, void* context) {
    UNUSED(context);
    if(!karr_ble) return;

    switch(event) {
        case BleCentralEventDeviceFound: {
            BleCentralAdvertisedDevice* dev = (BleCentralAdvertisedDevice*)data;
            if(karr_ble->scan_callback) {
                karr_ble->scan_callback(
                    KarrBleStatus_DeviceFound, dev->name, karr_ble->scan_context);
            }
            break;
        }
        case BleCentralEventGattProcComplete: {
            if(karr_ble->scanning && karr_ble->scan_callback) {
                karr_ble->scan_callback(
                    KarrBleStatus_ScanStarted, "Discovery complete", karr_ble->scan_context);
            }
            break;
        }
        default:
            break;
    }
}

bool karr_ble_scan_start(KarrBleStatusCallback callback, void* context) {
    if(!karr_ble) {
        karr_ble = malloc(sizeof(KarrBle));
    }
    if(karr_ble->scanning) return false;

    karr_ble->scan_callback = callback;
    karr_ble->scan_context = context;
    karr_ble->scanning = true;

    if(!ble_central_scan_start(karr_ble_on_central_event, karr_ble)) {
        karr_ble->scanning = false;
        return false;
    }

    if(callback) callback(KarrBleStatus_ScanStarted, "Scanning...", context);
    return true;
}

void karr_ble_scan_stop(void) {
    if(!karr_ble || !karr_ble->scanning) return;
    ble_central_scan_stop();
    karr_ble->scanning = false;
    karr_ble->scan_callback = NULL;
    karr_ble->scan_context = NULL;
}

bool karr_ble_connect_and_auth(
    const KarrBleDevice* device,
    QtMode mode,
    KarrBleStatusCallback callback,
    void* context) {
    furi_assert(device);

    FURI_LOG_I(TAG, "connect_and_auth to '%s' (STUB)", device->name);
    strncpy(karr_ble->unit_serial, device->name, sizeof(karr_ble->unit_serial) - 1);
    karr_ble->unit_serial[sizeof(karr_ble->unit_serial) - 1] = '\0';

    if(callback) callback(KarrBleStatus_Connecting, device->name, context);
    karr_ble->connected = true;
    if(callback) callback(KarrBleStatus_Connected, "Connected", context);
    if(callback) callback(KarrBleStatus_ServiceDiscovered, "Service+Char OK", context);

    uint8_t frame_buf[QT_MAX_FRAME_LEN];
    size_t frame_len;

    frame_len = qt_protocol_generate(QtCmd_AuthInit, NULL, 0, frame_buf, sizeof(frame_buf));
    FURI_LOG_I(TAG, "TX Auth_Init (%zu bytes)", frame_len);
    if(callback) callback(KarrBleStatus_AuthInitSent, "0x22 sent", context);

    uint8_t fake_challenge[QT_CHALLENGE_LEN] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    char challenge_msg[32];
    snprintf(
        challenge_msg,
        sizeof(challenge_msg),
        "%02x%02x%02x%02x%02x%02x%02x%02x",
        fake_challenge[0], fake_challenge[1], fake_challenge[2], fake_challenge[3],
        fake_challenge[4], fake_challenge[5], fake_challenge[6], fake_challenge[7]);
    if(callback) callback(KarrBleStatus_ChallengeReceived, challenge_msg, context);

    bool hash_ok = qt_generate_hash(mode, fake_challenge, karr_ble->unit_serial, karr_ble->hash);
    if(!hash_ok) {
        FURI_LOG_E(TAG, "qt_generate_hash failed");
        if(callback) callback(KarrBleStatus_Error, "Invalid serial for this mode", context);
        return false;
    }

    frame_len = qt_protocol_generate(
        QtCmd_AuthResponse, karr_ble->hash, QT_HASH_OUT_LEN, frame_buf, sizeof(frame_buf));
    FURI_LOG_I(TAG, "TX Auth_Response (%zu bytes)", frame_len);
    if(callback) callback(KarrBleStatus_AuthResponseSent, "0x0F sent", context);
    if(callback) callback(KarrBleStatus_AuthSuccess, "0x10 received", context);

    return true;
}

bool karr_ble_send_command(QtCommand command) {
    if(!karr_ble || !karr_ble->connected) {
        FURI_LOG_E(TAG, "send_command without connection/auth");
        return false;
    }

    uint8_t frame_buf[QT_MAX_FRAME_LEN];
    size_t frame_len = qt_protocol_generate(command, NULL, 0, frame_buf, sizeof(frame_buf));
    FURI_LOG_I(TAG, "TX command 0x%02x (%zu bytes)", command, frame_len);
    return frame_len > 0;
}

void karr_ble_disconnect(void) {
    if(!karr_ble) return;
    FURI_LOG_I(TAG, "disconnect (STUB)");
    karr_ble->connected = false;
}

void karr_ble_mayhem_run(
    const KarrBleDevice* devices,
    size_t device_count,
    QtMode mode,
    QtCommand command,
    KarrMayhemDeviceCallback callback,
    void* context) {
    for(size_t i = 0; i < device_count; i++) {
        const KarrBleDevice* dev = &devices[i];
        FURI_LOG_I(TAG, "mayhem: device %zu/%zu '%s'", i + 1, device_count, dev->name);

        bool auth_ok = karr_ble_connect_and_auth(dev, mode, NULL, NULL);
        if(!auth_ok) {
            if(callback) callback(dev, KarrMayhemResult_AuthFailed, context);
            karr_ble_disconnect();
            continue;
        }

        bool sent = karr_ble_send_command(command);
        karr_ble_disconnect();
        if(callback) {
            callback(dev, sent ? KarrMayhemResult_Success : KarrMayhemResult_AuthFailed, context);
        }
    }
}

#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <notification/notification.h>
#include <input/input.h>
#include <storage/storage.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LF_MAX_EDGES         4096
#define LF_GAP_THRESHOLD_US  5000
#define LF_CARRIER_HZ        125000
#define LF_BIT_PERIOD_US     250
#define LF_TIMEOUT_MS        10000
#define LF_MAX_PACKETS       16

#define LF_INPUT_PIN  (&gpio_ext_pb2)

typedef struct {
    uint32_t duration_us;
    bool     level;
} LFEdge;

typedef struct {
    uint32_t start_idx;
    uint32_t edge_count;
    uint32_t duration_us;
} LFPacket;

typedef enum {
    LFStateIdle,
    LFStateCapturing,
    LFStateDone,
    LFStateSaving,
    LFStateSaved,
    LFStateError,
} LFState;

typedef struct {
    Gui*              gui;
    ViewPort*         view_port;
    FuriMessageQueue* queue;
    FuriMutex*        mutex;
    NotificationApp*  notif;

    LFState   state;
    char      status_msg[64];

    LFEdge*   edges;
    uint32_t  edge_count;
    uint32_t  total_time_us;

    LFPacket  packets[LF_MAX_PACKETS];
    uint32_t  packet_count;

    uint32_t  last_edge_time_us;
    bool      capturing;

    uint32_t  carrier_pulses;
    uint32_t  data_pulses;
    uint32_t  min_pulse_us;
    uint32_t  max_pulse_us;

    char      filename[128];
    uint32_t  file_index;
} LFSnifferApp;

int32_t lf_sniffer_app(void* p);

#ifdef __cplusplus
}
#endif
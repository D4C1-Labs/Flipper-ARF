#pragma once

#include <gui/gui.h>
#include <furi.h>
#include <furi_hal.h>
#include <lib/subghz/subghz_tx_rx_worker.h>
#include <stdint.h>

typedef enum {
    JammerModAm,
    JammerModFm,
    JammerModCount,
} JammerModulation;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;

    const SubGhzDevice* device;
    SubGhzTxRxWorker* subghz_txrx;
    FuriThread* tx_thread;
    volatile bool tx_running;

    uint32_t frequency;
    uint8_t freq_index;
    JammerModulation modulation;
    bool jamming;
    bool ext_radio;
    bool radio_open;

    float rssi;
} JammerApp;

JammerApp* jammer_app_alloc(void);
void jammer_app_free(JammerApp* app);
int32_t jammer_app(void* p);

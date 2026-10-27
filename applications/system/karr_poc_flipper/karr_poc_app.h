#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_box.h>

#include "helpers/karr_ble.h"
#include "helpers/qt_auth.h"
#include "helpers/qt_protocol.h"

#define KARR_POC_LOG_MAX_LEN 1024

typedef enum {
    KarrPocView_Submenu,
    KarrPocView_Widget,
    KarrPocView_TextBox,
} KarrPocView;

#define KARR_ALLOWLIST_MAX 4

extern const char* const karr_default_allowlist[];
extern const size_t karr_default_allowlist_count;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    Submenu* submenu;
    Widget* widget;
    TextBox* text_box;

    KarrBleDevice devices[KARR_BLE_MAX_DEVICES];
    size_t device_count;
    size_t selected_device;

    QtMode mode;
    QtCommand pending_command;

    char allowlist[KARR_ALLOWLIST_MAX][KARR_BLE_NAME_MAX_LEN];
    size_t allowlist_count;
    bool allowlist_enabled;
    QtMode mayhem_mode;
    QtCommand mayhem_command;

    FuriString* log_text;
} KarrPocApp;

typedef enum {
    KarrPocEvent_DeviceFound,
    KarrPocEvent_AuthStep,
    KarrPocEvent_AuthDone,
    KarrPocEvent_AuthFailed,
    KarrPocEvent_CommandDone,

    KarrPocEvent_MayhemToggleAllowlist,
    KarrPocEvent_MayhemUnlockUser,
    KarrPocEvent_MayhemLockUser,
    KarrPocEvent_MayhemUnlockDealer,
    KarrPocEvent_MayhemLockDealer,
    KarrPocEvent_MayhemConfirmYes,
    KarrPocEvent_MayhemConfirmNo,
    KarrPocEvent_MayhemScanDone,
    KarrPocEvent_MayhemRunDone,
} KarrPocCustomEvent;

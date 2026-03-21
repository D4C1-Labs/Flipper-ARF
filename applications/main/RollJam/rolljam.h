#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/dialog_ex.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>

#define TAG "RollJam"

#define RAW_SIGNAL_MAX_SIZE 4096

// ============================================================
// Frequencies
// ============================================================
typedef enum {
    FreqIndex_300_00 = 0,
    FreqIndex_303_87,
    FreqIndex_315_00,
    FreqIndex_318_00,
    FreqIndex_390_00,
    FreqIndex_433_07,
    FreqIndex_433_92,
    FreqIndex_434_42,
    FreqIndex_438_90,
    FreqIndex_868_35,
    FreqIndex_915_00,
    FreqIndex_COUNT,
} FreqIndex;

extern const uint32_t freq_values[];
extern const char* freq_names[];

// ============================================================
// Modulations
// ============================================================
typedef enum {
    ModIndex_AM650 = 0,
    ModIndex_AM270,
    ModIndex_FM238,
    ModIndex_FM476,
    ModIndex_COUNT,
} ModIndex;

extern const char* mod_names[];

// ============================================================
// Jam offsets
// ============================================================
typedef enum {
    JamOffIndex_300k = 0,
    JamOffIndex_500k,
    JamOffIndex_700k,
    JamOffIndex_1000k,
    JamOffIndex_COUNT,
} JamOffIndex;

extern const uint32_t jam_offset_values[];
extern const char* jam_offset_names[];

// ============================================================
// Hardware type
// ============================================================
typedef enum {
    HwIndex_CC1101 = 0,
    HwIndex_FluxCapacitor,
    HwIndex_COUNT,
} HwIndex;

extern const char* hw_names[];

// ============================================================
// Scenes
// ============================================================
typedef enum {
    RollJamSceneMenu,
    RollJamSceneAttackPhase1,
    RollJamSceneAttackPhase2,
    RollJamSceneAttackPhase3,
    RollJamSceneResult,
    RollJamSceneCount,
} RollJamScene;

// ============================================================
// Views
// ============================================================
typedef enum {
    RollJamViewVarItemList,
    RollJamViewWidget,
    RollJamViewDialogEx,
    RollJamViewPopup,
} RollJamView;

// ============================================================
// Custom events
// ============================================================
typedef enum {
    RollJamEventStartAttack = 100,
    RollJamEventSignalCaptured,
    RollJamEventPhase3Done,
    RollJamEventReplayNow,
    RollJamEventSaveSignal,
    RollJamEventBack,
} RollJamEvent;

// ============================================================
// Raw signal container
// ============================================================
typedef struct {
    int16_t data[RAW_SIGNAL_MAX_SIZE];
    size_t size;
    bool valid;
} RawSignal;

// ============================================================
// Main app struct
// ============================================================
typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notification;
    Storage* storage;

    VariableItemList* var_item_list;
    Widget* widget;
    DialogEx* dialog_ex;
    Popup* popup;

    FreqIndex freq_index;
    ModIndex mod_index;
    JamOffIndex jam_offset_index;
    HwIndex hw_index;
    uint32_t frequency;
    uint32_t jam_frequency;
    uint32_t jam_offset_hz;

    RawSignal signal_first;
    RawSignal signal_second;

    bool jamming_active;
    FuriThread* jam_thread;
    volatile bool jam_thread_running;

    volatile bool raw_capture_active;


} RollJamApp;

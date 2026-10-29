#pragma once

#include "helpers/subghz_types.h"
#include <lib/subghz/types.h>
#include "subghz.h"
#include "views/receiver.h"
#include "views/transmitter.h"
#include "views/subghz_signal_visualizer.h"
#include "subghz_protocol_filter.h"
#include "subghz_modulation_filter.h"
#include <gui/modules/loading.h>
#include <gui/view_holder.h>
#include "views/subghz_read_raw.h"

#include <gui/gui.h>
#include <gui/view_port.h>
#include <assets_icons.h>
#include <dialogs/dialogs.h>
#include <gui/scene_manager.h>
#include <notification/notification_messages.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/text_input.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/widget.h>

#include "scenes/subghz_scene.h"
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>

#include "subghz_history.h"
#include "subghz_last_settings.h"

#include <gui/modules/variable_item_list.h>
#include <lib/toolbox/path.h>

#include "rpc/rpc_app.h"
#include "helpers/rpc_gui_screen_suppress_compat.h"
#include <cli/cli_vcp.h>

#include "helpers/subghz_threshold_rssi.h"

#include "helpers/subghz_txrx.h"

#define SUBGHZ_MAX_LEN_NAME      64
#define SUBGHZ_EXT_PRESET_NAME   true
#define SUBGHZ_RAW_THRESHOLD_MIN (-90.0f)
#define SUBGHZ_RAW_THRESHOLD_DEFAULT (-65.0f)
#define SUBGHZ_MEASURE_LOADING   false

#define SUBGHZ_LOW_RAM_FREE_HEAP_READ    5000
#define SUBGHZ_LOW_RAM_FREE_HEAP         14000
#define SUBGHZ_LOW_RAM_RECOVER_FREE_HEAP 16000

#define SUBGHZ_GARAGE_WORKER_RAM_COST 11000

#define SUBGHZ_LOW_RAM_FREE_HEAP_SAVED_LOAD 44000

#define SUBGHZ_LOW_RAM_FREE_HEAP_CHAIN_START (SUBGHZ_LOW_RAM_FREE_HEAP + SUBGHZ_GARAGE_WORKER_RAM_COST)

#define SUBGHZ_LOW_RAM_GRACE_MS 2000

struct SubGhz {
    Gui* gui;
    NotificationApp* notifications;

    SubGhzTxRx* txrx;

    SceneManager* scene_manager;
    ViewDispatcher* view_dispatcher;

    ViewPort* blank_transition_viewport;

    Submenu* submenu;
    Popup* popup;
    TextInput* text_input;
    ByteInput* byte_input;
    Widget* widget;
    DialogsApp* dialogs;
    FuriString* file_path;
    FuriString* file_path_tmp;

    bool        decoded_preview_active;
    FuriString* decoded_preview_orig_path;
    char file_name_tmp[SUBGHZ_MAX_LEN_NAME];
    SubGhzNotificationState state_notifications;

    SubGhzViewReceiver* subghz_receiver;
    SubGhzViewTransmitter* subghz_transmitter;
    VariableItemList* variable_item_list;

    SubGhzSignalVisualizer*       subghz_signal_visualizer;
    SubGhzGarageProtocolFilter*         protocol_filter;
    SubGhzModulationFilter*        modulation_filter;

    Loading*                       startup_loading;
    ViewHolder*                    startup_holder;
    SubGhzReadRAW* subghz_read_raw;
    bool raw_send_only;

    bool launched_from_mode_picker;
    bool savedmenu_return;

    bool save_datetime_set;
    DateTime save_datetime;

    SubGhzGarageLastSettings* last_settings;

    SubGhzProtocolFlag filter;
    FuriString* error_str;

    SubGhzFileEncoderWorker* decode_raw_file_worker_encoder;

    SubGhzThresholdRssi* threshold_rssi;
    SubGhzRxKeyState rx_key_state;
    SubGhzHistory* history;

    uint16_t idx_menu_chosen;
    SubGhzLoadTypeFile load_type_file;
    uint8_t tx_power;
    void* rpc_ctx;

    bool cli_sessions_locked_after_recovery;

    bool cli_sessions_soft_locked;

    void* cli_hard_disconnect_usb_config;

    bool shared_ram_warning_shown;

    bool reader_read_mode;

    uint32_t low_ram_grace_until_ms;
};

void subghz_ensure_submenu(SubGhz* subghz);
void subghz_ensure_text_input(SubGhz* subghz);
void subghz_ensure_byte_input(SubGhz* subghz);
void subghz_ensure_widget(SubGhz* subghz);
void subghz_ensure_variable_item_list(SubGhz* subghz);
void subghz_ensure_signal_visualizer(SubGhz* subghz);
void subghz_ensure_receiver_view(SubGhz* subghz);
void subghz_ensure_popup(SubGhz* subghz);
void subghz_ensure_transmitter_view(SubGhz* subghz);
void subghz_ensure_history(SubGhz* subghz);
void subghz_scene_start_launch_and_exit(SubGhz* subghz, const char* fap_path, const char* args);
void subghz_return_to_launcher(SubGhz* subghz);

void subghz_blink_start(SubGhz* subghz);
void subghz_blink_stop(SubGhz* subghz);

bool subghz_tx_start(SubGhz* subghz, FlipperFormat* flipper_format);
void subghz_dialog_message_freq_error(SubGhz* subghz, bool only_rx);

bool subghz_key_load(SubGhz* subghz, const char* file_path, bool show_dialog);
bool subghz_get_next_name_file(SubGhz* subghz, uint8_t max_len);
bool subghz_save_protocol_to_file(
    SubGhz* subghz,
    FlipperFormat* flipper_format,
    const char* dev_file_name);
void subghz_save_to_file(void* context);
bool subghz_load_protocol_from_file(SubGhz* subghz);
bool subghz_rename_file(SubGhz* subghz);
bool subghz_file_available(SubGhz* subghz);
bool subghz_delete_file(SubGhz* subghz);
void subghz_file_name_clear(SubGhz* subghz);
bool subghz_path_is_file(FuriString* path);
SubGhzLoadTypeFile subghz_get_load_type_file(SubGhz* subghz);

void subghz_rx_key_state_set(SubGhz* subghz, SubGhzRxKeyState state);
SubGhzRxKeyState subghz_rx_key_state_get(SubGhz* subghz);

extern const NotificationSequence subghz_sequence_rx;
void subghz_save_all(SubGhz* subghz);

void subghz_lock_cli_sessions(SubGhz* subghz);

void subghz_unlock_cli_sessions_after_recovery(SubGhz* subghz);

void subghz_cli_soft_lock(SubGhz* subghz);
void subghz_cli_soft_unlock(SubGhz* subghz);

bool subghz_low_ram_mitigate(SubGhz* subghz, size_t threshold);

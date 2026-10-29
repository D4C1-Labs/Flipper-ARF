#pragma once

#include <gui/view.h>
#include "../helpers/subghz_types.h"
#include "../helpers/subghz_custom_event.h"

typedef struct SubGhzReadRAW SubGhzReadRAW;

typedef void (*SubGhzReadRAWCallback)(SubGhzCustomEvent event, void* context);

typedef enum {
    SubGhzReadRAWStatusStart,
    SubGhzReadRAWStatusIDLE,
    SubGhzReadRAWStatusREC,
    SubGhzReadRAWStatusTX,
    SubGhzReadRAWStatusTXRepeat,

    SubGhzReadRAWStatusLoadKeyIDLE,
    SubGhzReadRAWStatusLoadKeyTX,
    SubGhzReadRAWStatusLoadKeyTXRepeat,
    SubGhzReadRAWStatusLoadKeyTXPaused,
    SubGhzReadRAWStatusSaveKey,

    SubGhzReadRAWStatusDecoding,
} SubGhzReadRAWStatus;

typedef enum {
    SubGhzReadRawVizBar  = 0,
    SubGhzReadRawVizLine = 1,
} SubGhzReadRawVizMode;

void subghz_read_raw_set_callback(
    SubGhzReadRAW* subghz_read_raw,
    SubGhzReadRAWCallback callback,
    void* context);

SubGhzReadRAW* subghz_read_raw_alloc(bool raw_send_only);
void subghz_read_raw_free(SubGhzReadRAW* subghz_static);

void subghz_read_raw_add_data_statusbar(
    SubGhzReadRAW* instance,
    const char* frequency_str,
    const char* preset_str);

void subghz_read_raw_set_radio_device_type(
    SubGhzReadRAW* instance,
    SubGhzRadioDeviceType device_type);

void subghz_read_raw_update_sample_write(SubGhzReadRAW* instance, size_t sample);
void subghz_read_raw_recording_tick(SubGhzReadRAW* instance);
uint32_t subghz_read_raw_get_recording_ticks(SubGhzReadRAW* instance);
void subghz_read_raw_stop_send(SubGhzReadRAW* instance);
void subghz_read_raw_add_data_rssi(SubGhzReadRAW* instance, float rssi, bool trace);

void subghz_read_raw_set_status(
    SubGhzReadRAW* instance,
    SubGhzReadRAWStatus status,
    const char* file_name,
    float raw_threshold_rssi);

void subghz_read_raw_set_start_countdown(SubGhzReadRAW* instance, uint8_t seconds_left);

void subghz_read_raw_set_decoding(
    SubGhzReadRAW* instance,
    uint8_t group,
    uint8_t group_total,
    uint8_t pct);

void subghz_read_raw_clear_decoding(SubGhzReadRAW* instance);

SubGhzReadRAWStatus subghz_read_raw_get_status(SubGhzReadRAW* instance);

void subghz_read_raw_set_allow_new(SubGhzReadRAW* instance, bool allow);

void subghz_read_raw_set_auto_capture_mode(SubGhzReadRAW* instance, bool auto_capture_mode);

void subghz_read_raw_set_viz_mode(SubGhzReadRAW* instance, SubGhzReadRawVizMode mode);

void subghz_read_raw_set_envelope(
    SubGhzReadRAW* instance,
    const uint8_t* envelope,
    uint32_t total_duration_us);

void subghz_read_raw_tick_tx(SubGhzReadRAW* instance);

void subghz_read_raw_set_zoom_level(SubGhzReadRAW* instance, uint8_t zoom_level);
uint8_t subghz_read_raw_get_zoom_level(SubGhzReadRAW* instance);

uint8_t subghz_read_raw_get_seek_pct(SubGhzReadRAW* instance);

bool subghz_read_raw_is_playback_overdue(SubGhzReadRAW* instance);

View* subghz_read_raw_get_view(SubGhzReadRAW* subghz_static);

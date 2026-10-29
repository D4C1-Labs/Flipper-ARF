#pragma once

#include <gui/view.h>
#include "../helpers/subghz_types.h"
#include "../helpers/subghz_custom_event.h"

typedef struct SubGhzViewTransmitter SubGhzViewTransmitter;

typedef void (*SubGhzViewTransmitterCallback)(SubGhzCustomEvent event, void* context);

void subghz_view_transmitter_set_callback(
    SubGhzViewTransmitter* subghz_transmitter,
    SubGhzViewTransmitterCallback callback,
    void* context);

void subghz_view_transmitter_set_radio_device_type(
    SubGhzViewTransmitter* subghz_transmitter,
    SubGhzRadioDeviceType device_type);

SubGhzViewTransmitter* subghz_view_transmitter_alloc(void);

void subghz_view_transmitter_free(SubGhzViewTransmitter* subghz_transmitter);

View* subghz_view_transmitter_get_view(SubGhzViewTransmitter* subghz_transmitter);

void subghz_view_transmitter_add_data_to_show(
    SubGhzViewTransmitter* subghz_transmitter,
    const char* key_str,
    const char* frequency_str,
    const char* preset_str,
    bool show_button);

void subghz_view_transmitter_set_btn_labels(
    SubGhzViewTransmitter* subghz_transmitter,
    const char* center,
    const char* up,    bool up_vis,
    const char* down,  bool down_vis,
    const char* left,  bool left_vis,
    const char* right, bool right_vis);

void subghz_view_transmitter_reset_labels(SubGhzViewTransmitter* subghz_transmitter);

void subghz_view_transmitter_update_direction(
    SubGhzViewTransmitter* subghz_transmitter,
    uint8_t btn_id,
    const char* label);

bool subghz_view_transmitter_is_labels_ready(SubGhzViewTransmitter* subghz_transmitter);

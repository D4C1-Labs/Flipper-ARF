// helpers/radio_device_loader.h
#pragma once

#include <lib/subghz/devices/devices.h>

#define SUBGHZ_DEVICE_CC1101_INT_NAME "cc1101_int"
#define SUBGHZ_DEVICE_CC1101_EXT_NAME "cc1101_ext"
// [ARF_PORT] second external radio
#define SUBGHZ_DEVICE_SI4463_EXT_NAME "si4463_ext"

/** SubGhzRadioDeviceType */
typedef enum {
    SubGhzRadioDeviceTypeInternal,
    SubGhzRadioDeviceTypeExternalCC1101,
} SubGhzRadioDeviceType;

const SubGhzDevice* radio_device_loader_set(
    const SubGhzDevice* current_radio_device,
    SubGhzRadioDeviceType radio_device_type);

bool radio_device_loader_is_connect_external(const char* name);
bool radio_device_loader_is_external(const SubGhzDevice* radio_device);
void radio_device_loader_end(const SubGhzDevice* radio_device);

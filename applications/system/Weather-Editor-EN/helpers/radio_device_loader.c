#include "radio_device_loader.h"

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

#define TAG "WeatherRadioLoader"

static bool radio_device_loader_otg_owned = false;

static bool radio_device_loader_power_on(void) {
    if(furi_hal_power_is_otg_enabled()) return true;

    uint8_t attempts = 0;
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();
        furi_delay_ms(20);
    }

    radio_device_loader_otg_owned = furi_hal_power_is_otg_enabled();
    return radio_device_loader_otg_owned;
}

static void radio_device_loader_power_off(void) {
    if(radio_device_loader_otg_owned && furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }
    radio_device_loader_otg_owned = false;
}

bool radio_device_loader_is_connect_external(const char* name) {
    bool was_otg_enabled = furi_hal_power_is_otg_enabled();
    if(!was_otg_enabled && !radio_device_loader_power_on()) return false;

    const SubGhzDevice* device = subghz_devices_get_by_name(name);
    bool connected = device && subghz_devices_is_connect(device);

    if(!was_otg_enabled) radio_device_loader_power_off();
    return connected;
}

const SubGhzDevice* radio_device_loader_set(
    const SubGhzDevice* current_radio_device,
    SubGhzRadioDeviceType radio_device_type) {
    const SubGhzDevice* internal =
        subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    const SubGhzDevice* target = internal;

    if(radio_device_type == SubGhzRadioDeviceTypeExternalCC1101 &&
       radio_device_loader_is_connect_external(SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
        if(radio_device_loader_power_on()) {
            const SubGhzDevice* external =
                subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
            if(external) target = external;
        }
    }

    if(!target) {
        FURI_LOG_E(TAG, "No Sub-GHz radio device available");
        radio_device_loader_power_off();
        return NULL;
    }

    if(current_radio_device == target) return target;
    if(current_radio_device) radio_device_loader_end(current_radio_device);

    /* ARF keeps the built-in CC1101 available through the device registry and
       does not call subghz_devices_begin() for it. begin()/end() are required
       only for an external radio. Calling begin() for INT made the loader
       report that neither INT nor EXT existed when no module was connected. */
    if(target == internal) {
        radio_device_loader_power_off();
        FURI_LOG_I(TAG, "Internal CC1101 selected");
        return target;
    }

    if(!subghz_devices_begin(target)) {
        FURI_LOG_W(TAG, "External CC1101 init failed; falling back to internal");
        radio_device_loader_power_off();
        if(!internal) {
            FURI_LOG_E(TAG, "Internal CC1101 unavailable");
            return NULL;
        }
        FURI_LOG_I(TAG, "Internal CC1101 selected");
        return internal;
    }

    FURI_LOG_I(TAG, "External CC1101 selected");
    return target;
}

bool radio_device_loader_is_external(const SubGhzDevice* radio_device) {
    if(!radio_device) return false;
    return radio_device != subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
}

const char* radio_device_loader_get_label(const SubGhzDevice* radio_device) {
    if(!radio_device) return "NO RADIO";
    return radio_device_loader_is_external(radio_device) ? "CC1101 EXT" : "RADIO INT";
}

void radio_device_loader_end(const SubGhzDevice* radio_device) {
    if(!radio_device) return;

    const SubGhzDevice* internal =
        subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(radio_device != internal) {
        subghz_devices_end(radio_device);
        radio_device_loader_power_off();
    }
}

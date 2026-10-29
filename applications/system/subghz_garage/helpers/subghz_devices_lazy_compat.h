#pragma once

#include <lib/subghz/devices/devices.h>

#ifdef SUBGHZ_GARAGE_HAS_DEVICES_LAZY_INIT

static inline void subghz_garage_devices_init_radio_only(void) {
    subghz_devices_init_internal_only();
}

static inline bool subghz_garage_devices_load_external(void) {
    return subghz_devices_load_external();
}

#else

static inline void subghz_garage_devices_init_radio_only(void) {
    subghz_devices_init();
}

static inline bool subghz_garage_devices_load_external(void) {
    return false;
}

#endif

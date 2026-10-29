#pragma once

#include <furi_hal_subghz.h>

#ifdef SUBGHZ_GARAGE_HAS_TX_ALLOWED_CHECK

static inline bool subghz_garage_is_tx_allowed(uint32_t frequency) {
    return furi_hal_subghz_is_tx_allowed(frequency);
}

#else

static inline bool subghz_garage_is_tx_allowed(uint32_t frequency) {
    UNUSED(frequency);

    return true;
}

#endif

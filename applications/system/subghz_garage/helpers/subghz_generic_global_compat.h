#pragma once

#ifdef SUBGHZ_GARAGE_HAS_GENERIC_GLOBAL

#include <lib/subghz/blocks/generic.h>

static inline void subghz_garage_counter_override_set(uint32_t counter) {
    subghz_block_generic_global_counter_override_set(counter);
}

static inline void subghz_garage_button_override_set(uint8_t button) {
    subghz_block_generic_global_button_override_set(button);
}

static inline bool subghz_garage_counter_override_get(uint32_t* counter) {
    return subghz_block_generic_global_counter_override_get(counter);
}

static inline bool subghz_garage_button_override_get(uint8_t* button) {
    return subghz_block_generic_global_button_override_get(button);
}

#else

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t current_cnt;
    uint32_t new_cnt;
    bool cnt_need_override;
    uint8_t cnt_length_bit;
    bool cnt_is_available;

    uint8_t current_btn;
    uint8_t new_btn;
    bool btn_need_override;
    uint8_t btn_length_bit;
    bool btn_is_available;

    bool endless_tx;
} SubGhzGarageGenericGlobalCompat;

static SubGhzGarageGenericGlobalCompat subghz_block_generic_global __attribute__((unused));

static inline void subghz_garage_counter_override_set(uint32_t counter) {
    UNUSED(counter);

}

static inline void subghz_garage_button_override_set(uint8_t button) {
    UNUSED(button);

}

static inline bool subghz_garage_counter_override_get(uint32_t* counter) {
    UNUSED(counter);

    return false;
}

static inline bool subghz_garage_button_override_get(uint8_t* button) {
    UNUSED(button);
    return false;
}

#endif

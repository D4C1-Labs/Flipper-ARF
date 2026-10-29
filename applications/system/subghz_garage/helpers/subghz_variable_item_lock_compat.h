#pragma once

#include <gui/modules/variable_item_list.h>

#ifdef SUBGHZ_GARAGE_HAS_ITEM_LOCK

static inline void subghz_garage_variable_item_set_locked(
    VariableItem* item,
    bool locked,
    const char* locked_message) {
    variable_item_set_locked(item, locked, locked_message);
}

#else

static inline void subghz_garage_variable_item_set_locked(
    VariableItem* item,
    bool locked,
    const char* locked_message) {

    UNUSED(item);
    UNUSED(locked);
    UNUSED(locked_message);
}

#endif

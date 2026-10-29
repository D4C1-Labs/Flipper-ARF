#pragma once

#if __has_include(<gui/modules/fox_theme.h>)
#include <gui/modules/fox_theme.h>
#else
#include <stdbool.h>

static inline bool fox_theme_is_active(void) {
    return false;
}
#endif

#pragma once

#if __has_include("rpc/rpc_gui_screen_suppress.h")
#include "rpc/rpc_gui_screen_suppress.h"
#else

#include <stdbool.h>

static inline void rpc_gui_screen_stream_set_suppressed(bool suppressed) {
    (void)suppressed;
}

static inline bool rpc_gui_screen_stream_is_suppressed(void) {
    return false;
}

static inline bool rpc_gui_screen_stream_is_active(void) {
    return false;
}

#endif

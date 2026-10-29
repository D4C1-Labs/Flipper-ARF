#pragma once

#include <furi/core/memmgr.h>

#ifdef SUBGHZ_GARAGE_HAS_MEMMGR_POOL_STATS

static inline size_t subghz_garage_pool_get_free(void) {
    return memmgr_pool_get_free();
}

static inline size_t subghz_garage_pool_get_max_block(void) {
    return memmgr_pool_get_max_block();
}

#else

static inline size_t subghz_garage_pool_get_free(void) {

    return 0;
}

static inline size_t subghz_garage_pool_get_max_block(void) {
    return 0;
}

#endif

#pragma once

#include <toolbox/name_generator.h>

#ifdef SUBGHZ_GARAGE_HAS_NAME_GEN_DATETIME

static inline void subghz_garage_name_generator_make_auto_datetime(
    char* name,
    size_t max_name_size,
    const char* prefix,
    DateTime* custom_time) {
    name_generator_make_auto_datetime(name, max_name_size, prefix, custom_time);
}

#else

static inline void subghz_garage_name_generator_make_auto_datetime(
    char* name,
    size_t max_name_size,
    const char* prefix,
    DateTime* custom_time) {

    UNUSED(custom_time);
    name_generator_make_auto(name, max_name_size, prefix);
}

#endif

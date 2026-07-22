#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char* name;
    uint32_t te_short;
    uint32_t te_long;
    uint32_t te_delta;
    uint32_t min_count_bit;
} SubGhzProtocolTiming;

const SubGhzProtocolTiming* subghz_protocol_timing_get(const char* protocol_name);
const SubGhzProtocolTiming* subghz_protocol_timing_get_by_index(size_t index);
size_t subghz_protocol_timing_get_count(void);

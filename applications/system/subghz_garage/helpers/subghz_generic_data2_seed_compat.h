#pragma once

#include <lib/subghz/blocks/generic.h>

#ifdef SUBGHZ_GARAGE_HAS_GENERIC_DATA2_SEED

typedef SubGhzBlockGeneric SubGhzGenericCompat;

#else

typedef struct {
    const char* protocol_name;
    uint64_t data;
    uint32_t serial;
    uint16_t data_count_bit;
    uint8_t btn;
    uint32_t cnt;
    uint64_t data_2;
    uint8_t cnt_2;
    uint32_t seed;
} SubGhzGenericCompat;

#endif

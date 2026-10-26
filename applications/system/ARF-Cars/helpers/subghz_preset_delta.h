#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    const uint8_t* delta;
    size_t delta_len;
    bool needs_scal;
    const uint8_t* pa_table;
} PresetDeltaEntry;

typedef enum {
    HOP_AM650 = 0,
    HOP_FM476 = 1,
    HOP_FM95 = 2,
    HOP_PRESET_COUNT,
} SubghzHopPreset;

extern const PresetDeltaEntry preset_delta_table[HOP_PRESET_COUNT][HOP_PRESET_COUNT];

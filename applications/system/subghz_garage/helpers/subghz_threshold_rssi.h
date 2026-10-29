#pragma once

#include <furi.h>

typedef struct {
    float rssi;
    bool is_above;
} SubGhzThresholdRssiData;

typedef struct SubGhzThresholdRssi SubGhzThresholdRssi;

SubGhzThresholdRssi* subghz_threshold_rssi_alloc(void);

void subghz_threshold_rssi_free(SubGhzThresholdRssi* instance);

void subghz_threshold_rssi_set(SubGhzThresholdRssi* instance, float rssi);

void subghz_threshold_rssi_reset(SubGhzThresholdRssi* instance);

float subghz_threshold_rssi_get(SubGhzThresholdRssi* instance);

SubGhzThresholdRssiData subghz_threshold_get_rssi_data(SubGhzThresholdRssi* instance, float rssi);

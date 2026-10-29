#pragma once

#include <furi_hal.h>
#include <stdint.h>
#include <stdbool.h>
#include <storage/storage.h>
#include <lib/subghz/types.h>
#include "protocols/protocol_groups.h"

#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_TRIGGER        (-93.0f)

#define SUBGHZ_LAST_SETTING_DEFAULT_PRESET                    1
#define SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY                 433920000
#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_FEEDBACK_LEVEL 2

#define SUBGHZ_LAST_SETTING_DEFAULT_VISUALIZER_MODE          0u
#define SUBGHZ_LAST_SETTING_DEFAULT_RAW_ZOOM_LEVEL           0u

typedef struct {
    uint32_t frequency;
    uint32_t preset_index;
    uint32_t frequency_analyzer_feedback_level;
    float frequency_analyzer_trigger;
    bool protocol_file_names;
    bool enable_hopping;
    uint32_t filter;
    float rssi;

    bool rssi_force_applied;
    bool auto_save;
    float hopping_threshold;
    bool leds_and_amp;
    uint8_t  tx_power;
    uint32_t visualizer_display_mode;
    uint32_t raw_playback_zoom_level;

    uint8_t protocol_filter_data[256];
    uint8_t mod_filter_data[64];
    bool    protocol_filter_present;
    bool    mod_filter_present;
    bool    bypass_region_lock;
    char    file_prefix[12];
    uint32_t protocol_group;

    uint8_t protocol_groups_enabled[SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT];
} SubGhzGarageLastSettings;

void subghz_garage_last_settings_set_protocol_filter(SubGhzGarageLastSettings* s,
                                               const uint8_t* data, size_t count);
void subghz_garage_last_settings_set_mod_filter(SubGhzGarageLastSettings* s,
                                          const uint8_t* data, size_t count);

SubGhzGarageLastSettings* subghz_garage_last_settings_alloc(void);

void subghz_garage_last_settings_free(SubGhzGarageLastSettings* instance);

void subghz_garage_last_settings_load(SubGhzGarageLastSettings* instance, size_t preset_count);

bool subghz_garage_last_settings_save(SubGhzGarageLastSettings* instance);

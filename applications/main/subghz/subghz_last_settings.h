#pragma once

#include <furi_hal.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <storage/storage.h>
#include <lib/subghz/types.h>

#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_TRIGGER        (-93.0f)
// 1 = "AM650"
// "AM270", "AM650", "FM238", "FM12K", "FM476",
#define SUBGHZ_LAST_SETTING_DEFAULT_PRESET                    1
#define SUBGHZ_LAST_SETTING_DEFAULT_FREQUENCY                 433920000
#define SUBGHZ_LAST_SETTING_FREQUENCY_ANALYZER_FEEDBACK_LEVEL 2
#define SUBGHZ_LAST_SETTING_DEFAULT_PRESET_HOPPING_THRESHOLD  (-80.0f)
#define SUBGHZ_LAST_SETTINGS_PROTOCOL_FILTER_SIZE             1024

typedef struct {
    uint32_t frequency;
    uint32_t preset_index;
    uint32_t frequency_analyzer_feedback_level;
    float frequency_analyzer_trigger;
    bool protocol_file_names;
    bool enable_hopping;
    uint32_t ignore_filter;
    uint32_t filter;
    float rssi;
    bool delete_old_signals;
    float hopping_threshold;
    bool enable_preset_hopping;
    float preset_hopping_threshold;
    bool leds_and_amp;
    uint8_t tx_power;
    bool custom_car_emulate;
    char protocol_filter[SUBGHZ_LAST_SETTINGS_PROTOCOL_FILTER_SIZE];  /* comma-separated disabled protocols, empty = all enabled */
} SubGhzLastSettings;

static inline void subghz_last_settings_protocol_filter_next_token(
    const char** cursor,
    const char** token,
    size_t* token_len) {
    const char* start = *cursor;
    while((*start == ',') || (*start == ' ') || (*start == '\t')) {
        start++;
    }

    const char* end = start;
    while((*end != '\0') && (*end != ',')) {
        end++;
    }

    const char* trim_end = end;
    while((trim_end > start) && ((trim_end[-1] == ' ') || (trim_end[-1] == '\t'))) {
        trim_end--;
    }

    *token = start;
    *token_len = (size_t)(trim_end - start);
    *cursor = (*end == ',') ? end + 1 : end;
}

static inline bool subghz_last_settings_protocol_filter_token_matches(
    const char* token,
    size_t token_len,
    const char* name,
    size_t name_len) {
    return (token_len == name_len) && (strncmp(token, name, token_len) == 0);
}

static inline bool subghz_last_settings_protocol_filter_contains_token(
    const char* filter,
    const char* token,
    size_t token_len) {
    const char* cursor = filter;
    const char* current = NULL;
    size_t current_len = 0;

    while(*cursor != '\0') {
        subghz_last_settings_protocol_filter_next_token(&cursor, &current, &current_len);
        if((current_len != 0) &&
           subghz_last_settings_protocol_filter_token_matches(
               current, current_len, token, token_len)) {
            return true;
        }
    }

    return false;
}

static inline bool subghz_last_settings_protocol_filter_append_token(
    char* filter,
    size_t filter_size,
    const char* token,
    size_t token_len) {
    if(token_len == 0) return true;

    size_t filter_len = strlen(filter);
    size_t separator_len = filter_len == 0 ? 0 : 1;
    if((filter_len + separator_len + token_len) >= filter_size) {
        return false;
    }

    if(separator_len != 0) {
        filter[filter_len++] = ',';
    }
    memcpy(&filter[filter_len], token, token_len);
    filter[filter_len + token_len] = '\0';
    return true;
}

/** Enable ALL protocols in one shot by clearing the OFF list.
 * @return true if the filter changed (was non-empty). */
static inline bool subghz_last_settings_protocol_filter_clear(SubGhzLastSettings* instance) {
    if(instance == NULL) return false;
    if(instance->protocol_filter[0] == '\0') return false;
    instance->protocol_filter[0] = '\0';
    return true;
}

static inline bool subghz_last_settings_protocol_filter_contains(
    const SubGhzLastSettings* instance,
    const char* protocol) {
    if((instance == NULL) || (protocol == NULL) || (protocol[0] == '\0')) return false;

    return subghz_last_settings_protocol_filter_contains_token(
        instance->protocol_filter, protocol, strlen(protocol));
}

static inline bool subghz_last_settings_protocol_filter_normalize(
    SubGhzLastSettings* instance) {
    if(instance == NULL) return false;

    char normalized[SUBGHZ_LAST_SETTINGS_PROTOCOL_FILTER_SIZE] = {0};
    const char* cursor = instance->protocol_filter;
    const char* token = NULL;
    size_t token_len = 0;

    while(*cursor != '\0') {
        subghz_last_settings_protocol_filter_next_token(&cursor, &token, &token_len);
        if((token_len == 0) ||
           subghz_last_settings_protocol_filter_contains_token(normalized, token, token_len)) {
            continue;
        }
        if(!subghz_last_settings_protocol_filter_append_token(
               normalized, sizeof(normalized), token, token_len)) {
            return false;
        }
    }

    bool changed = strcmp(instance->protocol_filter, normalized) != 0;
    if(changed) {
        memcpy(instance->protocol_filter, normalized, sizeof(instance->protocol_filter));
    }
    return changed;
}

static inline bool subghz_last_settings_protocol_filter_set(
    SubGhzLastSettings* instance,
    const char* protocol,
    bool disabled) {
    if((instance == NULL) || (protocol == NULL) || (protocol[0] == '\0')) return false;

    char updated[SUBGHZ_LAST_SETTINGS_PROTOCOL_FILTER_SIZE] = {0};
    const char* cursor = instance->protocol_filter;
    const char* token = NULL;
    size_t token_len = 0;
    const size_t protocol_len = strlen(protocol);
    bool protocol_written = false;

    while(*cursor != '\0') {
        subghz_last_settings_protocol_filter_next_token(&cursor, &token, &token_len);
        if(token_len == 0) continue;

        bool is_target = subghz_last_settings_protocol_filter_token_matches(
            token, token_len, protocol, protocol_len);
        if(is_target) {
            if(disabled && !protocol_written) {
                if(!subghz_last_settings_protocol_filter_append_token(
                       updated, sizeof(updated), protocol, protocol_len)) {
                    return false;
                }
                protocol_written = true;
            }
            continue;
        }

        if(!subghz_last_settings_protocol_filter_contains_token(updated, token, token_len)) {
            if(!subghz_last_settings_protocol_filter_append_token(
                   updated, sizeof(updated), token, token_len)) {
                return false;
            }
        }
    }

    if(disabled && !protocol_written) {
        if(!subghz_last_settings_protocol_filter_append_token(
               updated, sizeof(updated), protocol, protocol_len)) {
            return false;
        }
    }

    bool changed = strcmp(instance->protocol_filter, updated) != 0;
    if(changed) {
        memcpy(instance->protocol_filter, updated, sizeof(instance->protocol_filter));
    }
    return changed;
}

static inline uint8_t subghz_last_settings_protocol_filter_count(
    const SubGhzLastSettings* instance) {
    if(instance == NULL) return 0;

    char seen[SUBGHZ_LAST_SETTINGS_PROTOCOL_FILTER_SIZE] = {0};
    const char* cursor = instance->protocol_filter;
    const char* token = NULL;
    size_t token_len = 0;
    uint8_t count = 0;

    while(*cursor != '\0') {
        subghz_last_settings_protocol_filter_next_token(&cursor, &token, &token_len);
        if((token_len == 0) ||
           subghz_last_settings_protocol_filter_contains_token(seen, token, token_len)) {
            continue;
        }
        if(!subghz_last_settings_protocol_filter_append_token(seen, sizeof(seen), token, token_len)) {
            break;
        }
        count++;
    }

    return count;
}

SubGhzLastSettings* subghz_last_settings_alloc(void);

void subghz_last_settings_free(SubGhzLastSettings* instance);

void subghz_last_settings_load(SubGhzLastSettings* instance, size_t preset_count);

bool subghz_last_settings_save(SubGhzLastSettings* instance);

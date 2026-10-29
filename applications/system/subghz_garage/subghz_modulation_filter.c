#include "subghz_modulation_filter.h"
#include <stdlib.h>
#include <string.h>
#include <storage/storage.h>
#include <furi.h>

struct SubGhzModulationFilter {
    uint8_t enabled[SUBGHZ_MOD_FILTER_COUNT];
};

SubGhzModulationFilter* subghz_modulation_filter_alloc(void) {
    SubGhzModulationFilter* instance = malloc(sizeof(SubGhzModulationFilter));
    furi_assert(instance);
    memset(instance->enabled, 0x01, sizeof(instance->enabled));
    return instance;
}

void subghz_modulation_filter_free(SubGhzModulationFilter* instance) {
    furi_assert(instance);
    free(instance);
}

void subghz_modulation_filter_load(SubGhzModulationFilter* instance) {

    (void)instance;
}

void subghz_modulation_filter_save(SubGhzModulationFilter* instance) {

    (void)instance;
}

bool subghz_modulation_filter_is_enabled(const SubGhzModulationFilter* instance, size_t index) {
    furi_assert(instance);
    if(index >= SUBGHZ_MOD_FILTER_COUNT) return true;
    return instance->enabled[index] != 0x00;
}

void subghz_modulation_filter_set_enabled(SubGhzModulationFilter* instance,
                                           size_t index, bool enabled) {
    furi_assert(instance);
    if(index >= SUBGHZ_MOD_FILTER_COUNT) return;
    instance->enabled[index] = enabled ? 0x01 : 0x00;
}

void subghz_modulation_filter_get_raw(const SubGhzModulationFilter* instance,
                                       uint8_t* out, size_t count) {
    furi_assert(instance && out);
    size_t n = count < SUBGHZ_MOD_FILTER_COUNT ? count : SUBGHZ_MOD_FILTER_COUNT;
    memcpy(out, instance->enabled, n);
}

void subghz_modulation_filter_set_raw(SubGhzModulationFilter* instance,
                                       const uint8_t* in, size_t count) {
    furi_assert(instance && in);
    size_t n = count < SUBGHZ_MOD_FILTER_COUNT ? count : SUBGHZ_MOD_FILTER_COUNT;
    memcpy(instance->enabled, in, n);
}

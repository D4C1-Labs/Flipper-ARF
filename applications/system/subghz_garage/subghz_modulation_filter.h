#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_MOD_FILTER_COUNT  64u

typedef struct SubGhzModulationFilter SubGhzModulationFilter;

SubGhzModulationFilter* subghz_modulation_filter_alloc(void);
void  subghz_modulation_filter_free(SubGhzModulationFilter* instance);
void  subghz_modulation_filter_load(SubGhzModulationFilter* instance);
void  subghz_modulation_filter_save(SubGhzModulationFilter* instance);
bool  subghz_modulation_filter_is_enabled(const SubGhzModulationFilter* instance, size_t index);
void  subghz_modulation_filter_set_enabled(SubGhzModulationFilter* instance, size_t index, bool enabled);

void subghz_modulation_filter_get_raw(const SubGhzModulationFilter* instance,
                                       uint8_t* out, size_t count);

void subghz_modulation_filter_set_raw(SubGhzModulationFilter* instance,
                                       const uint8_t* in, size_t count);

#ifdef __cplusplus
}
#endif

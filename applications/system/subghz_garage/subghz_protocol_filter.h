#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_FILTER_MAX_PROTOCOLS 256u

typedef struct SubGhzGarageProtocolFilter SubGhzGarageProtocolFilter;

SubGhzGarageProtocolFilter* subghz_garage_protocol_filter_alloc(void);

void subghz_garage_protocol_filter_free(SubGhzGarageProtocolFilter* instance);

void subghz_garage_protocol_filter_load(SubGhzGarageProtocolFilter* instance);

void subghz_garage_protocol_filter_save(SubGhzGarageProtocolFilter* instance);

bool subghz_garage_protocol_filter_is_enabled(const SubGhzGarageProtocolFilter* instance, size_t index);

void subghz_garage_protocol_filter_set_enabled(SubGhzGarageProtocolFilter* instance, size_t index, bool enabled);

void subghz_garage_protocol_filter_reset(SubGhzGarageProtocolFilter* instance);

size_t subghz_garage_protocol_filter_enabled_count(const SubGhzGarageProtocolFilter* instance,
                                             size_t total_count);

void subghz_garage_protocol_filter_get_raw(const SubGhzGarageProtocolFilter* instance,
                                     uint8_t* out, size_t count);

void subghz_garage_protocol_filter_set_raw(SubGhzGarageProtocolFilter* instance,
                                     const uint8_t* in, size_t count);

#ifdef __cplusplus
}
#endif

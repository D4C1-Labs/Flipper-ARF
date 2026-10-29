#include "subghz_protocol_filter.h"
#include <stdlib.h>
#include <string.h>
#include <storage/storage.h>
#include <furi.h>

#define TAG "SubGhzGarageProtocolFilter"

struct SubGhzGarageProtocolFilter {
    uint8_t enabled[SUBGHZ_FILTER_MAX_PROTOCOLS];
};

SubGhzGarageProtocolFilter* subghz_garage_protocol_filter_alloc(void) {
    SubGhzGarageProtocolFilter* inst = malloc(sizeof(SubGhzGarageProtocolFilter));
    furi_assert(inst);

    memset(inst->enabled, 0x01, sizeof(inst->enabled));
    return inst;
}

void subghz_garage_protocol_filter_free(SubGhzGarageProtocolFilter* instance) {
    furi_assert(instance);
    free(instance);
}

void subghz_garage_protocol_filter_save(SubGhzGarageProtocolFilter* instance) {

    (void)instance;
}

void subghz_garage_protocol_filter_load(SubGhzGarageProtocolFilter* instance) {

    (void)instance;
}

void subghz_garage_protocol_filter_reset(SubGhzGarageProtocolFilter* instance) {
    furi_assert(instance);
    memset(instance->enabled, 0x01, sizeof(instance->enabled));
}

bool subghz_garage_protocol_filter_is_enabled(const SubGhzGarageProtocolFilter* instance, size_t index) {
    furi_assert(instance);
    if(index >= SUBGHZ_FILTER_MAX_PROTOCOLS) return true;
    return instance->enabled[index] != 0x00;
}

void subghz_garage_protocol_filter_set_enabled(SubGhzGarageProtocolFilter* instance,
                                         size_t index, bool enabled) {
    furi_assert(instance);
    if(index >= SUBGHZ_FILTER_MAX_PROTOCOLS) return;
    instance->enabled[index] = enabled ? 0x01 : 0x00;
}

size_t subghz_garage_protocol_filter_enabled_count(const SubGhzGarageProtocolFilter* instance,
                                              size_t total_count) {
    furi_assert(instance);
    size_t count = 0;
    size_t limit = total_count < SUBGHZ_FILTER_MAX_PROTOCOLS
                       ? total_count
                       : SUBGHZ_FILTER_MAX_PROTOCOLS;
    for(size_t i = 0; i < limit; i++) {
        if(instance->enabled[i] != 0x00) count++;
    }
    return count;
}

void subghz_garage_protocol_filter_get_raw(const SubGhzGarageProtocolFilter* instance,
                                     uint8_t* out, size_t count) {
    furi_assert(instance && out);
    size_t n = count < SUBGHZ_FILTER_MAX_PROTOCOLS ? count : SUBGHZ_FILTER_MAX_PROTOCOLS;
    memcpy(out, instance->enabled, n);
}

void subghz_garage_protocol_filter_set_raw(SubGhzGarageProtocolFilter* instance,
                                     const uint8_t* in, size_t count) {
    furi_assert(instance && in);
    size_t n = count < SUBGHZ_FILTER_MAX_PROTOCOLS ? count : SUBGHZ_FILTER_MAX_PROTOCOLS;
    memcpy(instance->enabled, in, n);
}

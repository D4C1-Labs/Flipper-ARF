#pragma once
#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzProtocolGroups SubGhzProtocolGroups;

typedef void (*SubGhzProtocolGroupsCallback)(void* context, uint8_t group_index, bool enabled);

SubGhzProtocolGroups* subghz_protocol_groups_alloc(void);
void subghz_protocol_groups_free(SubGhzProtocolGroups* instance);
View* subghz_protocol_groups_get_view(SubGhzProtocolGroups* instance);
void subghz_protocol_groups_set_callback(
    SubGhzProtocolGroups* instance,
    SubGhzProtocolGroupsCallback callback,
    void* context);

void subghz_protocol_groups_set_enabled_all(
    SubGhzProtocolGroups* instance,
    const uint8_t* enabled_groups);

void subghz_protocol_groups_resume_scroll(SubGhzProtocolGroups* instance);
void subghz_protocol_groups_pause_scroll(SubGhzProtocolGroups* instance);

#ifdef __cplusplus
}
#endif

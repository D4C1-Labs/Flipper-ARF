#pragma once

#include <gui/view.h>
#include "../helpers/subghz_custom_event.h"

typedef struct SubGhzViewKeeloqDecrypt SubGhzViewKeeloqDecrypt;

typedef void (*SubGhzViewKeeloqDecryptCallback)(SubGhzCustomEvent event, void* context);

SubGhzViewKeeloqDecrypt* subghz_view_keeloq_decrypt_alloc(void);
void subghz_view_keeloq_decrypt_free(SubGhzViewKeeloqDecrypt* instance);
View* subghz_view_keeloq_decrypt_get_view(SubGhzViewKeeloqDecrypt* instance);

void subghz_view_keeloq_decrypt_set_callback(
    SubGhzViewKeeloqDecrypt* instance,
    SubGhzViewKeeloqDecryptCallback callback,
    void* context);

void subghz_view_keeloq_decrypt_update_stats(
    SubGhzViewKeeloqDecrypt* instance,
    uint8_t progress,
    uint32_t keys_tested,
    uint32_t keys_per_sec,
    uint32_t elapsed_sec,
    uint32_t eta_sec);

void subghz_view_keeloq_decrypt_set_result(
    SubGhzViewKeeloqDecrypt* instance,
    bool success,
    const char* result);

void subghz_view_keeloq_decrypt_reset(SubGhzViewKeeloqDecrypt* instance);

void subghz_view_keeloq_decrypt_set_status(SubGhzViewKeeloqDecrypt* instance, const char* status);

void subghz_view_keeloq_decrypt_update_candidates(
    SubGhzViewKeeloqDecrypt* instance, uint32_t count);

#pragma once

#include <gui/view.h>
#include "../helpers/subghz_custom_event.h"

typedef struct SubGhzViewPsaDecrypt SubGhzViewPsaDecrypt;

typedef void (*SubGhzViewPsaDecryptCallback)(SubGhzCustomEvent event, void* context);

SubGhzViewPsaDecrypt* subghz_view_psa_decrypt_alloc(void);
void subghz_view_psa_decrypt_free(SubGhzViewPsaDecrypt* instance);
View* subghz_view_psa_decrypt_get_view(SubGhzViewPsaDecrypt* instance);

void subghz_view_psa_decrypt_set_callback(
    SubGhzViewPsaDecrypt* instance,
    SubGhzViewPsaDecryptCallback callback,
    void* context);

void subghz_view_psa_decrypt_update_stats(
    SubGhzViewPsaDecrypt* instance,
    uint8_t progress,
    uint32_t keys_tested,
    uint32_t keys_per_sec,
    uint32_t elapsed_sec,
    uint32_t eta_sec);

void subghz_view_psa_decrypt_set_result(
    SubGhzViewPsaDecrypt* instance,
    bool success,
    const char* result);

void subghz_view_psa_decrypt_reset(SubGhzViewPsaDecrypt* instance);

void subghz_view_psa_decrypt_set_status(SubGhzViewPsaDecrypt* instance, const char* status);

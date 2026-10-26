#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_BUTTON_LABEL_COUNT 8

void subghz_button_labels_reset(const char* labels[SUBGHZ_BUTTON_LABEL_COUNT]);

void subghz_button_labels_apply_protocol(
    const char* protocol,
    const char* labels[SUBGHZ_BUTTON_LABEL_COUNT]);

const char* subghz_button_labels_get(
    const char* const labels[SUBGHZ_BUTTON_LABEL_COUNT],
    uint8_t custom_btn_id,
    uint8_t original_custom_btn);

uint8_t subghz_button_labels_get_max_custom_btn(const char* protocol);

#ifdef __cplusplus
}
#endif

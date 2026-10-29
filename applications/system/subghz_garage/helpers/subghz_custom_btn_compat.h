#pragma once

#ifdef SUBGHZ_GARAGE_HAS_CUSTOM_BTN
#include <lib/subghz/blocks/custom_btn.h>
#else

#include <stdbool.h>
#include <stdint.h>

#define SUBGHZ_CUSTOM_BTN_OK    (0U)
#define SUBGHZ_CUSTOM_BTN_UP    (1U)
#define SUBGHZ_CUSTOM_BTN_DOWN  (2U)
#define SUBGHZ_CUSTOM_BTN_LEFT  (3U)
#define SUBGHZ_CUSTOM_BTN_RIGHT (4U)

static inline bool subghz_custom_btn_set(uint8_t btn_id) {
    (void)btn_id;
    return false;
}

static inline uint8_t subghz_custom_btn_get(void) {
    return SUBGHZ_CUSTOM_BTN_OK;
}

static inline uint8_t subghz_custom_btn_get_original(void) {
    return SUBGHZ_CUSTOM_BTN_OK;
}

static inline void subghz_custom_btn_set_original(uint8_t btn_code) {
    (void)btn_code;
}

static inline void subghz_custom_btn_set_max(uint8_t b) {
    (void)b;
}

static inline void subghz_custom_btns_reset(void) {
}

static inline bool subghz_custom_btn_is_allowed(void) {
    return false;
}

static inline void subghz_custom_btn_set_long(bool v) {
    (void)v;
}

static inline bool subghz_custom_btn_get_long(void) {
    return false;
}

static inline void subghz_custom_btn_set_pages(bool enabled) {
    (void)enabled;
}

static inline bool subghz_custom_btn_has_pages(void) {
    return false;
}

static inline void subghz_custom_btn_set_page(uint8_t page) {
    (void)page;
}

static inline uint8_t subghz_custom_btn_get_page(void) {
    return 0;
}

static inline void subghz_custom_btn_set_max_pages(uint8_t n) {
    (void)n;
}

static inline uint8_t subghz_custom_btn_get_max_pages(void) {
    return 0;
}

static inline const char*
    subghz_custom_btn_get_label_for_proto(const char* proto_name, uint8_t btn_dir) {
    (void)proto_name;
    (void)btn_dir;
    return NULL;
}

#endif

#pragma once

#include <lib/subghz/blocks/custom_btn.h>

#define PROG_MODE_OFF              (0U)
#define PROG_MODE_KEELOQ_BFT       (1U)
#define PROG_MODE_KEELOQ_APRIMATIC (2U)
#define PROG_MODE_KEELOQ_DEA_MIO   (3U)

typedef uint8_t ProgMode;

void subghz_custom_btn_set_original(uint8_t btn_code);

void subghz_custom_btn_set_max(uint8_t b);

uint8_t subghz_custom_btn_get_max(void);

void subghz_custom_btn_set_prog_mode(ProgMode prog_mode);

ProgMode subghz_custom_btn_get_prog_mode(void);

/**
 * Helper macro: declare a static button-map table and the two
 * conversion functions that every protocol with custom buttons needs.
 *
 * Usage in your protocol .c file:
 *
 *   SUBGHZ_CUSTOM_BTN_DEFINE_MAP(my_proto,
 *       {SUBGHZ_CUSTOM_BTN_OK,    0x01},   // OK    → Lock
 *       {SUBGHZ_CUSTOM_BTN_UP,    0x01},   // Up    → Lock
 *       {SUBGHZ_CUSTOM_BTN_DOWN,  0x02},   // Down  → Unlock
 *       {SUBGHZ_CUSTOM_BTN_LEFT,  0x04},   // Left  → Boot
 *       {SUBGHZ_CUSTOM_BTN_RIGHT, 0x08},   // Right → Panic
 *   )
 *
 * This generates:
 *   static uint8_t my_proto_custom_btn_to_code(uint8_t custom_btn);
 *   static uint8_t my_proto_code_to_custom_btn(uint8_t code);
 *   static const uint8_t my_proto_custom_btn_max;
 */

typedef struct {
    uint8_t custom_btn_id;  /* SUBGHZ_CUSTOM_BTN_OK / UP / DOWN / LEFT / RIGHT */
    uint8_t protocol_code;  /* the actual byte the protocol puts in the frame   */
} SubGhzCustomBtnEntry;

#define SUBGHZ_CUSTOM_BTN_DEFINE_MAP(prefix_, ...)                              \
    static const SubGhzCustomBtnEntry prefix_##_btn_map[] = {__VA_ARGS__};      \
    static const uint8_t prefix_##_custom_btn_max =                             \
        (sizeof(prefix_##_btn_map) / sizeof(SubGhzCustomBtnEntry)) - 1U;        \
                                                                                \
    static uint8_t prefix_##_custom_btn_to_code(uint8_t custom_btn) {           \
        for(size_t i = 0; i < sizeof(prefix_##_btn_map) /                       \
                              sizeof(SubGhzCustomBtnEntry); i++) {              \
            if(prefix_##_btn_map[i].custom_btn_id == custom_btn)                \
                return prefix_##_btn_map[i].protocol_code;                      \
        }                                                                       \
        /* fallback: return whatever OK maps to */                              \
        return prefix_##_btn_map[0].protocol_code;                              \
    }                                                                           \
                                                                                \
    static uint8_t prefix_##_code_to_custom_btn(uint8_t code) {                 \
        for(size_t i = 0; i < sizeof(prefix_##_btn_map) /                       \
                              sizeof(SubGhzCustomBtnEntry); i++) {              \
            if(prefix_##_btn_map[i].protocol_code == code)                      \
                return prefix_##_btn_map[i].custom_btn_id;                      \
        }                                                                       \
        return SUBGHZ_CUSTOM_BTN_OK;                                            \
    }                                                                           \
                                                                                \
    static void prefix_##_custom_btn_init(uint8_t current_code) {               \
        uint8_t original = prefix_##_code_to_custom_btn(current_code);          \
        if(subghz_custom_btn_get_original() == 0)                               \
            subghz_custom_btn_set_original(original);                           \
        subghz_custom_btn_set_max(prefix_##_custom_btn_max);                    \
    }

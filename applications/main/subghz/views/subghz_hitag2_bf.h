#pragma once

// [HITAG2_BF] Hitag2 Bruteforce view for Fiat V1 protocol

#include <gui/view.h>
#include "../helpers/subghz_custom_event.h"

typedef struct SubGhzViewHitag2Bf SubGhzViewHitag2Bf;

typedef void (*SubGhzViewHitag2BfCallback)(SubGhzCustomEvent event, void* context);

SubGhzViewHitag2Bf* subghz_view_hitag2_bf_alloc(void);
void subghz_view_hitag2_bf_free(SubGhzViewHitag2Bf* instance);
View* subghz_view_hitag2_bf_get_view(SubGhzViewHitag2Bf* instance);

void subghz_view_hitag2_bf_set_callback(
    SubGhzViewHitag2Bf* instance,
    SubGhzViewHitag2BfCallback callback,
    void* context);

/**
 * Update crack statistics.
 * @param level current level index (1..5)
 * @param level_name text shown at top ("Known", "Flash Dict", "SD Dict", "Heuristic", "Hitag2Hell")
 * @param progress percent 0..100 within the current level
 * @param keys_tested total keys tried across all levels so far
 * @param keys_per_sec throughput
 * @param elapsed_sec since start
 * @param eta_sec estimated for current level
 * @param captures how many captures are being validated against
 */
void subghz_view_hitag2_bf_update_stats(
    SubGhzViewHitag2Bf* instance,
    uint8_t level,
    const char* level_name,
    uint8_t progress,
    uint64_t keys_tested,
    uint32_t keys_per_sec,
    uint32_t elapsed_sec,
    uint32_t eta_sec,
    uint8_t captures);

void subghz_view_hitag2_bf_set_result(
    SubGhzViewHitag2Bf* instance,
    bool success,
    const char* result);

void subghz_view_hitag2_bf_reset(SubGhzViewHitag2Bf* instance);

void subghz_view_hitag2_bf_set_status(SubGhzViewHitag2Bf* instance, const char* status);

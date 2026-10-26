#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool has_key;
    uint8_t key[6];
    uint32_t epoch;
    bool has_seed;
    uint32_t seed;
} Hitag2KeyDbRecord;

bool hitag2_key_db_find(uint32_t uid, Hitag2KeyDbRecord* record);
bool hitag2_key_db_save_key(uint32_t uid, const uint8_t key[6], uint32_t epoch);
bool hitag2_key_db_save_seed(uint32_t uid, uint32_t seed);

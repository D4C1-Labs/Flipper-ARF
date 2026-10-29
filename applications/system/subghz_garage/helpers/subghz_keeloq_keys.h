#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <lib/subghz/subghz_keystore.h>

typedef struct SubGhzKeeloqKeysManager SubGhzKeeloqKeysManager;

SubGhzKeeloqKeysManager* subghz_keeloq_keys_alloc(void);

void subghz_keeloq_keys_free(SubGhzKeeloqKeysManager* m);

size_t subghz_keeloq_keys_count(SubGhzKeeloqKeysManager* m);

size_t subghz_keeloq_keys_user_count(SubGhzKeeloqKeysManager* m);

SubGhzKey* subghz_keeloq_keys_get(SubGhzKeeloqKeysManager* m, size_t i);

void subghz_keeloq_keys_add(
    SubGhzKeeloqKeysManager* m,
    uint64_t key,
    uint16_t type,
    const char* name);

void subghz_keeloq_keys_set(
    SubGhzKeeloqKeysManager* m,
    size_t i,
    uint64_t key,
    uint16_t type,
    const char* name);

void subghz_keeloq_keys_delete(SubGhzKeeloqKeysManager* m, size_t i);

bool subghz_keeloq_keys_save(SubGhzKeeloqKeysManager* m);

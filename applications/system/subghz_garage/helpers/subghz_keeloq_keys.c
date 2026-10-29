#include "subghz_keeloq_keys.h"

#include <furi.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>
#include <toolbox/stream/stream.h>
#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/types.h>

#define TAG "SubGhzKeeloqKeysManager"

#define KEELOQ_KEYSTORE_FILE_TYPE    "Flipper SubGhz Keystore File"
#define KEELOQ_KEYSTORE_FILE_VERSION 0

struct SubGhzKeeloqKeysManager {
    SubGhzKeystore* user_ks;
    SubGhzKeystore* system_ks;
    size_t user_count;
};

SubGhzKeeloqKeysManager* subghz_keeloq_keys_alloc(void) {
    SubGhzKeeloqKeysManager* m = malloc(sizeof(SubGhzKeeloqKeysManager));

    m->user_ks = subghz_keystore_alloc();
    subghz_keystore_load(m->user_ks, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    m->user_count = SubGhzKeyArray_size(*subghz_keystore_get_data(m->user_ks));

    m->system_ks = subghz_keystore_alloc();
    subghz_keystore_load(m->system_ks, SUBGHZ_KEYSTORE_DIR_NAME);

    return m;
}

void subghz_keeloq_keys_free(SubGhzKeeloqKeysManager* m) {
    furi_assert(m);
    subghz_keystore_free(m->user_ks);
    subghz_keystore_free(m->system_ks);
    free(m);
}

size_t subghz_keeloq_keys_count(SubGhzKeeloqKeysManager* m) {
    furi_assert(m);
    return m->user_count + SubGhzKeyArray_size(*subghz_keystore_get_data(m->system_ks));
}

size_t subghz_keeloq_keys_user_count(SubGhzKeeloqKeysManager* m) {
    furi_assert(m);
    return m->user_count;
}

SubGhzKey* subghz_keeloq_keys_get(SubGhzKeeloqKeysManager* m, size_t i) {
    furi_assert(m);
    if(i < m->user_count) {
        return SubGhzKeyArray_get(*subghz_keystore_get_data(m->user_ks), i);
    }
    size_t si = i - m->user_count;
    furi_assert(si < SubGhzKeyArray_size(*subghz_keystore_get_data(m->system_ks)));
    return SubGhzKeyArray_get(*subghz_keystore_get_data(m->system_ks), si);
}

void subghz_keeloq_keys_add(
    SubGhzKeeloqKeysManager* m,
    uint64_t key,
    uint16_t type,
    const char* name) {
    furi_assert(m);
    SubGhzKey* entry = SubGhzKeyArray_push_raw(*subghz_keystore_get_data(m->user_ks));
    entry->name = furi_string_alloc_set(name);
    entry->key = key;
    entry->type = type;
    m->user_count++;
}

void subghz_keeloq_keys_set(
    SubGhzKeeloqKeysManager* m,
    size_t i,
    uint64_t key,
    uint16_t type,
    const char* name) {
    furi_assert(m);
    furi_assert(i < m->user_count);
    SubGhzKey* entry = SubGhzKeyArray_get(*subghz_keystore_get_data(m->user_ks), i);
    furi_string_set_str(entry->name, name);
    entry->key = key;
    entry->type = type;
}

void subghz_keeloq_keys_delete(SubGhzKeeloqKeysManager* m, size_t i) {
    furi_assert(m);
    furi_assert(i < m->user_count);
    SubGhzKeyArray_t* arr = subghz_keystore_get_data(m->user_ks);
    size_t n = m->user_count;

    SubGhzKey* entry = SubGhzKeyArray_get(*arr, i);
    furi_string_free(entry->name);

    for(size_t j = i; j + 1 < n; j++) {
        *SubGhzKeyArray_get(*arr, j) = *SubGhzKeyArray_get(*arr, j + 1);
    }

    SubGhzKey _disc;
    SubGhzKeyArray_pop_back(&_disc, *arr);
    (void)_disc;
    m->user_count--;
}

bool subghz_keeloq_keys_save(SubGhzKeeloqKeysManager* m) {
    furi_assert(m);
    bool result = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        storage_simply_mkdir(storage, EXT_PATH("subghz/assets"));

        if(!flipper_format_file_open_always(ff, SUBGHZ_KEYSTORE_DIR_USER_NAME)) {
            FURI_LOG_E(TAG, "Cannot open user keystore for write");
            break;
        }
        if(!flipper_format_write_header_cstr(
               ff, KEELOQ_KEYSTORE_FILE_TYPE, KEELOQ_KEYSTORE_FILE_VERSION)) {
            FURI_LOG_E(TAG, "Cannot write header");
            break;
        }
        uint32_t enc = 0;
        if(!flipper_format_write_uint32(ff, "Encryption", &enc, 1)) {
            FURI_LOG_E(TAG, "Cannot write Encryption");
            break;
        }
        Stream* stream = flipper_format_get_raw_stream(ff);
        result = true;

        SubGhzKeyArray_t* arr = subghz_keystore_get_data(m->user_ks);
        for(size_t i = 0; i < m->user_count; i++) {
            SubGhzKey* entry = SubGhzKeyArray_get(*arr, i);
            size_t written = stream_write_format(
                stream,
                "%016llX:%hu:%s\r\n",
                (unsigned long long)entry->key,
                entry->type,
                furi_string_get_cstr(entry->name));
            if(written == 0) {
                FURI_LOG_E(TAG, "Stream write error");
                result = false;
                break;
            }
        }
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return result;
}

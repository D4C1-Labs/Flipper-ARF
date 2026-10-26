// [ARF_PORT 2026-10-26] Global UID->key/seed database, ported from
// doctor-firmware. Lets a cracked Hitag2 key/seed be reused across different
// captures of the SAME car (keyed by UID), instead of re-cracking every time.
// Self-contained: only standard storage/furi. Coexists with the existing flat
// key dictionary at subghz/assets/hitag2 (read by the L3 brute-force level) —
// different file, different purpose.

#include "hitag2_key_db.h"

#include <furi.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define TAG "Hitag2KeyDb"

// [ARF_PORT] Upstream defines this in lib/subghz/types.h; kept local here so the
// DB stays a drop-in, dependency-free module.
#define SUBGHZ_HITAG2_KEYS_FILE EXT_PATH("subghz/assets/hitag2_keys.txt")
#define HITAG2_KEY_DB_TMP       EXT_PATH("subghz/assets/hitag2_keys.txt.tmp")
#define HITAG2_KEY_DB_LINE      96U

static const char hitag2_key_db_header[] = "# Flipper Hitag2 UID keys\n"
                                           "# UID KEY EPOCH SEED\n";

static bool hitag2_key_db_uid_ok(uint32_t uid) {
    return uid != 0U && uid != UINT32_MAX;
}

static const char* hitag2_key_db_skip_ws(const char* cursor) {
    while(*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    return cursor;
}

static int hitag2_key_db_hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hitag2_key_db_parse_hex(const char** cursor, uint8_t* out, size_t nbytes) {
    const char* text = *cursor;
    for(size_t i = 0; i < nbytes; i++) {
        const int hi = hitag2_key_db_hex_nibble(text[0]);
        const int lo = hitag2_key_db_hex_nibble(text[1]);
        if(hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
        text += 2;
    }
    *cursor = text;
    return true;
}

static bool hitag2_key_db_parse_absent_or_hex(
    const char** cursor,
    uint8_t* out,
    size_t nbytes,
    bool* present) {
    const char* text = hitag2_key_db_skip_ws(*cursor);
    if(*text == '-') {
        *present = false;
        *cursor = text + 1;
        return true;
    }
    if(!hitag2_key_db_parse_hex(&text, out, nbytes)) return false;
    *present = true;
    *cursor = text;
    return true;
}

static bool hitag2_key_db_parse_epoch(const char** cursor, bool has_key, uint32_t* epoch) {
    const char* text = hitag2_key_db_skip_ws(*cursor);
    if(*text == '-') {
        *epoch = 0U;
        *cursor = text + 1;
        return true;
    }
    if(*text < '0' || *text > '9') return false;
    uint32_t value = 0U;
    while(*text >= '0' && *text <= '9') {
        value = (value * 10U) + (uint32_t)(*text - '0');
        text++;
    }
    *epoch = has_key ? (value & 0x3FFFFUL) : 0U;
    *cursor = text;
    return true;
}

static bool hitag2_key_db_parse_line(const char* line, uint32_t* uid, Hitag2KeyDbRecord* record) {
    const char* cursor = hitag2_key_db_skip_ws(line);
    if(*cursor == '\0' || *cursor == '#') return false;

    uint8_t uid_be[4] = {0};
    if(!hitag2_key_db_parse_hex(&cursor, uid_be, sizeof(uid_be))) return false;
    *uid = ((uint32_t)uid_be[0] << 24U) | ((uint32_t)uid_be[1] << 16U) |
           ((uint32_t)uid_be[2] << 8U) | uid_be[3];

    memset(record, 0, sizeof(*record));
    uint8_t key[6] = {0};
    if(!hitag2_key_db_parse_absent_or_hex(&cursor, key, sizeof(key), &record->has_key)) {
        return false;
    }
    if(record->has_key) memcpy(record->key, key, sizeof(key));
    if(!hitag2_key_db_parse_epoch(&cursor, record->has_key, &record->epoch)) return false;

    uint8_t seed_be[4] = {0};
    if(!hitag2_key_db_parse_absent_or_hex(&cursor, seed_be, sizeof(seed_be), &record->has_seed)) {
        return false;
    }
    if(record->has_seed) {
        record->seed = ((uint32_t)seed_be[0] << 24U) | ((uint32_t)seed_be[1] << 16U) |
                       ((uint32_t)seed_be[2] << 8U) | seed_be[3];
    }

    cursor = hitag2_key_db_skip_ws(cursor);
    return *cursor == '\0' && hitag2_key_db_uid_ok(*uid);
}

static bool hitag2_key_db_read_line(File* file, char* buffer, size_t cap) {
    size_t used = 0U;
    bool any = false;
    while(used + 1U < cap) {
        char ch = 0;
        if(storage_file_read(file, &ch, 1) != 1) break;
        any = true;
        if(ch == '\n') break;
        if(ch != '\r') buffer[used++] = ch;
    }
    buffer[used] = '\0';
    if(used + 1U >= cap) {
        char ch = 0;
        while(storage_file_read(file, &ch, 1) == 1 && ch != '\n') {
        }
    }
    return any;
}

static void hitag2_key_db_format_line(
    uint32_t uid,
    const Hitag2KeyDbRecord* record,
    char* out,
    size_t cap) {
    char key[13] = "-";
    char epoch[12] = "-";
    char seed[9] = "-";
    if(record->has_key) {
        snprintf(
            key,
            sizeof(key),
            "%02X%02X%02X%02X%02X%02X",
            record->key[0],
            record->key[1],
            record->key[2],
            record->key[3],
            record->key[4],
            record->key[5]);
        snprintf(epoch, sizeof(epoch), "%lu", (unsigned long)(record->epoch & 0x3FFFFUL));
    }
    if(record->has_seed) {
        snprintf(seed, sizeof(seed), "%08lX", (unsigned long)record->seed);
    }
    snprintf(out, cap, "%08lX %s %s %s\n", (unsigned long)uid, key, epoch, seed);
}

static bool hitag2_key_db_upsert(uint32_t uid, const Hitag2KeyDbRecord* record) {
    if(!hitag2_key_db_uid_ok(uid) || !record || (!record->has_key && !record->has_seed)) {
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, EXT_PATH("subghz"));
    storage_simply_mkdir(storage, EXT_PATH("subghz/assets"));

    File* in_file = storage_file_alloc(storage);
    File* out_file = storage_file_alloc(storage);
    bool ok = false;
    bool replaced = false;
    bool failed = false;

    const bool have_in =
        storage_file_open(in_file, SUBGHZ_HITAG2_KEYS_FILE, FSAM_READ, FSOM_OPEN_EXISTING);
    if(!storage_file_open(out_file, HITAG2_KEY_DB_TMP, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        if(have_in) storage_file_close(in_file);
        storage_file_free(in_file);
        storage_file_free(out_file);
        furi_record_close(RECORD_STORAGE);
        return false;
    }

    if(!have_in) {
        const size_t header_len = strlen(hitag2_key_db_header);
        failed = storage_file_write(out_file, hitag2_key_db_header, header_len) != header_len;
    }

    if(have_in) {
        char line[HITAG2_KEY_DB_LINE];
        while(!failed && hitag2_key_db_read_line(in_file, line, sizeof(line))) {
            uint32_t line_uid = 0U;
            Hitag2KeyDbRecord parsed = {0};
            char written[HITAG2_KEY_DB_LINE];
            if(hitag2_key_db_parse_line(line, &line_uid, &parsed) && line_uid == uid) {
                if(replaced) continue;
                hitag2_key_db_format_line(uid, record, written, sizeof(written));
                replaced = true;
            } else {
                const size_t line_len = strlen(line);
                if(line_len + 2U > sizeof(written)) {
                    failed = true;
                    break;
                }
                memcpy(written, line, line_len);
                written[line_len] = '\n';
                written[line_len + 1U] = '\0';
            }
            const size_t len = strlen(written);
            if(len == 0U || storage_file_write(out_file, written, len) != len) {
                failed = true;
            }
        }
        storage_file_close(in_file);
    }

    if(!replaced && !failed) {
        char written[HITAG2_KEY_DB_LINE];
        hitag2_key_db_format_line(uid, record, written, sizeof(written));
        const size_t len = strlen(written);
        if(storage_file_write(out_file, written, len) == len) {
            replaced = true;
        } else {
            failed = true;
        }
    }

    storage_file_close(out_file);
    storage_file_free(in_file);
    storage_file_free(out_file);

    if(!failed && replaced) {
        storage_common_remove(storage, SUBGHZ_HITAG2_KEYS_FILE);
        ok = storage_common_rename(storage, HITAG2_KEY_DB_TMP, SUBGHZ_HITAG2_KEYS_FILE) == FSE_OK;
    }
    if(!ok) {
        storage_common_remove(storage, HITAG2_KEY_DB_TMP);
        FURI_LOG_W(TAG, "Failed to update %s", SUBGHZ_HITAG2_KEYS_FILE);
    }
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool hitag2_key_db_find(uint32_t uid, Hitag2KeyDbRecord* record) {
    if(!record || !hitag2_key_db_uid_ok(uid)) return false;
    memset(record, 0, sizeof(*record));

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool found = false;
    if(storage_file_open(file, SUBGHZ_HITAG2_KEYS_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[HITAG2_KEY_DB_LINE];
        while(hitag2_key_db_read_line(file, line, sizeof(line))) {
            uint32_t line_uid = 0U;
            Hitag2KeyDbRecord parsed = {0};
            if(hitag2_key_db_parse_line(line, &line_uid, &parsed) && line_uid == uid) {
                *record = parsed;
                found = record->has_key || record->has_seed;
                break;
            }
        }
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return found;
}

bool hitag2_key_db_save_key(uint32_t uid, const uint8_t key[6], uint32_t epoch) {
    if(!key) return false;
    Hitag2KeyDbRecord record = {0};
    hitag2_key_db_find(uid, &record);
    record.has_key = true;
    memcpy(record.key, key, sizeof(record.key));
    record.epoch = epoch & 0x3FFFFUL;
    return hitag2_key_db_upsert(uid, &record);
}

bool hitag2_key_db_save_seed(uint32_t uid, uint32_t seed) {
    Hitag2KeyDbRecord record = {0};
    hitag2_key_db_find(uid, &record);
    record.has_seed = true;
    record.seed = seed;
    return hitag2_key_db_upsert(uid, &record);
}

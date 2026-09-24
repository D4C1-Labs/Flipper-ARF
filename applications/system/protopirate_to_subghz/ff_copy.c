// ff_copy.c - copy-if-present FlipperFormat field primitives.
// Lifted from ProtoPirate's protopirate_storage.c and renamed to the p2s prefix.
#include "ff_copy.h"

#include <lib/flipper_format/flipper_format_i.h>
#include <toolbox/stream/stream.h>
#include <string.h>

#define TAG "P2sFfCopy"

static bool p2s_fail(const char* action, const char* key) {
    FURI_LOG_E(TAG, "%s failed: %s", action, key);
    return false;
}

static bool p2s_get_count(FlipperFormat* src, const char* key, uint32_t* count) {
    *count = 0;
    flipper_format_rewind(src);
    return flipper_format_get_value_count(src, key, count) && (*count > 0);
}

static bool p2s_stream_read_char(Stream* stream, char* out) {
    uint8_t value = 0;
    if(stream_read(stream, &value, 1U) != 1U) return false;
    *out = (char)value;
    return true;
}

static bool p2s_stream_write_char(Stream* stream, char value) {
    return stream_write_char(stream, value) == 1U;
}

// Read one full line (up to and including '\n', or EOF) into `line`.
// Returns false ONLY at a genuine EOF with no bytes read (so the caller's loop
// always terminates). This is the anti-freeze core: every call either consumes
// at least one byte or reports EOF, so the outer scan cannot spin forever.
static bool p2s_read_line(Stream* in_stream, FuriString* line) {
    furi_string_reset(line);
    char c = '\0';
    bool any = false;
    while(p2s_stream_read_char(in_stream, &c)) {
        any = true;
        furi_string_push_back(line, c);
        if(c == '\n') break;
    }
    return any;
}

// Copy an entire "key: ..." value line verbatim (handles arrays of any length).
// Rewritten to be freeze-proof: reads whole lines (guaranteed forward progress),
// matches the "key:" prefix on the assembled line, and appends the matching line
// to the destination. No char-by-char position juggling that could stall.
static bool p2s_copy_raw_value_line(Stream* out_stream, Stream* in_stream, const char* key) {
    const size_t key_len = strlen(key);
    if(!key_len || !stream_rewind(in_stream) || !stream_seek(out_stream, 0, StreamOffsetFromEnd)) {
        return p2s_fail("Stream", key);
    }

    bool copied = false;
    FuriString* line = furi_string_alloc();

    while(p2s_read_line(in_stream, line)) {
        const char* s = furi_string_get_cstr(line);
        // Match "key:" at the start of the line.
        if(strncmp(s, key, key_len) == 0 && s[key_len] == ':') {
            size_t n = furi_string_size(line);
            if(stream_write(out_stream, (const uint8_t*)s, n) != n) {
                furi_string_free(line);
                return p2s_fail("Write", key);
            }
            // Ensure the value line ends with a newline.
            if(n == 0 || s[n - 1] != '\n') {
                if(!p2s_stream_write_char(out_stream, '\n')) {
                    furi_string_free(line);
                    return p2s_fail("Write", key);
                }
            }
            copied = true;
            break; // first match is enough
        }
    }

    furi_string_free(line);
    return copied ? true : p2s_fail("Read", key);
}

bool p2s_copy_string_optional(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* key,
    FuriString* value) {
    flipper_format_rewind(src);
    if(!flipper_format_read_string(src, key, value)) {
        return true;
    }
    if(!flipper_format_write_string(dst, key, value)) {
        return p2s_fail("Write", key);
    }
    return true;
}

bool p2s_copy_u32_optional(FlipperFormat* dst, FlipperFormat* src, const char* key) {
    uint32_t value = 0;
    flipper_format_rewind(src);
    if(!flipper_format_read_uint32(src, key, &value, 1)) {
        return true;
    }
    if(!flipper_format_write_uint32(dst, key, &value, 1)) {
        return p2s_fail("Write", key);
    }
    return true;
}

bool p2s_copy_u32_fields(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* const* fields,
    size_t field_count) {
    for(size_t i = 0; i < field_count; i++) {
        if(!p2s_copy_u32_optional(dst, src, fields[i])) {
            return false;
        }
    }
    return true;
}

bool p2s_copy_hex_fixed(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* key,
    size_t len,
    bool* copied) {
    uint8_t data[8];
    furi_check(len <= sizeof(data));
    if(copied) {
        *copied = false;
    }

    flipper_format_rewind(src);
    if(!flipper_format_read_hex(src, key, data, len)) {
        return true;
    }
    if(copied) {
        *copied = true;
    }
    if(!flipper_format_write_hex(dst, key, data, len)) {
        return p2s_fail("Write", key);
    }
    return true;
}

static bool p2s_copy_u32_array(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* key,
    uint32_t count,
    uint32_t max_count) {
    if(count > max_count) {
        FURI_LOG_E(TAG, "%s too large: %lu", key, (unsigned long)count);
        return false;
    }

    Stream* in_stream = flipper_format_get_raw_stream(src);
    Stream* out_stream = flipper_format_get_raw_stream(dst);
    if(!in_stream || !out_stream) {
        FURI_LOG_E(TAG, "Raw stream missing: %s", key);
        return false;
    }

    return p2s_copy_raw_value_line(out_stream, in_stream, key);
}

bool p2s_copy_u32_array_if_present(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* key,
    uint32_t max_count) {
    uint32_t count = 0;
    if(!p2s_get_count(src, key, &count)) {
        return true;
    }
    return p2s_copy_u32_array(dst, src, key, count, max_count);
}

bool p2s_copy_hex_array_if_present(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* key,
    uint32_t max_count) {
    uint32_t count = 0;
    if(!p2s_get_count(src, key, &count)) {
        return true;
    }
    if(count > max_count) {
        FURI_LOG_E(TAG, "%s too large: %lu", key, (unsigned long)count);
        return false;
    }

    Stream* in_stream = flipper_format_get_raw_stream(src);
    Stream* out_stream = flipper_format_get_raw_stream(dst);
    if(!in_stream || !out_stream) {
        FURI_LOG_E(TAG, "Raw stream missing: %s", key);
        return false;
    }

    return p2s_copy_raw_value_line(out_stream, in_stream, key);
}

bool p2s_copy_hex_or_u32(FlipperFormat* dst, FlipperFormat* src, const char* key, size_t hex_len) {
    bool copied = false;
    if(!p2s_copy_hex_fixed(dst, src, key, hex_len, &copied)) {
        return false;
    }
    return copied || p2s_copy_u32_optional(dst, src, key);
}

bool p2s_copy_key(FlipperFormat* dst, FlipperFormat* src, FuriString* value) {
    uint32_t count = 0;

    flipper_format_rewind(src);
    if(flipper_format_read_string(src, "Key", value)) {
        if(!flipper_format_write_string(dst, "Key", value)) {
            return p2s_fail("Write", "Key");
        }
        return true;
    }

    if(p2s_get_count(src, "Key", &count)) {
        return p2s_copy_u32_array(dst, src, "Key", count, 1024);
    }

    return p2s_copy_hex_fixed(dst, src, "Key", 8, NULL);
}

bool p2s_copy_key2(FlipperFormat* dst, FlipperFormat* src) {
    bool copied = false;
    if(!p2s_copy_hex_fixed(dst, src, "Key2", 8, &copied)) {
        return false;
    }
    if(copied) {
        return true;
    }
    return p2s_copy_hex_or_u32(dst, src, "Key2", 4);
}

bool p2s_copy_raw_if_present(FlipperFormat* dst, FlipperFormat* src, const char* key) {
    uint32_t count = 0;
    if(!p2s_get_count(src, key, &count)) {
        return true; // not present
    }

    Stream* in_stream = flipper_format_get_raw_stream(src);
    Stream* out_stream = flipper_format_get_raw_stream(dst);
    if(!in_stream || !out_stream) {
        FURI_LOG_E(TAG, "Raw stream missing: %s", key);
        return false;
    }

    return p2s_copy_raw_value_line(out_stream, in_stream, key);
}

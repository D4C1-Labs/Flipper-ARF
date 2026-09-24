// ff_copy.h - copy-if-present FlipperFormat field primitives.
// Lifted from ProtoPirate's protopirate_storage.c and renamed to the p2s prefix.
#pragma once

#include <flipper_format.h>
#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

// Copy a string field from src to dst if the key exists (rewind-based).
bool p2s_copy_string_optional(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* key,
    FuriString* value);

// Copy a single uint32 field if present.
bool p2s_copy_u32_optional(FlipperFormat* dst, FlipperFormat* src, const char* key);

// Copy a list of single-uint32 fields if present.
bool p2s_copy_u32_fields(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* const* fields,
    size_t field_count);

// Copy a fixed-length hex field if present. *copied set to whether it was found.
bool p2s_copy_hex_fixed(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* key,
    size_t len,
    bool* copied);

// Copy a uint32 array (raw line copy) if present, bounded by max_count.
bool p2s_copy_u32_array_if_present(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* key,
    uint32_t max_count);

// Copy a hex array (raw line copy) if present, bounded by max_count.
bool p2s_copy_hex_array_if_present(
    FlipperFormat* dst,
    FlipperFormat* src,
    const char* key,
    uint32_t max_count);

// Copy a field that may be stored as fixed hex or as uint32.
bool p2s_copy_hex_or_u32(FlipperFormat* dst, FlipperFormat* src, const char* key, size_t hex_len);

// Copy the "Key" field (string, uint32-array, or 8-byte hex forms).
bool p2s_copy_key(FlipperFormat* dst, FlipperFormat* src, FuriString* value);

// Copy the "Key2" field (8-byte hex, or hex/u32 forms).
bool p2s_copy_key2(FlipperFormat* dst, FlipperFormat* src);

// Copy a whole "key: ..." line verbatim if present, regardless of value form.
// Preserves the exact stored representation (string/hex/u32/array).
bool p2s_copy_raw_if_present(FlipperFormat* dst, FlipperFormat* src, const char* key);

#ifdef __cplusplus
}
#endif

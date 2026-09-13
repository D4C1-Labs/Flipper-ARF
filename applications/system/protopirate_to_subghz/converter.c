// converter.c - the conversion engine.
#include "converter.h"
#include "paths.h"
#include "ff_copy.h"

#include <storage/storage.h>
#include <flipper_format.h>
#include <toolbox/path.h>
#include <string.h>

#define TAG "P2sConvert"

// ---------------------------------------------------------------------------
// Protocol name translation table: { ProtoPirate name, main-subghz .name, brand }
// If a name is not found, it is passed through unchanged and routed to "Other".
// ---------------------------------------------------------------------------
const P2sNameEntry p2s_name_table[] = {
    {"Chrysler V0", "Chrysler", "Chrysler"},
    {"Fiat V0", "Fiat V0", "Fiat"},
    {"Fiat V1", "Fiat V1", "Fiat"},
    {"Fiat V2", "Fiat V2", "Fiat"},
    {"Ford V0", "FORD V0", "Ford"},
    {"Ford V1", "Ford V1", "Ford"},
    {"Ford V2", "Ford V2", "Ford"},
    {"Ford V3", "Ford V3", "Ford"},
    {"Honda Static", "Honda Static", "Honda"},
    {"Honda V1", "Honda V1", "Honda"},
    {"Honda V2", "Honda V2", "Honda"},
    {"Kia V0", "KIA/HYU V0", "Kia_Hyundai"},
    {"Kia V1", "KIA/HYU V1", "Kia_Hyundai"},
    {"Kia V2", "KIA/HYU V2", "Kia_Hyundai"},
    {"Kia V3", "KIA/HYU V3/V4", "Kia_Hyundai"},
    {"Kia V4", "KIA/HYU V3/V4", "Kia_Hyundai"},
    {"Kia V5", "KIA/HYU V5", "Kia_Hyundai"},
    {"Kia V6", "KIA/HYU V6", "Kia_Hyundai"},
    {"Kia V7", "Kia V7", "Kia_Hyundai"},
    {"Mazda V0", "Mazda V0", "Mazda"},
    {"Mitsubishi V0", "Mitsubishi V0", "Mitsubishi"},
    {"Porsche Touareg", "Porsche AG", "Porsche"},
    {"PSA", "PSA GROUP", "PSA"},
    {"Renault V0", "Renault V0", "Renault"},
    {"Subaru", "SUBARU", "Subaru"},
    {"VAG", "VAG GROUP", "VAG"},
    {"Star Line", "Star Line", "StarLine"},
    {"Scher-Khan", "Scher-Khan", "ScherKhan"},
    // Additional main-tree entries that have no distinct PP name; included for
    // the reverse (.sub -> .psf) direction and brand routing.
    {"Fiat SPA", "FIAT SPA", "Fiat"},
    {"MazdaSiemens", "MazdaSiemens", "Mazda"},
    {"Renault V1", "Renault V1", "Renault"},
    {"Suzuki", "SUZUKI", "Suzuki"},
};
const size_t p2s_name_table_size = COUNT_OF(p2s_name_table);

// ---------------------------------------------------------------------------
// Preset short<->long normalization map.
// ---------------------------------------------------------------------------
const P2sPresetEntry p2s_preset_table[] = {
    {"AM270", "FuriHalSubGhzPresetOok270Async"},
    {"AM650", "FuriHalSubGhzPresetOok650Async"},
    {"FM238", "FuriHalSubGhzPreset2FSKDev238Async"},
    {"FM12K", "FuriHalSubGhzPreset2FSKDev12KAsync"},
    {"FM476", "FuriHalSubGhzPreset2FSKDev476Async"},
};
const size_t p2s_preset_table_size = COUNT_OF(p2s_preset_table);

// ---------------------------------------------------------------------------
// Extra fields copied verbatim (both directions), after the header block.
// ---------------------------------------------------------------------------
// Note: "Counter" is handled explicitly (with the Porsche Cnt->Counter map),
// so it is intentionally absent from this list to avoid a double-write.
static const char* const p2s_extra_u32_fields[] = {
    "Serial",       "Btn",          "Cnt",   "CRC",  "Type", "Encrypted",
    "Decrypted",    "KIAVersion",   "Key_4", "Fx",   "Hop",  "Hitag2 Epoch",
    "Hitag2 IV",    "Hitag2 Slice", "Checksum", "Seed", "YekHi", "YekLo",
    "TE",
};

// ---------------------------------------------------------------------------
// Name / brand translation helpers.
// ---------------------------------------------------------------------------

// Translate a Protocol value from the pp column to the sub column.
// Returns the brand folder via *brand (may stay "Other"). If not found, the
// original name is returned unchanged.
static const char* p2s_translate_pp_to_sub(const char* pp_name, const char** brand) {
    for(size_t i = 0; i < p2s_name_table_size; i++) {
        if(strcmp(p2s_name_table[i].pp_name, pp_name) == 0) {
            *brand = p2s_name_table[i].brand;
            return p2s_name_table[i].sub_name;
        }
    }
    *brand = P2S_BRAND_OTHER;
    return pp_name;
}

// Translate a Protocol value from the sub column to the pp column.
static const char* p2s_translate_sub_to_pp(const char* sub_name) {
    for(size_t i = 0; i < p2s_name_table_size; i++) {
        if(strcmp(p2s_name_table[i].sub_name, sub_name) == 0) {
            return p2s_name_table[i].pp_name;
        }
    }
    return sub_name;
}

// Normalize a preset value to the long form. Returns pointer into the table if
// a short form was matched, otherwise NULL (already long / unknown).
static const char* p2s_preset_to_long(const char* preset) {
    for(size_t i = 0; i < p2s_preset_table_size; i++) {
        if(strcmp(p2s_preset_table[i].short_name, preset) == 0) {
            return p2s_preset_table[i].long_name;
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// kia_v0 subtype routing (brand folder only). Protocol stays "KIA/HYU V0".
// ---------------------------------------------------------------------------
static const char* p2s_kia_v0_brand(FlipperFormat* src) {
    uint32_t type = 0;
    flipper_format_rewind(src);
    if(flipper_format_read_uint32(src, "Type", &type, 1)) {
        switch(type) {
        case 1:
            return "Kia_Hyundai";
        case 2:
            return "Suzuki";
        case 3:
            return "Honda";
        default:
            return "Kia_Hyundai";
        }
    }

    uint32_t bit = 0;
    flipper_format_rewind(src);
    if(flipper_format_read_uint32(src, "Bit", &bit, 1)) {
        switch(bit) {
        case 61:
            return "Kia_Hyundai";
        case 64:
            return "Suzuki";
        case 72:
            return "Honda";
        default:
            return "Kia_Hyundai";
        }
    }

    return "Kia_Hyundai";
}

// ---------------------------------------------------------------------------
// Per-protocol required-field check (for the .psf -> .sub direction).
// Keyed by the translated main-subghz name.
// ---------------------------------------------------------------------------
typedef struct {
    const char* sub_name;
    const char* required[3];
} P2sRequired;

static const P2sRequired p2s_required_table[] = {
    {"Porsche AG", {"Serial", "Btn", "Counter"}},
    {"PSA GROUP", {"Key_2", NULL, NULL}},
    {"Fiat V2", {"Raw", NULL, NULL}},
    {"Renault V0", {"Key2", NULL, NULL}},
    {"Renault V1", {"Key2", NULL, NULL}},
    {"Chrysler", {"Key_2", NULL, NULL}},
    {"Ford V1", {"Key", "Key_2", NULL}},
};

static const P2sRequired* p2s_required_for(const char* sub_name) {
    for(size_t i = 0; i < COUNT_OF(p2s_required_table); i++) {
        if(strcmp(p2s_required_table[i].sub_name, sub_name) == 0) {
            return &p2s_required_table[i];
        }
    }
    return NULL;
}

// Check a key exists in the destination flipper_format (any value type).
static bool p2s_dst_has_key(FlipperFormat* dst, const char* key) {
    flipper_format_rewind(dst);
    return flipper_format_key_exist(dst, key);
}

// ---------------------------------------------------------------------------
// Copy the shared "extra" fields verbatim. Handles the polymorphic Key/Key2
// forms plus the fixed/array/uint32 fields. Direction-independent.
// ---------------------------------------------------------------------------
static bool p2s_copy_extras(FlipperFormat* dst, FlipperFormat* src) {
    // Polymorphic key fields (string/hex/u32): copy the line verbatim so the
    // exact stored representation is preserved regardless of length/type.
    if(!p2s_copy_raw_if_present(dst, src, "Key_2")) return false;
    if(!p2s_copy_raw_if_present(dst, src, "Key_3")) return false;
    if(!p2s_copy_raw_if_present(dst, src, "Key2")) return false;
    if(!p2s_copy_raw_if_present(dst, src, "ValidationField")) return false;

    // Fixed-length hex fields.
    if(!p2s_copy_hex_fixed(dst, src, "Hitag2 Key", 6, NULL)) return false;

    // Long arrays / verbatim lines.
    if(!p2s_copy_raw_if_present(dst, src, "Raw")) return false;
    if(!p2s_copy_u32_array_if_present(dst, src, "RAW_Data", 4096)) return false;
    if(!p2s_copy_hex_array_if_present(dst, src, "Custom_preset_data", 1024)) return false;
    if(!p2s_copy_raw_if_present(dst, src, "Custom_preset_module")) return false;

    // Simple uint32 fields.
    if(!p2s_copy_u32_fields(dst, src, p2s_extra_u32_fields, COUNT_OF(p2s_extra_u32_fields)))
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Preset: read from src, normalize to long form, write to dst.
// ---------------------------------------------------------------------------
static bool p2s_write_preset_long(FlipperFormat* dst, FlipperFormat* src, FuriString* tmp) {
    flipper_format_rewind(src);
    if(!flipper_format_read_string(src, "Preset", tmp)) {
        return true; // no preset present, nothing to write
    }
    const char* longform = p2s_preset_to_long(furi_string_get_cstr(tmp));
    if(longform) {
        furi_string_set(tmp, longform);
    }
    return flipper_format_write_string(dst, "Preset", tmp);
}

// ---------------------------------------------------------------------------
// Output path helpers.
// ---------------------------------------------------------------------------

// Ensure cars/<brand> exists and produce a non-colliding output path.
static void p2s_build_sub_out_path(
    Storage* storage,
    const char* brand,
    const char* src_path,
    FuriString* out_path) {
    storage_simply_mkdir(storage, CARS_DIR);

    FuriString* brand_dir = furi_string_alloc();
    furi_string_printf(brand_dir, "%s/%s", CARS_DIR, brand);
    storage_simply_mkdir(storage, furi_string_get_cstr(brand_dir));

    FuriString* src = furi_string_alloc();
    furi_string_set(src, src_path);
    FuriString* base = furi_string_alloc();
    path_extract_filename(src, base, true); // strip extension

    storage_get_next_filename(
        storage,
        furi_string_get_cstr(brand_dir),
        furi_string_get_cstr(base),
        SUB_EXTENSION,
        out_path,
        200);
    // storage_get_next_filename returns bare name; prepend the directory.
    FuriString* full = furi_string_alloc();
    furi_string_printf(
        full, "%s/%s%s", furi_string_get_cstr(brand_dir), furi_string_get_cstr(out_path), SUB_EXTENSION);
    furi_string_set(out_path, full);

    furi_string_free(full);
    furi_string_free(base);
    furi_string_free(src);
    furi_string_free(brand_dir);
}

static void
    p2s_build_psf_out_path(Storage* storage, const char* src_path, FuriString* out_path) {
    storage_simply_mkdir(storage, PP_SAVED_DIR);

    FuriString* src = furi_string_alloc();
    furi_string_set(src, src_path);
    FuriString* base = furi_string_alloc();
    path_extract_filename(src, base, true);

    storage_get_next_filename(
        storage, PP_SAVED_DIR, furi_string_get_cstr(base), PP_EXTENSION, out_path, 200);
    FuriString* full = furi_string_alloc();
    furi_string_printf(
        full, "%s/%s%s", PP_SAVED_DIR, furi_string_get_cstr(out_path), PP_EXTENSION);
    furi_string_set(out_path, full);

    furi_string_free(full);
    furi_string_free(base);
    furi_string_free(src);
}

// ---------------------------------------------------------------------------
// .psf -> .sub
// ---------------------------------------------------------------------------
P2sResult p2s_convert_psf_to_sub(const char* src_path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* src = flipper_format_file_alloc(storage);
    FlipperFormat* dst = flipper_format_file_alloc(storage);
    FuriString* tmp = furi_string_alloc();
    FuriString* protocol = furi_string_alloc();
    FuriString* out_path = furi_string_alloc();

    P2sResult result = P2sResultError;
    bool opened_dst = false;

    do {
        uint32_t version = 0;
        FuriString* filetype = furi_string_alloc();
        bool header_ok = flipper_format_file_open_existing(src, src_path) &&
                         flipper_format_read_header(src, filetype, &version);
        furi_string_free(filetype);
        if(!header_ok) {
            FURI_LOG_E(TAG, "Bad source header: %s", src_path);
            break;
        }

        // Read + translate the protocol name.
        flipper_format_rewind(src);
        if(!flipper_format_read_string(src, "Protocol", protocol)) {
            FURI_LOG_E(TAG, "No Protocol in %s", src_path);
            break;
        }
        const char* brand = P2S_BRAND_OTHER;
        const char* sub_name =
            p2s_translate_pp_to_sub(furi_string_get_cstr(protocol), &brand);

        // kia_v0 subtype routing (brand folder only).
        if(strcmp(furi_string_get_cstr(protocol), "Kia V0") == 0) {
            brand = p2s_kia_v0_brand(src);
        }

        // Build the output path (creates the brand folder).
        p2s_build_sub_out_path(storage, brand, src_path, out_path);

        if(!flipper_format_file_open_new(dst, furi_string_get_cstr(out_path))) {
            FURI_LOG_E(TAG, "Cannot create %s", furi_string_get_cstr(out_path));
            break;
        }
        opened_dst = true;

        // Header.
        if(!flipper_format_write_header_cstr(dst, P2S_FILETYPE, P2S_FILE_VERSION)) break;

        // .sub order: Frequency, Preset(long), Protocol(translated), Bit, Key, extras.
        if(!p2s_copy_u32_optional(dst, src, "Frequency")) break;
        if(!p2s_write_preset_long(dst, src, tmp)) break;
        if(!flipper_format_write_string_cstr(dst, "Protocol", sub_name)) break;
        if(!p2s_copy_u32_optional(dst, src, "Bit")) break;
        if(!p2s_copy_key(dst, src, tmp)) break;

        // Counter: copy verbatim if present. For Porsche the PP capture may only
        // carry "Cnt"; map it to the "Counter" name the main app requires.
        {
            uint32_t counter = 0;
            flipper_format_rewind(src);
            if(flipper_format_read_uint32(src, "Counter", &counter, 1)) {
                if(!flipper_format_write_uint32(dst, "Counter", &counter, 1)) break;
            } else if(strcmp(sub_name, "Porsche AG") == 0) {
                flipper_format_rewind(src);
                if(flipper_format_read_uint32(src, "Cnt", &counter, 1)) {
                    if(!flipper_format_write_uint32(dst, "Counter", &counter, 1)) break;
                }
            }
        }

        if(!p2s_copy_extras(dst, src)) break;

        // Enforce required fields for the destination protocol.
        const P2sRequired* req = p2s_required_for(sub_name);
        if(req) {
            bool missing = false;
            for(size_t i = 0; i < COUNT_OF(req->required); i++) {
                if(req->required[i] && !p2s_dst_has_key(dst, req->required[i])) {
                    FURI_LOG_W(
                        TAG,
                        "%s missing required field %s -> skip",
                        sub_name,
                        req->required[i]);
                    missing = true;
                    break;
                }
            }
            if(missing) {
                result = P2sResultSkipped;
                break;
            }
        }

        result = P2sResultOk;
    } while(false);

    flipper_format_free(dst);
    flipper_format_free(src);

    // If we created a file but did not finish successfully, remove it.
    if(opened_dst && result != P2sResultOk) {
        storage_simply_remove(storage, furi_string_get_cstr(out_path));
    }

    furi_string_free(out_path);
    furi_string_free(protocol);
    furi_string_free(tmp);
    furi_record_close(RECORD_STORAGE);
    return result;
}

// ---------------------------------------------------------------------------
// .sub -> .psf
// ---------------------------------------------------------------------------
P2sResult p2s_convert_sub_to_psf(const char* src_path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* src = flipper_format_file_alloc(storage);
    FlipperFormat* dst = flipper_format_file_alloc(storage);
    FuriString* tmp = furi_string_alloc();
    FuriString* protocol = furi_string_alloc();
    FuriString* out_path = furi_string_alloc();

    P2sResult result = P2sResultError;
    bool opened_dst = false;

    do {
        uint32_t version = 0;
        FuriString* filetype = furi_string_alloc();
        bool header_ok = flipper_format_file_open_existing(src, src_path) &&
                         flipper_format_read_header(src, filetype, &version);
        furi_string_free(filetype);
        if(!header_ok) {
            FURI_LOG_E(TAG, "Bad source header: %s", src_path);
            break;
        }

        flipper_format_rewind(src);
        if(!flipper_format_read_string(src, "Protocol", protocol)) {
            FURI_LOG_E(TAG, "No Protocol in %s", src_path);
            break;
        }
        const char* pp_name = p2s_translate_sub_to_pp(furi_string_get_cstr(protocol));

        p2s_build_psf_out_path(storage, src_path, out_path);

        if(!flipper_format_file_open_new(dst, furi_string_get_cstr(out_path))) {
            FURI_LOG_E(TAG, "Cannot create %s", furi_string_get_cstr(out_path));
            break;
        }
        opened_dst = true;

        if(!flipper_format_write_header_cstr(dst, P2S_FILETYPE, P2S_FILE_VERSION)) break;

        // .psf order: Protocol(translated), Bit, Key, Frequency, Preset, extras.
        if(!flipper_format_write_string_cstr(dst, "Protocol", pp_name)) break;
        if(!p2s_copy_u32_optional(dst, src, "Bit")) break;
        if(!p2s_copy_key(dst, src, tmp)) break;
        if(!p2s_copy_u32_optional(dst, src, "Frequency")) break;
        // Preset copied verbatim; PP tolerates the long form.
        if(!p2s_copy_string_optional(dst, src, "Preset", tmp)) break;

        // Counter is handled explicitly in extras' sibling; preserve it here.
        if(!p2s_copy_u32_optional(dst, src, "Counter")) break;

        if(!p2s_copy_extras(dst, src)) break;

        result = P2sResultOk;
    } while(false);

    flipper_format_free(dst);
    flipper_format_free(src);

    if(opened_dst && result != P2sResultOk) {
        storage_simply_remove(storage, furi_string_get_cstr(out_path));
    }

    furi_string_free(out_path);
    furi_string_free(protocol);
    furi_string_free(tmp);
    furi_record_close(RECORD_STORAGE);
    return result;
}

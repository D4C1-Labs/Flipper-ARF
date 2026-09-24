// paths.h - path constants, preset map and the protocol name / brand table.
#pragma once

#include <stddef.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

// ProtoPirate saves its captures here. This app's own APP_DATA_PATH would
// resolve to its own appid, so we use the literal proto_pirate folder.
#define PP_SAVED_DIR  "/ext/apps_data/proto_pirate/saved"
#define PP_EXTENSION  ".psf"

// Standard SubGHz tree.
#define SUBGHZ_DIR    EXT_PATH("subghz")
#define CARS_DIR      EXT_PATH("subghz/cars")
#define SUB_EXTENSION ".sub"

// Common Flipper SubGhz Key File header.
#define P2S_FILETYPE     "Flipper SubGhz Key File"
#define P2S_FILE_VERSION 1

// Brand folder used for protocols that are not present in the table.
#define P2S_BRAND_OTHER "Other"

// One entry of the protocol name translation table.
typedef struct {
    const char* pp_name; // ProtoPirate ".Protocol" value
    const char* sub_name; // main-subghz registry ".name" (exact strcmp)
    const char* brand; // brand folder under cars/
} P2sNameEntry;

// The translation table. Defined in converter.c.
extern const P2sNameEntry p2s_name_table[];
extern const size_t p2s_name_table_size;

// One entry of the preset short<->long normalization map.
typedef struct {
    const char* short_name; // e.g. "AM650"
    const char* long_name; // e.g. "FuriHalSubGhzPresetOok650Async"
} P2sPresetEntry;

extern const P2sPresetEntry p2s_preset_table[];
extern const size_t p2s_preset_table_size;

#ifdef __cplusplus
}
#endif

#include "subghz_debug_log.h"

#include <furi.h>
#include <storage/storage.h>
#include <stdarg.h>

#define SUBGHZ_DEBUG_LOG_DIR  "/ext/apps_data/subghz_garage"
#define SUBGHZ_DEBUG_LOG_PATH SUBGHZ_DEBUG_LOG_DIR "/ram_debug.log"

void subghz_debug_log_write(const char* format, ...) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    storage_simply_mkdir(storage, "/ext/apps_data");
    storage_simply_mkdir(storage, SUBGHZ_DEBUG_LOG_DIR);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, SUBGHZ_DEBUG_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        FuriString* line = furi_string_alloc_printf("[%lu] ", (unsigned long)furi_get_tick());

        va_list args;
        va_start(args, format);
        furi_string_cat_vprintf(line, format, args);
        va_end(args);

        furi_string_cat(line, "\n");

        storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));
        furi_string_free(line);
    }
    storage_file_close(file);
    storage_file_free(file);

    furi_record_close(RECORD_STORAGE);
}

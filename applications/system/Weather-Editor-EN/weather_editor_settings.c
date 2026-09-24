#include "weather_editor_settings.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/stream/file_stream.h>

#define WEATHER_EDITOR_SETTINGS_DIR EXT_PATH("apps_data/weather_editor")
#define WEATHER_EDITOR_SETTINGS_FILE EXT_PATH("apps_data/weather_editor/settings.bin")
#define WEATHER_EDITOR_SETTINGS_MAGIC 0x574C4142UL /* WLAB */
#define WEATHER_EDITOR_SETTINGS_VERSION 6U
#define WEATHER_EDITOR_DEFAULT_AUTO_TX_INTERVAL_S 5U
#define WEATHER_EDITOR_DEFAULT_PROTOCOL_TEST_INTERVAL_S 5U
#define WEATHER_EDITOR_DEFAULT_PROTOCOL_TEST_FREQUENCY_HZ 433920000UL

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t display_fahrenheit;
    uint16_t auto_tx_interval_s;
    uint16_t legacy_reserved_interval;
    uint8_t legacy_reserved_source;
    uint8_t reserved;
    uint16_t protocol_test_interval_s;
    uint32_t protocol_test_frequency_hz;
    uint8_t legacy_reserved_logging;
    uint8_t reserved2[3];
} WeatherEditorSettingsFile;

void weather_editor_settings_set_defaults(WeatherEditorSettings* settings) {
    furi_assert(settings);
    settings->display_fahrenheit = false;
    settings->auto_tx_interval_s = WEATHER_EDITOR_DEFAULT_AUTO_TX_INTERVAL_S;
    settings->protocol_test_interval_s = WEATHER_EDITOR_DEFAULT_PROTOCOL_TEST_INTERVAL_S;
    settings->protocol_test_frequency_hz = WEATHER_EDITOR_DEFAULT_PROTOCOL_TEST_FREQUENCY_HZ;
}

bool weather_editor_settings_load(WeatherEditorSettings* settings) {
    furi_assert(settings);
    weather_editor_settings_set_defaults(settings);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    bool ok = false;

    if(file_stream_open(stream, WEATHER_EDITOR_SETTINGS_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        WeatherEditorSettingsFile file = {0};
        const size_t read = stream_read(stream, (uint8_t*)&file, sizeof(file));
        /* Versions 1-2 occupied the first 8 bytes. Version 3 extends the file
           but preserves the old field layout for backwards compatibility. */
        if(read >= 8U && file.magic == WEATHER_EDITOR_SETTINGS_MAGIC) {
            settings->display_fahrenheit = file.display_fahrenheit != 0;
            if(file.version >= 2U && file.auto_tx_interval_s >= 1U &&
               file.auto_tx_interval_s <= 3600U) {
                settings->auto_tx_interval_s = file.auto_tx_interval_s;
            }
            if(file.version >= 4U && read >= 14U &&
               file.protocol_test_interval_s >= 1U &&
               file.protocol_test_interval_s <= 10U) {
                settings->protocol_test_interval_s = file.protocol_test_interval_s;
            }
            if(file.version >= 5U && read >= 20U &&
               file.protocol_test_frequency_hz >= 1000UL &&
               file.protocol_test_frequency_hz <= 999999000UL) {
                settings->protocol_test_frequency_hz = file.protocol_test_frequency_hz;
            }
            ok = file.version >= 1U && file.version <= WEATHER_EDITOR_SETTINGS_VERSION;
        }
        file_stream_close(stream);
    }

    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool weather_editor_settings_save(const WeatherEditorSettings* settings) {
    furi_assert(settings);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, WEATHER_EDITOR_SETTINGS_DIR);
    Stream* stream = file_stream_alloc(storage);
    bool ok = false;

    if(file_stream_open(stream, WEATHER_EDITOR_SETTINGS_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        uint16_t auto_interval = settings->auto_tx_interval_s;
        if(auto_interval < 1U || auto_interval > 3600U)
            auto_interval = WEATHER_EDITOR_DEFAULT_AUTO_TX_INTERVAL_S;
        uint16_t protocol_test_interval = settings->protocol_test_interval_s;
        if(protocol_test_interval < 1U || protocol_test_interval > 10U)
            protocol_test_interval = WEATHER_EDITOR_DEFAULT_PROTOCOL_TEST_INTERVAL_S;
        uint32_t protocol_test_frequency = settings->protocol_test_frequency_hz;
        if(protocol_test_frequency < 1000UL || protocol_test_frequency > 999999000UL)
            protocol_test_frequency = WEATHER_EDITOR_DEFAULT_PROTOCOL_TEST_FREQUENCY_HZ;

        const WeatherEditorSettingsFile file = {
            .magic = WEATHER_EDITOR_SETTINGS_MAGIC,
            .version = WEATHER_EDITOR_SETTINGS_VERSION,
            .display_fahrenheit = settings->display_fahrenheit ? 1U : 0U,
            .auto_tx_interval_s = auto_interval,
            .legacy_reserved_interval = 0U,
            .legacy_reserved_source = 0U,
            .reserved = 0U,
            .protocol_test_interval_s = protocol_test_interval,
            .protocol_test_frequency_hz = protocol_test_frequency,
            .legacy_reserved_logging = 0U,
            .reserved2 = {0U, 0U, 0U},
        };
        ok = stream_write(stream, (const uint8_t*)&file, sizeof(file)) == sizeof(file);
        file_stream_close(stream);
    }

    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

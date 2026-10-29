#include <furi.h>
#include <furi_hal.h>

#include <targets/f7/furi_hal/furi_hal_subghz.h>

#include <flipper_format/flipper_format_i.h>

#include <subghz/subghz_last_settings.h>

void subghz_dangerous_freq() {
    bool is_extended_i = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_existing(fff_data_file, "/ext/subghz/assets/dangerous_settings")) {
        flipper_format_read_bool(
            fff_data_file, "yes_i_want_to_destroy_my_flipper", &is_extended_i, 1);
    }

    flipper_format_free(fff_data_file);

    SubGhzGarageLastSettings* last_settings = subghz_garage_last_settings_alloc();
    subghz_garage_last_settings_load(last_settings, 0);

    furi_hal_subghz_set_dangerous_frequency(is_extended_i || last_settings->bypass_region_lock);

    subghz_garage_set_ext_leds_and_amp(last_settings->leds_and_amp);

    subghz_garage_last_settings_free(last_settings);

    furi_record_close(RECORD_STORAGE);
}

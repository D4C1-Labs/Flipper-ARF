#include <furi.h>
#include <furi_hal.h>

#include <targets/f7/furi_hal/furi_hal_subghz_i.h>

#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>

/* This STARTUP hook runs inside the firmware, but the SubGHz app (which owns
 * subghz_last_settings.c) is now an external FAP. To avoid linking the app's
 * settings code into the firmware, read the LED / power-amp preference straight
 * from the last-settings file. The field key mirrors
 * applications/main/subghz/subghz_last_settings.c
 * (SUBGHZ_LAST_SETTING_FIELD_LED_AND_POWER_AMP). */
#define SUBGHZ_LAST_SETTINGS_PATH EXT_PATH("subghz/assets/last_subghz.settings")
#define SUBGHZ_LAST_SETTING_FIELD_LED_AND_POWER_AMP "LedAndPowerAmp"

void subghz_dangerous_freq() {
    bool is_extended_i = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_existing(fff_data_file, "/ext/subghz/assets/dangerous_settings")) {
        flipper_format_read_bool(
            fff_data_file, "yes_i_want_to_destroy_my_flipper", &is_extended_i, 1);
    }

    furi_hal_subghz_set_dangerous_frequency(is_extended_i);

    flipper_format_free(fff_data_file);

    /* Default: enabled. */
    bool leds_and_amp = true;
    FlipperFormat* fff_last = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_existing(fff_last, SUBGHZ_LAST_SETTINGS_PATH)) {
        if(!flipper_format_read_bool(
               fff_last, SUBGHZ_LAST_SETTING_FIELD_LED_AND_POWER_AMP, &leds_and_amp, 1)) {
            leds_and_amp = true;
        }
    }
    flipper_format_free(fff_last);

    // Set LED and Amp GPIO control state
    furi_hal_subghz_set_ext_leds_and_amp(leds_and_amp);

    furi_record_close(RECORD_STORAGE);
}

#pragma once

#include <lib/subghz/environment.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <furi_hal_subghz.h>

#ifdef SUBGHZ_GARAGE_HAS_LIB_EXTENSIONS

static inline void subghz_garage_env_reset_keeloq(SubGhzEnvironment* env) {
    subghz_environment_reset_keeloq(env);
}

static inline void subghz_garage_setting_mark_default_frequency(
    SubGhzSetting* setting, uint32_t frequency) {
    subghz_setting_set_default_frequency(setting, frequency);
}

static inline void subghz_garage_set_rolling_counter_mult(int32_t mult) {
    furi_hal_subghz_set_rolling_counter_mult(mult);
}

static inline int32_t subghz_garage_get_rolling_counter_mult(void) {
    return furi_hal_subghz_get_rolling_counter_mult();
}

static inline void subghz_garage_encoder_get_text_progress(
    SubGhzFileEncoderWorker* worker, FuriString* output) {
    subghz_file_encoder_worker_get_text_progress(worker, output);
}

static inline void subghz_garage_set_ext_leds_and_amp(bool enabled) {
    furi_hal_subghz_set_ext_leds_and_amp(enabled);
}

#else

static inline void subghz_garage_env_reset_keeloq(SubGhzEnvironment* env) {
    UNUSED(env);

}

static inline void subghz_garage_setting_mark_default_frequency(
    SubGhzSetting* setting, uint32_t frequency) {
    UNUSED(setting);
    UNUSED(frequency);

}

static inline void subghz_garage_set_rolling_counter_mult(int32_t mult) {
    UNUSED(mult);
}

static inline int32_t subghz_garage_get_rolling_counter_mult(void) {

    return -0x7FFFFFFF;
}

static inline void subghz_garage_encoder_get_text_progress(
    SubGhzFileEncoderWorker* worker, FuriString* output) {
    UNUSED(worker);

    furi_string_set(output, "Decoding...");
}

static inline void subghz_garage_set_ext_leds_and_amp(bool enabled) {
    UNUSED(enabled);

}

#endif

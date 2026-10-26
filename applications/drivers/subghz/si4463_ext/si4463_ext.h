#pragma once

#include <lib/subghz/devices/preset.h>
#include <lib/subghz/devices/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <toolbox/level_duration.h>
#include <furi_hal_gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*SubGhzDeviceSi4463ExtCaptureCallback)(bool level, uint32_t duration, void* context);
typedef LevelDuration (*SubGhzDeviceSi4463ExtCallback)(void* context);

bool subghz_device_si4463_ext_alloc(SubGhzDeviceConf* conf);
void subghz_device_si4463_ext_free(void);
bool subghz_device_si4463_ext_is_connect(void);
void subghz_device_si4463_ext_reset(void);
void subghz_device_si4463_ext_sleep(void);
void subghz_device_si4463_ext_idle(void);
void subghz_device_si4463_ext_load_custom_preset(const uint8_t* preset_data);
uint32_t subghz_device_si4463_ext_set_frequency(uint32_t frequency);
bool subghz_device_si4463_ext_is_frequency_valid(uint32_t frequency);
void subghz_device_si4463_ext_set_async_mirror_pin(const GpioPin* pin);
const GpioPin* subghz_device_si4463_ext_get_data_gpio(void);

bool subghz_device_si4463_ext_tx(void);
void subghz_device_si4463_ext_flush_tx(void);
bool subghz_device_si4463_ext_start_async_tx(SubGhzDeviceSi4463ExtCallback callback, void* context);
bool subghz_device_si4463_ext_is_async_tx_complete(void);
void subghz_device_si4463_ext_stop_async_tx(void);

void subghz_device_si4463_ext_rx(void);
void subghz_device_si4463_ext_flush_rx(void);
void subghz_device_si4463_ext_start_async_rx(
    SubGhzDeviceSi4463ExtCaptureCallback callback,
    void* context);
void subghz_device_si4463_ext_stop_async_rx(void);

float subghz_device_si4463_ext_get_rssi(void);
uint32_t subghz_device_si4463_ext_rssi_burst(uint8_t* out, size_t count, uint32_t period_us);
uint8_t subghz_device_si4463_ext_get_lqi(void);
int8_t subghz_device_si4463_ext_get_freq_est(void);

#ifdef __cplusplus
}
#endif

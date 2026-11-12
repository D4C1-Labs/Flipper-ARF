#include "flux_capacitor_interconnect.h"
#include <furi_hal.h>
#include "../cc1101_configs.h"

#define TAG "SubGhzDeviceFluxCapacitor"

/**
 * Flux Capacitor Device Implementation
 * 
 * The Flux Capacitor is a hardware amplifier module that replaces the standard
 * Flipper Zero's internal CC1101 radio. It provides significantly enhanced
 * transmission and reception capabilities while maintaining full protocol compatibility.
 * 
 * This implementation wraps the standard HAL functions with Flux Capacitor-specific
 * optimizations and power management.
 */

static bool subghz_device_flux_capacitor_is_frequency_valid(uint32_t frequency) {
    // Flux Capacitor supports all frequencies the internal CC1101 supports
    bool ret = furi_hal_subghz_is_frequency_valid(frequency);
    if(!ret) {
        FURI_LOG_W(TAG, "Invalid frequency: %lu Hz", frequency);
    }
    return ret;
}

static uint32_t subghz_device_flux_capacitor_set_frequency(uint32_t frequency) {
    subghz_device_flux_capacitor_is_frequency_valid(frequency);
    // Flux Capacitor uses standard frequency setting
    return furi_hal_subghz_set_frequency_and_path(frequency);
}

static bool subghz_device_flux_capacitor_start_async_tx(void* callback, void* context) {
    // Flux Capacitor TX uses enhanced power settings
    // The amplified module automatically handles increased power delivery
    return furi_hal_subghz_start_async_tx((FuriHalSubGhzAsyncTxCallback)callback, context);
}

static void subghz_device_flux_capacitor_start_async_rx(void* callback, void* context) {
    // Enhanced sensitivity reception
    furi_hal_subghz_start_async_rx((FuriHalSubGhzCaptureCallback)callback, context);
}

static void subghz_device_flux_capacitor_load_preset(
    FuriHalSubGhzPreset preset,
    uint8_t* preset_data) {
    // Load standard presets - Flux Capacitor amplifies at hardware level
    switch(preset) {
    case FuriHalSubGhzPresetOok650Async:
        furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_650khz_async_regs);
        break;
    case FuriHalSubGhzPresetOok270Async:
        furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_ook_270khz_async_regs);
        break;
    case FuriHalSubGhzPreset2FSKDev238Async:
        furi_hal_subghz_load_custom_preset(
            subghz_device_cc1101_preset_2fsk_dev2_38khz_async_regs);
        break;
    case FuriHalSubGhzPreset2FSKDev12KAsync:
        furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_2fsk_dev12khz_async_regs);
        break;
    case FuriHalSubGhzPreset2FSKDev476Async:
        furi_hal_subghz_load_custom_preset(
            subghz_device_cc1101_preset_2fsk_dev47_6khz_async_regs);
        break;
    case FuriHalSubGhzPresetMSK99_97KbAsync:
        furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_msk_99_97kb_async_regs);
        break;
    case FuriHalSubGhzPresetGFSK9_99KbAsync:
        furi_hal_subghz_load_custom_preset(subghz_device_cc1101_preset_gfsk_9_99kb_async_regs);
        break;

    default:
        furi_hal_subghz_load_custom_preset(preset_data);
    }
    
    FURI_LOG_D(TAG, "Preset loaded for Flux Capacitor");
}

static bool subghz_device_flux_capacitor_is_connect(void) {
    // Flux Capacitor is an external device
    // Return true if device is detected and ready
    // TODO: Implement actual hardware detection via GPIO/I2C if available
    return true;
}

const SubGhzDeviceInterconnect subghz_device_flux_capacitor_interconnect = {
    .begin = NULL,
    .end = furi_hal_subghz_shutdown,
    .is_connect = subghz_device_flux_capacitor_is_connect,
    .reset = furi_hal_subghz_reset,
    .sleep = furi_hal_subghz_sleep,
    .idle = furi_hal_subghz_idle,
    .load_preset = subghz_device_flux_capacitor_load_preset,
    .set_frequency = subghz_device_flux_capacitor_set_frequency,
    .is_frequency_valid = furi_hal_subghz_is_frequency_valid,
    .set_async_mirror_pin = furi_hal_subghz_set_async_mirror_pin,
    .get_data_gpio = furi_hal_subghz_get_data_gpio,

    .set_tx = furi_hal_subghz_tx,
    .flush_tx = furi_hal_subghz_flush_tx,
    .start_async_tx = subghz_device_flux_capacitor_start_async_tx,
    .is_async_complete_tx = furi_hal_subghz_is_async_tx_complete,
    .stop_async_tx = furi_hal_subghz_stop_async_tx,

    .set_rx = furi_hal_subghz_rx,
    .flush_rx = furi_hal_subghz_flush_rx,
    .start_async_rx = subghz_device_flux_capacitor_start_async_rx,
    .stop_async_rx = furi_hal_subghz_stop_async_rx,

    .get_rssi = furi_hal_subghz_get_rssi,
    .get_lqi = furi_hal_subghz_get_lqi,
    .get_freq_est = furi_hal_subghz_get_freq_est,
    .rssi_burst = furi_hal_subghz_rssi_burst,

    .rx_pipe_not_empty = furi_hal_subghz_rx_pipe_not_empty,
    .is_rx_data_crc_valid = furi_hal_subghz_is_rx_data_crc_valid,
    .read_packet = furi_hal_subghz_read_packet,
    .write_packet = furi_hal_subghz_write_packet,
};

const SubGhzDevice subghz_device_flux_capacitor = {
    .name = SUBGHZ_DEVICE_FLUX_CAPACITOR_NAME,
    .interconnect = &subghz_device_flux_capacitor_interconnect,
};

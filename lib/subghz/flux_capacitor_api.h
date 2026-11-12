/**
 * Flux Capacitor Support Module
 * 
 * This module provides an application-level interface for selecting and configuring
 * the Flux Capacitor RF amplifier module in Flipper-ARF.
 * 
 * The Flux Capacitor can be used as a drop-in replacement for the standard Flipper
 * Zero internal radio, providing up to 100mW transmission power instead of the
 * standard module's <1% efficiency output.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

// Device selection modes
typedef enum {
    FluxCapacitorModeInternal,   // Standard Flipper Zero internal CC1101
    FluxCapacitorModeExternal,   // External Flux Capacitor module
    FluxCapacitorModeAuto,       // Auto-detect (internal if Flux not present)
} FluxCapacitorMode;

// Power levels for Flux Capacitor
typedef enum {
    FluxCapacitorPowerLow,       // ~10 mW
    FluxCapacitorPowerMedium,    // ~50 mW
    FluxCapacitorPowerHigh,      // ~100 mW
    FluxCapacitorPowerVeryHigh,  // ~120 mW (if supported)
} FluxCapacitorPowerLevel;

// Flux Capacitor configuration
typedef struct {
    FluxCapacitorMode mode;
    FluxCapacitorPowerLevel power;
    bool enable_logging;
    uint32_t tx_timeout_ms;
} FluxCapacitorConfig;

/**
 * Initialize Flux Capacitor support
 * @return true on success
 */
bool flux_capacitor_init(void);

/**
 * Deinitialize Flux Capacitor support
 */
void flux_capacitor_deinit(void);

/**
 * Check if Flux Capacitor module is present and connected
 * @return true if Flux Capacitor is detected
 */
bool flux_capacitor_is_present(void);

/**
 * Get current Flux Capacitor mode
 * @return current mode
 */
FluxCapacitorMode flux_capacitor_get_mode(void);

/**
 * Set Flux Capacitor mode
 * @param mode desired mode
 * @return true on success
 */
bool flux_capacitor_set_mode(FluxCapacitorMode mode);

/**
 * Get current power level
 * @return current power level
 */
FluxCapacitorPowerLevel flux_capacitor_get_power(void);

/**
 * Set power level
 * @param level desired power level
 * @return true on success
 */
bool flux_capacitor_set_power(FluxCapacitorPowerLevel level);

/**
 * Get device name (for SubGhz device selection)
 * @return device name string
 */
const char* flux_capacitor_get_device_name(void);

/**
 * Get status string for UI
 * @param buf output buffer
 * @param buf_size buffer size
 * @return pointer to status string
 */
const char* flux_capacitor_get_status_string(char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

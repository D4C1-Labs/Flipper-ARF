/**
 * Flux Capacitor Support Module Implementation
 * 
 * Provides runtime management and configuration of the external Flux Capacitor
 * RF amplifier module for Flipper-ARF.
 */

#include "flux_capacitor_api.h"
#include "devices/types.h"
#include <furi.h>
#include <furi_hal.h>

#define TAG "FluxCapacitor"

// Flux Capacitor runtime configuration
static struct {
    bool initialized;
    bool present;
    FluxCapacitorMode current_mode;
    FluxCapacitorPowerLevel power_level;
} flux_capacitor_state = {
    .initialized = false,
    .present = false,
    .current_mode = FluxCapacitorModeAuto,
    .power_level = FluxCapacitorPowerMedium,
};

bool flux_capacitor_init(void) {
    if(flux_capacitor_state.initialized) {
        return true;
    }

    FURI_LOG_I(TAG, "Initializing Flux Capacitor support");

    // Check for Flux Capacitor presence
    // TODO: Implement actual hardware detection
    // For now, we assume it's present if in External or Auto mode
    flux_capacitor_state.present = true;
    flux_capacitor_state.initialized = true;

    FURI_LOG_I(TAG, "Flux Capacitor support initialized");
    return true;
}

void flux_capacitor_deinit(void) {
    flux_capacitor_state.initialized = false;
    FURI_LOG_I(TAG, "Flux Capacitor support deinitialized");
}

bool flux_capacitor_is_present(void) {
    // Check if Flux Capacitor hardware is detected
    // This could be done via GPIO detection, I2C, or other means
    return flux_capacitor_state.present;
}

FluxCapacitorMode flux_capacitor_get_mode(void) {
    return flux_capacitor_state.current_mode;
}

bool flux_capacitor_set_mode(FluxCapacitorMode mode) {
    flux_capacitor_state.current_mode = mode;
    
    const char* mode_str = "Unknown";
    switch(mode) {
    case FluxCapacitorModeInternal:
        mode_str = "Internal";
        break;
    case FluxCapacitorModeExternal:
        mode_str = "External (Flux Capacitor)";
        break;
    case FluxCapacitorModeAuto:
        mode_str = "Auto-detect";
        break;
    }
    
    FURI_LOG_I(TAG, "Radio mode changed to: %s", mode_str);
    return true;
}

FluxCapacitorPowerLevel flux_capacitor_get_power(void) {
    return flux_capacitor_state.power_level;
}

bool flux_capacitor_set_power(FluxCapacitorPowerLevel level) {
    flux_capacitor_state.power_level = level;
    
    const char* power_str = "Unknown";
    uint8_t power_dbm = 0;
    
    switch(level) {
    case FluxCapacitorPowerLow:
        power_str = "Low (10 mW)";
        power_dbm = 10;
        break;
    case FluxCapacitorPowerMedium:
        power_str = "Medium (50 mW)";
        power_dbm = 17;
        break;
    case FluxCapacitorPowerHigh:
        power_str = "High (100 mW)";
        power_dbm = 20;
        break;
    case FluxCapacitorPowerVeryHigh:
        power_str = "Very High (120 mW)";
        power_dbm = 21;
        break;
    }
    
    FURI_LOG_I(TAG, "TX power level changed to: %s (%d dBm)", power_str, power_dbm);
    return true;
}

const char* flux_capacitor_get_device_name(void) {
    switch(flux_capacitor_state.current_mode) {
    case FluxCapacitorModeInternal:
        return "CC1101 Internal";
    case FluxCapacitorModeExternal:
        return "Flux Capacitor";
    case FluxCapacitorModeAuto:
        // Return Flux Capacitor if present, otherwise internal
        if(flux_capacitor_is_present()) {
            return "Flux Capacitor";
        } else {
            return "CC1101 Internal";
        }
    default:
        return "Unknown";
    }
}

const char* flux_capacitor_get_status_string(char* buf, size_t buf_size) {
    const char* mode_str = "";
    const char* power_str = "";
    
    switch(flux_capacitor_state.current_mode) {
    case FluxCapacitorModeInternal:
        mode_str = "Internal";
        break;
    case FluxCapacitorModeExternal:
        mode_str = "Flux Capacitor";
        break;
    case FluxCapacitorModeAuto:
        mode_str = flux_capacitor_is_present() ? "Flux Capacitor (auto)" : "Internal (auto)";
        break;
    }
    
    switch(flux_capacitor_state.power_level) {
    case FluxCapacitorPowerLow:
        power_str = "10mW";
        break;
    case FluxCapacitorPowerMedium:
        power_str = "50mW";
        break;
    case FluxCapacitorPowerHigh:
        power_str = "100mW";
        break;
    case FluxCapacitorPowerVeryHigh:
        power_str = "120mW";
        break;
    }
    
    snprintf(buf, buf_size, "%s @ %s", mode_str, power_str);
    return buf;
}

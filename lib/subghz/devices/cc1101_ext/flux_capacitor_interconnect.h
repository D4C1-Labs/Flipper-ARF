#pragma once

#include "../types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUBGHZ_DEVICE_FLUX_CAPACITOR_NAME "Flux Capacitor"

// Flux Capacitor-specific power configuration
#define FLUX_CAPACITOR_TX_POWER_MAX 20  // 100 mW in dBm (~10x standard Flipper)
#define FLUX_CAPACITOR_TX_POWER_DEFAULT 17  // ~50 mW default
#define FLUX_CAPACITOR_RANGE_MULTIPLIER 10  // Approximate range improvement factor

/**
 * Flux Capacitor device interconnect
 * 
 * The Flux Capacitor is an amplified CC1101 radio module by Rabbit Labs that
 * provides up to 100 mW transmission/reception power, compared to the standard
 * Flipper Zero's <1% efficiency internal module.
 * 
 * Features:
 * - Up to 100 mW TX/RX power output
 * - Tuned antenna for 433MHz, 315MHz, and 868MHz bands
 * - Drop-in replacement for Flipper Zero's internal radio
 * - Enhanced range: up to 100m+ depending on antenna and environment
 * - Full protocol compatibility with all automotive and gate protocols
 */
extern const SubGhzDeviceInterconnect subghz_device_flux_capacitor_interconnect;
extern const SubGhzDevice subghz_device_flux_capacitor;

#ifdef __cplusplus
}
#endif

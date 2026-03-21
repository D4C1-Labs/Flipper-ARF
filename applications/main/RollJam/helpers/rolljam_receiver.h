#pragma once

#include "../rolljam.h"

/*
 * Internal CC1101 raw signal capture and transmission.
 *
 * Capture: uses narrow RX bandwidth so the offset jamming
 *          from the external CC1101 is filtered out.
 *
 * The captured raw data is stored as signed int16 values:
 *   positive = high-level duration (microseconds)
 *   negative = low-level duration (microseconds)
 *
 * This matches the Flipper .sub RAW format.
 */

void rolljam_capture_start(RollJamApp* app);
void rolljam_capture_stop(RollJamApp* app);

bool rolljam_signal_is_valid(RawSignal* signal);

void rolljam_signal_cleanup(RawSignal* signal);
void rolljam_transmit_signal(RollJamApp* app, RawSignal* signal);
void rolljam_save_signal(RollJamApp* app, RawSignal* signal);

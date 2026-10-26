#pragma once

#include <furi.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/subghz_setting.h>

typedef struct FreqAnalyzerWorker FreqAnalyzerWorker;

typedef void (
    *FreqAnalyzerWorkerPairCallback)(void* context, uint32_t frequency, float rssi, bool signal);

typedef struct {
    uint32_t frequency_coarse;
    float rssi_coarse;
    uint32_t frequency_fine;
    float rssi_fine;
} FrequencyRSSI;

/** Allocate the analyzer worker for the selected radio + frequency list.
 *
 * @param device        the (already begun) internal/external CC1101 device to scan on
 * @param setting       source of the frequency list to sweep (borrowed, not owned)
 * @param trigger_level initial RSSI trigger level (dBm)
 */
FreqAnalyzerWorker* freq_analyzer_worker_alloc(
    const SubGhzDevice* device,
    SubGhzSetting* setting,
    float trigger_level);

void freq_analyzer_worker_free(FreqAnalyzerWorker* instance);

void freq_analyzer_worker_set_pair_callback(
    FreqAnalyzerWorker* instance,
    FreqAnalyzerWorkerPairCallback callback,
    void* context);

void freq_analyzer_worker_start(FreqAnalyzerWorker* instance);
void freq_analyzer_worker_stop(FreqAnalyzerWorker* instance);
bool freq_analyzer_worker_is_running(FreqAnalyzerWorker* instance);

void freq_analyzer_worker_set_trigger_level(FreqAnalyzerWorker* instance, float value);
float freq_analyzer_worker_get_trigger_level(FreqAnalyzerWorker* instance);

/** Snap an arbitrary frequency to the nearest entry in the scan list. */
uint32_t freq_analyzer_worker_get_nearest_frequency(FreqAnalyzerWorker* instance, uint32_t input);

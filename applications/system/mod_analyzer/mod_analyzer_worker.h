#pragma once

#include "mod_analyzer_stats.h"

#include <furi.h>
#include <lib/subghz/devices/devices.h>

typedef struct ModAnalyzerWorker ModAnalyzerWorker;

typedef struct {
    ModResult result;
    uint8_t confidence_pct;

    float floor_dbm;
    float last_p90_dbm;
    float last_carrier_db;
    float last_floor_db;
    uint8_t last_duty_pct;
    uint16_t last_transitions;
    uint32_t last_on_run_max_us;

    uint32_t achieved_period_us;
    uint8_t atten_idx;
    int8_t atten_db;

    uint16_t windows_total;
    uint16_t windows_valid;
    uint16_t windows_decisive;
    uint16_t disagreements;
} ModAnalyzerReport;

typedef void (*ModAnalyzerWorkerCallback)(void* context, const ModAnalyzerReport* report);

ModAnalyzerWorker* mod_analyzer_worker_alloc(const SubGhzDevice* device);
void mod_analyzer_worker_free(ModAnalyzerWorker* instance);

void mod_analyzer_worker_set_callback(
    ModAnalyzerWorker* instance,
    ModAnalyzerWorkerCallback callback,
    void* context);

void mod_analyzer_worker_set_frequency(ModAnalyzerWorker* instance, uint32_t frequency);

void mod_analyzer_worker_start(ModAnalyzerWorker* instance);
void mod_analyzer_worker_stop(ModAnalyzerWorker* instance);

bool mod_analyzer_worker_is_running(ModAnalyzerWorker* instance);

void mod_analyzer_worker_restart_session(ModAnalyzerWorker* instance);

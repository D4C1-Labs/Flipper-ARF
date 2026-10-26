#include "mod_analyzer_worker.h"

#include <lib/drivers/cc1101_regs.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <applications/drivers/subghz/si4463_ext/si4463_ext_interconnect.h>

#include <furi.h>
#include <string.h>

#define TAG "ModAnalyzerWorker"

#define MOD_PRESET_MAX 96

#define MOD_ATTEN_STEPS 8
static const uint8_t mod_atten_agcctrl2[MOD_ATTEN_STEPS] =
    {0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37, 0x3F};
static const int8_t mod_atten_db[MOD_ATTEN_STEPS] = {0, -3, -6, -7, -9, -12, -15, -17};

#define MOD_SETTLE_MS       5
#define MOD_SESSION_IDLE    40
#define MOD_SATURATION_RUNS 3

struct ModAnalyzerWorker {
    FuriThread* thread;
    volatile bool running;

    const SubGhzDevice* device;
    volatile uint32_t frequency;
    volatile bool restart_requested;

    uint8_t preset[MOD_PRESET_MAX];
    size_t preset_len;
    uint8_t atten_idx;
    bool cc1101_attenuation;

    uint8_t* samples;
    uint16_t* hist;

    ModSession session;
    float floor_dbm;
    bool floor_valid;
    uint32_t achieved_period_us;

    ModAnalyzerWorkerCallback callback;
    void* context;
};

static bool mod_worker_preset_copy(ModAnalyzerWorker* w) {
    const uint8_t* src = subghz_device_cc1101_preset_2fsk_dev47_6khz_async_regs;
    size_t i = 0;
    while(i + 1 < MOD_PRESET_MAX && !(src[i] == 0 && src[i + 1] == 0))
        i += 2;
    if(i + 2 + 8 > MOD_PRESET_MAX) return false;
    const size_t len = i + 2 + 8;
    memcpy(w->preset, src, len);
    w->preset_len = len;
    return true;
}

static void mod_worker_preset_set_atten(ModAnalyzerWorker* w, uint8_t idx) {
    if(!w->cc1101_attenuation) {
        w->atten_idx = 0;
        return;
    }
    if(idx >= MOD_ATTEN_STEPS) idx = MOD_ATTEN_STEPS - 1;
    w->atten_idx = idx;
    for(size_t i = 0; i + 1 < w->preset_len; i += 2) {
        if(w->preset[i] == 0 && w->preset[i + 1] == 0) break;
        if(w->preset[i] == CC1101_AGCCTRL2) {
            w->preset[i + 1] = mod_atten_agcctrl2[idx];
            return;
        }
    }
}

static void mod_worker_retune(ModAnalyzerWorker* w) {
    subghz_devices_idle(w->device);
    subghz_devices_load_preset(w->device, FuriHalSubGhzPresetCustom, w->preset);
    subghz_devices_set_frequency(w->device, w->frequency);
    subghz_devices_flush_rx(w->device);
    subghz_devices_set_rx(w->device);
    furi_delay_ms(MOD_SETTLE_MS);
}

static int32_t mod_analyzer_worker_thread(void* context) {
    ModAnalyzerWorker* w = context;

    w->samples = malloc(MOD_STATS_SAMPLES);
    w->hist = malloc(256 * sizeof(uint16_t));

    mod_worker_preset_set_atten(w, 0);
    mod_worker_retune(w);

    subghz_devices_rssi_burst(w->device, w->samples, MOD_STATS_SAMPLES, MOD_STATS_PERIOD_US);

    mod_stats_session_reset(&w->session);
    w->floor_valid = false;
    w->achieved_period_us = MOD_STATS_PERIOD_US;

    uint32_t last_frequency = w->frequency;
    uint8_t saturated_run = 0;
    uint16_t idle_rounds = 0;

    while(w->running) {
        if(w->restart_requested) {
            w->restart_requested = false;
            mod_stats_session_reset(&w->session);
            saturated_run = 0;
            idle_rounds = 0;
        }
        if(w->frequency != last_frequency) {
            last_frequency = w->frequency;
            mod_worker_retune(w);
            subghz_devices_rssi_burst(
                w->device, w->samples, MOD_STATS_SAMPLES, MOD_STATS_PERIOD_US);
            mod_stats_session_reset(&w->session);
            w->floor_valid = false;
        }

        const uint32_t span_us = subghz_devices_rssi_burst(
            w->device, w->samples, MOD_STATS_SAMPLES, MOD_STATS_PERIOD_US);
        if(span_us == 0) break;

        w->achieved_period_us = span_us / MOD_STATS_SAMPLES;
        if(w->achieved_period_us == 0) w->achieved_period_us = MOD_STATS_PERIOD_US;

        if(!w->floor_valid) {
            w->floor_dbm = mod_stats_raw_to_dbm(w->samples[0]);
            w->floor_valid = true;
        }
        ModWindowStats st;
        mod_stats_window(
            w->samples, MOD_STATS_SAMPLES, w->achieved_period_us, w->floor_dbm, w->hist, &st);

        const float p50_dbm = mod_stats_raw_to_dbm((uint8_t)st.p50);
        const bool valid = mod_stats_window_is_valid(&st, w->floor_dbm);
        if(!valid) {
            w->floor_dbm = w->floor_dbm * 0.95f + p50_dbm * 0.05f;
        }

        mod_stats_session_add(&w->session, &st, w->floor_dbm);

        if(w->cc1101_attenuation && valid && mod_stats_suspect_saturation(&st)) {
            saturated_run++;
            if(saturated_run >= MOD_SATURATION_RUNS && w->atten_idx + 1 < MOD_ATTEN_STEPS) {
                mod_worker_preset_set_atten(w, w->atten_idx + 1);
                mod_worker_retune(w);
                subghz_devices_rssi_burst(
                    w->device, w->samples, MOD_STATS_SAMPLES, MOD_STATS_PERIOD_US);
                mod_stats_session_reset(&w->session);
                saturated_run = 0;
            }
        } else if(valid) {
            saturated_run = 0;
        }

        uint8_t confidence = 0;
        const ModResult result = mod_stats_session_verdict(&w->session, &confidence);

        if(result == ModResultAm || result == ModResultFm) {
            if(!valid && ++idle_rounds >= MOD_SESSION_IDLE) {
                mod_stats_session_reset(&w->session);
                idle_rounds = 0;
            } else if(valid) {
                idle_rounds = 0;
            }
        } else {
            idle_rounds = 0;
        }

        if(w->callback) {
            ModAnalyzerReport r = {
                .result = result,
                .confidence_pct = confidence,
                .floor_dbm = w->floor_dbm,
                .last_p90_dbm = st.p90_dbm,
                .last_carrier_db = st.carrier_db,
                .last_floor_db = st.floor_db,
                .last_duty_pct = st.duty_pct,
                .last_transitions = st.n_transitions,
                .last_on_run_max_us = st.on_run_max_us,
                .achieved_period_us = w->achieved_period_us,
                .atten_idx = w->atten_idx,
                .atten_db = mod_atten_db[w->atten_idx],
                .windows_total = w->session.windows_total,
                .windows_valid = w->session.windows_valid,
                .windows_decisive = w->session.windows_decisive,
                .disagreements = w->session.disagreements,
            };
            w->callback(w->context, &r);
        }

        furi_delay_ms(4);
    }

    subghz_devices_idle(w->device);
    subghz_devices_sleep(w->device);

    free(w->hist);
    w->hist = NULL;
    free(w->samples);
    w->samples = NULL;
    return 0;
}

ModAnalyzerWorker* mod_analyzer_worker_alloc(const SubGhzDevice* device) {
    furi_assert(device);
    ModAnalyzerWorker* w = malloc(sizeof(ModAnalyzerWorker));
    memset(w, 0, sizeof(*w));

    w->device = device;
    w->cc1101_attenuation =
        strcmp(subghz_devices_get_name(device), SUBGHZ_DEVICE_SI4463_EXT_NAME) != 0;
    w->frequency = 433920000;

    if(!mod_worker_preset_copy(w)) {
        FURI_LOG_E(TAG, "preset blob larger than %d bytes", MOD_PRESET_MAX);
    }

    w->thread = furi_thread_alloc_ex("ModAnalyzerWorker", 2048, mod_analyzer_worker_thread, w);
    furi_thread_set_priority(w->thread, FuriThreadPriorityHigh);
    return w;
}

void mod_analyzer_worker_free(ModAnalyzerWorker* w) {
    furi_assert(w);
    furi_thread_free(w->thread);
    free(w);
}

void mod_analyzer_worker_set_callback(
    ModAnalyzerWorker* w,
    ModAnalyzerWorkerCallback callback,
    void* context) {
    furi_assert(w);
    w->callback = callback;
    w->context = context;
}

void mod_analyzer_worker_set_frequency(ModAnalyzerWorker* w, uint32_t frequency) {
    furi_assert(w);
    w->frequency = frequency;
}

void mod_analyzer_worker_start(ModAnalyzerWorker* w) {
    furi_assert(w);
    furi_assert(!w->running);
    w->running = true;
    furi_thread_start(w->thread);
}

void mod_analyzer_worker_stop(ModAnalyzerWorker* w) {
    furi_assert(w);
    furi_assert(w->running);
    w->running = false;
    furi_thread_join(w->thread);
}

bool mod_analyzer_worker_is_running(ModAnalyzerWorker* w) {
    furi_assert(w);
    return w->running;
}

void mod_analyzer_worker_restart_session(ModAnalyzerWorker* w) {
    furi_assert(w);
    w->restart_requested = true;
}

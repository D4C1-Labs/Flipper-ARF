#include "freq_analyzer_worker.h"

#include <lib/drivers/cc1101_regs.h>
#include <lib/subghz/devices/devices.h>

#include <furi.h>
#include <float_tools.h>
#include <math.h>

#define TAG "FreqAnalyzerWorker"

// Full custom presets (register list + PA table) so the analyzer runs on either the
// internal OR external CC1101 through the subghz_devices layer.
#define FA_PRESET_BODY(mdmcfg4)                                                                  \
    CC1101_IOCFG0, CC1101IocfgHW, /* GD0 high-Z: RSSI only, no data out */                       \
        CC1101_FIFOTHR, 0x07, CC1101_PKTCTRL0, 0x32, CC1101_FSCTRL1, 0x06, CC1101_MDMCFG0, 0x00, \
        CC1101_MDMCFG1, 0x00, CC1101_MDMCFG2, 0x30, CC1101_MDMCFG3,                              \
        0x7F, /* analyzer symbol rate */                                                         \
        CC1101_MDMCFG4, (mdmcfg4), CC1101_MCSM0, 0x18, /* autocalibrate on idle->rx */           \
        CC1101_FOCCFG, 0x18, CC1101_AGCCTRL0, 0x30, /* analyzer AGC */                           \
        CC1101_AGCCTRL1, 0x08, CC1101_AGCCTRL2, 0x07, CC1101_WORCTRL, 0xFB, CC1101_FREND0, 0x11, \
        CC1101_FREND1, 0xB6, 0, 0, /* end */                                                     \
        0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 /* PA table */

static uint8_t fa_preset_coarse[] = {FA_PRESET_BODY(0x17)}; // 650kHz BW
static uint8_t fa_preset_fine[] = {FA_PRESET_BODY(0xF7)}; // 58kHz BW

struct FreqAnalyzerWorker {
    FuriThread* thread;

    volatile bool worker_running;
    uint8_t sample_hold_counter;
    FrequencyRSSI frequency_rssi_buf;
    SubGhzSetting* setting;
    const SubGhzDevice* device;

    float filVal;
    float trigger_level;

    FreqAnalyzerWorkerPairCallback pair_callback;
    void* context;
};

// Tune the active radio to `frequency`, enter RX and return the settled RSSI (dBm).
// `actual` (optional) receives the frequency the synthesizer actually locked to.
static float freq_analyzer_worker_read_rssi(
    const SubGhzDevice* device,
    uint32_t frequency,
    uint32_t* actual) {
    subghz_devices_idle(device);
    uint32_t set = subghz_devices_set_frequency(device, frequency);
    if(actual) *actual = set;
    subghz_devices_flush_rx(device);
    subghz_devices_set_rx(device);
    furi_delay_ms(3);
    return subghz_devices_get_rssi(device);
}

// running average with adaptive coefficient
static uint32_t
    freq_analyzer_worker_expRunningAverageAdaptive(FreqAnalyzerWorker* instance, uint32_t newVal) {
    float k;
    float newValFloat = newVal;
    // the sharpness of the filter depends on the absolute value of the difference
    if(fabsf(newValFloat - instance->filVal) > 500000.f)
        k = 0.9;
    else
        k = 0.03;

    instance->filVal += (newValFloat - instance->filVal) * k;
    return (uint32_t)instance->filVal;
}

static int32_t freq_analyzer_worker_thread(void* context) {
    FreqAnalyzerWorker* instance = context;

    FrequencyRSSI frequency_rssi = {
        .frequency_coarse = 0, .rssi_coarse = 0, .frequency_fine = 0, .rssi_fine = 0};
    float rssi = 0;
    uint32_t frequency = 0;
    float rssi_temp = 0;
    uint32_t frequency_temp = 0;

    const SubGhzDevice* device = instance->device;

    //Start the selected radio (internal or external CC1101)
    subghz_devices_reset(device);
    subghz_devices_idle(device);

    while(instance->worker_running) {
        furi_delay_ms(10);

        size_t rssi_avg_samples = 0;

        frequency_rssi.rssi_coarse = -127.0f;
        frequency_rssi.rssi_fine = -127.0f;
        subghz_devices_idle(device);
        subghz_devices_load_preset(device, FuriHalSubGhzPresetCustom, fa_preset_coarse);

        // First stage: coarse scan
        for(size_t i = 0; i < subghz_setting_get_frequency_count(instance->setting); i++) {
            uint32_t current_frequency = subghz_setting_get_frequency(instance->setting, i);
            if(subghz_devices_is_frequency_valid(device, current_frequency)) {
                rssi = freq_analyzer_worker_read_rssi(device, current_frequency, &frequency);

                rssi_avg_samples++;

                if(frequency_rssi.rssi_coarse < rssi) {
                    frequency_rssi.rssi_coarse = rssi;
                    frequency_rssi.frequency_coarse = frequency;
                }
            }
        }

        // Second stage: fine scan
        if(rssi_avg_samples > 0 && frequency_rssi.rssi_coarse > instance->trigger_level) {
            subghz_devices_idle(device);
            subghz_devices_load_preset(device, FuriHalSubGhzPresetCustom, fa_preset_fine);
            //for example -0.3 ... 433.92 ... +0.3 step 20KHz
            for(uint32_t i = frequency_rssi.frequency_coarse - 300000;
                i < frequency_rssi.frequency_coarse + 300000;
                i += 20000) {
                if(subghz_devices_is_frequency_valid(device, i)) {
                    rssi = freq_analyzer_worker_read_rssi(device, i, &frequency);

                    if(frequency_rssi.rssi_fine < rssi) {
                        frequency_rssi.rssi_fine = rssi;
                        frequency_rssi.frequency_fine = frequency;
                    }
                }
            }
        }

        // Deliver results fine
        if(frequency_rssi.rssi_fine > instance->trigger_level) {
            instance->sample_hold_counter = 20;
            rssi_temp = frequency_rssi.rssi_fine;
            frequency_temp = frequency_rssi.frequency_fine;

            if(!float_is_equal(instance->filVal, 0.f)) {
                frequency_rssi.frequency_fine = freq_analyzer_worker_expRunningAverageAdaptive(
                    instance, frequency_rssi.frequency_fine);
            }
            // Deliver callback
            if(instance->pair_callback) {
                instance->pair_callback(
                    instance->context,
                    frequency_rssi.frequency_fine,
                    frequency_rssi.rssi_fine,
                    true);
            }
        } else if( // Deliver results coarse
            (frequency_rssi.rssi_coarse > instance->trigger_level) &&
            (instance->sample_hold_counter < 10)) {
            instance->sample_hold_counter = 20;
            rssi_temp = frequency_rssi.rssi_coarse;
            frequency_temp = frequency_rssi.frequency_coarse;
            if(!float_is_equal(instance->filVal, 0.f)) {
                frequency_rssi.frequency_coarse = freq_analyzer_worker_expRunningAverageAdaptive(
                    instance, frequency_rssi.frequency_coarse);
            }
            // Deliver callback
            if(instance->pair_callback) {
                instance->pair_callback(
                    instance->context,
                    frequency_rssi.frequency_coarse,
                    frequency_rssi.rssi_coarse,
                    true);
            }
        } else {
            if(instance->sample_hold_counter > 0) {
                instance->sample_hold_counter--;
                if(instance->sample_hold_counter == 18) {
                    if(instance->pair_callback) {
                        instance->pair_callback(
                            instance->context, frequency_temp, rssi_temp, false);
                    }
                }
            } else {
                instance->filVal = 0;
                if(instance->pair_callback)
                    instance->pair_callback(instance->context, 0, 0, false);
            }
        }
    }

    //Stop the radio
    subghz_devices_idle(device);
    subghz_devices_sleep(device);

    return 0;
}

FreqAnalyzerWorker* freq_analyzer_worker_alloc(
    const SubGhzDevice* device,
    SubGhzSetting* setting,
    float trigger_level) {
    furi_assert(device);
    furi_assert(setting);
    FreqAnalyzerWorker* instance = malloc(sizeof(FreqAnalyzerWorker));

    instance->thread =
        furi_thread_alloc_ex("FreqAnalyzerWorker", 2048, freq_analyzer_worker_thread, instance);
    instance->setting = setting;
    instance->device = device;
    instance->trigger_level = trigger_level;
    instance->worker_running = false;
    instance->sample_hold_counter = 0;
    instance->filVal = 0;
    instance->pair_callback = NULL;
    instance->context = NULL;
    return instance;
}

void freq_analyzer_worker_free(FreqAnalyzerWorker* instance) {
    furi_assert(instance);

    furi_thread_free(instance->thread);
    free(instance);
}

void freq_analyzer_worker_set_pair_callback(
    FreqAnalyzerWorker* instance,
    FreqAnalyzerWorkerPairCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(context);
    instance->pair_callback = callback;
    instance->context = context;
}

void freq_analyzer_worker_start(FreqAnalyzerWorker* instance) {
    furi_assert(instance);
    furi_assert(!instance->worker_running);

    instance->worker_running = true;

    furi_thread_start(instance->thread);
}

void freq_analyzer_worker_stop(FreqAnalyzerWorker* instance) {
    furi_assert(instance);
    furi_assert(instance->worker_running);

    instance->worker_running = false;

    furi_thread_join(instance->thread);
}

bool freq_analyzer_worker_is_running(FreqAnalyzerWorker* instance) {
    furi_assert(instance);
    return instance->worker_running;
}

void freq_analyzer_worker_set_trigger_level(FreqAnalyzerWorker* instance, float value) {
    instance->trigger_level = value;
}

float freq_analyzer_worker_get_trigger_level(FreqAnalyzerWorker* instance) {
    return instance->trigger_level;
}

uint32_t freq_analyzer_worker_get_nearest_frequency(FreqAnalyzerWorker* instance, uint32_t input) {
    uint32_t prev_freq = 0;
    uint32_t result = 0;
    uint32_t current;

    for(size_t i = 0; i < subghz_setting_get_frequency_count(instance->setting); i++) {
        current = subghz_setting_get_frequency(instance->setting, i);
        if(current == 0) {
            continue;
        }
        if(current == input) {
            result = current;
            break;
        }
        if(current > input && prev_freq < input) {
            if(current - input < input - prev_freq) {
                result = current;
            } else {
                result = prev_freq;
            }
            break;
        }
        prev_freq = current;
    }

    return result;
}

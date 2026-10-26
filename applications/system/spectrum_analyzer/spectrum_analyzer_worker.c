#include "spectrum_analyzer.h"
#include "spectrum_analyzer_worker.h"

#include <furi_hal.h>
#include <furi.h>

#include "helpers/radio_device_loader.h"

#include <lib/drivers/cc1101_regs.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

struct SpectrumAnalyzerWorker {
    FuriThread* thread;
    volatile bool should_work;

    SpectrumAnalyzerWorkerCallback callback;
    void* callback_context;

    const SubGhzDevice* radio_device; // radio A (internal in dual mode, or the single radio)
    const SubGhzDevice* radio_device_ext; // radio B (external), only in dual mode
    bool dual; // both radios present: sweep is split across them (~2x faster)

    uint32_t channel0_frequency;
    uint32_t spacing;
    uint8_t width;
    uint8_t modulation;
    float max_rssi;
    uint8_t max_rssi_dec;
    uint8_t max_rssi_channel;

    uint8_t channel_ss[NUM_CHANNELS];
};

static int32_t spectrum_analyzer_worker_thread(void* context) {
    furi_assert(context);
    SpectrumAnalyzerWorker* instance = context;

    FURI_LOG_D("SpectrumWorker", "spectrum_analyzer_worker_thread: Start");

    // Start CC1101 (both radios in dual mode)
    subghz_devices_reset(instance->radio_device);
    subghz_devices_load_preset(instance->radio_device, FuriHalSubGhzPresetOok650Async, NULL);
    subghz_devices_set_frequency(instance->radio_device, 433920000);
    subghz_devices_flush_rx(instance->radio_device);
    subghz_devices_set_rx(instance->radio_device);
    if(instance->dual) {
        subghz_devices_reset(instance->radio_device_ext);
        subghz_devices_load_preset(
            instance->radio_device_ext, FuriHalSubGhzPresetOok650Async, NULL);
        subghz_devices_set_frequency(instance->radio_device_ext, 433920000);
        subghz_devices_flush_rx(instance->radio_device_ext);
        subghz_devices_set_rx(instance->radio_device_ext);
    }

    // Default modulation
    const uint8_t default_modulation[] = {

        /* Frequency Synthesizer Control */
        CC1101_FSCTRL0,
        0x00,
        CC1101_FSCTRL1,
        0x12, // IF = (26*10^6) / (2^10) * 0x12 = 304687.5 Hz

        // Modem Configuration
        // CC1101_MDMCFG0,
        // 0x00, // Channel spacing is 25kHz
        // CC1101_MDMCFG1,
        // 0x00, // Channel spacing is 25kHz
        // CC1101_MDMCFG2,
        // 0x30, // Format ASK/OOK, No preamble/sync
        // CC1101_MDMCFG3,
        // 0x32, // Data rate is 121.399 kBaud
        CC1101_MDMCFG4,
        0x6C, // Rx BW filter is 270.83 kHz

        /* Frequency Offset Compensation Configuration */
        // CC1101_FOCCFG,
        // 0x18, // no frequency offset compensation, POST_K same as PRE_K, PRE_K is 4K, GATE is off

        /* Automatic Gain Control */
        // CC1101_AGCCTRL0,
        // 0x91, // 10 - Medium hysteresis, medium asymmetric dead zone, medium gain ; 01 - 16 samples agc; 00 - Normal AGC, 01 - 8dB boundary
        // CC1101_AGCCTRL1,
        // 0x0, // 0; 0 - LNA 2 gain is decreased to minimum before decreasing LNA gain; 00 - Relative carrier sense threshold disabled; 0000 - RSSI to MAIN_TARGET
        CC1101_AGCCTRL2,
        0xC0, // 03 - The 3 highest DVGA gain settings can not be used; 000 - MAX LNA+LNA2; 000 - MAIN_TARGET 24 dB

        /* Frontend configuration */
        // CC1101_FREND0,
        // 0x11, // Adjusts current TX LO buffer + high is PATABLE[1]
        // CC1101_FREND1,
        // 0xB6, //

        CC1101_TEST2,
        0x88,
        CC1101_TEST1,
        0x31,
        CC1101_TEST0,
        0x09,

        /* End  */
        0,
        0,

        // ook_async_patable
        0x00,
        0xC0, // 12dBm 0xC0, 10dBm 0xC5, 7dBm 0xCD, 5dBm 0x86, 0dBm 0x50, -6dBm 0x37, -10dBm 0x26, -15dBm 0x1D, -20dBm 0x17, -30dBm 0x03
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00};

    // Narrow modulation
    const uint8_t narrow_modulation[] = {

        /* Frequency Synthesizer Control */
        CC1101_FSCTRL0,
        0x00,
        CC1101_FSCTRL1,
        0x00, // IF = (26*10^6) / (2^10) * 0x00 = 0 Hz

        // Modem Configuration
        // CC1101_MDMCFG0,
        // 0x00, // Channel spacing is 25kHz
        // CC1101_MDMCFG1,
        // 0x00, // Channel spacing is 25kHz
        // CC1101_MDMCFG2,
        // 0x30, // Format ASK/OOK, No preamble/sync
        // CC1101_MDMCFG3,
        // 0x32, // Data rate is 121.399 kBaud
        CC1101_MDMCFG4,
        0xFC, // Rx BW filter is 58.04 kHz

        /* Frequency Offset Compensation Configuration */
        // CC1101_FOCCFG,
        // 0x18, // no frequency offset compensation, POST_K same as PRE_K, PRE_K is 4K, GATE is off

        /* Automatic Gain Control */
        CC1101_AGCCTRL0,
        0x30, // 00 - NO hysteresis, symmetric dead zone, high gain ; 11 - 32 samples agc; 00 - Normal AGC, 00 - 8dB boundary
        CC1101_AGCCTRL1,
        0x0, // 0; 0 - LNA 2 gain is decreased to minimum before decreasing LNA gain; 00 - Relative carrier sense threshold disabled; 0000 - RSSI to MAIN_TARGET
        CC1101_AGCCTRL2,
        0x84, // 02 - The 2 highest DVGA gain settings can not be used; 000 - MAX LNA+LNA2; 100 - MAIN_TARGET 36 dB

        /* Frontend configuration */
        // CC1101_FREND0,
        // 0x11, // Adjusts current TX LO buffer + high is PATABLE[1]
        // CC1101_FREND1,
        // 0xB6, //

        CC1101_TEST2,
        0x88,
        CC1101_TEST1,
        0x31,
        CC1101_TEST0,
        0x09,

        /* End  */
        0,
        0,

        // ook_async_patable
        0x00,
        0xC0, // 12dBm 0xC0, 10dBm 0xC5, 7dBm 0xCD, 5dBm 0x86, 0dBm 0x50, -6dBm 0x37, -10dBm 0x26, -15dBm 0x1D, -20dBm 0x17, -30dBm 0x03
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00};

    const uint8_t* modulations[] = {default_modulation, narrow_modulation};

    while(instance->should_work) {
        furi_delay_ms(50);

        // FURI_LOG_T("SpectrumWorker", "spectrum_analyzer_worker_thread: Worker Loop");
        subghz_devices_idle(instance->radio_device);
        subghz_devices_load_preset(
            instance->radio_device,
            FuriHalSubGhzPresetCustom,
            (uint8_t*)modulations[instance->modulation]);
        if(instance->dual) {
            subghz_devices_idle(instance->radio_device_ext);
            subghz_devices_load_preset(
                instance->radio_device_ext,
                FuriHalSubGhzPresetCustom,
                (uint8_t*)modulations[instance->modulation]);
        }

        instance->max_rssi_dec = 0;

        if(instance->dual) {
            // Dual radio: internal scans the lower half of channels, external the
            // upper half, sharing a single settle delay per pair -> ~2x faster refresh.
            uint8_t half = NUM_CHANNELS / 2;
            for(uint8_t i = 0; i < half && instance->should_work; i++) {
                uint8_t cha = i;
                uint8_t chb = half + i;
                uint32_t fa = instance->channel0_frequency + (cha * instance->spacing);
                uint32_t fb = instance->channel0_frequency + (chb * instance->spacing);

                if(subghz_devices_is_frequency_valid(instance->radio_device, fa))
                    subghz_devices_set_frequency(instance->radio_device, fa);
                if(subghz_devices_is_frequency_valid(instance->radio_device_ext, fb))
                    subghz_devices_set_frequency(instance->radio_device_ext, fb);

                subghz_devices_set_rx(instance->radio_device);
                subghz_devices_set_rx(instance->radio_device_ext);
                furi_delay_ms(3); // both radios settle in parallel

                instance->channel_ss[cha] =
                    (subghz_devices_get_rssi(instance->radio_device) + 138) * 2;
                instance->channel_ss[chb] =
                    (subghz_devices_get_rssi(instance->radio_device_ext) + 138) * 2;

                if(instance->channel_ss[cha] > instance->max_rssi_dec) {
                    instance->max_rssi_dec = instance->channel_ss[cha];
                    instance->max_rssi = (instance->channel_ss[cha] / 2) - 138;
                    instance->max_rssi_channel = cha;
                }
                if(instance->channel_ss[chb] > instance->max_rssi_dec) {
                    instance->max_rssi_dec = instance->channel_ss[chb];
                    instance->max_rssi = (instance->channel_ss[chb] / 2) - 138;
                    instance->max_rssi_channel = chb;
                }

                subghz_devices_idle(instance->radio_device);
                subghz_devices_idle(instance->radio_device_ext);
            }
        } else {
            // Single radio: visit each channel non-consecutively
            for(uint8_t ch_offset = 0, chunk = 0; ch_offset < CHUNK_SIZE;
                ++chunk >= NUM_CHUNKS && ++ch_offset && (chunk = 0)) {
                uint8_t ch = chunk * CHUNK_SIZE + ch_offset;

                if(subghz_devices_is_frequency_valid(
                       instance->radio_device,
                       instance->channel0_frequency + (ch * instance->spacing)))
                    subghz_devices_set_frequency(
                        instance->radio_device,
                        instance->channel0_frequency + (ch * instance->spacing));

                subghz_devices_set_rx(instance->radio_device);
                furi_delay_ms(3);

                //         dec      dBm
                //max_ss = 127 ->  -10.5
                //max_ss = 0   ->  -74.0
                //max_ss = 255 ->  -74.5
                //max_ss = 128 -> -138.0
                instance->channel_ss[ch] =
                    (subghz_devices_get_rssi(instance->radio_device) + 138) * 2;

                if(instance->channel_ss[ch] > instance->max_rssi_dec) {
                    instance->max_rssi_dec = instance->channel_ss[ch];
                    instance->max_rssi = (instance->channel_ss[ch] / 2) - 138;
                    instance->max_rssi_channel = ch;
                }

                subghz_devices_idle(instance->radio_device);
            }
        }

        // FURI_LOG_T("SpectrumWorker", "channel_ss[0]: %u", instance->channel_ss[0]);

        // Report results back to main thread
        if(instance->callback) {
            instance->callback(
                (void*)&(instance->channel_ss),
                instance->max_rssi,
                instance->max_rssi_dec,
                instance->max_rssi_channel,
                instance->callback_context);
        }
    }

    subghz_devices_idle(instance->radio_device);
    if(instance->dual && instance->radio_device_ext) {
        subghz_devices_idle(instance->radio_device_ext);
    }

    return 0;
}

SpectrumAnalyzerWorker* spectrum_analyzer_worker_alloc() {
    FURI_LOG_D("Spectrum", "spectrum_analyzer_worker_alloc: Start");

    SpectrumAnalyzerWorker* instance = malloc(sizeof(SpectrumAnalyzerWorker));

    instance->should_work = false;
    instance->channel0_frequency = DEFAULT_FREQ * 1000; // DEFAULT_FREQ is in kHz
    instance->spacing = WIDE_SPACING;
    instance->width = WIDE;
    instance->modulation = DEFAULT_MODULATION;
    instance->max_rssi = 0;
    instance->max_rssi_dec = 0;
    instance->max_rssi_channel = 0;
    memset(instance->channel_ss, 0, sizeof(instance->channel_ss));

    instance->thread = furi_thread_alloc();
    furi_thread_set_name(instance->thread, "SpectrumWorker");
    furi_thread_set_stack_size(instance->thread, 2048);
    furi_thread_set_context(instance->thread, instance);
    furi_thread_set_callback(instance->thread, spectrum_analyzer_worker_thread);

    subghz_devices_init();

    // If the external CC1101 is present, run BOTH radios: internal = radio A,
    // external = radio B. The sweep is then split across them (~2x faster). If no
    // external module, fall back to a single radio (internal).
    // radio_device_loader_set() powers-on + begins the external module when present,
    // and returns the internal device otherwise.
    const SubGhzDevice* int_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    const SubGhzDevice* preferred =
        radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);
    instance->radio_device = int_device;
    instance->radio_device_ext = NULL;
    instance->dual = false;
    if(preferred != int_device) {
        instance->radio_device_ext = preferred; // external, already powered + begun
        instance->dual = true;
    }

    FURI_LOG_D("Spectrum", "spectrum_analyzer_worker_alloc: End (dual=%u)", instance->dual);

    return instance;
}

bool spectrum_analyzer_worker_is_dual(SpectrumAnalyzerWorker* instance) {
    furi_assert(instance);
    return instance->dual;
}

void spectrum_analyzer_worker_free(SpectrumAnalyzerWorker* instance) {
    FURI_LOG_D("Spectrum", "spectrum_analyzer_worker_free");
    furi_assert(instance);
    furi_thread_free(instance->thread);

    subghz_devices_sleep(instance->radio_device);
    if(instance->dual && instance->radio_device_ext) {
        // Powers off OTG + ends the external module. The internal radio needs neither.
        subghz_devices_sleep(instance->radio_device_ext);
        radio_device_loader_end(instance->radio_device_ext);
    }

    subghz_devices_deinit();

    free(instance);
}

void spectrum_analyzer_worker_set_callback(
    SpectrumAnalyzerWorker* instance,
    SpectrumAnalyzerWorkerCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->callback_context = context;
}

void spectrum_analyzer_worker_set_frequencies(
    SpectrumAnalyzerWorker* instance,
    uint32_t channel0_frequency,
    uint32_t spacing,
    uint8_t width) {
    furi_assert(instance);

    FURI_LOG_D(
        "SpectrumWorker",
        "spectrum_analyzer_worker_set_frequencies - channel0_frequency= %lu - spacing = %lu - width = %u",
        channel0_frequency,
        spacing,
        width);

    instance->channel0_frequency = channel0_frequency;
    instance->spacing = spacing;
    instance->width = width;
}

void spectrum_analyzer_worker_set_modulation(SpectrumAnalyzerWorker* instance, uint8_t modulation) {
    furi_assert(instance);

    FURI_LOG_D(
        "SpectrumWorker", "spectrum_analyzer_worker_set_modulation - modulation = %u", modulation);

    instance->modulation = modulation;
}

void spectrum_analyzer_worker_start(SpectrumAnalyzerWorker* instance) {
    FURI_LOG_D("Spectrum", "spectrum_analyzer_worker_start");

    furi_assert(instance);
    furi_assert(instance->should_work == false);

    instance->should_work = true;
    furi_thread_start(instance->thread);
}

void spectrum_analyzer_worker_stop(SpectrumAnalyzerWorker* instance) {
    FURI_LOG_D("Spectrum", "spectrum_analyzer_worker_stop");
    furi_assert(instance);
    furi_assert(instance->should_work == true);

    instance->should_work = false;
    furi_thread_join(instance->thread);
}

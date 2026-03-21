#include "rolljam_receiver.h"
#include <furi_hal_subghz.h>
#include <furi_hal_rtc.h>

#define CC_IOCFG0   0x02
#define CC_FIFOTHR  0x03
#define CC_MDMCFG4  0x10
#define CC_MDMCFG3  0x11
#define CC_MDMCFG2  0x12
#define CC_MDMCFG1  0x13
#define CC_MDMCFG0  0x14
#define CC_DEVIATN  0x15
#define CC_MCSM0    0x18
#define CC_FOCCFG   0x19
#define CC_AGCCTRL2 0x1B
#define CC_AGCCTRL1 0x1C
#define CC_AGCCTRL0 0x1D
#define CC_FREND0   0x22
#define CC_FSCAL3   0x23
#define CC_FSCAL2   0x24
#define CC_FSCAL1   0x25
#define CC_FSCAL0   0x26

#define CC_PKTCTRL0  0x08
#define CC_PKTCTRL1  0x07
#define CC_FSCTRL1   0x0B
#define CC_WORCTRL   0x20
#define CC_FREND1    0x21

// OOK 650kHz
static const uint8_t preset_ook_650_async[] = {
    CC_IOCFG0,   0x0D,
    CC_FIFOTHR,  0x07,
    CC_PKTCTRL0, 0x32,
    CC_FSCTRL1,  0x06,
    CC_MDMCFG0,  0x00,
    CC_MDMCFG1,  0x00,
    CC_MDMCFG2,  0x30,
    CC_MDMCFG3,  0x32,
    CC_MDMCFG4,  0x17,
    CC_MCSM0,    0x18,
    CC_FOCCFG,   0x18,
    CC_AGCCTRL0, 0x91,
    CC_AGCCTRL1, 0x00,
    CC_AGCCTRL2, 0x07,
    CC_WORCTRL,  0xFB,
    CC_FREND0,   0x11,
    CC_FREND1,   0xB6,
    0x00,        0x00,
    0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// OOK 270kHz
static const uint8_t preset_ook_270_async[] = {
    CC_IOCFG0,   0x0D,
    CC_FIFOTHR,  0x47,
    CC_PKTCTRL0, 0x32,
    CC_FSCTRL1,  0x06,
    CC_MDMCFG0,  0x00,
    CC_MDMCFG1,  0x00,
    CC_MDMCFG2,  0x30,
    CC_MDMCFG3,  0x32,
    CC_MDMCFG4,  0x67,
    CC_MCSM0,    0x18,
    CC_FOCCFG,   0x18,
    CC_AGCCTRL0, 0x40,
    CC_AGCCTRL1, 0x00,
    CC_AGCCTRL2, 0x03,
    CC_WORCTRL,  0xFB,
    CC_FREND0,   0x11,
    CC_FREND1,   0xB6,
    0x00,        0x00,
    0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// 2FSK Dev 2.38kHz
static const uint8_t preset_2fsk_238_async[] = {
    CC_IOCFG0,   0x0D,
    CC_FIFOTHR,  0x47,
    CC_PKTCTRL0, 0x32,
    CC_FSCTRL1,  0x06,
    CC_MDMCFG0,  0x00,
    CC_MDMCFG1,  0x00,
    CC_MDMCFG2,  0x00,
    CC_MDMCFG3,  0x75,
    CC_MDMCFG4,  0x57,
    CC_DEVIATN,  0x15,
    CC_MCSM0,    0x18,
    CC_FOCCFG,   0x16,
    CC_AGCCTRL0, 0x91,
    CC_AGCCTRL1, 0x00,
    CC_AGCCTRL2, 0x07,
    CC_WORCTRL,  0xFB,
    CC_FREND0,   0x10,
    CC_FREND1,   0xB6,
    0x00,        0x00,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// 2FSK Dev 47.6kHz
static const uint8_t preset_2fsk_476_async[] = {
    CC_IOCFG0,   0x0D,
    CC_FIFOTHR,  0x47,
    CC_PKTCTRL0, 0x32,
    CC_FSCTRL1,  0x06,
    CC_MDMCFG0,  0x00,
    CC_MDMCFG1,  0x00,
    CC_MDMCFG2,  0x00,
    CC_MDMCFG3,  0x75,
    CC_MDMCFG4,  0x57,
    CC_DEVIATN,  0x47,
    CC_MCSM0,    0x18,
    CC_FOCCFG,   0x16,
    CC_AGCCTRL0, 0x91,
    CC_AGCCTRL1, 0x00,
    CC_AGCCTRL2, 0x07,
    CC_WORCTRL,  0xFB,
    CC_FREND0,   0x10,
    CC_FREND1,   0xB6,
    0x00,        0x00,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// TX OOK
static const uint8_t preset_ook_tx[] = {
    CC_IOCFG0,   0x0D,
    CC_FIFOTHR,  0x07,
    CC_PKTCTRL0, 0x32,
    CC_FSCTRL1,  0x06,
    CC_MDMCFG0,  0x00,
    CC_MDMCFG1,  0x00,
    CC_MDMCFG2,  0x30,
    CC_MDMCFG3,  0x32,
    CC_MDMCFG4,  0x17,
    CC_MCSM0,    0x18,
    CC_FOCCFG,   0x18,
    CC_AGCCTRL0, 0x91,
    CC_AGCCTRL1, 0x00,
    CC_AGCCTRL2, 0x07,
    CC_WORCTRL,  0xFB,
    CC_FREND0,   0x11,
    CC_FREND1,   0xB6,
    0x00,        0x00,
    0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t preset_fsk_tx_238[] = {
    CC_IOCFG0,   0x0D,
    CC_FIFOTHR,  0x47,
    CC_PKTCTRL0, 0x32,
    CC_FSCTRL1,  0x06,
    CC_MDMCFG0,  0x00,
    CC_MDMCFG1,  0x00,
    CC_MDMCFG2,  0x00,
    CC_MDMCFG3,  0x75,
    CC_MDMCFG4,  0x57,
    CC_DEVIATN,  0x15,
    CC_MCSM0,    0x18,
    CC_FOCCFG,   0x16,
    CC_AGCCTRL0, 0x91,
    CC_AGCCTRL1, 0x00,
    CC_AGCCTRL2, 0x07,
    CC_WORCTRL,  0xFB,
    CC_FREND0,   0x10,
    CC_FREND1,   0xB6,
    0x00,        0x00,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t preset_fsk_tx_476[] = {
    CC_IOCFG0,   0x0D,
    CC_FIFOTHR,  0x47,
    CC_PKTCTRL0, 0x32,
    CC_FSCTRL1,  0x06,
    CC_MDMCFG0,  0x00,
    CC_MDMCFG1,  0x00,
    CC_MDMCFG2,  0x00,
    CC_MDMCFG3,  0x75,
    CC_MDMCFG4,  0x57,
    CC_DEVIATN,  0x47,
    CC_MCSM0,    0x18,
    CC_FOCCFG,   0x16,
    CC_AGCCTRL0, 0x91,
    CC_AGCCTRL1, 0x00,
    CC_AGCCTRL2, 0x07,
    CC_WORCTRL,  0xFB,
    CC_FREND0,   0x10,
    CC_FREND1,   0xB6,
    0x00,        0x00,
    0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// ============================================================
// Capture state machine
// ============================================================

#define MIN_PULSE_US           100
#define MAX_PULSE_US           32767
#define SILENCE_GAP_US         50000
#define MIN_FRAME_PULSES       40
#define AUTO_ACCEPT_PULSES     300
#define MAX_CONTINUOUS_SAMPLES 800

static bool rolljam_is_jammer_pattern_mod(RawSignal* s, uint8_t mod_index) {
    if(s->size < 20) return false;

    // Calcular estadísticas una sola vez
    int16_t max_abs = 0;
    int64_t sum = 0;
    for(size_t i = 0; i < s->size; i++) {
        int16_t v = s->data[i] > 0 ? s->data[i] : -s->data[i];
        if(v > max_abs) max_abs = v;
        sum += v;
    }
    int32_t mean = (int32_t)(sum / (int64_t)s->size);

    FURI_LOG_D(TAG, "JamCheck: mod=%d max=%d mean=%ld size=%d",
               mod_index, max_abs, mean, (int)s->size);

    if(mod_index == 2 || mod_index == 3) { // ModIndex_FM238=2, FM476=3
        if((int)s->size < 120) {
            FURI_LOG_W(TAG, "Jammer FSK rechazado: size=%d < 120", (int)s->size);
            return true;
        }
        return false;
    }

    if(max_abs < 25000) {
        FURI_LOG_W(TAG, "Jammer AM650 rechazado: max=%d < 25000", max_abs);
        return true;
    }

    if(mod_index == 1) { // ModIndex_AM270=1
        if(mean < 3000) {
            FURI_LOG_W(TAG, "Jammer AM270 rechazado: mean=%ld < 3000 (max=%d)", mean, max_abs);
            return true;
        }
    }

    return false;
}



#define MIN_VARIANCE 2000

static bool rolljam_has_sufficient_variance(RawSignal* s) {
    if(s->size < 20) return false;

    int64_t sum = 0;
    for(size_t i = 0; i < s->size; i++) {
        int16_t val = s->data[i];
        sum += (val > 0) ? val : -val;
    }
    int32_t mean = (int32_t)(sum / (int64_t)s->size);

    int64_t var_sum = 0;
    for(size_t i = 0; i < s->size; i++) {
        int16_t val = s->data[i];
        int32_t abs_val = (val > 0) ? val : -val;
        int32_t diff = abs_val - mean;
        var_sum += (int64_t)diff * diff;
    }
    int32_t variance = (int32_t)(var_sum / (int64_t)s->size);

    bool has_var = (variance > MIN_VARIANCE);
    FURI_LOG_I(TAG, "Variance: mean=%ld var=%ld %s",
               mean, variance, has_var ? "PASS" : "FAIL");
    return has_var;
}

typedef enum {
    CapWaiting,
    CapRecording,
    CapDone,
} CapState;

typedef struct {
    volatile CapState state;
    volatile int      valid_count;
    volatile int      total_count;
    volatile bool     target_first;
    volatile uint32_t callback_count;
    volatile uint32_t continuous_count;
    float             rssi_baseline;
    uint8_t           mod_index;
} CapCtx;

static CapCtx g_cap;

static void cap_ctx_reset(CapCtx* c) {
    c->state            = CapWaiting;
    c->valid_count      = 0;
    c->total_count      = 0;
    c->callback_count   = 0;
    c->continuous_count = 0;
}

static void capture_rx_callback(bool level, uint32_t duration, void* context) {
    RollJamApp* app = context;

    if(!app->raw_capture_active) return;
    if(g_cap.state == CapDone)   return;

    g_cap.callback_count++;

    RawSignal* target = g_cap.target_first ? &app->signal_first : &app->signal_second;
    if(target->valid) return;

    uint32_t dur = duration;
    bool is_silence    = (dur > SILENCE_GAP_US);
    bool is_medium_gap = (dur > 5000 && dur <= SILENCE_GAP_US);
    if(dur > 32767) dur = 32767;

    switch(g_cap.state) {
    case CapWaiting:
        g_cap.continuous_count = 0;
        if(dur >= MIN_PULSE_US && dur <= MAX_PULSE_US && !is_silence) {
            target->size        = 0;
            g_cap.valid_count   = 0;
            g_cap.total_count   = 0;
            g_cap.state         = CapRecording;
            int16_t s = level ? (int16_t)dur : -(int16_t)dur;
            target->data[target->size++] = s;
            g_cap.valid_count++;
            g_cap.total_count++;
            g_cap.continuous_count = 1;
        }
        break;

    case CapRecording:
        g_cap.continuous_count++;

        if(g_cap.continuous_count > MAX_CONTINUOUS_SAMPLES && !is_medium_gap && !is_silence) {
            target->size = 0;
            cap_ctx_reset(&g_cap);
            return;
        }

        if(target->size >= RAW_SIGNAL_MAX_SIZE) {
            g_cap.state = (g_cap.valid_count >= MIN_FRAME_PULSES) ? CapDone : CapWaiting;
            if(g_cap.state == CapWaiting) {
                target->size = 0;
                g_cap.valid_count     = 0;
                g_cap.total_count     = 0;
                g_cap.continuous_count = 0;
            }
            return;
        }

        if(is_silence) {
            if(g_cap.valid_count >= MIN_FRAME_PULSES) {
                if(target->size < RAW_SIGNAL_MAX_SIZE)
                    target->data[target->size++] = level ? (int16_t)32767 : -32767;
                g_cap.state = CapDone;
            } else {
                target->size = 0;
                cap_ctx_reset(&g_cap);
            }
            return;
        }

        if(is_medium_gap) g_cap.continuous_count = 0;

        {
            int16_t s = level ? (int16_t)dur : -(int16_t)dur;
            target->data[target->size++] = s;
            g_cap.total_count++;
            if(dur >= MIN_PULSE_US && dur <= MAX_PULSE_US) {
                g_cap.valid_count++;
                if(g_cap.valid_count >= AUTO_ACCEPT_PULSES)
                    g_cap.state = CapDone;
            }
        }
        break;

    case CapDone:
        break;
    }
}

// ============================================================
// Capture start/stop
// ============================================================

void rolljam_capture_start(RollJamApp* app) {
    FURI_LOG_I(TAG, "Capture start: freq=%lu mod=%d offset=%lu",
               app->frequency, app->mod_index, app->jam_offset_hz);

    const uint8_t* src_preset;
    switch(app->mod_index) {
    case ModIndex_AM270: src_preset = preset_ook_270_async;  break;
    case ModIndex_FM238: src_preset = preset_2fsk_238_async; break;
    case ModIndex_FM476: src_preset = preset_2fsk_476_async; break;
    default:             src_preset = preset_ook_650_async;  break;
    }

    furi_hal_subghz_load_custom_preset(src_preset);
    furi_delay_ms(5);

    uint32_t real_freq = furi_hal_subghz_set_frequency_and_path(app->frequency);
    FURI_LOG_I(TAG, "Capture: freq=%lu (requested %lu)", real_freq, app->frequency);
    furi_delay_ms(5);

    furi_hal_subghz_rx();
    furi_delay_ms(50);
    float rssi_baseline = furi_hal_subghz_get_rssi();
    g_cap.rssi_baseline = rssi_baseline;
    FURI_LOG_I(TAG, "Capture: RSSI baseline=%.1f dBm", (double)rssi_baseline);

    furi_hal_subghz_idle();
    furi_delay_ms(5);

    cap_ctx_reset(&g_cap);

    if(!app->signal_first.valid) {
        g_cap.target_first      = true;
        app->signal_first.size  = 0;
        app->signal_first.valid = false;
        FURI_LOG_I(TAG, "Capture target: FIRST signal");
    } else {
        g_cap.target_first       = false;
        app->signal_second.size  = 0;
        app->signal_second.valid = false;
        FURI_LOG_I(TAG, "Capture target: SECOND signal");
    }

    g_cap.mod_index = app->mod_index;
    app->raw_capture_active = true;
    furi_hal_subghz_start_async_rx(capture_rx_callback, app);
    FURI_LOG_I(TAG, "Capture: RX STARTED");
}

void rolljam_capture_stop(RollJamApp* app) {
    if(!app->raw_capture_active) {
        FURI_LOG_W(TAG, "Capture stop: was not active");
        return;
    }
    app->raw_capture_active = false;
    furi_hal_subghz_stop_async_rx();
    furi_delay_ms(5);
    FURI_LOG_I(TAG, "Capture stopped. cb=%lu state=%d valid=%d total=%d",
               g_cap.callback_count, g_cap.state, g_cap.valid_count, g_cap.total_count);
    FURI_LOG_I(TAG, "  Sig1: size=%d valid=%d", app->signal_first.size, app->signal_first.valid);
    FURI_LOG_I(TAG, "  Sig2: size=%d valid=%d", app->signal_second.size, app->signal_second.valid);
}

// ============================================================
// Validation
// ============================================================

bool rolljam_signal_is_valid(RawSignal* signal) {
    if(g_cap.state != CapDone) {
        static int check_count = 0;
        check_count++;
        if(check_count % 10 == 0)
            FURI_LOG_D(TAG, "Validate: state=%d cb=%lu valid=%d total=%d size=%d",
                       g_cap.state, g_cap.callback_count,
                       g_cap.valid_count, g_cap.total_count, (int)signal->size);
        return false;
    }

    if(signal->size < (size_t)MIN_FRAME_PULSES) return false;

    if(rolljam_is_jammer_pattern_mod(signal, g_cap.mod_index)) {
        signal->size = 0;
        cap_ctx_reset(&g_cap);
        return false;
    }

    if(!rolljam_has_sufficient_variance(signal)) {
        signal->size = 0;
        cap_ctx_reset(&g_cap);
        return false;
    }

    int good  = 0;
    int total = (int)signal->size;
    for(int i = 0; i < total; i++) {
        int16_t abs_val = signal->data[i] > 0 ? signal->data[i] : -signal->data[i];
        if(abs_val >= MIN_PULSE_US) good++;
    }
    int ratio_pct = (total > 0) ? ((good * 100) / total) : 0;

    if(ratio_pct > 50 && good >= MIN_FRAME_PULSES) {
        FURI_LOG_I(TAG, "Signal VALID: %d/%d (%d%%) size=%d", good, total, ratio_pct, total);
        return true;
    }

    FURI_LOG_D(TAG, "Signal rejected: %d/%d (%d%%)", good, total, ratio_pct);
    signal->size = 0;
    cap_ctx_reset(&g_cap);
    return false;
}

// ============================================================
// Signal cleanup
// ============================================================

void rolljam_signal_cleanup(RawSignal* signal) {
    if(signal->size < (size_t)MIN_FRAME_PULSES) return;

    int16_t* cleaned = malloc(RAW_SIGNAL_MAX_SIZE * sizeof(int16_t));
    if(!cleaned) return;
    size_t out = 0;

    size_t start = 0;
    while(start < signal->size) {
        int16_t abs_val = signal->data[start] > 0 ? signal->data[start] : -signal->data[start];
        if(abs_val >= MIN_PULSE_US) break;
        start++;
    }

    for(size_t i = start; i < signal->size; i++) {
        int16_t val      = signal->data[i];
        int16_t abs_val  = val > 0 ? val : -val;
        bool is_positive = (val > 0);

        if(abs_val < MIN_PULSE_US) {
            if(out > 0) {
                int16_t prev       = cleaned[out - 1];
                bool prev_positive = (prev > 0);
                int16_t prev_abs   = prev > 0 ? prev : -prev;
                if(prev_positive == is_positive) {
                    int32_t merged = (int32_t)prev_abs + abs_val;
                    if(merged > 32767) merged = 32767;
                    cleaned[out - 1] = prev_positive ? (int16_t)merged : -(int16_t)merged;
                }
            }
            continue;
        }

        int32_t q = ((abs_val + 50) / 100) * 100;
        if(q < MIN_PULSE_US) q = MIN_PULSE_US;
        if(q > 32767) q = 32767;

        if(out < RAW_SIGNAL_MAX_SIZE)
            cleaned[out++] = is_positive ? (int16_t)q : -(int16_t)q;
    }

    while(out > 0) {
        int16_t abs_last = cleaned[out-1] > 0 ? cleaned[out-1] : -cleaned[out-1];
        if(abs_last >= MIN_PULSE_US && abs_last < 32767) break;
        out--;
    }

    if(out >= (size_t)MIN_FRAME_PULSES) {
        size_t orig = signal->size;
        memcpy(signal->data, cleaned, out * sizeof(int16_t));
        signal->size = out;
        FURI_LOG_I(TAG, "Cleanup: %d -> %d samples", (int)orig, (int)out);
    }
    free(cleaned);
}

// ============================================================
// TX
// ============================================================

typedef struct {
    const int16_t*  data;
    size_t          size;
    volatile size_t index;
} TxCtx;

static TxCtx g_tx;

static LevelDuration tx_feed(void* context) {
    UNUSED(context);
    if(g_tx.index >= g_tx.size) return level_duration_reset();
    int16_t sample = g_tx.data[g_tx.index++];
    bool     level = (sample > 0);
    uint32_t dur   = (uint32_t)(sample > 0 ? sample : -sample);
    return level_duration_make(level, dur);
}

void rolljam_transmit_signal(RollJamApp* app, RawSignal* signal) {
    if(!signal->valid || signal->size == 0) {
        FURI_LOG_E(TAG, "TX: no valid signal");
        return;
    }
    FURI_LOG_I(TAG, "TX: %d samples at %lu Hz (3x)", (int)signal->size, app->frequency);

    const uint8_t* tx_src;
    switch(app->mod_index) {
    case ModIndex_FM238: tx_src = preset_fsk_tx_238; break;
    case ModIndex_FM476: tx_src = preset_fsk_tx_476; break;
    default:             tx_src = preset_ook_tx;     break;
    }
    furi_hal_subghz_load_custom_preset(tx_src);
    uint32_t real_freq = furi_hal_subghz_set_frequency_and_path(app->frequency);
    FURI_LOG_I(TAG, "TX: freq=%lu", real_freq);
    furi_hal_subghz_idle();
    furi_delay_ms(5);

    for(int tx_repeat = 0; tx_repeat < 3; tx_repeat++) {
        g_tx.data  = signal->data;
        g_tx.size  = signal->size;
        g_tx.index = 0;

        if(!furi_hal_subghz_start_async_tx(tx_feed, NULL)) {
            FURI_LOG_E(TAG, "TX: start failed on repeat %d!", tx_repeat);
            furi_hal_subghz_idle();
            return;
        }

        uint32_t timeout = 0;
        while(!furi_hal_subghz_is_async_tx_complete()) {
            furi_delay_ms(5);
            if(++timeout > 2000) {
                FURI_LOG_E(TAG, "TX: timeout on repeat %d!", tx_repeat);
                break;
            }
        }
        furi_hal_subghz_stop_async_tx();
        FURI_LOG_I(TAG, "TX: repeat %d done (%d/%d)",
                   tx_repeat, (int)g_tx.index, (int)signal->size);
        if(tx_repeat < 2) furi_delay_ms(50);
    }
    furi_hal_subghz_idle();
    FURI_LOG_I(TAG, "TX: all repeats done");
}

// ============================================================
// Save
// ============================================================

void rolljam_save_signal(RollJamApp* app, RawSignal* signal) {
    if(!signal->valid || signal->size == 0) {
        FURI_LOG_E(TAG, "Save: no signal");
        return;
    }

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);

    FuriString* path = furi_string_alloc_printf(
        "/ext/subghz/RJ_%04d%02d%02d_%02d%02d%02d.sub",
        dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);

    FURI_LOG_I(TAG, "Saving: %s", furi_string_get_cstr(path));

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, "/ext/subghz");
    File* file = storage_file_alloc(storage);

    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* line = furi_string_alloc();

        furi_string_set(line, "Filetype: Flipper SubGhz RAW File\n");
        storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));
        furi_string_printf(line, "Version: 1\n");
        storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));
        furi_string_printf(line, "Frequency: %lu\n", app->frequency);
        storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));

        const char* pname;
        switch(app->mod_index) {
        case ModIndex_AM270: pname = "FuriHalSubGhzPresetOok270Async";     break;
        case ModIndex_FM238: pname = "FuriHalSubGhzPreset2FSKDev238Async"; break;
        case ModIndex_FM476: pname = "FuriHalSubGhzPreset2FSKDev476Async"; break;
        default:             pname = "FuriHalSubGhzPresetOok650Async";     break;
        }
        furi_string_printf(line, "Preset: %s\n", pname);
        storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));
        furi_string_printf(line, "Protocol: RAW\n");
        storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));

        size_t i = 0;
        while(i < signal->size) {
            furi_string_set(line, "RAW_Data:");
            size_t end = i + 512;
            if(end > signal->size) end = signal->size;
            for(; i < end; i++)
                furi_string_cat_printf(line, " %d", signal->data[i]);
            furi_string_cat(line, "\n");
            storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));
        }
        furi_string_free(line);
        FURI_LOG_I(TAG, "Saved: %d samples", (int)signal->size);
    } else {
        FURI_LOG_E(TAG, "Save failed!");
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    furi_string_free(path);
}

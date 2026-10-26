#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <input/input.h>
#include <spectrum_analyzer_icons.h>
#include <stdlib.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include "spectrum_analyzer.h"

#include <lib/drivers/cc1101_regs.h>
#include "spectrum_analyzer_worker.h"

#define SA_BAR_BASELINE   40
#define SA_BAR_TOP        13 // bars fill from just below the header
#define SA_MAX_PEAKS      24
#define SA_PEAK_DEDUP_KHZ 200 // peaks within this many kHz are the same signal
#define SA_SCALE_LABEL_Y  53
#define SA_STATUS_Y       62
#define SA_BADGE_X        109
#define SA_BADGE_W        19
#define SA_LAST_PATH      EXT_PATH("subghz/assets/last_subghz.settings")

typedef struct {
    uint32_t freq; // kHz
    float rssi; // dBm
} SpectrumPeak;

typedef enum {
    SA_VIEW_LIVE = 0,
    SA_VIEW_HISTORY = 1,
} SpectrumViewMode;

typedef struct {
    uint32_t center_freq;
    uint8_t width;
    uint8_t modulation;
    uint8_t band;
    uint8_t vscroll;

    uint32_t channel0_frequency;
    uint32_t spacing;

    bool mode_change;
    bool modulation_change;

    float max_rssi;
    uint8_t max_rssi_dec;
    uint8_t max_rssi_channel;
    uint8_t channel_ss[NUM_CHANNELS];
    uint8_t max_hold[NUM_CHANNELS]; // slowly-decaying peak-hold trace
    bool dual; // sweep split across two radios (~2x faster)

    SpectrumPeak peaks[SA_MAX_PEAKS]; // unique peaks seen this session
    uint8_t peak_count;
    uint8_t view_mode; // SA_VIEW_LIVE / SA_VIEW_HISTORY
    uint8_t hist_scroll; // selected row in the peak history
} SpectrumAnalyzerModel;

// How fast the peak-hold trace decays, in raw-RSSI units per sweep.
#define PEAK_HOLD_DECAY 2

static const char* spectrum_width_name(uint8_t width) {
    switch(width) {
    case NARROW:
        return "NARROW";
    case ULTRANARROW:
        return "U.NARROW";
    case PRECISE:
        return "PRECISE";
    case ULTRAWIDE:
        return "U.WIDE";
    default:
        return "WIDE";
    }
}

// Cycle the zoom mode by span (widest..narrowest); Up narrows, Down widens.
static uint8_t sa_cycle_width(uint8_t width, bool narrower) {
    static const uint8_t order[] = {ULTRAWIDE, WIDE, NARROW, ULTRANARROW, PRECISE};
    int idx = 0;
    for(int i = 0; i < 5; i++) {
        if(order[i] == width) {
            idx = i;
            break;
        }
    }
    idx += narrower ? 1 : -1;
    if(idx < 0) idx = 0;
    if(idx > 4) idx = 4;
    return order[idx];
}

typedef struct {
    SpectrumAnalyzerModel* model;
    FuriMutex* model_mutex;

    FuriMessageQueue* event_queue;

    ViewPort* view_port;
    Gui* gui;

    SpectrumAnalyzerWorker* worker;
} SpectrumAnalyzer;

void spectrum_analyzer_draw_scale(Canvas* canvas, const SpectrumAnalyzerModel* model) {
    // Draw line
    canvas_draw_line(
        canvas, FREQ_START_X, FREQ_BOTTOM_Y, FREQ_START_X + FREQ_LENGTH_X, FREQ_BOTTOM_Y);
    // Draw minor scale
    for(int i = FREQ_START_X; i < FREQ_START_X + FREQ_LENGTH_X; i += 5) {
        canvas_draw_line(canvas, i, FREQ_BOTTOM_Y, i, FREQ_BOTTOM_Y + 2);
    }
    // Draw major scale
    for(int i = FREQ_START_X; i < FREQ_START_X + FREQ_LENGTH_X; i += 25) {
        canvas_draw_line(canvas, i, FREQ_BOTTOM_Y, i, FREQ_BOTTOM_Y + 4);
    }

    // Draw scale tags
    uint32_t tag_left = 0;
    uint32_t tag_center = 0;
    uint32_t tag_right = 0;
    char temp_str[18];

    tag_center = model->center_freq;

    switch(model->width) {
    case NARROW:
        tag_left = model->center_freq - 2000;
        tag_right = model->center_freq + 2000;
        break;
    case ULTRANARROW:
        tag_left = model->center_freq - 1000;
        tag_right = model->center_freq + 1000;
        break;
    case PRECISE:
        tag_left = model->center_freq - 200;
        tag_right = model->center_freq + 200;
        break;
    case ULTRAWIDE:
        tag_left = model->center_freq - 40000;
        tag_right = model->center_freq + 40000;
        break;
    default:
        tag_left = model->center_freq - 10000;
        tag_right = model->center_freq + 10000;
    }

    canvas_set_font(canvas, FontSecondary);
    switch(model->width) {
    case PRECISE:
    case ULTRANARROW:
        snprintf(temp_str, 18, "%.1f", ((double)tag_left) / 1000);
        canvas_draw_str_aligned(
            canvas, FREQ_START_X, SA_SCALE_LABEL_Y, AlignCenter, AlignBottom, temp_str);
        snprintf(temp_str, 18, "%.1f", ((double)tag_center) / 1000);
        canvas_draw_str_aligned(
            canvas, 128 / 2, SA_SCALE_LABEL_Y, AlignCenter, AlignBottom, temp_str);
        snprintf(temp_str, 18, "%.1f", ((double)tag_right) / 1000);
        canvas_draw_str_aligned(
            canvas,
            FREQ_START_X + FREQ_LENGTH_X - 1,
            SA_SCALE_LABEL_Y,
            AlignCenter,
            AlignBottom,
            temp_str);
        break;
    default:
        snprintf(temp_str, 18, "%lu", tag_left / 1000);
        canvas_draw_str_aligned(
            canvas, FREQ_START_X, SA_SCALE_LABEL_Y, AlignCenter, AlignBottom, temp_str);
        snprintf(temp_str, 18, "%lu", tag_center / 1000);
        canvas_draw_str_aligned(
            canvas, 128 / 2, SA_SCALE_LABEL_Y, AlignCenter, AlignBottom, temp_str);
        snprintf(temp_str, 18, "%lu", tag_right / 1000);
        canvas_draw_str_aligned(
            canvas,
            FREQ_START_X + FREQ_LENGTH_X - 1,
            SA_SCALE_LABEL_Y,
            AlignCenter,
            AlignBottom,
            temp_str);
    }
}

static void spectrum_analyzer_draw_badge(Canvas* canvas, SpectrumAnalyzerModel* m) {
    const char* bw = (m->modulation == NARROW_MODULATION) ? "58k" : "270k";
    const int badge_x = m->dual ? SA_BADGE_X - SA_BADGE_W - 1 : SA_BADGE_X;

    if(m->dual) {
        canvas_draw_icon(canvas, badge_x, 0, &I_Internal_ant_1_9x11);
    }
    canvas_draw_icon(
        canvas, SA_BADGE_X, 0, m->dual ? &I_External_ant_1_9x11 : &I_Internal_ant_1_9x11);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, badge_x - 4, 8, AlignRight, AlignBottom, bw);
}

static void spectrum_analyzer_draw_live(Canvas* canvas, SpectrumAnalyzerModel* m) {
    const int barmax = SA_BAR_BASELINE - SA_BAR_TOP;

    canvas_set_font(canvas, FontPrimary);
    char freq[16];
    snprintf(freq, sizeof(freq), "%lu.%02lu", m->center_freq / 1000, (m->center_freq % 1000) / 10);
    canvas_draw_str(canvas, 0, 9, freq);

    spectrum_analyzer_draw_badge(canvas, m);

    for(uint8_t c = 0; c < 128; c++) {
        int s = ((int)m->channel_ss[c + 2] - (int)m->vscroll) >> 2;
        if(s > barmax) s = barmax;
        if(s > 0) canvas_draw_line(canvas, c, SA_BAR_BASELINE, c, SA_BAR_BASELINE - s);
        int hs = ((int)m->max_hold[c + 2] - (int)m->vscroll) >> 2;
        if(hs > barmax) hs = barmax;
        if(hs > 0) canvas_draw_dot(canvas, c, SA_BAR_BASELINE - hs);
    }

    spectrum_analyzer_draw_scale(canvas, m);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, SA_STATUS_Y, spectrum_width_name(m->width));
    char cnt[12];
    snprintf(cnt, sizeof(cnt), "%u pk", m->peak_count);
    canvas_draw_str_aligned(canvas, 127, SA_STATUS_Y, AlignRight, AlignBottom, cnt);
}

// Peak-history list (OK on the live view opens it).
static void spectrum_analyzer_draw_history(Canvas* canvas, SpectrumAnalyzerModel* m) {
    canvas_set_font(canvas, FontSecondary);
    char title[24];
    snprintf(title, sizeof(title), "Peak History (%u)", m->peak_count);
    canvas_draw_str(canvas, 2, 9, title);
    canvas_draw_line(canvas, 0, 11, 127, 11);

    if(m->peak_count == 0) {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "No peaks yet");
        canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignCenter, "Back = live");
        return;
    }

    const uint8_t rows = 4;
    uint8_t first = (m->hist_scroll >= rows) ? (m->hist_scroll - rows + 1) : 0;
    for(uint8_t r = 0; r < rows && (first + r) < m->peak_count; r++) {
        uint8_t idx = first + r;
        uint8_t y = 12 + r * 11;
        bool sel = (idx == m->hist_scroll);
        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y, 128, 11);
            canvas_set_color(canvas, ColorWhite);
        }
        char line[32];
        snprintf(
            line,
            sizeof(line),
            "%lu.%02lu MHz  %d dBm",
            m->peaks[idx].freq / 1000,
            (m->peaks[idx].freq % 1000) / 10,
            (int)m->peaks[idx].rssi);
        canvas_draw_str(canvas, 3, y + 9, line);
        if(sel) canvas_set_color(canvas, ColorBlack);
    }
}

static void spectrum_analyzer_render_callback(Canvas* const canvas, void* ctx) {
    SpectrumAnalyzer* spectrum_analyzer = ctx;

    // The worker thread and the input loop both mutate the model under this mutex, so
    // the draw (GUI thread) must take it too to avoid reading a half-updated model.
    if(furi_mutex_acquire(spectrum_analyzer->model_mutex, FuriWaitForever) != FuriStatusOk) {
        return;
    }
    SpectrumAnalyzerModel* model = spectrum_analyzer->model;

    if(model->view_mode == SA_VIEW_HISTORY) {
        spectrum_analyzer_draw_history(canvas, model);
    } else {
        spectrum_analyzer_draw_live(canvas, model);
    }

    furi_mutex_release(spectrum_analyzer->model_mutex);
}

static void spectrum_analyzer_input_callback(InputEvent* input_event, void* ctx) {
    SpectrumAnalyzer* spectrum_analyzer = ctx;
    if(input_event->type == InputTypeShort || input_event->type == InputTypeLong ||
       input_event->type == InputTypeRepeat) {
        furi_message_queue_put(spectrum_analyzer->event_queue, input_event, FuriWaitForever);
    }
}

static uint32_t spectrum_analyzer_load_center_khz(void) {
    uint32_t center_khz = DEFAULT_FREQ;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_existing(file, SA_LAST_PATH)) {
        uint32_t freq_hz = 0;
        if(flipper_format_read_uint32(file, "Frequency", &freq_hz, 1) && freq_hz >= 1000000u) {
            center_khz = freq_hz / 1000u;
        }
    }

    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
    return center_khz;
}

static void spectrum_analyzer_worker_callback(
    void* channel_ss,
    float max_rssi,
    uint8_t max_rssi_dec,
    uint8_t max_rssi_channel,
    void* context) {
    SpectrumAnalyzer* spectrum_analyzer = context;
    furi_check(
        furi_mutex_acquire(spectrum_analyzer->model_mutex, FuriWaitForever) == FuriStatusOk);

    SpectrumAnalyzerModel* model = (SpectrumAnalyzerModel*)spectrum_analyzer->model;
    memcpy(model->channel_ss, (uint8_t*)channel_ss, sizeof(uint8_t) * NUM_CHANNELS);
    model->max_rssi = max_rssi;
    model->max_rssi_dec = max_rssi_dec;
    model->max_rssi_channel = max_rssi_channel;

    // Update the peak-hold trace: rise instantly to new peaks, decay slowly otherwise.
    for(uint16_t i = 0; i < NUM_CHANNELS; i++) {
        uint8_t cur = model->channel_ss[i];
        if(cur >= model->max_hold[i]) {
            model->max_hold[i] = cur;
        } else if(model->max_hold[i] > PEAK_HOLD_DECAY) {
            model->max_hold[i] -= PEAK_HOLD_DECAY;
        } else {
            model->max_hold[i] = 0;
        }
    }

    // Auto-track the noise floor so the display baseline stays sensible without a
    // manual vscroll control (Up/Down are the width selector now).
    uint8_t floor = 255;
    for(uint8_t c = 0; c < 128; c++) {
        if(model->channel_ss[c + 2] < floor) floor = model->channel_ss[c + 2];
    }
    if(floor > model->vscroll) {
        model->vscroll++;
    } else if(floor < model->vscroll && model->vscroll > 0) {
        model->vscroll--;
    }

    // Track unique frequency peaks (dedup within SA_PEAK_DEDUP_KHZ).
    if(model->max_rssi > PEAK_THRESHOLD) {
        uint32_t pf =
            (model->channel0_frequency + (model->max_rssi_channel * model->spacing)) / 1000;
        int16_t found = -1;
        for(uint8_t i = 0; i < model->peak_count; i++) {
            uint32_t d = (pf > model->peaks[i].freq) ? (pf - model->peaks[i].freq) :
                                                       (model->peaks[i].freq - pf);
            if(d <= SA_PEAK_DEDUP_KHZ) {
                found = i;
                break;
            }
        }
        if(found >= 0) {
            if(model->max_rssi > model->peaks[found].rssi) {
                model->peaks[found].rssi = model->max_rssi;
                model->peaks[found].freq = pf;
            }
        } else if(model->peak_count < SA_MAX_PEAKS) {
            model->peaks[model->peak_count].freq = pf;
            model->peaks[model->peak_count].rssi = model->max_rssi;
            model->peak_count++;
        } else {
            uint8_t weakest = 0;
            for(uint8_t i = 1; i < SA_MAX_PEAKS; i++) {
                if(model->peaks[i].rssi < model->peaks[weakest].rssi) weakest = i;
            }
            if(model->max_rssi > model->peaks[weakest].rssi) {
                model->peaks[weakest].freq = pf;
                model->peaks[weakest].rssi = model->max_rssi;
            }
        }
    }

    furi_mutex_release(spectrum_analyzer->model_mutex);
    view_port_update(spectrum_analyzer->view_port);
}

void spectrum_analyzer_calculate_frequencies(SpectrumAnalyzerModel* model) {
    // REDO ALL THIS. CALCULATE ONLY WITH SPACING!

    uint8_t new_band;
    uint32_t min_hz;
    uint32_t max_hz;
    uint32_t margin;
    uint32_t step;
    uint32_t upper_limit;
    uint32_t lower_limit;
    uint32_t next_up;
    uint32_t next_down;
    uint8_t next_band_up;
    uint8_t next_band_down;

    switch(model->width) {
    case NARROW:
        margin = NARROW_MARGIN;
        step = NARROW_STEP;
        model->spacing = NARROW_SPACING;
        break;
    case ULTRANARROW:
        margin = ULTRANARROW_MARGIN;
        step = ULTRANARROW_STEP;
        model->spacing = ULTRANARROW_SPACING;
        break;
    case PRECISE:
        margin = PRECISE_MARGIN;
        step = PRECISE_STEP;
        model->spacing = PRECISE_SPACING;
        break;
    case ULTRAWIDE:
        margin = ULTRAWIDE_MARGIN;
        step = ULTRAWIDE_STEP;
        model->spacing = ULTRAWIDE_SPACING;
        /* nearest 20 MHz step */
        model->center_freq = ((model->center_freq + 10000) / 20000) * 20000;
        break;
    default:
        margin = WIDE_MARGIN;
        step = WIDE_STEP;
        model->spacing = WIDE_SPACING;
        /* nearest 5 MHz step */
        model->center_freq = ((model->center_freq + 2000) / 5000) * 5000;
        break;
    }

    /* handle cases near edges of bands */
    if(model->center_freq > EDGE_900) {
        new_band = BAND_900;
        upper_limit = UPPER(MAX_900, margin, step);
        lower_limit = LOWER(MIN_900, margin, step);
        next_up = LOWER(MIN_300, margin, step);
        next_down = UPPER(MAX_400, margin, step);
        next_band_up = BAND_300;
        next_band_down = BAND_400;
    } else if(model->center_freq > EDGE_400) {
        new_band = BAND_400;
        upper_limit = UPPER(MAX_400, margin, step);
        lower_limit = LOWER(MIN_400, margin, step);
        next_up = LOWER(MIN_900, margin, step);
        next_down = UPPER(MAX_300, margin, step);
        next_band_up = BAND_900;
        next_band_down = BAND_300;
    } else {
        new_band = BAND_300;
        upper_limit = UPPER(MAX_300, margin, step);
        lower_limit = LOWER(MIN_300, margin, step);
        next_up = LOWER(MIN_400, margin, step);
        next_down = UPPER(MAX_900, margin, step);
        next_band_up = BAND_400;
        next_band_down = BAND_900;
    }

    if(model->center_freq > upper_limit) {
        model->center_freq = upper_limit;
        if(new_band == model->band) {
            new_band = next_band_up;
            model->center_freq = next_up;
        }
    } else if(model->center_freq < lower_limit) {
        model->center_freq = lower_limit;
        if(new_band == model->band) {
            new_band = next_band_down;
            model->center_freq = next_down;
        }
    }

    model->band = new_band;
    /* doing everything in Hz from here on */
    switch(model->band) {
    case BAND_400:
        min_hz = MIN_400 * 1000;
        max_hz = MAX_400 * 1000;
        break;
    case BAND_300:
        min_hz = MIN_300 * 1000;
        max_hz = MAX_300 * 1000;
        break;
    default:
        min_hz = MIN_900 * 1000;
        max_hz = MAX_900 * 1000;
        break;
    }

    model->channel0_frequency =
        model->center_freq * 1000 - (model->spacing * ((NUM_CHANNELS / 2) + 1));

    // /* calibrate upper channels */
    // hz = model->center_freq * 1000000;
    // max_chan = NUM_CHANNELS / 2;
    // while (hz <= max_hz && max_chan < NUM_CHANNELS) {
    //     instance->chan_table[max_chan].frequency = hz;
    //     FURI_LOG_T("Spectrum", "calibrate_freq ch[%u]: %lu", max_chan, hz);
    //     hz += model->spacing;
    //     max_chan++;
    // }

    // /* calibrate lower channels */
    // hz = instance->freq * 1000000 - model->spacing;
    // min_chan = NUM_CHANNELS / 2;
    // while (hz >= min_hz && min_chan > 0) {
    //     min_chan--;
    //     instance->chan_table[min_chan].frequency = hz;
    //     FURI_LOG_T("Spectrum", "calibrate_freq ch[%u]: %lu", min_chan, hz);
    //     hz -= model->spacing;
    // }

    model->max_rssi = -200.0;
    model->max_rssi_dec = 0;

    FURI_LOG_D("Spectrum", "setup_frequencies - max_hz: %lu - min_hz: %lu", max_hz, min_hz);
    FURI_LOG_D("Spectrum", "center_freq: %lu", model->center_freq);
    FURI_LOG_D(
        "Spectrum",
        "ch[0]: %lu - ch[%u]: %lu",
        model->channel0_frequency,
        NUM_CHANNELS - 1,
        model->channel0_frequency + ((NUM_CHANNELS - 1) * model->spacing));
}

SpectrumAnalyzer* spectrum_analyzer_alloc() {
    SpectrumAnalyzer* instance = malloc(sizeof(SpectrumAnalyzer));
    instance->model = malloc(sizeof(SpectrumAnalyzerModel));

    SpectrumAnalyzerModel* model = instance->model;

    for(uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
        model->channel_ss[ch] = 0;
        model->max_hold[ch] = 0;
    }
    model->max_rssi_dec = 0;
    model->max_rssi_channel = 0;
    model->max_rssi = PEAK_THRESHOLD - 1; // Should initializar to < PEAK_THRESHOLD

    model->center_freq = spectrum_analyzer_load_center_khz();
    model->width = WIDE;
    model->modulation = DEFAULT_MODULATION;
    model->band = BAND_400;

    model->vscroll = DEFAULT_VSCROLL;
    model->dual = false;
    model->peak_count = 0;
    model->view_mode = SA_VIEW_LIVE;
    model->hist_scroll = 0;

    instance->model_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    instance->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    instance->worker = spectrum_analyzer_worker_alloc();

    spectrum_analyzer_worker_set_callback(
        instance->worker, spectrum_analyzer_worker_callback, instance);

    // Set system callbacks
    instance->view_port = view_port_alloc();
    view_port_draw_callback_set(instance->view_port, spectrum_analyzer_render_callback, instance);
    view_port_input_callback_set(instance->view_port, spectrum_analyzer_input_callback, instance);

    // Open GUI and register view_port
    instance->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(instance->gui, instance->view_port, GuiLayerFullscreen);

    return instance;
}

void spectrum_analyzer_free(SpectrumAnalyzer* instance) {
    // view_port_enabled_set(view_port, false);
    gui_remove_view_port(instance->gui, instance->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(instance->view_port);

    spectrum_analyzer_worker_free(instance->worker);

    furi_message_queue_free(instance->event_queue);

    furi_mutex_free(instance->model_mutex);

    free(instance->model);
    free(instance);
}

int32_t spectrum_analyzer_app(void* p) {
    UNUSED(p);

    SpectrumAnalyzer* spectrum_analyzer = spectrum_analyzer_alloc();
    InputEvent input;

    furi_hal_power_suppress_charge_enter();
    furi_delay_ms(50);

    spectrum_analyzer->model->dual = spectrum_analyzer_worker_is_dual(spectrum_analyzer->worker);
    spectrum_analyzer_calculate_frequencies(spectrum_analyzer->model);
    spectrum_analyzer_worker_set_frequencies(
        spectrum_analyzer->worker,
        spectrum_analyzer->model->channel0_frequency,
        spectrum_analyzer->model->spacing,
        spectrum_analyzer->model->width);
    spectrum_analyzer_worker_set_modulation(
        spectrum_analyzer->worker, spectrum_analyzer->model->modulation);

    spectrum_analyzer_worker_start(spectrum_analyzer->worker);
    furi_delay_ms(50);

    while(furi_message_queue_get(spectrum_analyzer->event_queue, &input, FuriWaitForever) ==
          FuriStatusOk) {
        furi_check(
            furi_mutex_acquire(spectrum_analyzer->model_mutex, FuriWaitForever) == FuriStatusOk);

        SpectrumAnalyzerModel* model = spectrum_analyzer->model;

        uint32_t hstep;

        bool exit_loop = false;

        switch(model->width) {
        case NARROW:
            hstep = NARROW_STEP;
            break;
        case ULTRANARROW:
            hstep = ULTRANARROW_STEP;
            break;
        case ULTRAWIDE:
            hstep = ULTRAWIDE_STEP;
            break;
        case PRECISE:
            hstep = PRECISE_STEP;
            break;
        default:
            hstep = WIDE_STEP;
            break;
        }

        if(model->view_mode == SA_VIEW_HISTORY) {
            if(input.type == InputTypeShort || input.type == InputTypeRepeat) {
                switch(input.key) {
                case InputKeyUp:
                    if(model->hist_scroll > 0) model->hist_scroll--;
                    break;
                case InputKeyDown:
                    if(model->peak_count && model->hist_scroll < model->peak_count - 1)
                        model->hist_scroll++;
                    break;
                case InputKeyOk:
                    if(input.type == InputTypeShort && model->peak_count) {
                        model->center_freq = model->peaks[model->hist_scroll].freq;
                        spectrum_analyzer_calculate_frequencies(model);
                        spectrum_analyzer_worker_set_frequencies(
                            spectrum_analyzer->worker,
                            model->channel0_frequency,
                            model->spacing,
                            model->width);
                        model->view_mode = SA_VIEW_LIVE;
                    }
                    break;
                case InputKeyBack:
                    if(input.type == InputTypeShort) model->view_mode = SA_VIEW_LIVE;
                    break;
                default:
                    break;
                }
            }
        } else if(input.type == InputTypeShort || input.type == InputTypeRepeat) {
            switch(input.key) {
            case InputKeyUp:
            case InputKeyDown:
                if(input.type == InputTypeShort) {
                    model->width = sa_cycle_width(model->width, input.key == InputKeyUp);
                    spectrum_analyzer_calculate_frequencies(model);
                    spectrum_analyzer_worker_set_frequencies(
                        spectrum_analyzer->worker,
                        model->channel0_frequency,
                        model->spacing,
                        model->width);
                }
                break;
            case InputKeyRight:
                model->center_freq += hstep;
                spectrum_analyzer_calculate_frequencies(model);
                spectrum_analyzer_worker_set_frequencies(
                    spectrum_analyzer->worker,
                    model->channel0_frequency,
                    model->spacing,
                    model->width);
                break;
            case InputKeyLeft:
                model->center_freq -= hstep;
                spectrum_analyzer_calculate_frequencies(model);
                spectrum_analyzer_worker_set_frequencies(
                    spectrum_analyzer->worker,
                    model->channel0_frequency,
                    model->spacing,
                    model->width);
                break;
            case InputKeyOk:
                if(input.type == InputTypeShort) {
                    model->view_mode = SA_VIEW_HISTORY;
                    model->hist_scroll = 0;
                }
                break;
            case InputKeyBack:
                if(input.type == InputTypeShort) exit_loop = true;
                break;
            default:
                break;
            }
        } else if(input.type == InputTypeLong) {
            if(input.key == InputKeyOk) {
                model->modulation = (model->modulation == NARROW_MODULATION) ? DEFAULT_MODULATION :
                                                                               NARROW_MODULATION;
                spectrum_analyzer_worker_set_modulation(
                    spectrum_analyzer->worker, model->modulation);
            }
        }

        furi_mutex_release(spectrum_analyzer->model_mutex);
        view_port_update(spectrum_analyzer->view_port);
        if(exit_loop == true) break;
    }

    spectrum_analyzer_worker_stop(spectrum_analyzer->worker);

    furi_hal_power_suppress_charge_exit();

    spectrum_analyzer_free(spectrum_analyzer);

    return 0;
}

#include "subghz_signal_visualizer.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/view.h>
#include <gui/elements.h>
#include <input/input.h>
#include <float_tools.h>
#include <string.h>

#define TAG "SubGhzVisualizer"

#define VIZ_RAW_BUF_SIZE     1024u
#define VIZ_RAW_BUF_MASK     (VIZ_RAW_BUF_SIZE - 1u)

#define VIZ_DISP_W           128u

#define VIZ_OSCOPE_H         64u

#define RSSI_FLOOR           (-100.0f)
#define RSSI_CEIL            (-30.0f)
#define RSSI_SPAN            (RSSI_CEIL - RSSI_FLOOR)

#define TRIGGER_DEFAULT      (-72.0f)
#define TRIGGER_STEP         (1.0f)
#define TRIGGER_MIN          RSSI_FLOOR
#define TRIGGER_MAX          (RSSI_CEIL - TRIGGER_STEP)

#define SAMPLE_TIMER_MS      1u

#define REDRAW_TIMER_MS      33u

typedef enum {
    VizModeBar = 0,
    VizModeLine,
    VizModeCount,
} VizMode;

typedef struct {

    uint8_t disp_samples[VIZ_DISP_W];

    VizMode        mode;
    float          trigger_threshold;
    bool           trigger_active;
    char           freq_str[20];
    char           mod_str[16];
} SubGhzSignalVisualizerModel;

struct SubGhzSignalVisualizer {
    View*                          view;
    SubGhzSignalVisualizerCallback callback;
    void*                          context;
    SubGhzTxRx*                   txrx;

    FuriMutex* sample_mutex;
    int8_t     raw_buf[VIZ_RAW_BUF_SIZE];
    uint16_t   write_head;

    float trigger_threshold;
    bool  trigger_armed;
    bool  trigger_active;
    uint16_t trigger_idx;

    FuriTimer* sample_timer;
    FuriTimer* redraw_timer;

    bool running;
};

static inline uint8_t rssi_to_pixel_h(float rssi, uint8_t area_h) {
    if(rssi < RSSI_FLOOR) rssi = RSSI_FLOOR;
    if(rssi > RSSI_CEIL)  rssi = RSSI_CEIL;
    float norm = (rssi - RSSI_FLOOR) / RSSI_SPAN;
    return (uint8_t)(norm * (float)(area_h - 1));
}

static void viz_sample_timer_callback(void* ctx) {
    SubGhzSignalVisualizer* inst = ctx;

    float rssi = subghz_txrx_radio_device_get_rssi(inst->txrx);
    int8_t rssi_i = (rssi < -127.0f) ? -127 : (rssi > 0.0f) ? 0 : (int8_t)rssi;

    furi_mutex_acquire(inst->sample_mutex, FuriWaitForever);

    inst->raw_buf[inst->write_head & VIZ_RAW_BUF_MASK] = rssi_i;

    if((float)rssi_i < inst->trigger_threshold) {
        inst->trigger_armed = true;
    } else if(inst->trigger_armed) {

        inst->trigger_armed  = false;
        inst->trigger_active = true;
        inst->trigger_idx    = inst->write_head;
    }

    inst->write_head = (inst->write_head + 1u) & VIZ_RAW_BUF_MASK;

    furi_mutex_release(inst->sample_mutex);
}

static void viz_redraw_timer_callback(void* ctx) {
    SubGhzSignalVisualizer* inst = ctx;

    furi_mutex_acquire(inst->sample_mutex, FuriWaitForever);

    uint16_t start;
    bool trig = inst->trigger_active;
    if(trig) {

        start = (inst->trigger_idx - (VIZ_DISP_W / 4)) & VIZ_RAW_BUF_MASK;
        inst->trigger_active = false;
    } else {

        start = (inst->write_head - VIZ_DISP_W) & VIZ_RAW_BUF_MASK;
    }

    int8_t snapshot[VIZ_DISP_W];
    for(uint16_t i = 0; i < VIZ_DISP_W; i++) {
        snapshot[i] = inst->raw_buf[(start + i) & VIZ_RAW_BUF_MASK];
    }

    float thr = inst->trigger_threshold;

    furi_mutex_release(inst->sample_mutex);

    with_view_model(
        inst->view,
        SubGhzSignalVisualizerModel * mdl,
        {

            for(uint16_t i = 0; i < VIZ_DISP_W; i++) {
                float rv = (float)snapshot[i];
                if(rv < RSSI_FLOOR) rv = RSSI_FLOOR;
                if(rv > RSSI_CEIL)  rv = RSSI_CEIL;
                mdl->disp_samples[i] =
                    (uint8_t)((rv - RSSI_FLOOR) / RSSI_SPAN * 127.0f);
            }

            mdl->trigger_threshold = thr;
            mdl->trigger_active    = trig;

            FuriString* fs = furi_string_alloc();
            FuriString* ms = furi_string_alloc();
            subghz_txrx_get_frequency_and_modulation(inst->txrx, fs, ms, false);
            snprintf(mdl->freq_str, sizeof(mdl->freq_str), "%s", furi_string_get_cstr(fs));
            snprintf(mdl->mod_str,  sizeof(mdl->mod_str),  "%s", furi_string_get_cstr(ms));
            furi_string_free(fs);
            furi_string_free(ms);
        },
        true );
}

static void draw_signal_area(Canvas* canvas, SubGhzSignalVisualizerModel* mdl) {
    const uint8_t y_top  = 0;
    const uint8_t area_h = VIZ_OSCOPE_H;

    canvas_draw_frame(canvas, 0, y_top, VIZ_DISP_W, area_h);

    float thr_norm = (mdl->trigger_threshold - RSSI_FLOOR) / RSSI_SPAN;
    if(thr_norm < 0.0f) thr_norm = 0.0f;
    if(thr_norm > 1.0f) thr_norm = 1.0f;
    uint8_t thr_y = (uint8_t)((float)(y_top + area_h - 2) -
                               thr_norm * (float)(area_h - 2));
    for(uint8_t x = 1; x < VIZ_DISP_W - 1; x += 4) {
        canvas_draw_dot(canvas, x, thr_y);
        canvas_draw_dot(canvas, x + 1, thr_y);
    }

    uint8_t y_base = (uint8_t)(y_top + area_h - 2);

    if(mdl->mode == VizModeLine) {
        int prev_x = -1, prev_y = y_base;
        for(uint8_t x = 1; x < VIZ_DISP_W - 1; x++) {
            uint8_t h = (uint8_t)((float)mdl->disp_samples[x] / 127.0f *
                                   (float)(area_h - 2));
            int y = y_base - h;
            if(prev_x >= 0) canvas_draw_line(canvas, prev_x, prev_y, x, y);
            prev_x = x;
            prev_y = y;
        }
    } else {

        for(uint8_t x = 1; x < VIZ_DISP_W - 1; x++) {
            uint8_t h = (uint8_t)((float)mdl->disp_samples[x] / 127.0f *
                                   (float)(area_h - 2));
            if(h == 0) continue;
            canvas_draw_line(canvas, x, y_base, x, (uint8_t)(y_base - h));
        }
    }

    if(mdl->trigger_active) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, (uint8_t)(y_top + 8), "TRG");
    }
}

static void viz_draw_callback(Canvas* canvas, void* model_ptr) {
    SubGhzSignalVisualizerModel* mdl = model_ptr;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    draw_signal_area(canvas, mdl);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, 7, mdl->freq_str);

    uint8_t mod_w = (uint8_t)(strlen(mdl->mod_str) * 5);
    canvas_draw_str(canvas, (uint8_t)(VIZ_DISP_W - mod_w - 3), 7, mdl->mod_str);

    char thr_label[16];
    snprintf(thr_label, sizeof(thr_label), "T:%.0f", (double)mdl->trigger_threshold);
    canvas_draw_str(canvas, 3, VIZ_OSCOPE_H - 2, thr_label);
}

static bool viz_input_callback(InputEvent* event, void* ctx) {
    SubGhzSignalVisualizer* inst = ctx;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    switch(event->key) {
    case InputKeyUp:

        furi_mutex_acquire(inst->sample_mutex, FuriWaitForever);
        inst->trigger_threshold += TRIGGER_STEP;
        if(inst->trigger_threshold > TRIGGER_MAX) inst->trigger_threshold = TRIGGER_MAX;
        furi_mutex_release(inst->sample_mutex);
        return true;

    case InputKeyDown:

        furi_mutex_acquire(inst->sample_mutex, FuriWaitForever);
        inst->trigger_threshold -= TRIGGER_STEP;
        if(inst->trigger_threshold < TRIGGER_MIN) inst->trigger_threshold = TRIGGER_MIN;
        furi_mutex_release(inst->sample_mutex);
        return true;

    case InputKeyOk:

        with_view_model(
            inst->view,
            SubGhzSignalVisualizerModel * mdl,
            { mdl->mode = (VizMode)((mdl->mode + 1u) % VizModeCount); },
            false);
        return true;

    case InputKeyBack:
        if(inst->callback) {
            inst->callback(SubGhzCustomEventViewSignalVisualizerBack, inst->context);
        }
        return true;

    default:
        return false;
    }
}

SubGhzSignalVisualizer* subghz_signal_visualizer_alloc(SubGhzTxRx* txrx) {
    furi_assert(txrx);

    SubGhzSignalVisualizer* inst = malloc(sizeof(SubGhzSignalVisualizer));
    memset(inst, 0, sizeof(*inst));

    inst->txrx               = txrx;
    inst->trigger_threshold  = TRIGGER_DEFAULT;
    inst->trigger_armed      = false;
    inst->trigger_active     = false;
    inst->trigger_idx        = 0;
    inst->write_head         = 0;
    inst->running            = false;

    inst->sample_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    furi_assert(inst->sample_mutex);

    inst->view = view_alloc();
    view_set_context(inst->view, inst);
    view_allocate_model(inst->view, ViewModelTypeLocking,
                        sizeof(SubGhzSignalVisualizerModel));
    view_set_draw_callback(inst->view, viz_draw_callback);
    view_set_input_callback(inst->view, viz_input_callback);

    with_view_model(
        inst->view,
        SubGhzSignalVisualizerModel * mdl,
        {
            memset(mdl, 0, sizeof(*mdl));
            mdl->mode              = VizModeBar;
            mdl->trigger_threshold = TRIGGER_DEFAULT;
            strncpy(mdl->freq_str, "---", sizeof(mdl->freq_str));
            strncpy(mdl->mod_str,  "---", sizeof(mdl->mod_str));
        },
        false);

    inst->sample_timer = furi_timer_alloc(viz_sample_timer_callback,
                                          FuriTimerTypePeriodic, inst);
    inst->redraw_timer = furi_timer_alloc(viz_redraw_timer_callback,
                                          FuriTimerTypePeriodic, inst);

    FURI_LOG_I(TAG, "Allocated");
    return inst;
}

void subghz_signal_visualizer_free(SubGhzSignalVisualizer* instance) {
    furi_assert(instance);

    subghz_signal_visualizer_stop(instance);

    furi_timer_free(instance->redraw_timer);
    furi_timer_free(instance->sample_timer);

    view_free(instance->view);
    furi_mutex_free(instance->sample_mutex);

    free(instance);
    FURI_LOG_I(TAG, "Freed");
}

View* subghz_signal_visualizer_get_view(SubGhzSignalVisualizer* instance) {
    furi_assert(instance);
    return instance->view;
}

void subghz_signal_visualizer_set_callback(
    SubGhzSignalVisualizer*         instance,
    SubGhzSignalVisualizerCallback  callback,
    void*                           context)
{
    furi_assert(instance);
    furi_assert(callback);
    instance->callback = callback;
    instance->context  = context;
}

void subghz_signal_visualizer_start(SubGhzSignalVisualizer* instance) {
    furi_assert(instance);

    if(instance->running) return;
    instance->running = true;

    subghz_txrx_rx_start(instance->txrx);

    furi_mutex_acquire(instance->sample_mutex, FuriWaitForever);
    memset(instance->raw_buf, (int8_t)RSSI_FLOOR, sizeof(instance->raw_buf));
    instance->write_head     = 0;
    instance->trigger_armed  = false;
    instance->trigger_active = false;
    furi_mutex_release(instance->sample_mutex);

    furi_timer_start(instance->sample_timer, SAMPLE_TIMER_MS);
    furi_timer_start(instance->redraw_timer, REDRAW_TIMER_MS);

    FURI_LOG_I(TAG, "Started – sample %u ms, redraw %u ms",
               SAMPLE_TIMER_MS, REDRAW_TIMER_MS);
}

void subghz_signal_visualizer_stop(SubGhzSignalVisualizer* instance) {
    furi_assert(instance);

    if(!instance->running) return;
    instance->running = false;

    furi_timer_stop(instance->redraw_timer);
    furi_timer_stop(instance->sample_timer);

    FURI_LOG_I(TAG, "Stopped");
}

uint32_t subghz_signal_visualizer_get_mode(SubGhzSignalVisualizer* instance) {
    furi_assert(instance);
    uint32_t mode = 0;
    with_view_model(
        instance->view,
        SubGhzSignalVisualizerModel * mdl,
        { mode = (uint32_t)mdl->mode; },
        false);
    return mode;
}

void subghz_signal_visualizer_set_mode(SubGhzSignalVisualizer* instance, uint32_t mode) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzSignalVisualizerModel * mdl,
        { mdl->mode = (VizMode)(mode % VizModeCount); },
        false);
}

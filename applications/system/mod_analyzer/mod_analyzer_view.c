#include "mod_analyzer_view.h"
#include "mod_analyzer_worker.h"
#include "helpers/radio_device_loader.h"

#include <furi.h>
#include <gui/elements.h>
#include <mod_analyzer_icons.h>
#include <stdio.h>
#include <string.h>

#define TAG "ModAnalyzerView"

#define MOD_WAVE_COLS 128
#define MOD_WAVE_H    18
#define MOD_WAVE_BASE 44

#define MOD_ADV_Y     21
#define MOD_ADV_PITCH 9
#define MOD_RSSI_MIN  (-110.0f)
#define MOD_RSSI_MAX  (-50.0f)

typedef struct {
    ModAnalyzerReport report;
    bool have_report;
    bool running;
    bool advanced;
    uint32_t frequency;
    uint8_t freq_index;
    uint8_t freq_count;
    bool ext_radio;
    uint8_t wave[MOD_WAVE_COLS];
    uint8_t wave_idx;
} ModAnalyzerViewModel;

struct ModAnalyzerView {
    View* view;
    ModAnalyzerWorker* worker;
    const SubGhzDevice* device;
    SubGhzSetting* setting;
    ModAnalyzerViewCallback callback;
    void* context;
};

static void mod_view_fmt1(char* out, size_t size, float v) {
    if(!(v > -999.9f)) v = -999.9f;
    if(v > 999.9f) v = 999.9f;
    int32_t t = (int32_t)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f));
    const char* sign = (t < 0) ? "-" : "";
    if(t < 0) t = -t;
    if(t > 9999) t = 9999;
    snprintf(out, size, "%s%u.%u", sign, (unsigned)(t / 10), (unsigned)(t % 10));
}

static uint8_t mod_view_rssi_to_bar(float dbm) {
    if(dbm <= MOD_RSSI_MIN) return 1;
    if(dbm >= MOD_RSSI_MAX) return MOD_WAVE_H;
    const uint8_t h =
        (uint8_t)(((dbm - MOD_RSSI_MIN) / (MOD_RSSI_MAX - MOD_RSSI_MIN)) * MOD_WAVE_H);
    return h < 1 ? 1 : h;
}

static void mod_view_draw_wave(Canvas* canvas, const ModAnalyzerViewModel* m) {
    for(uint8_t col = 0; col < MOD_WAVE_COLS; col++) {
        const uint8_t idx = (uint8_t)((m->wave_idx + col) % MOD_WAVE_COLS);
        uint8_t h = m->wave[idx];
        if(!h) continue;
        if(h > MOD_WAVE_H) h = MOD_WAVE_H;
        canvas_draw_line(canvas, col, MOD_WAVE_BASE, col, (uint8_t)(MOD_WAVE_BASE - h + 1));
    }
}

static void mod_view_draw_header(Canvas* canvas, const ModAnalyzerViewModel* m) {
    char buf[24];
    canvas_set_font(canvas, FontPrimary);
    snprintf(
        buf, sizeof(buf), "%lu.%02lu", m->frequency / 1000000, (m->frequency % 1000000) / 10000);
    canvas_draw_str(canvas, 0, 10, buf);
    const size_t w = canvas_string_width(canvas, buf);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, w + 4, 10, "MHz");

    canvas_draw_icon(
        canvas, 107, 1, m->ext_radio ? &I_External_ant_1_9x11 : &I_Internal_ant_1_9x11);
}

static void mod_view_draw_advanced(Canvas* canvas, const ModAnalyzerViewModel* m) {
    const ModAnalyzerReport* r = &m->report;
    char buf[64], a[16], b[16];

    canvas_draw_line(canvas, 0, 12, 128, 12);
    canvas_set_font(canvas, FontSecondary);

    mod_view_fmt1(a, sizeof(a), r->last_carrier_db);
    mod_view_fmt1(b, sizeof(b), r->last_floor_db);
    snprintf(buf, sizeof(buf), "carrier %sdB  floor %sdB", a, b);
    canvas_draw_str(canvas, 0, MOD_ADV_Y + 0 * MOD_ADV_PITCH, buf);

    mod_view_fmt1(a, sizeof(a), r->last_p90_dbm);
    mod_view_fmt1(b, sizeof(b), r->floor_dbm);
    snprintf(buf, sizeof(buf), "p90 %s  noise %s dBm", a, b);
    canvas_draw_str(canvas, 0, MOD_ADV_Y + 1 * MOD_ADV_PITCH, buf);

    snprintf(
        buf,
        sizeof(buf),
        "duty %u%% tr %u on %luus",
        r->last_duty_pct,
        r->last_transitions,
        (unsigned long)r->last_on_run_max_us);
    canvas_draw_str(canvas, 0, MOD_ADV_Y + 2 * MOD_ADV_PITCH, buf);

    snprintf(
        buf,
        sizeof(buf),
        "win %u/%u dec %u dis %u",
        r->windows_valid,
        r->windows_total,
        r->windows_decisive,
        r->disagreements);
    canvas_draw_str(canvas, 0, MOD_ADV_Y + 3 * MOD_ADV_PITCH, buf);

    elements_button_center(canvas, "Hide");
}

static void mod_analyzer_view_draw(Canvas* canvas, void* _model) {
    ModAnalyzerViewModel* m = _model;
    char buf[64], a[16];

    canvas_clear(canvas);
    mod_view_draw_header(canvas, m);

    if(m->advanced) {
        mod_view_draw_advanced(canvas, m);
        return;
    }

    const ModResult res = m->have_report ? m->report.result : ModResultListening;
    const bool decided = (res == ModResultAm || res == ModResultFm);

    canvas_set_font(canvas, FontPrimary);
    if(!m->running) {
        canvas_draw_str(canvas, 0, 22, "Stopped");
    } else if(decided) {
        canvas_draw_str(canvas, 0, 22, res == ModResultAm ? "ASK/OOK" : "2-FSK");
        const uint8_t w = (uint8_t)((m->report.confidence_pct * 44) / 100);
        canvas_draw_frame(canvas, 82, 14, 46, 9);
        if(w) canvas_draw_box(canvas, 83, 15, w, 7);
    } else if(res == ModResultListening || !m->have_report) {
        canvas_draw_str(canvas, 0, 22, "Press the fob");
    } else {
        canvas_draw_str(canvas, 0, 22, mod_stats_result_text(res));
        canvas_set_font(canvas, FontSecondary);
        mod_view_fmt1(a, sizeof(a), m->report.last_p90_dbm);
        snprintf(buf, sizeof(buf), "%s dBm", a);
        canvas_draw_str_aligned(canvas, 128, 22, AlignRight, AlignBottom, buf);
    }

    mod_view_draw_wave(canvas, m);

    elements_button_left(canvas, "Freq-");
    elements_button_right(canvas, "Freq+");
    if(decided) {
        elements_button_center(canvas, "Read");
    } else {
        elements_button_center(canvas, "Info");
    }
}

static void mod_analyzer_view_report_callback(void* context, const ModAnalyzerReport* report) {
    ModAnalyzerView* v = context;
    with_view_model(
        v->view,
        ModAnalyzerViewModel * model,
        {
            model->report = *report;
            model->have_report = true;
            model->wave[model->wave_idx] = mod_view_rssi_to_bar(report->last_p90_dbm);
            model->wave_idx = (uint8_t)((model->wave_idx + 1) % MOD_WAVE_COLS);
        },
        true);
}

static void mod_analyzer_view_apply_frequency(ModAnalyzerView* v, int delta) {
    with_view_model(
        v->view,
        ModAnalyzerViewModel * model,
        {
            if(model->freq_count) {
                int32_t idx = (int32_t)model->freq_index + delta;
                if(idx < 0) idx = model->freq_count - 1;
                if(idx >= model->freq_count) idx = 0;
                model->freq_index = (uint8_t)idx;
                model->frequency = subghz_setting_get_frequency(v->setting, model->freq_index);
                memset(model->wave, 0, sizeof(model->wave));
                model->wave_idx = 0;
                model->have_report = false;
            }
        },
        true);

    uint32_t f = 0;
    with_view_model(v->view, ModAnalyzerViewModel * model, { f = model->frequency; }, false);
    if(v->worker) mod_analyzer_worker_set_frequency(v->worker, f);
}

static bool mod_analyzer_view_input(InputEvent* event, void* context) {
    ModAnalyzerView* v = context;
    bool advanced = false;
    with_view_model(v->view, ModAnalyzerViewModel * model, { advanced = model->advanced; }, false);

    if(event->key == InputKeyBack) {
        if(advanced) {
            with_view_model(
                v->view, ModAnalyzerViewModel * model, { model->advanced = false; }, true);
            return true;
        }
        return false;
    }

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(advanced) {
            if(event->type == InputTypeShort && event->key == InputKeyOk) {
                with_view_model(
                    v->view, ModAnalyzerViewModel * model, { model->advanced = false; }, true);
                return true;
            }
            return true;
        }

        switch(event->key) {
        case InputKeyLeft:
            mod_analyzer_view_apply_frequency(v, -1);
            return true;
        case InputKeyRight:
            mod_analyzer_view_apply_frequency(v, 1);
            return true;
        case InputKeyOk:
            if(event->type != InputTypeShort) return true;
            {
                bool decided = false;
                with_view_model(
                    v->view,
                    ModAnalyzerViewModel * model,
                    {
                        decided = model->have_report && (model->report.result == ModResultAm ||
                                                         model->report.result == ModResultFm);
                        if(!decided) model->advanced = true;
                    },
                    true);
                if(decided && v->callback) {
                    v->callback(ModAnalyzerViewEventOpenReceiver, v->context);
                }
            }
            return true;
        default:
            return true;
        }
    }

    return true;
}

static void mod_analyzer_view_enter(void* context) {
    ModAnalyzerView* v = context;

    v->worker = mod_analyzer_worker_alloc(v->device);
    mod_analyzer_worker_set_callback(v->worker, mod_analyzer_view_report_callback, v);

    uint32_t f = 0;
    with_view_model(
        v->view,
        ModAnalyzerViewModel * model,
        {
            model->freq_count = (uint8_t)subghz_setting_get_frequency_count(v->setting);
            if(model->frequency == 0) {
                model->freq_index =
                    (uint8_t)subghz_setting_get_frequency_default_index(v->setting);
                if(model->freq_index >= model->freq_count) model->freq_index = 0;
                model->frequency = subghz_setting_get_frequency(v->setting, model->freq_index);
            } else {
                for(uint8_t i = 0; i < model->freq_count; i++) {
                    if(subghz_setting_get_frequency(v->setting, i) == model->frequency) {
                        model->freq_index = i;
                        break;
                    }
                }
            }
            model->ext_radio = radio_device_loader_is_external(v->device);
            model->running = true;
            model->advanced = false;
            f = model->frequency;
        },
        true);

    mod_analyzer_worker_set_frequency(v->worker, f);
    mod_analyzer_worker_start(v->worker);
}

static void mod_analyzer_view_exit(void* context) {
    ModAnalyzerView* v = context;
    if(v->worker) {
        if(mod_analyzer_worker_is_running(v->worker)) mod_analyzer_worker_stop(v->worker);
        mod_analyzer_worker_free(v->worker);
        v->worker = NULL;
    }
    with_view_model(v->view, ModAnalyzerViewModel * model, { model->running = false; }, true);
}

ModAnalyzerView* mod_analyzer_view_alloc(const SubGhzDevice* device, SubGhzSetting* setting) {
    ModAnalyzerView* v = malloc(sizeof(ModAnalyzerView));
    memset(v, 0, sizeof(*v));
    v->device = device;
    v->setting = setting;

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ModAnalyzerViewModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, mod_analyzer_view_draw);
    view_set_input_callback(v->view, mod_analyzer_view_input);
    view_set_enter_callback(v->view, mod_analyzer_view_enter);
    view_set_exit_callback(v->view, mod_analyzer_view_exit);
    return v;
}

void mod_analyzer_view_free(ModAnalyzerView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* mod_analyzer_view_get_view(ModAnalyzerView* v) {
    furi_assert(v);
    return v->view;
}

void mod_analyzer_view_set_callback(
    ModAnalyzerView* v,
    ModAnalyzerViewCallback callback,
    void* context) {
    furi_assert(v);
    v->callback = callback;
    v->context = context;
}

uint32_t mod_analyzer_view_get_frequency(ModAnalyzerView* v) {
    furi_assert(v);
    uint32_t f = 0;
    with_view_model(v->view, ModAnalyzerViewModel * model, { f = model->frequency; }, false);
    return f;
}

void mod_analyzer_view_set_frequency(ModAnalyzerView* v, uint32_t frequency) {
    furi_assert(v);
    with_view_model(
        v->view,
        ModAnalyzerViewModel * model,
        {
            model->frequency = frequency;
            model->freq_count = (uint8_t)subghz_setting_get_frequency_count(v->setting);
            for(uint8_t i = 0; i < model->freq_count; i++) {
                if(subghz_setting_get_frequency(v->setting, i) == frequency) {
                    model->freq_index = i;
                    break;
                }
            }
        },
        false);
}

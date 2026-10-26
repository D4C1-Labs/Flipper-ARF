#include "freq_analyzer_view.h"
#include "freq_analyzer_worker.h"

#include <furi.h>
#include <input/input.h>
#include <gui/elements.h>
#include <freq_analyzer_icons.h>
#include <float_tools.h>

#define TAG "FreqAnalyzerView"

#define RSSI_MIN     (-97.0f)
#define RSSI_MAX     (-60.0f)
#define RSSI_SCALE   2.3f
#define TRIGGER_STEP 1
#define MAX_HISTORY  4

#define FREQ_BOX_Y    11
#define FREQ_BOX_H    18
#define HISTORY_Y     38
#define HISTORY_PITCH 11
#define RSSI_BAR_Y    60
#define RSSI_BAR_H    8
#define RSSI_BAR_W    ((uint8_t)((RSSI_MAX - RSSI_MIN) * 2 / RSSI_SCALE))
#define RSSI_BAR_X    ((128 - RSSI_BAR_W) / 2)

struct FreqAnalyzerView {
    View* view;
    FreqAnalyzerWorker* worker;
    FreqAnalyzerViewCallback callback;
    void* context;
    const SubGhzDevice* device;
    SubGhzSetting* setting;
    float initial_trigger;
    bool locked;
    bool ext_radio;
    FreqAnalyzerFeedbackLevel feedback_level;
    float rssi_last;
    uint8_t selected_index;
    uint8_t max_index;
    bool show_frame;
    uint32_t handoff_frequency;
};

typedef struct {
    uint32_t frequency;
    uint32_t frequency_to_save;
    float rssi;
    uint32_t history_frequency[MAX_HISTORY];
    uint8_t history_frequency_rx_count[MAX_HISTORY];
    bool signal;
    float rssi_last;
    float trigger;
    uint8_t selected_index;
    uint8_t max_index;
    bool show_frame;
    bool ext_radio;
} FreqAnalyzerViewModel;

void freq_analyzer_view_set_callback(
    FreqAnalyzerView* instance,
    FreqAnalyzerViewCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(callback);
    instance->callback = callback;
    instance->context = context;
}

static void freq_analyzer_view_draw_rssi(
    Canvas* canvas,
    float rssi,
    float rssi_last,
    float trigger,
    uint8_t x,
    uint8_t y) {
    if(!float_is_equal(rssi, 0.f)) {
        if(rssi > RSSI_MAX) {
            rssi = RSSI_MAX;
        }
        rssi = (rssi - RSSI_MIN) / RSSI_SCALE;
        uint8_t column_number = 0;
        for(size_t i = 0; i <= (uint8_t)rssi; i++) {
            if((i + 1) % 4) {
                column_number++;
                const uint8_t h = (column_number > RSSI_BAR_H) ? RSSI_BAR_H :
                                                                 (uint8_t)column_number;
                canvas_draw_box(canvas, x + 2 * i, y - h, 2, h);
            }
        }
    }

    if(!float_is_equal(rssi_last, 0.f)) {
        if(rssi_last > RSSI_MAX) {
            rssi_last = RSSI_MAX;
        }
        const int peak_x = (int)((rssi_last - RSSI_MIN) / RSSI_SCALE) * 2;
        canvas_draw_line(canvas, x + peak_x + 1, y - RSSI_BAR_H, x + peak_x + 1, y);
    }

    trigger = (trigger - RSSI_MIN) / RSSI_SCALE;
    uint8_t tr_x = (uint8_t)((float)x + (2 * trigger));
    canvas_draw_dot(canvas, tr_x, y + 1);
    canvas_draw_line(canvas, tr_x - 1, y + 2, tr_x + 1, y + 2);

    canvas_draw_line(canvas, x, y, x + RSSI_BAR_W, y);
}

static void freq_analyzer_view_history_draw(Canvas* canvas, FreqAnalyzerViewModel* model) {
    char buffer[64] = {0};
    const uint8_t x1 = 2;
    const uint8_t x2 = 66;
    const uint8_t y = HISTORY_Y;

    canvas_set_font(canvas, FontSecondary);
    uint8_t line = 0;
    bool show_frame = model->show_frame && model->max_index > 0;
    for(uint8_t i = 0; i < MAX_HISTORY; i++) {
        uint8_t current_x;
        uint8_t current_y = y + line * HISTORY_PITCH;

        if(i % 2 == 0) {
            current_x = x1;
        } else {
            current_x = x2;
            line++;
        }
        if(model->history_frequency[i]) {
            snprintf(
                buffer,
                sizeof(buffer),
                "%03ld.%03ld",
                model->history_frequency[i] / 1000000 % 1000,
                model->history_frequency[i] / 1000 % 1000);
            canvas_draw_str(canvas, current_x, current_y, buffer);
        } else {
            canvas_draw_str(canvas, current_x, current_y, "---.---");
        }
        if(model->history_frequency_rx_count[i] > 1) {
            snprintf(buffer, sizeof(buffer), "x%d", model->history_frequency_rx_count[i]);
            canvas_draw_str(canvas, current_x + 41, current_y, buffer);
        } else {
            canvas_draw_str(canvas, current_x + 41, current_y, "MHz");
        }

        if(show_frame && i == model->selected_index) {
            elements_frame(canvas, current_x - 2, current_y - 9, 63, 11);
        }
    }
}

static bool freq_analyzer_view_can_read(const FreqAnalyzerViewModel* model) {
    if(model->show_frame && model->max_index > 0 && !model->signal) {
        return model->history_frequency[model->selected_index] > 0;
    }
    return model->frequency > 0;
}

static void freq_analyzer_view_draw(Canvas* canvas, FreqAnalyzerViewModel* model) {
    char buffer[64] = {0};

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    canvas_draw_str(canvas, 2, 7, "Freq Analyzer");
    if(freq_analyzer_view_can_read(model)) {
        canvas_draw_str_aligned(canvas, 105, 7, AlignRight, AlignBottom, "OK");
    }
    canvas_draw_icon(
        canvas, 109, 0, model->ext_radio ? &I_External_ant_1_9x11 : &I_Internal_ant_1_9x11);

    canvas_set_font(canvas, FontBigNumbers);
    snprintf(
        buffer,
        sizeof(buffer),
        "%03ld.%03ld",
        model->frequency / 1000000 % 1000,
        model->frequency / 1000 % 1000);
    if(model->signal) {
        canvas_draw_box(canvas, 4, FREQ_BOX_Y, 121, FREQ_BOX_H);
        canvas_set_color(canvas, ColorWhite);
    }

    canvas_draw_str(canvas, 8, FREQ_BOX_Y + FREQ_BOX_H - 2, buffer);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 99, FREQ_BOX_Y + FREQ_BOX_H - 3, "MHz");

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    freq_analyzer_view_history_draw(canvas, model);

    elements_button_left(canvas, "T-");
    elements_button_right(canvas, "+T");
    freq_analyzer_view_draw_rssi(
        canvas, model->rssi, model->rssi_last, model->trigger, RSSI_BAR_X, RSSI_BAR_Y);
}

static uint32_t freq_analyzer_view_pick_frequency(FreqAnalyzerView* instance) {
    uint32_t freq = 0;
    with_view_model(
        instance->view,
        FreqAnalyzerViewModel * model,
        {
            if(model->show_frame && !model->signal) {
                freq = model->history_frequency[model->selected_index];
            } else if(model->frequency) {
                freq =
                    freq_analyzer_worker_get_nearest_frequency(instance->worker, model->frequency);
            }
        },
        false);
    if(freq > 0 && subghz_devices_is_frequency_valid(instance->device, freq)) {
        return freq;
    }
    return 0;
}

static bool freq_analyzer_view_input(InputEvent* event, void* context) {
    furi_assert(context);
    FreqAnalyzerView* instance = (FreqAnalyzerView*)context;

    bool need_redraw = false;
    if(event->key == InputKeyBack) {
        return false;
    }

    bool is_press_or_repeat = (event->type == InputTypePress) || (event->type == InputTypeRepeat);
    if(is_press_or_repeat && (event->key == InputKeyLeft || event->key == InputKeyRight)) {
        float trigger_level = freq_analyzer_worker_get_trigger_level(instance->worker);
        if(event->key == InputKeyLeft) {
            trigger_level -= TRIGGER_STEP;
            if(trigger_level < RSSI_MIN) {
                trigger_level = RSSI_MIN;
            }
        } else {
            trigger_level += TRIGGER_STEP;
            if(trigger_level > RSSI_MAX) {
                trigger_level = RSSI_MAX;
            }
        }
        freq_analyzer_worker_set_trigger_level(instance->worker, trigger_level);
        need_redraw = true;
    } else if(event->type == InputTypePress && event->key == InputKeyUp) {
        if(instance->feedback_level == FreqAnalyzerFeedbackLevelAll) {
            instance->feedback_level = FreqAnalyzerFeedbackLevelMute;
        } else {
            instance->feedback_level--;
        }
        need_redraw = true;
    } else if(is_press_or_repeat && event->key == InputKeyDown) {
        instance->show_frame = instance->max_index > 0;
        if(instance->show_frame) {
            instance->selected_index = (instance->selected_index + 1) % instance->max_index;
            need_redraw = true;
        }
    } else if(
        (event->type == InputTypeShort || event->type == InputTypeLong) &&
        event->key == InputKeyOk) {
        uint32_t freq = freq_analyzer_view_pick_frequency(instance);
        if(freq > 0) {
            instance->handoff_frequency = freq;
            if(instance->callback) {
                instance->callback(FreqAnalyzerViewEventOpenReceiver, instance->context);
            }
        }
    }

    if(need_redraw) {
        with_view_model(
            instance->view,
            FreqAnalyzerViewModel * model,
            {
                model->rssi_last = instance->rssi_last;
                model->trigger = freq_analyzer_worker_get_trigger_level(instance->worker);
                model->max_index = instance->max_index;
                model->show_frame = instance->show_frame;
                model->selected_index = instance->selected_index;
            },
            true);
    }

    return true;
}

static uint32_t freq_analyzer_round_int(uint32_t value, uint8_t n) {
    uint8_t on = n;
    while(n--) {
        uint8_t i = value % 10;
        value /= 10;
        if(i >= 5) value++;
    }
    while(on--)
        value *= 10;
    return value;
}

static void
    freq_analyzer_view_pair_callback(void* context, uint32_t frequency, float rssi, bool signal) {
    FreqAnalyzerView* instance = (FreqAnalyzerView*)context;
    if(float_is_equal(rssi, 0.f) && instance->locked) {
        if(instance->callback) {
            instance->callback(FreqAnalyzerViewEventUnlock, instance->context);
        }
        instance->show_frame = true;
        uint8_t max_index = instance->max_index;
        with_view_model(
            instance->view,
            FreqAnalyzerViewModel * model,
            {
                bool in_array = false;
                uint32_t normal_frequency =
                    freq_analyzer_worker_get_nearest_frequency(instance->worker, model->frequency);
                for(size_t i = 0; i < MAX_HISTORY; i++) {
                    if(model->history_frequency[i] == normal_frequency) {
                        in_array = true;
                        if(model->history_frequency[i] > 0) {
                            if(model->history_frequency_rx_count[i] < 255) {
                                model->history_frequency_rx_count[i]++;
                            }
                        }
                        if(i > 0) {
                            size_t offset = 0;
                            uint8_t temp_rx_count = model->history_frequency_rx_count[i];

                            for(size_t j = MAX_HISTORY - 1; j > 0; j--) {
                                if(j == i) {
                                    offset++;
                                }
                                model->history_frequency[j] = model->history_frequency[j - offset];
                                model->history_frequency_rx_count[j] =
                                    model->history_frequency_rx_count[j - offset];
                            }
                            model->history_frequency[0] = normal_frequency;
                            model->history_frequency_rx_count[0] = temp_rx_count;
                        }

                        break;
                    }
                }

                if(!in_array) {
                    model->history_frequency[3] = model->history_frequency[2];
                    model->history_frequency[2] = model->history_frequency[1];
                    model->history_frequency[1] = model->history_frequency[0];
                    model->history_frequency[0] = normal_frequency;

                    model->history_frequency_rx_count[3] = model->history_frequency_rx_count[2];
                    model->history_frequency_rx_count[2] = model->history_frequency_rx_count[1];
                    model->history_frequency_rx_count[1] = model->history_frequency_rx_count[0];
                    model->history_frequency_rx_count[0] = 1;
                }

                if(max_index < MAX_HISTORY) {
                    for(size_t i = 0; i < MAX_HISTORY; i++) {
                        if(model->history_frequency[i] > 0) {
                            max_index = i + 1;
                        }
                    }
                }
            },
            false);
        instance->max_index = max_index;
    } else if(!float_is_equal(rssi, 0.f) && !instance->locked) {
        frequency = freq_analyzer_round_int(frequency, 3);

        instance->rssi_last = rssi;
        if(instance->callback) {
            instance->callback(FreqAnalyzerViewEventLock, instance->context);
        }
    }

    if(rssi >= instance->rssi_last && frequency != 0) {
        instance->rssi_last = rssi;
    }

    instance->locked = !float_is_equal(rssi, 0.f);
    with_view_model(
        instance->view,
        FreqAnalyzerViewModel * model,
        {
            model->rssi = rssi;
            model->rssi_last = instance->rssi_last;
            model->frequency = frequency;
            model->signal = signal;
            model->trigger = freq_analyzer_worker_get_trigger_level(instance->worker);
            model->max_index = instance->max_index;
            model->show_frame = instance->show_frame;
            model->selected_index = instance->selected_index;
            model->ext_radio = instance->ext_radio;
        },
        true);
}

static void freq_analyzer_view_enter(void* context) {
    furi_assert(context);
    FreqAnalyzerView* instance = (FreqAnalyzerView*)context;

    instance->worker =
        freq_analyzer_worker_alloc(instance->device, instance->setting, instance->initial_trigger);

    freq_analyzer_worker_set_pair_callback(
        instance->worker, freq_analyzer_view_pair_callback, instance);

    freq_analyzer_worker_start(instance->worker);

    instance->rssi_last = 0;
    instance->selected_index = 0;
    instance->max_index = 0;
    instance->show_frame = false;

    with_view_model(
        instance->view,
        FreqAnalyzerViewModel * model,
        {
            model->selected_index = 0;
            model->max_index = 0;
            model->show_frame = false;
            model->rssi = 0;
            model->rssi_last = 0;
            model->frequency = 0;
            for(size_t i = 0; i < MAX_HISTORY; i++) {
                model->history_frequency[i] = 0;
                model->history_frequency_rx_count[i] = 0;
            }
            model->frequency_to_save = 0;
            model->trigger = instance->initial_trigger;
            model->ext_radio = instance->ext_radio;
        },
        true);
}

static void freq_analyzer_view_exit(void* context) {
    furi_assert(context);
    FreqAnalyzerView* instance = (FreqAnalyzerView*)context;

    if(freq_analyzer_worker_is_running(instance->worker)) {
        freq_analyzer_worker_stop(instance->worker);
    }
    freq_analyzer_worker_free(instance->worker);
    instance->worker = NULL;
}

FreqAnalyzerView* freq_analyzer_view_alloc(
    const SubGhzDevice* device,
    SubGhzSetting* setting,
    float trigger_level,
    FreqAnalyzerFeedbackLevel feedback_level,
    bool ext_radio) {
    furi_assert(device);
    furi_assert(setting);
    FreqAnalyzerView* instance = malloc(sizeof(FreqAnalyzerView));

    instance->device = device;
    instance->setting = setting;
    instance->initial_trigger = trigger_level;
    instance->feedback_level = feedback_level;
    instance->ext_radio = ext_radio;
    instance->worker = NULL;
    instance->callback = NULL;
    instance->context = NULL;
    instance->locked = false;
    instance->rssi_last = 0;
    instance->selected_index = 0;
    instance->max_index = 0;
    instance->show_frame = false;
    instance->handoff_frequency = 0;

    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(FreqAnalyzerViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, (ViewDrawCallback)freq_analyzer_view_draw);
    view_set_input_callback(instance->view, freq_analyzer_view_input);
    view_set_enter_callback(instance->view, freq_analyzer_view_enter);
    view_set_exit_callback(instance->view, freq_analyzer_view_exit);

    return instance;
}

void freq_analyzer_view_free(FreqAnalyzerView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* freq_analyzer_view_get_view(FreqAnalyzerView* instance) {
    furi_assert(instance);
    return instance->view;
}

FreqAnalyzerFeedbackLevel freq_analyzer_view_get_feedback_level(FreqAnalyzerView* instance) {
    furi_assert(instance);
    return instance->feedback_level;
}

float freq_analyzer_view_get_trigger_level(FreqAnalyzerView* instance) {
    furi_assert(instance);
    if(instance->worker) {
        return freq_analyzer_worker_get_trigger_level(instance->worker);
    }
    return instance->initial_trigger;
}

uint32_t freq_analyzer_view_get_handoff_frequency(FreqAnalyzerView* instance) {
    furi_assert(instance);
    return instance->handoff_frequency;
}

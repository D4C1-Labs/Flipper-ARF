#include "subghz_read_raw.h"
#include "../subghz_i.h"

#include <furi.h>
#include <furi_hal.h>
#include <input/input.h>
#include <gui/elements.h>
#include <assets_icons.h>

#define SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE 100
#define TAG                               "SubGhzReadRaw"

#define SEEK_STEP_PCT 5u

#define REVEAL_TIMER_MS      15u
#define REVEAL_COLS_PER_TICK 4u

#define SCENE_TICK_PERIOD_US 100000u

#define ZOOM_LEVEL_COUNT 5u

struct SubGhzReadRAW {
    View*                  view;
    SubGhzReadRAWCallback  callback;
    void*                  context;
    FuriTimer*             reveal_timer;
    bool                   auto_capture_mode;
};

typedef struct {
    FuriString* frequency_str;
    FuriString* preset_str;
    FuriString* sample_write;
    FuriString* file_name;
    uint8_t*    rssi_history;
    uint8_t     rssi_current;
    bool        rssi_history_end;
    uint8_t     ind_write;
    SubGhzReadRAWStatus    status;
    bool        raw_send_only;
    bool        allow_new;
    float       raw_threshold_rssi;
    bool        not_showing_samples;
    SubGhzRadioDeviceType device_type;

        SubGhzReadRawVizMode viz_mode;
    uint8_t  target_envelope[SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE];
    uint8_t  shown_envelope[SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE];
    uint8_t  reveal_count;
    bool     has_envelope;
    uint8_t  play_pct;
    uint8_t  seek_pct;
    uint32_t tx_tick;
    uint32_t tx_total_ticks;
    uint8_t  zoom_level;
    uint32_t recording_ticks;

    bool     auto_capture_mode;

    uint8_t  start_countdown_sec;

    uint8_t  decode_group;
    uint8_t  decode_group_total;
    uint8_t  decode_pct;
} SubGhzReadRAWModel;

void subghz_read_raw_set_callback(
    SubGhzReadRAW* instance,
    SubGhzReadRAWCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(callback);
    instance->callback = callback;
    instance->context  = context;
}

void subghz_read_raw_set_auto_capture_mode(SubGhzReadRAW* instance, bool auto_capture_mode) {
    furi_assert(instance);
    instance->auto_capture_mode = auto_capture_mode;
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        { model->auto_capture_mode = auto_capture_mode; },
        true);
}

void subghz_read_raw_add_data_statusbar(
    SubGhzReadRAW* instance,
    const char* frequency_str,
    const char* preset_str) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            furi_string_set(model->frequency_str, frequency_str);
            furi_string_set(model->preset_str, preset_str);
        },
        true);
}

void subghz_read_raw_set_radio_device_type(
    SubGhzReadRAW* instance,
    SubGhzRadioDeviceType device_type) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        { model->device_type = device_type; },
        true);
}

void subghz_read_raw_set_start_countdown(SubGhzReadRAW* instance, uint8_t seconds_left) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        { model->start_countdown_sec = seconds_left; },
        true);
}

SubGhzReadRAWStatus subghz_read_raw_get_status(SubGhzReadRAW* instance) {
    furi_assert(instance);
    SubGhzReadRAWStatus status = SubGhzReadRAWStatusIDLE;
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        { status = model->status; },
        false);
    return status;
}

void subghz_read_raw_add_data_rssi(SubGhzReadRAW* instance, float rssi, bool trace) {
    furi_assert(instance);
    uint8_t u_rssi = 0;

    if(rssi < SUBGHZ_RAW_THRESHOLD_MIN) {
        u_rssi = 0;
    } else {
        u_rssi = (uint8_t)((rssi - SUBGHZ_RAW_THRESHOLD_MIN) / 2.7f);
    }

    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            model->rssi_current = u_rssi;
            if(trace) {
                model->rssi_history[model->ind_write++] = u_rssi;
            } else {
                model->rssi_history[model->ind_write] = u_rssi;
            }
            if(model->ind_write >= SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE) {
                model->rssi_history_end = true;
                model->ind_write        = 0;
            }
        },
        true);
}

void subghz_read_raw_update_sample_write(SubGhzReadRAW* instance, size_t sample) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            model->not_showing_samples = false;
            furi_string_printf(model->sample_write, "%zu spl.", sample);
        },
        false);
}

void subghz_read_raw_recording_tick(SubGhzReadRAW* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel* model,
        { model->recording_ticks++; },
        false);
}

uint32_t subghz_read_raw_get_recording_ticks(SubGhzReadRAW* instance) {
    furi_assert(instance);
    uint32_t ticks = 0;
    with_view_model(
        instance->view,
        SubGhzReadRAWModel* model,
        { ticks = model->recording_ticks; },
        false);
    return ticks;
}

void subghz_read_raw_stop_send(SubGhzReadRAW* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            switch(model->status) {
            case SubGhzReadRAWStatusTXRepeat:

                instance->callback(SubGhzCustomEventViewReadRAWSendStart, instance->context);
                break;
            case SubGhzReadRAWStatusTX:
                model->status = SubGhzReadRAWStatusIDLE;
                break;
            case SubGhzReadRAWStatusLoadKeyTX:
            case SubGhzReadRAWStatusLoadKeyTXRepeat:
            case SubGhzReadRAWStatusLoadKeyTXPaused:

                model->status   = SubGhzReadRAWStatusLoadKeyIDLE;
                model->play_pct = 0;
                model->tx_tick  = 0;
                break;
            default:
                FURI_LOG_W(TAG, "unknown status");
                model->status = SubGhzReadRAWStatusIDLE;
                break;
            }
        },
        true);
}

void subghz_read_raw_set_viz_mode(SubGhzReadRAW* instance, SubGhzReadRawVizMode mode) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        { model->viz_mode = mode; },
        false);
}

void subghz_read_raw_set_allow_new(SubGhzReadRAW* instance, bool allow) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        { model->allow_new = allow; },
        false);
}

void subghz_read_raw_set_zoom_level(SubGhzReadRAW* instance, uint8_t zoom_level) {
    furi_assert(instance);
    if(zoom_level >= ZOOM_LEVEL_COUNT) zoom_level = ZOOM_LEVEL_COUNT - 1;
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        { model->zoom_level = zoom_level; },
        false);
}

uint8_t subghz_read_raw_get_zoom_level(SubGhzReadRAW* instance) {
    furi_assert(instance);
    uint8_t z = 0;
    with_view_model(instance->view, SubGhzReadRAWModel * model, { z = model->zoom_level; }, false);
    return z;
}

uint8_t subghz_read_raw_get_seek_pct(SubGhzReadRAW* instance) {
    furi_assert(instance);
    uint8_t p = 0;
    with_view_model(instance->view, SubGhzReadRAWModel * model, { p = model->seek_pct; }, false);
    return p;
}

bool subghz_read_raw_is_playback_overdue(SubGhzReadRAW* instance) {
    furi_assert(instance);
    bool overdue = false;
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            if(model->status == SubGhzReadRAWStatusLoadKeyTX) {
                const uint32_t GRACE_TICKS = 3;
                overdue = model->tx_tick >= (model->tx_total_ticks + GRACE_TICKS);
            }
        },
        false);
    return overdue;
}

static void reveal_timer_cb(void* ctx) {
    SubGhzReadRAW* instance = ctx;
    bool done = false;
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            if(model->reveal_count < SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE) {
                uint8_t next = (uint8_t)(model->reveal_count + REVEAL_COLS_PER_TICK);
                if(next > SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE)
                    next = SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE;
                for(uint8_t i = model->reveal_count; i < next; i++) {
                    model->shown_envelope[i] = model->has_envelope ? model->target_envelope[i] : 0;
                }
                model->reveal_count = next;
            }
            done = model->reveal_count >= SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE;
        },
        true);

    if(done) furi_timer_stop(instance->reveal_timer);
}

void subghz_read_raw_set_envelope(
    SubGhzReadRAW* instance,
    const uint8_t* envelope,
    uint32_t total_duration_us) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            if(envelope) {
                memcpy(model->target_envelope, envelope, SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE);
                model->has_envelope = true;
            } else {
                memset(model->target_envelope, 0, SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE);
                model->has_envelope = false;
            }
            memset(model->shown_envelope, 0, SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE);
            model->reveal_count   = 0;
            model->play_pct       = 0;
            model->seek_pct       = 0;
            model->tx_tick        = 0;

            uint32_t ticks = total_duration_us / SCENE_TICK_PERIOD_US;
            model->tx_total_ticks = (ticks > 0) ? ticks : 1;
        },
        true);

    furi_timer_start(instance->reveal_timer, REVEAL_TIMER_MS);
}

void subghz_read_raw_tick_tx(SubGhzReadRAW* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            if(model->status == SubGhzReadRAWStatusLoadKeyTX ||
               model->status == SubGhzReadRAWStatusLoadKeyTXRepeat) {
                model->tx_tick++;
                if(model->tx_total_ticks > 0) {
                    uint32_t pct = (model->tx_tick * 100) / model->tx_total_ticks;
                    model->play_pct = (uint8_t)(pct > 100 ? 100 : pct);
                }
            } else if(model->status == SubGhzReadRAWStatusTX ||
                      model->status == SubGhzReadRAWStatusTXRepeat) {

                model->tx_tick++;
                uint32_t total = model->recording_ticks > 0 ? model->recording_ticks : 1;
                uint32_t pct   = (model->tx_tick * 100) / total;
                model->play_pct = (uint8_t)(pct > 100 ? 100 : pct);
            }
        },
        true);
}

static void subghz_read_raw_draw_scale(Canvas* canvas, SubGhzReadRAWModel* model) {
#define SUBGHZ_RAW_TOP_SCALE 15
#define SUBGHZ_RAW_END_SCALE 112
    if(!model->rssi_history_end) {
        for(int i = SUBGHZ_RAW_END_SCALE; i > 0; i -= 15) {
            canvas_draw_line(canvas, i, SUBGHZ_RAW_TOP_SCALE, i, SUBGHZ_RAW_TOP_SCALE + 4);
            canvas_draw_line(canvas, i - 5, SUBGHZ_RAW_TOP_SCALE, i - 5, SUBGHZ_RAW_TOP_SCALE + 2);
            canvas_draw_line(canvas, i - 10, SUBGHZ_RAW_TOP_SCALE, i - 10, SUBGHZ_RAW_TOP_SCALE + 2);
        }
    } else {
        for(int i = SUBGHZ_RAW_END_SCALE - model->ind_write % 15; i > -15; i -= 15) {
            canvas_draw_line(canvas, i, SUBGHZ_RAW_TOP_SCALE, i, SUBGHZ_RAW_TOP_SCALE + 4);
            if(SUBGHZ_RAW_END_SCALE > i + 5)
                canvas_draw_line(canvas, i + 5, SUBGHZ_RAW_TOP_SCALE, i + 5, SUBGHZ_RAW_TOP_SCALE + 2);
            if(SUBGHZ_RAW_END_SCALE > i + 10)
                canvas_draw_line(canvas, i + 10, SUBGHZ_RAW_TOP_SCALE, i + 10, SUBGHZ_RAW_TOP_SCALE + 2);
        }
    }
}

static void subghz_read_raw_draw_rssi(Canvas* canvas, SubGhzReadRAWModel* model) {
    uint8_t width = 2;

    const uint8_t MARKER_X = 85;

    canvas_draw_line(canvas, 1, 47, 114, 47);

    uint32_t iw = model->ind_write;
    bool wrapped = model->rssi_history_end;

    if(!wrapped && iw < MARKER_X) {

        for(uint32_t i = 0; i <= iw; i++)
            canvas_draw_line(canvas, (uint8_t)i, 47,
                             (uint8_t)i, 47 - model->rssi_history[i]);
    } else {

        uint32_t base_idx = wrapped
            ? (iw + SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE - MARKER_X)
            : (iw - MARKER_X);
        for(uint8_t screen_x = 1; screen_x <= 113; screen_x++) {
            uint32_t hist_idx =
                (base_idx + screen_x) % SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE;
            canvas_draw_line(canvas, screen_x, 47,
                             screen_x, 47 - model->rssi_history[hist_idx]);
        }
    }

    uint8_t px;
    if(model->status == SubGhzReadRAWStatusTX ||
       model->status == SubGhzReadRAWStatusTXRepeat) {
        uint8_t data_end = (!wrapped && iw < MARKER_X) ? (uint8_t)iw : MARKER_X;
        px = (uint8_t)((uint32_t)model->play_pct * data_end / 100);
    } else {
        px = (!wrapped && iw < MARKER_X) ? (uint8_t)iw : MARKER_X;
    }
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_line(canvas, px, 15, px, 47);
    canvas_set_color(canvas, ColorBlack);

    canvas_draw_line(canvas, px, 47, px, 47 - model->rssi_current);

    for(uint8_t i = 15; i < 47; i += width * 2)
        canvas_draw_line(canvas, px, i, px, i + width);

    canvas_draw_line(canvas, px - 2, 13, px + 2, 13);
    canvas_draw_line(canvas, px - 1, 14, px + 1, 14);
}

static void subghz_read_raw_draw_threshold_rssi(Canvas* canvas, SubGhzReadRAWModel* model) {
    uint8_t x = 114;
    uint8_t y = 48;
    if(model->raw_threshold_rssi > SUBGHZ_RAW_THRESHOLD_MIN) {
        y -= (uint8_t)((model->raw_threshold_rssi - SUBGHZ_RAW_THRESHOLD_MIN) / 2.7f);
        uint8_t width = 3;
        for(uint8_t i = 0; i < x; i += width * 2)
            canvas_draw_line(canvas, i, y, i + width, y);
    }
    canvas_draw_line(canvas, x, y - 2, x, y + 2);
    canvas_draw_line(canvas, x - 1, y - 1, x - 1, y + 1);
    canvas_draw_dot(canvas, x - 2, y);
}

static void subghz_read_raw_draw_envelope(Canvas* canvas, SubGhzReadRAWModel* model) {
    const uint8_t W      = 114;
    const uint8_t TOP    = 15;
    const uint8_t BOTTOM = 39;
    const uint8_t BAR_Y  = 44;
    const uint8_t BAR_H  = 3;

    if(!model->has_envelope && model->reveal_count == 0) {

        canvas_draw_line(canvas, 0, BOTTOM, W, BOTTOM);
    } else if(model->viz_mode == SubGhzReadRawVizLine) {
        int prev_x = -1, prev_y = BOTTOM;
        for(uint8_t col = 0; col < model->reveal_count; col++) {
            uint8_t x = (uint8_t)((uint16_t)col * W / 100);
            uint8_t h = (uint8_t)(((uint16_t)model->shown_envelope[col] * (BOTTOM - TOP - 1)) / 255);
            int y = BOTTOM - h;
            if(prev_x >= 0) canvas_draw_line(canvas, prev_x, prev_y, x, y);
            prev_x = x;
            prev_y = y;
        }
        if(model->reveal_count == 0) {
            canvas_draw_line(canvas, 0, BOTTOM, W, BOTTOM);
        }
    } else {
        for(uint8_t col = 0; col < model->reveal_count; col++) {
            uint8_t x = (uint8_t)((uint16_t)col * W / 100);
            uint8_t h = (uint8_t)(((uint16_t)model->shown_envelope[col] * (BOTTOM - TOP - 1)) / 255);
            if(h == 0) continue;
            canvas_draw_line(canvas, x, BOTTOM, x, BOTTOM - h);
        }
    }

        uint8_t cursor_x = (uint8_t)((uint16_t)model->play_pct * W / 100);
    for(uint8_t y = TOP; y < BAR_Y; y += 3)
        canvas_draw_dot(canvas, cursor_x, y);

    canvas_draw_frame(canvas, 0, BAR_Y, W + 2, BAR_H + 2);
    uint8_t fill_w = (uint8_t)((uint16_t)model->play_pct * W / 100);
    if(fill_w > 0) canvas_draw_box(canvas, 1, BAR_Y + 1, fill_w, BAR_H);

        if(model->status == SubGhzReadRAWStatusLoadKeyTXPaused) {
        uint8_t seek_x = (uint8_t)((uint16_t)model->seek_pct * W / 100);
        canvas_draw_dot(canvas, seek_x, BAR_Y - 1);
        canvas_draw_dot(canvas, seek_x - 1, BAR_Y - 2);
        canvas_draw_dot(canvas, seek_x + 1, BAR_Y - 2);
    }

        if(model->zoom_level > 0) {
        char zb[8];
        snprintf(zb, sizeof(zb), "%ux", model->zoom_level + 1);
        canvas_draw_str_aligned(canvas, W - 2, TOP - 1, AlignRight, AlignBottom, zb);
    }
}

static void subghz_read_raw_draw_legacy_bargraph(Canvas* canvas, SubGhzReadRAWModel* model) {
    const uint8_t W   = 114;
    const uint8_t TOP = 14;
    const uint8_t BOT = 47;

    for(uint8_t x = 0; x < W; x++) {
        uint8_t src = (uint8_t)((uint16_t)x * SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE / W);
        uint8_t v = model->rssi_history[src];
        uint8_t h = v;
        if(h > (BOT - TOP - 1)) h = BOT - TOP - 1;
        if(h == 0) continue;
        canvas_draw_line(canvas, x, BOT, x, BOT - h);
    }
}

#define SUBGHZ_READ_RAW_COUNTDOWN_BTN_X_NUDGE 7
static void subghz_read_raw_draw_button_center_nudged(Canvas* canvas, const char* str) {
    const size_t button_height = 12;
    const size_t vertical_offset = 3;
    const size_t horizontal_offset = 1;
    const size_t string_width = canvas_string_width(canvas, str);
    const Icon* icon = &I_ButtonCenter_7x7;
    const int32_t icon_h_offset = 3;
    const int32_t icon_width_with_offset = icon_get_width(icon) + icon_h_offset;
    const int32_t icon_v_offset = icon_get_height(icon) + vertical_offset;
    const size_t button_width = string_width + horizontal_offset * 2 + icon_width_with_offset;

    const int32_t x =
        (canvas_width(canvas) - button_width) / 2 + SUBGHZ_READ_RAW_COUNTDOWN_BTN_X_NUDGE;
    const int32_t y = canvas_height(canvas);

    canvas_draw_box(canvas, x, y - button_height, button_width, button_height);

    canvas_draw_line(canvas, x - 1, y, x - 1, y - button_height + 0);
    canvas_draw_line(canvas, x - 2, y, x - 2, y - button_height + 1);
    canvas_draw_line(canvas, x - 3, y, x - 3, y - button_height + 2);

    canvas_draw_line(canvas, x + button_width + 0, y, x + button_width + 0, y - button_height + 0);
    canvas_draw_line(canvas, x + button_width + 1, y, x + button_width + 1, y - button_height + 1);
    canvas_draw_line(canvas, x + button_width + 2, y, x + button_width + 2, y - button_height + 2);

    canvas_invert_color(canvas);
    canvas_draw_icon(canvas, x + horizontal_offset, y - icon_v_offset, &I_ButtonCenter_7x7);
    canvas_draw_str(
        canvas, x + horizontal_offset + icon_width_with_offset, y - vertical_offset, str);
    canvas_invert_color(canvas);
}

void subghz_read_raw_draw(Canvas* canvas, SubGhzReadRAWModel* model) {
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    if(model->status == SubGhzReadRAWStatusDecoding) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignTop, "Decoding");
        elements_progress_bar(canvas, 14, 28, 100, (float)model->decode_pct / 100.0f);
        canvas_set_font(canvas, FontSecondary);
        char group_str[32];
        snprintf(
            group_str,
            sizeof(group_str),
            "Protocol group %u/%u",
            (unsigned)model->decode_group,
            (unsigned)model->decode_group_total);
        canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignTop, group_str);
        return;
    }

    bool auto_listening =
        (model->status == SubGhzReadRAWStatusREC && model->auto_capture_mode);
    bool auto_start_idle =
        (model->status == SubGhzReadRAWStatusStart && model->auto_capture_mode);
    bool auto_freq_screen = auto_listening || auto_start_idle;

    if(auto_freq_screen) {
        canvas_draw_str_aligned(canvas, 1, 1, AlignLeft, AlignTop, "[Fix]");
        canvas_draw_str_aligned(
            canvas,
            40,
            1,
            AlignCenter,
            AlignTop,
            (model->device_type == SubGhzRadioDeviceTypeInternal) ? "Int" : "Ext");
        char mod_str[24];
        snprintf(mod_str, sizeof(mod_str), "Mod: %s", furi_string_get_cstr(model->preset_str));
        canvas_draw_str_aligned(canvas, 126, 1, AlignRight, AlignTop, mod_str);
    } else {
        canvas_draw_str(canvas, 0, 7, furi_string_get_cstr(model->frequency_str));
        canvas_draw_str(canvas, 35, 7, furi_string_get_cstr(model->preset_str));

        if(model->not_showing_samples) {
            canvas_draw_str(canvas, 77, 7,
                (model->device_type == SubGhzRadioDeviceTypeInternal) ? "R: Int" : "R: Ext");
        } else {
            canvas_draw_str(canvas, 70, 7,
                (model->device_type == SubGhzRadioDeviceTypeInternal) ? "I" : "E");
        }

        canvas_draw_str_aligned(canvas, 126, 0, AlignRight, AlignTop,
                                furi_string_get_cstr(model->sample_write));
    }

    if(auto_freq_screen) {
        canvas_draw_line(canvas, 0, 36, 127, 36);
        canvas_set_font(canvas, FontPrimary);
        char freq_mhz[24];
        snprintf(
            freq_mhz, sizeof(freq_mhz), "%s MHz", furi_string_get_cstr(model->frequency_str));
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, freq_mhz);
        canvas_set_font(canvas, FontSecondary);
    } else {

        canvas_draw_line(canvas, 0, 48, 115, 48);
        canvas_draw_line(canvas, 115, 14, 115, 48);
        canvas_draw_line(canvas, 0, 14, 0, 48);
    }

    uint8_t signal_mode = 1;

    switch(model->status) {
    case SubGhzReadRAWStatusIDLE:
        elements_button_left(canvas, "Erase");
        elements_button_center(canvas, "Send");
        elements_button_right(canvas, "Save");
        break;

    case SubGhzReadRAWStatusLoadKeyIDLE: {

        signal_mode = 2;
        if(model->allow_new) {
            elements_button_left(canvas, "New");
        }
        elements_button_right(canvas, "More");
        elements_button_center(canvas, "Send");

        break;
    }

    case SubGhzReadRAWStatusLoadKeyTX:
    case SubGhzReadRAWStatusLoadKeyTXRepeat:
        signal_mode = 2;
        elements_button_center(canvas, "Pause");
        break;

    case SubGhzReadRAWStatusLoadKeyTXPaused:
        signal_mode = 2;
        elements_button_left(canvas, "Rew");
        elements_button_center(canvas, "Resume");
        elements_button_right(canvas, "Fwd");

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 57, 16, AlignCenter, AlignTop, "PAUSED");
        canvas_set_font(canvas, FontSecondary);
        break;

    case SubGhzReadRAWStatusTX:
    case SubGhzReadRAWStatusTXRepeat:

        signal_mode = 1;
        elements_button_center(canvas, "Hold to repeat");
        break;

    case SubGhzReadRAWStatusStart:
        elements_button_left(canvas, "Config");
        if(model->start_countdown_sec > 0) {
            char start_label[16];
            snprintf(start_label, sizeof(start_label), "Start (%u)", model->start_countdown_sec);
            subghz_read_raw_draw_button_center_nudged(canvas, start_label);
        } else {
            elements_button_center(canvas, "Start");
        }
        signal_mode = 3;
        break;

    case SubGhzReadRAWStatusREC:
        if(auto_listening) {
            elements_button_left(canvas, "Config");
            signal_mode = 3;
        } else {
            elements_button_center(canvas, "Stop");
        }
        break;

    default:
        elements_button_center(canvas, "Stop");
        break;
    }

    if(!auto_listening) {
        uint32_t timer_ticks = 0;
        bool show_timer = false;

        if(model->status == SubGhzReadRAWStatusTX         ||
           model->status == SubGhzReadRAWStatusTXRepeat    ||
           model->status == SubGhzReadRAWStatusLoadKeyTX   ||
           model->status == SubGhzReadRAWStatusLoadKeyTXRepeat ||
           model->status == SubGhzReadRAWStatusLoadKeyTXPaused) {
            timer_ticks = model->tx_tick;
            show_timer  = true;
        } else if(model->recording_ticks > 0) {
            timer_ticks = model->recording_ticks;
            show_timer  = true;
        }

        if(show_timer) {
            uint32_t secs = timer_ticks / 10;
            uint8_t  mins = (uint8_t)(secs / 60 > 99 ? 99 : secs / 60);
            uint8_t  sec2 = (uint8_t)(secs % 60);
            uint8_t  decs = (uint8_t)(timer_ticks % 10);
            char timer_str[10];
            snprintf(timer_str, sizeof(timer_str), "%u:%02u.%u",
                     (unsigned)mins, (unsigned)sec2, (unsigned)decs);
            canvas_set_color(canvas, ColorBlack);
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str_aligned(canvas, 113, 17, AlignRight, AlignTop, timer_str);
        }
    }

    if(signal_mode == 0) {
        subghz_read_raw_draw_legacy_bargraph(canvas, model);
    } else if(signal_mode == 2) {

        subghz_read_raw_draw_scale(canvas, model);
        subghz_read_raw_draw_envelope(canvas, model);
    } else if(signal_mode == 1) {

        subghz_read_raw_draw_rssi(canvas, model);
        subghz_read_raw_draw_scale(canvas, model);
        subghz_read_raw_draw_threshold_rssi(canvas, model);
        canvas_set_font_direction(canvas, CanvasDirectionBottomToTop);
        canvas_draw_str(canvas, 126, 40, "RSSI");
        canvas_set_font_direction(canvas, CanvasDirectionLeftToRight);
    }

}

bool subghz_read_raw_input(InputEvent* event, void* context) {
    furi_assert(context);
    SubGhzReadRAW* instance = context;

    bool decoding = false;
    with_view_model(instance->view, SubGhzReadRAWModel * model, {
        decoding = (model->status == SubGhzReadRAWStatusDecoding);
    }, false);
    if(decoding) {
        if(event->key == InputKeyBack && event->type == InputTypeShort) {
            instance->callback(SubGhzCustomEventViewReadRAWBack, instance->context);
        }
        return true;
    }

    if(event->key == InputKeyOk && event->type == InputTypePress) {
        uint8_t ret = false;
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            switch(model->status) {
            case SubGhzReadRAWStatusIDLE:
                instance->callback(SubGhzCustomEventViewReadRAWSendStart, instance->context);
                model->status   = SubGhzReadRAWStatusTXRepeat;
                model->play_pct = 0;
                model->tx_tick  = 0;
                ret = true;
                break;
            case SubGhzReadRAWStatusTX:
                model->status = SubGhzReadRAWStatusTXRepeat;
                break;

            case SubGhzReadRAWStatusLoadKeyIDLE:
                instance->callback(SubGhzCustomEventViewReadRAWSendStart, instance->context);
                model->status   = SubGhzReadRAWStatusLoadKeyTX;
                model->play_pct = 0;
                model->seek_pct = 0;
                model->tx_tick  = 0;
                ret = true;
                break;
            case SubGhzReadRAWStatusLoadKeyTX:
                instance->callback(SubGhzCustomEventViewReadRAWTXPause, instance->context);
                model->status   = SubGhzReadRAWStatusLoadKeyTXPaused;
                model->seek_pct = model->play_pct;
                ret = true;
                break;
            case SubGhzReadRAWStatusLoadKeyTXPaused:
                instance->callback(SubGhzCustomEventViewReadRAWTXResume, instance->context);
                model->status   = SubGhzReadRAWStatusLoadKeyTX;
                model->play_pct = model->seek_pct;
                model->tx_tick  = (uint32_t)model->seek_pct * model->tx_total_ticks / 100;
                ret = true;
                break;
            default:
                break;
            }
        }, ret);
        return true;
    }

    if(event->key == InputKeyOk && event->type == InputTypeRelease) {
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            if(model->status == SubGhzReadRAWStatusTXRepeat)
                model->status = SubGhzReadRAWStatusTX;
        }, false);
        return true;
    }

    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            switch(model->status) {
            case SubGhzReadRAWStatusREC:
                instance->callback(SubGhzCustomEventViewReadRAWIDLE, instance->context);

                if(!instance->auto_capture_mode) {
                    model->status = SubGhzReadRAWStatusIDLE;
                }
                break;
            case SubGhzReadRAWStatusLoadKeyTX:
            case SubGhzReadRAWStatusLoadKeyTXRepeat:
            case SubGhzReadRAWStatusLoadKeyTXPaused:
                instance->callback(SubGhzCustomEventViewReadRAWTXRXStop, instance->context);
                model->status = SubGhzReadRAWStatusLoadKeyIDLE;
                break;
            case SubGhzReadRAWStatusTX:
                instance->callback(SubGhzCustomEventViewReadRAWTXRXStop, instance->context);
                model->status = SubGhzReadRAWStatusIDLE;
                break;
            case SubGhzReadRAWStatusLoadKeyIDLE:
                instance->callback(SubGhzCustomEventViewReadRAWBack, instance->context);
                break;
            default:
                instance->callback(SubGhzCustomEventViewReadRAWBack, instance->context);
                break;
            }
        }, true);
        return true;
    }

        if(event->key == InputKeyLeft &&
       (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
        bool fire_erase = false;
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            if(model->status == SubGhzReadRAWStatusLoadKeyTXPaused) {
                if(model->seek_pct >= SEEK_STEP_PCT) model->seek_pct -= SEEK_STEP_PCT;
                else model->seek_pct = 0;
            } else if(!model->raw_send_only) {
                if(model->status == SubGhzReadRAWStatusStart) {
                    instance->callback(SubGhzCustomEventViewReadRAWConfig, instance->context);
                } else if(model->status == SubGhzReadRAWStatusREC) {

                    instance->callback(SubGhzCustomEventViewReadRAWConfig, instance->context);
                } else if(model->status == SubGhzReadRAWStatusIDLE) {
                    model->status = SubGhzReadRAWStatusStart;
                    model->rssi_history_end = false;
                    model->ind_write = 0;
                    model->not_showing_samples = true;
                    model->recording_ticks = 0;
                    furi_string_set(model->sample_write, "0 spl.");
                    furi_string_reset(model->file_name);
                    fire_erase = true;
                } else if(model->status == SubGhzReadRAWStatusLoadKeyIDLE && model->allow_new) {
                    model->status = SubGhzReadRAWStatusStart;
                    model->rssi_history_end = false;
                    model->ind_write = 0;
                    model->not_showing_samples = true;
                    model->recording_ticks = 0;
                    furi_string_set(model->sample_write, "0 spl.");
                    furi_string_reset(model->file_name);
                    fire_erase = true;
                }

            }
        }, true);
        if(fire_erase) instance->callback(SubGhzCustomEventViewReadRAWErase, instance->context);
        return true;
    }

    if(event->key == InputKeyRight &&
       (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            if(model->status == SubGhzReadRAWStatusLoadKeyTXPaused) {
                if(model->seek_pct + SEEK_STEP_PCT <= 95) model->seek_pct += SEEK_STEP_PCT;
                else model->seek_pct = 95;
            } else if(model->status == SubGhzReadRAWStatusIDLE && !model->raw_send_only) {
                instance->callback(SubGhzCustomEventViewReadRAWSave, instance->context);
            } else if(model->status == SubGhzReadRAWStatusLoadKeyIDLE) {

                instance->callback(SubGhzCustomEventViewReadRAWMore, instance->context);
            }
        }, true);
        return true;
    }

    if((event->key == InputKeyUp || event->key == InputKeyDown) &&
       (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
        bool paused = false;
        bool zoom_in = (event->key == InputKeyUp);
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            paused = (model->status == SubGhzReadRAWStatusLoadKeyTXPaused);
            if(paused) {
                if(zoom_in && (unsigned)model->zoom_level + 1u < ZOOM_LEVEL_COUNT) {
                    model->zoom_level++;
                } else if(!zoom_in && model->zoom_level > 0) {
                    model->zoom_level--;
                }
            }
        }, false);
        if(paused && instance->callback) {
            instance->callback(
                zoom_in ? SubGhzCustomEventViewReadRAWZoomIn : SubGhzCustomEventViewReadRAWZoomOut,
                instance->context);
        }
        return true;
    }

    if(event->key == InputKeyOk && event->type == InputTypeShort) {
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            if(model->status == SubGhzReadRAWStatusStart) {
                instance->callback(SubGhzCustomEventViewReadRAWREC, instance->context);
                model->status = SubGhzReadRAWStatusREC;
                model->ind_write = 0;
                model->rssi_history_end = false;
                model->recording_ticks = 0;
            } else if(model->status == SubGhzReadRAWStatusREC) {
                instance->callback(SubGhzCustomEventViewReadRAWIDLE, instance->context);
                if(!instance->auto_capture_mode) {
                    model->status = SubGhzReadRAWStatusIDLE;
                }
            }
        }, true);
        return true;
    }

    return true;
}

void subghz_read_raw_set_decoding(
    SubGhzReadRAW* instance,
    uint8_t group,
    uint8_t group_total,
    uint8_t pct) {
    furi_assert(instance);
    with_view_model(instance->view, SubGhzReadRAWModel * model, {
        model->status = SubGhzReadRAWStatusDecoding;
        model->decode_group = group;
        model->decode_group_total = group_total;
        model->decode_pct = (pct > 100) ? 100 : pct;
    }, true);
}

void subghz_read_raw_clear_decoding(SubGhzReadRAW* instance) {
    furi_assert(instance);
    with_view_model(instance->view, SubGhzReadRAWModel * model, {
        if(model->status == SubGhzReadRAWStatusDecoding) {
            model->status = SubGhzReadRAWStatusIDLE;
        }
    }, false);
}

void subghz_read_raw_set_status(
    SubGhzReadRAW* instance,
    SubGhzReadRAWStatus status,
    const char* file_name,
    float raw_threshold_rssi) {
    furi_assert(instance);
    switch(status) {
    case SubGhzReadRAWStatusStart:
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            model->status = SubGhzReadRAWStatusStart;
            model->rssi_history_end = false;
            model->ind_write = 0;
            model->not_showing_samples = true;
            model->recording_ticks = 0;
            furi_string_reset(model->file_name);
            furi_string_set(model->sample_write, "0 spl.");
            model->raw_threshold_rssi = raw_threshold_rssi;
        }, true);
        break;
    case SubGhzReadRAWStatusIDLE:
        with_view_model(instance->view, SubGhzReadRAWModel * model,
            { model->status = SubGhzReadRAWStatusIDLE; }, true);
        break;
    case SubGhzReadRAWStatusREC:

        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            model->status = SubGhzReadRAWStatusREC;
            model->rssi_history_end = false;
            model->ind_write = 0;
            model->not_showing_samples = true;
            model->recording_ticks = 0;
            furi_string_reset(model->file_name);
            furi_string_set(model->sample_write, "0 spl.");
            model->raw_threshold_rssi = raw_threshold_rssi;
        }, true);
        break;
    case SubGhzReadRAWStatusLoadKeyTX:
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            model->status = SubGhzReadRAWStatusLoadKeyIDLE;
            model->rssi_history_end = false;
            model->ind_write = 0;
            model->not_showing_samples = true;
            model->play_pct = 0;
            model->seek_pct = 0;
            model->tx_tick  = 0;
            furi_string_set(model->file_name, file_name);
            furi_string_set(model->sample_write, "RAW");
        }, true);
        break;
    case SubGhzReadRAWStatusSaveKey:
        with_view_model(instance->view, SubGhzReadRAWModel * model, {
            model->status = SubGhzReadRAWStatusLoadKeyIDLE;
            if(!model->ind_write) {
                model->not_showing_samples = true;
                furi_string_set(model->file_name, file_name);
                furi_string_set(model->sample_write, "RAW");
            } else {
                furi_string_reset(model->file_name);
            }
        }, true);
        break;
    default:
        FURI_LOG_W(TAG, "unknown status");
        break;
    }
}

void subghz_read_raw_enter(void* context) { UNUSED(context); }

void subghz_read_raw_exit(void* context) {
    furi_assert(context);
    SubGhzReadRAW* instance = context;
    with_view_model(instance->view, SubGhzReadRAWModel * model, {
        if(model->status != SubGhzReadRAWStatusIDLE &&
           model->status != SubGhzReadRAWStatusStart &&
           model->status != SubGhzReadRAWStatusLoadKeyIDLE &&
           model->status != SubGhzReadRAWStatusDecoding) {
            instance->callback(SubGhzCustomEventViewReadRAWIDLE, instance->context);
            model->status = SubGhzReadRAWStatusStart;
        }
    }, true);
}

SubGhzReadRAW* subghz_read_raw_alloc(bool raw_send_only) {
    SubGhzReadRAW* instance = malloc(sizeof(SubGhzReadRAW));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SubGhzReadRAWModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, (ViewDrawCallback)subghz_read_raw_draw);
    view_set_input_callback(instance->view, subghz_read_raw_input);
    view_set_enter_callback(instance->view, subghz_read_raw_enter);
    view_set_exit_callback(instance->view, subghz_read_raw_exit);

    with_view_model(instance->view, SubGhzReadRAWModel * model, {
        model->frequency_str = furi_string_alloc();
        model->preset_str    = furi_string_alloc();
        model->sample_write  = furi_string_alloc();
        model->file_name     = furi_string_alloc();
        model->raw_send_only = raw_send_only;
        model->allow_new     = false;
        model->rssi_history  = malloc(SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE * sizeof(uint8_t));
        model->raw_threshold_rssi = -127.0f;
        model->viz_mode       = SubGhzReadRawVizBar;
        model->has_envelope   = false;
        model->reveal_count   = 0;
        model->play_pct       = 0;
        model->seek_pct       = 0;
        model->tx_tick        = 0;
        model->tx_total_ticks = 1;
        model->zoom_level     = 0;
        model->auto_capture_mode = false;
    }, true);

    instance->reveal_timer = furi_timer_alloc(reveal_timer_cb, FuriTimerTypePeriodic, instance);

    return instance;
}

void subghz_read_raw_free(SubGhzReadRAW* instance) {
    furi_assert(instance);
    furi_timer_stop(instance->reveal_timer);
    furi_timer_free(instance->reveal_timer);
    with_view_model(instance->view, SubGhzReadRAWModel * model, {
        furi_string_free(model->frequency_str);
        furi_string_free(model->preset_str);
        furi_string_free(model->sample_write);
        furi_string_free(model->file_name);
        free(model->rssi_history);
    }, true);
    view_free(instance->view);
    free(instance);
}

View* subghz_read_raw_get_view(SubGhzReadRAW* instance) {
    furi_assert(instance);
    return instance->view;
}

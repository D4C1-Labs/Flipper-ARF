#include "transmitter.h"

#include <assets_icons.h>
#include <input/input.h>
#include <gui/elements.h>

#include "../helpers/subghz_custom_btn_compat.h"
#include <string.h>

struct SubGhzViewTransmitter {
    View* view;
    SubGhzViewTransmitterCallback callback;
    void* context;
};

typedef struct {
    FuriString* frequency_str;
    FuriString* preset_str;
    FuriString* key_str;
    bool show_button;
    SubGhzRadioDeviceType device_type;
    FuriString* temp_button_id;
    bool draw_temp_button;

    char btn_center[14];
    char btn_up[14];
    char btn_down[14];
    char btn_left[14];
    char btn_right[14];
    bool btn_up_vis;
    bool btn_down_vis;
    bool btn_left_vis;
    bool btn_right_vis;
    bool labels_ready;
} SubGhzViewTransmitterModel;

void subghz_view_transmitter_set_callback(
    SubGhzViewTransmitter* subghz_transmitter,
    SubGhzViewTransmitterCallback callback,
    void* context) {
    furi_assert(subghz_transmitter);

    subghz_transmitter->callback = callback;
    subghz_transmitter->context = context;
}

void subghz_view_transmitter_add_data_to_show(
    SubGhzViewTransmitter* subghz_transmitter,
    const char* key_str,
    const char* frequency_str,
    const char* preset_str,
    bool show_button) {
    furi_assert(subghz_transmitter);
    with_view_model(
        subghz_transmitter->view,
        SubGhzViewTransmitterModel * model,
        {
            furi_string_set(model->key_str, key_str);
            furi_string_set(model->frequency_str, frequency_str);
            furi_string_set(model->preset_str, preset_str);
            model->show_button   = show_button;
            model->draw_temp_button = false;
            furi_string_reset(model->temp_button_id);
        },
        true);
}

void subghz_view_transmitter_set_radio_device_type(
    SubGhzViewTransmitter* subghz_transmitter,
    SubGhzRadioDeviceType device_type) {
    furi_assert(subghz_transmitter);
    with_view_model(
        subghz_transmitter->view,
        SubGhzViewTransmitterModel * model,
        { model->device_type = device_type; },
        true);
}

static void txv_first_line(const char* s, char* d, size_t n) {
    size_t i = 0;
    while(s[i] && s[i] != '\n' && i < n-1) { d[i]=s[i]; i++; }
    d[i]=0;
}
static bool txv_after(const char* s, const char* key, char* d, size_t n) {
    const char* p = strstr(s, key);
    if(!p) { d[0]=0; return false; }
    p += strlen(key);
    size_t i=0;
    while(p[i] && p[i]!='\n' && p[i]!=' ' && p[i]!='\t' && i<n-1) { d[i]=p[i]; i++; }
    d[i]=0;
    return i>0;
}
bool txv_btn_label_extract(const char* s, char* d, size_t n) {
    const char* btn = strstr(s, "Btn:");
    const char* p = btn ? strstr(btn, "[") : strstr(s, "[");
    if(!p) { d[0]=0; return false; }
    p++; size_t i=0;
    while(p[i] && p[i]!=']' && i<n-1) { d[i]=p[i]; i++; }
    d[i]=0;
    return i>0;
}

static void txv_btn_box(
    Canvas* canvas, uint8_t x, uint8_t y, uint8_t w, uint8_t h,
    const char* label, bool filled) {
    if(filled) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rbox(canvas, x, y, w, h, 3);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, x, y, w, h, 3);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, x+w/2, y+h/2+1, AlignCenter, AlignCenter, label);
    canvas_set_color(canvas, ColorBlack);
}

void subghz_view_transmitter_draw(Canvas* canvas, SubGhzViewTransmitterModel* model) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    const char* key  = furi_string_get_cstr(model->key_str);
    const char* freq = furi_string_get_cstr(model->frequency_str);
    const char* mod  = furi_string_get_cstr(model->preset_str);
    bool has_custom  = subghz_custom_btn_is_allowed();

        char proto[40] = {0};
    txv_first_line(key, proto, sizeof(proto));
    canvas_draw_box(canvas, 0, 0, 128, 11);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 6, AlignCenter, AlignCenter, proto);
    canvas_set_color(canvas, ColorBlack);

        char sn[12]={0}, cnt[12]={0};
    txv_after(key, "Sn:", sn, sizeof(sn));
    if(!sn[0]) txv_after(key, "SN:", sn, sizeof(sn));
    txv_after(key, "Cnt:", cnt, sizeof(cnt));
    if(!cnt[0]) txv_after(key, "CNT:", cnt, sizeof(cnt));
    canvas_set_font(canvas, FontSecondary);
    if(sn[0] || cnt[0]) {
        char line2[40];
        snprintf(line2, sizeof(line2), "SN:%.8s   CNT:%.6s", sn, cnt);
        canvas_draw_str_aligned(canvas, 64, 13, AlignCenter, AlignTop, line2);
    }

    char crc_short[8] = {0};
    const char* crc_p2 = strstr(key, "CRC:");
    if(crc_p2) {
        size_t ci = 0;
        crc_p2 += 4;
        while(crc_p2[ci] && crc_p2[ci] != ' ' && crc_p2[ci] != '\n' && ci < sizeof(crc_short)-1)
            { crc_short[ci] = crc_p2[ci]; ci++; }
    }
    char line3[40];
    if(crc_short[0])
        snprintf(line3, sizeof(line3), "F:%-7s %s  CRC:%s", freq, mod, crc_short);
    else
        snprintf(line3, sizeof(line3), "F:%-10s %s", freq, mod);
    canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, line3);

    if(has_custom) {
        const uint8_t bw=34, bh=9;
        const uint8_t cx=47, lx=4, rx=90;
        const uint8_t row1=31, row2=43, row3=55;

        const char* clabel = model->labels_ready ? model->btn_center : "SEND";
        const char* ulabel = model->labels_ready ? model->btn_up    : "UP";
        const char* dlabel = model->labels_ready ? model->btn_down  : "DOWN";
        const char* llabel = model->labels_ready ? model->btn_left  : "LEFT";
        const char* rlabel = model->labels_ready ? model->btn_right : "RIGHT";

        bool up_vis = !model->labels_ready || model->btn_up_vis;
        bool dn_vis = !model->labels_ready || model->btn_down_vis;
        bool lt_vis = !model->labels_ready || model->btn_left_vis;
        bool rt_vis = !model->labels_ready || model->btn_right_vis;

        if(up_vis) txv_btn_box(canvas, cx, row1, bw, bh, ulabel, false);
        if(lt_vis) txv_btn_box(canvas, lx, row2, bw, bh, llabel, false);
        txv_btn_box(canvas, cx, row2, bw, bh, clabel, true);
        if(rt_vis) txv_btn_box(canvas, rx, row2, bw, bh, rlabel, false);
        if(dn_vis) txv_btn_box(canvas, cx, row3, bw, bh, dlabel, false);

    } else if(model->show_button) {

        const uint8_t bw=34, bh=9;
        const uint8_t cx=47, lx=4, rx=90;
        const uint8_t row1=31, row2=43, row3=55;
        txv_btn_box(canvas, cx, row1, bw, bh, "", false);
        txv_btn_box(canvas, lx, row2, bw, bh, "", false);
        txv_btn_box(canvas, cx, row2, bw, bh, "SEND", true);
        txv_btn_box(canvas, rx, row2, bw, bh, "", false);
        txv_btn_box(canvas, cx, row3, bw, bh, "", false);
    }

    if(model->draw_temp_button) {

#ifdef SUBGHZ_GARAGE_HAS_FONT_BATTERY_PERCENT
        canvas_set_font(canvas, FontBatteryPercent);
#else
        canvas_set_font(canvas, FontSecondary);
#endif
        canvas_draw_str(canvas, 117, 40, furi_string_get_cstr(model->temp_button_id));
    }
}

bool subghz_view_transmitter_input(InputEvent* event, void* context) {
    furi_assert(context);
    SubGhzViewTransmitter* subghz_transmitter = context;
    bool can_be_sent = false;

    if(event->key == InputKeyBack && event->type == InputTypeLong) {

        with_view_model(
            subghz_transmitter->view,
            SubGhzViewTransmitterModel * model,
            {
                furi_string_reset(model->frequency_str);
                furi_string_reset(model->preset_str);
                furi_string_reset(model->key_str);
                furi_string_reset(model->temp_button_id);
                model->show_button = false;
                model->draw_temp_button = false;
            },
            false);
        return false;
    }

    with_view_model(
        subghz_transmitter->view,
        SubGhzViewTransmitterModel * model,
        {
            if(model->show_button) {
                can_be_sent = true;
            }
        },
        true);

    if(can_be_sent) {

        if(event->type == InputTypeLong) {
            if(event->key == InputKeyUp) {
                subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_UP);
                subghz_custom_btn_set_long(true);
            } else if(event->key == InputKeyDown) {
                subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_DOWN);
                subghz_custom_btn_set_long(true);
            } else if(event->key == InputKeyLeft) {
                subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_LEFT);
                subghz_custom_btn_set_long(true);
            } else if(event->key == InputKeyRight) {
                subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_RIGHT);
                subghz_custom_btn_set_long(true);
            }
        }

        if(event->key == InputKeyOk) {
            if(event->type == InputTypePress) {
                if(subghz_custom_btn_has_pages()) {

                    uint8_t max_pages = subghz_custom_btn_get_max_pages();
                    uint8_t next_page = (subghz_custom_btn_get_page() + 1) % max_pages;
                    subghz_custom_btn_set_page(next_page);

                    subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_OK);
                    with_view_model(
                        subghz_transmitter->view,
                        SubGhzViewTransmitterModel * model,
                        {
                            furi_string_reset(model->temp_button_id);
                            furi_string_printf(model->temp_button_id, "P%u", next_page + 1);
                            model->draw_temp_button = true;
                        },
                        true);

                    subghz_transmitter->callback(
                        SubGhzCustomEventViewTransmitterPageChange, subghz_transmitter->context);
                    return true;
                }

                subghz_custom_btn_set(SUBGHZ_CUSTOM_BTN_OK);
                with_view_model(
                    subghz_transmitter->view,
                    SubGhzViewTransmitterModel * model,
                    {
                        furi_string_reset(model->temp_button_id);
                        model->draw_temp_button = false;
                    },
                    true);
                subghz_transmitter->callback(
                    SubGhzCustomEventViewTransmitterSendStart, subghz_transmitter->context);
                return true;
            } else if(event->type == InputTypeRelease) {

                if(!subghz_custom_btn_has_pages()) {
                    subghz_transmitter->callback(
                        SubGhzCustomEventViewTransmitterSendStop, subghz_transmitter->context);
                }
                return true;
            }
        }

        if(subghz_custom_btn_is_allowed()) {
            uint8_t temp_btn_id;
            if(event->key == InputKeyUp) {
                temp_btn_id = SUBGHZ_CUSTOM_BTN_UP;
            } else if(event->key == InputKeyDown) {
                temp_btn_id = SUBGHZ_CUSTOM_BTN_DOWN;
            } else if(event->key == InputKeyLeft) {
                temp_btn_id = SUBGHZ_CUSTOM_BTN_LEFT;
            } else if(event->key == InputKeyRight) {
                temp_btn_id = SUBGHZ_CUSTOM_BTN_RIGHT;
            } else {

                return true;
            }

            if(event->type == InputTypePress) {
                with_view_model(
                    subghz_transmitter->view,
                    SubGhzViewTransmitterModel * model,
                    {
                        furi_string_reset(model->temp_button_id);
                        if(subghz_custom_btn_get_original() != 0) {
                            if(subghz_custom_btn_set(temp_btn_id)) {
                                furi_string_printf(
                                    model->temp_button_id,
                                    "%01X",
                                    subghz_custom_btn_get_original());
                                model->draw_temp_button = true;
                            }
                        }
                    },
                    true);
                subghz_transmitter->callback(
                    SubGhzCustomEventViewTransmitterSendStart, subghz_transmitter->context);
                return true;
            } else if(event->type == InputTypeRelease) {
                subghz_transmitter->callback(
                    SubGhzCustomEventViewTransmitterSendStop, subghz_transmitter->context);
                return true;
            }
        }
    }

    return true;
}

void subghz_view_transmitter_set_btn_labels(
    SubGhzViewTransmitter* t,
    const char* center,
    const char* up,   bool up_vis,
    const char* down, bool down_vis,
    const char* left, bool left_vis,
    const char* right, bool right_vis) {
    furi_assert(t);
    with_view_model(
        t->view,
        SubGhzViewTransmitterModel * model,
        {
            snprintf(model->btn_center, sizeof(model->btn_center), "%s", center ? center : "OK");
            snprintf(model->btn_up,     sizeof(model->btn_up),     "%s", up     ? up     : "UP");
            snprintf(model->btn_down,   sizeof(model->btn_down),   "%s", down   ? down   : "DOWN");
            snprintf(model->btn_left,   sizeof(model->btn_left),   "%s", left   ? left   : "LEFT");
            snprintf(model->btn_right,  sizeof(model->btn_right),  "%s", right  ? right  : "RIGHT");
            model->btn_up_vis    = up_vis;
            model->btn_down_vis  = down_vis;
            model->btn_left_vis  = left_vis;
            model->btn_right_vis = right_vis;
            model->labels_ready  = true;
        },
        true);
}

void subghz_view_transmitter_reset_labels(SubGhzViewTransmitter* t) {
    furi_assert(t);
    with_view_model(
        t->view,
        SubGhzViewTransmitterModel * model,
        {
            model->labels_ready     = false;
            model->draw_temp_button = false;
            furi_string_reset(model->temp_button_id);
        },
        true);
}

void subghz_view_transmitter_update_direction(
    SubGhzViewTransmitter* t, uint8_t btn_id, const char* label) {
    furi_assert(t);
    with_view_model(
        t->view,
        SubGhzViewTransmitterModel * model,
        {
            bool dup = (strcmp(label, model->btn_center) == 0);
            switch(btn_id) {
            case SUBGHZ_CUSTOM_BTN_UP:
                snprintf(model->btn_up,    sizeof(model->btn_up),    "%s", label);
                model->btn_up_vis    = !dup; break;
            case SUBGHZ_CUSTOM_BTN_DOWN:
                snprintf(model->btn_down,  sizeof(model->btn_down),  "%s", label);
                model->btn_down_vis  = !dup; break;
            case SUBGHZ_CUSTOM_BTN_LEFT:
                snprintf(model->btn_left,  sizeof(model->btn_left),  "%s", label);
                model->btn_left_vis  = !dup; break;
            case SUBGHZ_CUSTOM_BTN_RIGHT:
                snprintf(model->btn_right, sizeof(model->btn_right), "%s", label);
                model->btn_right_vis = !dup; break;
            default: break;
            }
        },
        true);
}

bool subghz_view_transmitter_is_labels_ready(SubGhzViewTransmitter* t) {
    bool ready = false;
    furi_assert(t);
    with_view_model(
        t->view, SubGhzViewTransmitterModel * model, { ready = model->labels_ready; }, false);
    return ready;
}

void subghz_view_transmitter_enter(void* context) {
    furi_assert(context);
}

void subghz_view_transmitter_exit(void* context) {
    furi_assert(context);
}

SubGhzViewTransmitter* subghz_view_transmitter_alloc(void) {
    SubGhzViewTransmitter* subghz_transmitter = malloc(sizeof(SubGhzViewTransmitter));

    subghz_transmitter->view = view_alloc();
    view_allocate_model(
        subghz_transmitter->view, ViewModelTypeLocking, sizeof(SubGhzViewTransmitterModel));
    view_set_context(subghz_transmitter->view, subghz_transmitter);
    view_set_draw_callback(
        subghz_transmitter->view, (ViewDrawCallback)subghz_view_transmitter_draw);
    view_set_input_callback(subghz_transmitter->view, subghz_view_transmitter_input);
    view_set_enter_callback(subghz_transmitter->view, subghz_view_transmitter_enter);
    view_set_exit_callback(subghz_transmitter->view, subghz_view_transmitter_exit);

    with_view_model(
        subghz_transmitter->view,
        SubGhzViewTransmitterModel * model,
        {
            model->frequency_str = furi_string_alloc();
            model->preset_str = furi_string_alloc();
            model->key_str = furi_string_alloc();
            model->temp_button_id = furi_string_alloc();
        },
        true);
    return subghz_transmitter;
}

void subghz_view_transmitter_free(SubGhzViewTransmitter* subghz_transmitter) {
    furi_assert(subghz_transmitter);

    with_view_model(
        subghz_transmitter->view,
        SubGhzViewTransmitterModel * model,
        {
            furi_string_free(model->frequency_str);
            furi_string_free(model->preset_str);
            furi_string_free(model->key_str);
            furi_string_free(model->temp_button_id);
        },
        true);
    view_free(subghz_transmitter->view);
    free(subghz_transmitter);
}

View* subghz_view_transmitter_get_view(SubGhzViewTransmitter* subghz_transmitter) {
    furi_assert(subghz_transmitter);
    return subghz_transmitter->view;
}

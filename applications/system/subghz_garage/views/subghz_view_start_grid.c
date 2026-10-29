#include "subghz_view_start_grid.h"
#include <gui/elements.h>
#include <gui/icon.h>
#include <furi.h>

extern const Icon I_btn_read_10x10;
extern const Icon I_btn_saved_10x10;
extern const Icon I_btn_readraw_10x10;
extern const Icon I_btn_frequencyanalyzer_10x10;
extern const Icon I_btn_modulationanalyzer_10x10;
extern const Icon I_btn_protocols_10x10;

#define BTN_H        26
#define BTN_R         5
#define ROW_TOP_Y     4
#define ROW_BOT_Y    34
#define ICON_SIZE    10
#define ICON_PAD_TOP  3
#define ICON_GAP      2
#define TEXT_Y_OFF   21

#define LX  1
#define LW  61
#define RX  66
#define RW  61
#define FW 126

#define NN 0xFF

typedef struct {
    uint8_t      col;
    uint8_t      row;
    bool         full;
    const char*  label;
    uint32_t     event;
    uint8_t      nav[4];
    const Icon*  icon;
} SubGhzGridBtnDef;

static const SubGhzGridBtnDef k_btns[SGRID_BTN_COUNT] = {

    {0,  0, false,"Read",               10, {NN,  2, NN,  1}, &I_btn_read_10x10},
    {1,  0, false,"Saved",              11, {NN,  2,  0, NN}, &I_btn_saved_10x10},
    {0,  1, true, "Read Raw",           15, { 0,  3, NN, NN}, &I_btn_readraw_10x10},
    {0,  2, true, "Frequency Analyzer", 13, { 2,  4, NN, NN}, &I_btn_frequencyanalyzer_10x10},
    {0,  3, true, "Modulation Analyzer",14, { 3,  5, NN, NN}, &I_btn_modulationanalyzer_10x10},
    {0,  4, true, "Protocol Group",     17, { 4, NN, NN, NN}, &I_btn_protocols_10x10},
};

#define TOTAL_ROWS 5

typedef struct {
    uint8_t selected;
    uint8_t window_row;
    bool    visible[SGRID_BTN_COUNT];
} SubGhzStartGridModel;

struct SubGhzStartGrid {
    View*                    view;
    SubGhzStartGridCallback  callback;
    void*                    context;
};

static uint8_t sgrid_nav(const bool* vis, uint8_t from, uint8_t dir) {
    uint8_t next  = k_btns[from].nav[dir];
    uint8_t guard = 0;
    while(next != NN && !vis[next] && guard++ < SGRID_BTN_COUNT)
        next = k_btns[next].nav[dir];
    return (next == NN || !vis[next]) ? from : next;
}

static void draw_btn(Canvas* canvas, uint8_t idx, uint8_t screen_y,
                      bool selected) {
    const SubGhzGridBtnDef* b = &k_btns[idx];
    uint8_t x = b->full ? LX : (b->col == 0 ? LX : RX);
    uint8_t w = b->full ? FW : (b->col == 0 ? LW : RW);

    if(selected) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rbox(canvas, x, screen_y, w, BTN_H, BTN_R);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, x, screen_y, w, BTN_H, BTN_R);
    }

    if(b->icon) {
        uint8_t icon_x = x + (w - ICON_SIZE) / 2;
        uint8_t icon_y = screen_y + ICON_PAD_TOP;
        canvas_draw_icon(canvas, icon_x, icon_y, b->icon);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        x + w / 2,
        screen_y + TEXT_Y_OFF,
        AlignCenter,
        AlignCenter,
        b->label);

    if(selected) canvas_set_color(canvas, ColorBlack);
}

static void draw_arrow_up(Canvas* canvas) {
    canvas_draw_line(canvas, 63, 0, 63, 0);
    canvas_draw_line(canvas, 62, 1, 64, 1);
    canvas_draw_line(canvas, 61, 2, 65, 2);
}
static void draw_arrow_down(Canvas* canvas) {
    canvas_draw_line(canvas, 61, 61, 65, 61);
    canvas_draw_line(canvas, 62, 62, 64, 62);
    canvas_draw_line(canvas, 63, 63, 63, 63);
}

static uint8_t sgrid_nth_visible_row(const bool* vis, uint8_t start_row, uint8_t n) {
    uint8_t found = 0;
    for(uint8_t r = start_row; r < TOTAL_ROWS; r++) {
        for(uint8_t i = 0; i < SGRID_BTN_COUNT; i++) {
            if(vis[i] && k_btns[i].row == r) {
                if(found == n) return r;
                found++;
                break;
            }
        }
    }
    return 0xFF;
}

static uint8_t sgrid_prev_visible_row(const bool* vis, uint8_t before_row) {
    for(uint8_t r = before_row; r > 0; r--) {
        for(uint8_t i = 0; i < SGRID_BTN_COUNT; i++) {
            if(vis[i] && k_btns[i].row == r - 1) return r - 1;
        }
    }
    return 0xFF;
}

static void sgrid_draw_cb(Canvas* canvas, void* _model) {
    SubGhzStartGridModel* m = _model;
    canvas_clear(canvas);

    uint8_t row0 = sgrid_nth_visible_row(m->visible, m->window_row, 0);
    uint8_t row1 = (row0 != 0xFF) ? sgrid_nth_visible_row(m->visible, row0 + 1, 0) : 0xFF;

    for(uint8_t i = 0; i < SGRID_BTN_COUNT; i++) {
        if(!m->visible[i]) continue;
        uint8_t r = k_btns[i].row;
        uint8_t screen_y;
        if(r == row0)      screen_y = ROW_TOP_Y;
        else if(r == row1) screen_y = ROW_BOT_Y;
        else               continue;
        draw_btn(canvas, i, screen_y, i == m->selected);
    }

    canvas_set_color(canvas, ColorBlack);
    if(row0 != 0xFF && sgrid_nth_visible_row(m->visible, 0, 0) < row0)
        draw_arrow_up(canvas);
    if(row1 != 0xFF && sgrid_nth_visible_row(m->visible, row1 + 1, 0) != 0xFF)
        draw_arrow_down(canvas);
}

static bool sgrid_input_cb(InputEvent* event, void* context) {
    SubGhzStartGrid* instance = context;
    if(event->type != InputTypeShort && event->type != InputTypeLong)
        return false;

    bool     consumed = false;
    bool     fire     = false;
    uint32_t ev_val   = 0;

    with_view_model(
        instance->view,
        SubGhzStartGridModel* m,
        {
            uint8_t sel = m->selected;
            uint8_t dir = NN;

            switch(event->key) {
            case InputKeyUp:    dir = 0; break;
            case InputKeyDown:  dir = 1; break;
            case InputKeyLeft:  dir = 2; break;
            case InputKeyRight: dir = 3; break;
            case InputKeyOk:
                fire   = true;
                ev_val = k_btns[sel].event;
                break;
            default: break;
            }

            if(dir != NN) {
                uint8_t next = sgrid_nav(m->visible, sel, dir);
                if(next == sel && (dir == 0 || dir == 1)) {

                    if(dir == 1) {

                        for(uint8_t wi = 0; wi < SGRID_BTN_COUNT; wi++) {
                            if(m->visible[wi]) { next = wi; break; }
                        }
                    } else {

                        for(uint8_t wi = SGRID_BTN_COUNT - 1; wi < 255; wi--) {
                            if(m->visible[wi]) { next = wi; break; }
                        }
                    }
                }
                if(next != sel) {
                    m->selected = next;
                    uint8_t r = k_btns[next].row;
                    if(r < m->window_row)
                        m->window_row = r;
                    else if(r > m->window_row + 1) {
                        uint8_t prev_vis = sgrid_prev_visible_row(m->visible, r);
                        m->window_row = (prev_vis != 0xFF) ? prev_vis : r;
                    }
                    consumed = true;
                }
            } else if(fire) {
                consumed = true;
            }
        },
        true);

    if(fire && instance->callback)
        instance->callback(instance->context, ev_val);

    return consumed;
}

SubGhzStartGrid* subghz_start_grid_alloc(void) {
    SubGhzStartGrid* instance = malloc(sizeof(SubGhzStartGrid));
    instance->view     = view_alloc();
    instance->callback = NULL;
    instance->context  = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking,
                        sizeof(SubGhzStartGridModel));
    view_set_draw_callback(instance->view, sgrid_draw_cb);
    view_set_input_callback(instance->view, sgrid_input_cb);

    with_view_model(
        instance->view,
        SubGhzStartGridModel* m,
        {
            m->selected   = 0;
            m->window_row = 0;
            for(uint8_t i = 0; i < SGRID_BTN_COUNT; i++)
                m->visible[i] = true;
        },
        false);

    return instance;
}

void subghz_start_grid_free(SubGhzStartGrid* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* subghz_start_grid_get_view(SubGhzStartGrid* instance) {
    furi_assert(instance);
    return instance->view;
}

void subghz_start_grid_set_callback(SubGhzStartGrid* instance,
                                     SubGhzStartGridCallback callback,
                                     void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context  = context;
}

void subghz_start_grid_set_visible(SubGhzStartGrid* instance,
                                    uint8_t btn_idx,
                                    bool visible) {
    furi_assert(instance);
    if(btn_idx >= SGRID_BTN_COUNT) return;
    with_view_model(
        instance->view,
        SubGhzStartGridModel* m,
        { m->visible[btn_idx] = visible; },
        false);
}

void subghz_start_grid_set_selected(SubGhzStartGrid* instance,
                                     uint8_t btn_idx) {
    furi_assert(instance);
    if(btn_idx >= SGRID_BTN_COUNT) return;
    with_view_model(
        instance->view,
        SubGhzStartGridModel* m,
        {
            m->selected = btn_idx;
            uint8_t r   = k_btns[btn_idx].row;
            if(sgrid_nth_visible_row(m->visible, r + 1, 0) != 0xFF) {
                m->window_row = r;
            } else {
                uint8_t prev_vis = sgrid_prev_visible_row(m->visible, r);
                m->window_row = (prev_vis != 0xFF) ? prev_vis : r;
            }
        },
        false);
}

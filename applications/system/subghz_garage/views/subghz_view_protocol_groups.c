#include "subghz_view_protocol_groups.h"
#include "../protocols/protocol_groups.h"
#include "../helpers/subghz_elements_compat.h"
#include <assets_icons.h>
#include <gui/elements.h>
#include <gui/icon.h>
#include <furi.h>
#include <stdio.h>
#include <string.h>

#define BOX_X          4
#define BOX_W          120
#define BOX_H          28
#define BOX_R          4
#define GROUPS_VISIBLE 2

#define ICON_GAP 3
#define TEXT_PAD 3

#define SCROLL_TIMER_PERIOD_MS 333

static const uint8_t k_slot_y[GROUPS_VISIBLE] = {2, 34};

typedef struct {
    uint8_t cursor;
    uint8_t enabled[SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT];
    size_t scroll_counter;
} SubGhzProtocolGroupsModel;

struct SubGhzProtocolGroups {
    View* view;
    FuriTimer* scroll_timer;
    bool scroll_running;
    SubGhzProtocolGroupsCallback callback;
    void* context;
};

static uint8_t protocol_groups_scroll_top(uint8_t cursor) {
    uint8_t max_top = SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT - GROUPS_VISIBLE;
    return (cursor > max_top) ? max_top : cursor;
}

static void protocol_groups_draw_cb(Canvas* canvas, void* model_ptr) {
    SubGhzProtocolGroupsModel* m = model_ptr;
    canvas_clear(canvas);

    uint8_t top = protocol_groups_scroll_top(m->cursor);
    char line2_buf[128];

    for(uint8_t slot = 0; slot < GROUPS_VISIBLE; slot++) {
        uint8_t idx = top + slot;
        if(idx >= SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT) break;
        bool at_cursor = (idx == m->cursor);
        bool is_enabled = m->enabled[idx] != 0;
        uint8_t y = k_slot_y[slot];

        canvas_set_color(canvas, ColorBlack);
        if(at_cursor) {
            canvas_draw_rbox(canvas, BOX_X, y, BOX_W, BOX_H, BOX_R);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, BOX_X, y, BOX_W, BOX_H, BOX_R);
        }

        uint8_t icon_x = BOX_X + TEXT_PAD;
        uint8_t icon_y = y + (BOX_H - icon_get_height(&I_ButtonCenter_7x7)) / 2;
        uint8_t text_x = icon_x + icon_get_width(&I_ButtonCenter_7x7) + ICON_GAP;
        uint8_t text_w = BOX_X + BOX_W - text_x - TEXT_PAD;

        if(is_enabled) {
            canvas_draw_icon(canvas, icon_x, icon_y, &I_ButtonCenter_7x7);
        } else {
            canvas_draw_circle(
                canvas,
                icon_x + icon_get_width(&I_ButtonCenter_7x7) / 2,
                icon_y + icon_get_height(&I_ButtonCenter_7x7) / 2,
                icon_get_width(&I_ButtonCenter_7x7) / 2);
        }

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, text_x, y + 11, subghz_garage_protocol_group_names[idx]);

        canvas_set_font(canvas, FontSecondary);
        snprintf(
            line2_buf, sizeof(line2_buf), "Protocols: %s",
            subghz_garage_protocol_group_members[idx]);

        subghz_garage_scrollable_text_line_str(
            canvas, text_x, y + 23, text_w, line2_buf,
            at_cursor ? m->scroll_counter : 0, false, false);

        canvas_set_color(canvas, ColorBlack);
    }
}

static void protocol_groups_notify_toggle(SubGhzProtocolGroups* instance, uint8_t idx, bool enabled) {
    if(instance->callback) {
        instance->callback(instance->context, idx, enabled);
    }
}

static bool protocol_groups_input_cb(InputEvent* event, void* context) {
    SubGhzProtocolGroups* instance = context;
    if(event->type != InputTypeShort) return false;

    bool consumed = false;
    bool toggled = false;
    uint8_t toggled_idx = 0;
    bool toggled_enabled = false;

    with_view_model(
        instance->view,
        SubGhzProtocolGroupsModel* m,
        {
            if(event->key == InputKeyUp) {
                m->cursor = (m->cursor == 0) ?
                                (SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT - 1) :
                                m->cursor - 1;
                m->scroll_counter = 0;
                consumed = true;
            } else if(event->key == InputKeyDown) {
                m->cursor = (uint8_t)((m->cursor + 1) % SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT);
                m->scroll_counter = 0;
                consumed = true;
            } else if(event->key == InputKeyOk) {
                bool turning_off = m->enabled[m->cursor] != 0;
                bool refuse = false;
                if(turning_off) {

                    uint8_t enabled_count = 0;
                    for(uint8_t i = 0; i < SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT; i++) {
                        if(m->enabled[i]) enabled_count++;
                    }
                    refuse = (enabled_count <= 1);
                }
                if(!refuse) {
                    m->enabled[m->cursor] = turning_off ? 0 : 1;
                    toggled_idx = m->cursor;
                    toggled_enabled = m->enabled[m->cursor] != 0;
                    toggled = true;
                }
                consumed = true;
            }
        },
        consumed);

    if(toggled) {
        protocol_groups_notify_toggle(instance, toggled_idx, toggled_enabled);
    }

    return consumed;
}

static void protocol_groups_scroll_timer_cb(void* context) {
    SubGhzProtocolGroups* instance = context;
    with_view_model(
        instance->view, SubGhzProtocolGroupsModel* m, { m->scroll_counter++; }, true);
}

SubGhzProtocolGroups* subghz_protocol_groups_alloc(void) {
    SubGhzProtocolGroups* instance = malloc(sizeof(SubGhzProtocolGroups));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SubGhzProtocolGroupsModel));
    view_set_draw_callback(instance->view, protocol_groups_draw_cb);
    view_set_input_callback(instance->view, protocol_groups_input_cb);

    with_view_model(
        instance->view,
        SubGhzProtocolGroupsModel* m,
        {
            m->cursor = 0;
            memset(m->enabled, 0x01, sizeof(m->enabled));
            m->scroll_counter = 0;
        },
        false);

    instance->scroll_timer = furi_timer_alloc(
        protocol_groups_scroll_timer_cb, FuriTimerTypePeriodic, instance);
    instance->scroll_running = false;

    return instance;
}

void subghz_protocol_groups_free(SubGhzProtocolGroups* instance) {
    furi_assert(instance);
    furi_timer_stop(instance->scroll_timer);
    furi_timer_free(instance->scroll_timer);
    view_free(instance->view);
    free(instance);
}

void subghz_protocol_groups_resume_scroll(SubGhzProtocolGroups* instance) {
    furi_assert(instance);
    if(instance->scroll_running) return;
    instance->scroll_running = true;
    furi_timer_start(instance->scroll_timer, SCROLL_TIMER_PERIOD_MS);
}

void subghz_protocol_groups_pause_scroll(SubGhzProtocolGroups* instance) {
    furi_assert(instance);
    if(!instance->scroll_running) return;
    instance->scroll_running = false;
    furi_timer_stop(instance->scroll_timer);
}

View* subghz_protocol_groups_get_view(SubGhzProtocolGroups* instance) {
    furi_assert(instance);
    return instance->view;
}

void subghz_protocol_groups_set_callback(
    SubGhzProtocolGroups* instance,
    SubGhzProtocolGroupsCallback callback,
    void* context) {
    furi_assert(instance);
    instance->callback = callback;
    instance->context = context;
}

void subghz_protocol_groups_set_enabled_all(
    SubGhzProtocolGroups* instance,
    const uint8_t* enabled_groups) {
    furi_assert(instance);
    furi_assert(enabled_groups);
    with_view_model(
        instance->view,
        SubGhzProtocolGroupsModel* m,
        { memcpy(m->enabled, enabled_groups, sizeof(m->enabled)); },
        false);
}

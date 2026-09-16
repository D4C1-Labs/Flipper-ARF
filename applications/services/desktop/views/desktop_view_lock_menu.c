// Visual lock menu adapted from Momentum Firmware (GPLv3)
#include <furi.h>
#include <gui/elements.h>
#include <assets_icons.h>

#include "../desktop_i.h"
#include "desktop_view_lock_menu.h"

static const NotificationSequence sequence_note_c = {
    &message_note_c5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

typedef enum {
    DesktopLockMenuIndexLock,
    DesktopLockMenuIndexStealth,
    DesktopLockMenuIndexBt,
    DesktopLockMenuIndexSettings,
    DesktopLockMenuIndexBrightness,
    DesktopLockMenuIndexVolume,

    DesktopLockMenuIndexTotalCount
} DesktopLockMenuIndex;

// Number of grid toggle buttons (the rest are vertical bars).
#define LOCK_MENU_TOGGLE_COUNT 4

void desktop_lock_menu_set_callback(
    DesktopLockMenuView* lock_menu,
    DesktopLockMenuViewCallback callback,
    void* context) {
    furi_assert(lock_menu);
    furi_assert(callback);
    lock_menu->callback = callback;
    lock_menu->context = context;
}

void desktop_lock_menu_set_stealth_mode_state(DesktopLockMenuView* lock_menu, bool stealth_mode) {
    with_view_model(
        lock_menu->view,
        DesktopLockMenuViewModel * model,
        { model->stealth_mode = stealth_mode; },
        true);
}

void desktop_lock_menu_set_bt_mode_state(DesktopLockMenuView* lock_menu, bool bt_mode) {
    with_view_model(
        lock_menu->view, DesktopLockMenuViewModel * model, { model->bt_mode = bt_mode; }, true);
}

void desktop_lock_menu_set_idx(DesktopLockMenuView* lock_menu, uint8_t idx) {
    furi_assert(idx < DesktopLockMenuIndexTotalCount);
    with_view_model(
        lock_menu->view, DesktopLockMenuViewModel * model, { model->idx = idx; }, true);
}

void desktop_lock_menu_save_settings(DesktopLockMenuView* lock_menu) {
    furi_assert(lock_menu);
    if(lock_menu->save_notification) {
        notification_message_save_settings(lock_menu->notification);
        lock_menu->save_notification = false;
    }
}

void desktop_lock_menu_draw_callback(Canvas* canvas, void* model) {
    DesktopLockMenuViewModel* m = model;

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontBatteryPercent);

    int8_t x = 0, y = 0, w = 0, h = 0;
    bool selected, toggle;
    bool enabled = false;
    uint8_t value = 0;
    const int8_t total = 58;
    const Icon* icon = NULL;

    for(size_t i = 0; i < DesktopLockMenuIndexTotalCount; ++i) {
        selected = m->idx == i;
        toggle = i < LOCK_MENU_TOGGLE_COUNT;
        if(toggle) {
            x = 2 + 32 * (i / 2);
            y = 2 + 32 * (i % 2);
            w = 28;
            h = 28;
            enabled = false;
        } else {
            uint8_t bar = i - LOCK_MENU_TOGGLE_COUNT;
            x = 80 + 20 * bar;
            y = 2;
            w = 12;
            h = 60;
            value = 0;
        }

        switch(i) {
        case DesktopLockMenuIndexLock:
            icon = &I_CC_Lock_16x16;
            break;
        case DesktopLockMenuIndexStealth:
            icon = m->stealth_mode ? &I_Muted_8x8 : &I_Volup_8x6;
            enabled = m->stealth_mode;
            break;
        case DesktopLockMenuIndexBt:
            icon = &I_CC_Bluetooth_16x16;
            enabled = m->bt_mode;
            break;
        case DesktopLockMenuIndexSettings:
            icon = &I_CC_Settings_16x16;
            break;
        case DesktopLockMenuIndexBrightness:
            icon = &I_Pin_star_7x7;
            value = total - m->lock_menu->notification->settings.display_brightness * total;
            break;
        case DesktopLockMenuIndexVolume:
            icon = m->stealth_mode ? &I_Muted_8x8 : &I_Volup_8x6;
            value = total - m->lock_menu->notification->settings.speaker_volume * total;
            break;
        default:
            break;
        }

        if(selected) {
            elements_bold_rounded_frame(canvas, x - 1, y - 1, w + 1, h + 1);
        } else {
            canvas_draw_rframe(canvas, x, y, w, h, 5);
        }

        if(toggle) {
            if(enabled) {
                canvas_draw_rbox(canvas, x, y, w, h, 5);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_icon(
                canvas,
                x + (w - icon_get_width(icon)) / 2,
                y + (h - icon_get_height(icon)) / 2,
                icon);
            if(enabled) {
                canvas_set_color(canvas, ColorBlack);
            }
        } else {
            canvas_draw_icon(
                canvas,
                x + (w - icon_get_width(icon)) / 2,
                y + (h - icon_get_height(icon)) / 2,
                icon);
            canvas_set_color(canvas, ColorXOR);
            canvas_draw_box(canvas, x + 1, y + 1 + value, w - 2, h - 2 - value);
            if(selected) {
                canvas_set_color(canvas, ColorBlack);
            } else {
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_dot(canvas, x + 1, y + 1);
            canvas_draw_dot(canvas, x + 1, y + h - 2);
            canvas_draw_dot(canvas, x + w - 2, y + 1);
            canvas_draw_dot(canvas, x + w - 2, y + h - 2);
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_rframe(canvas, x, y, w, h, 5);
        }
    }
}

View* desktop_lock_menu_get_view(DesktopLockMenuView* lock_menu) {
    furi_assert(lock_menu);
    return lock_menu->view;
}

bool desktop_lock_menu_input_callback(InputEvent* event, void* context) {
    furi_assert(event);
    furi_assert(context);

    DesktopLockMenuView* lock_menu = context;
    uint8_t idx = 0;
    bool stealth_mode = false;
    bool consumed = true;

    with_view_model(
        lock_menu->view,
        DesktopLockMenuViewModel * model,
        {
            stealth_mode = model->stealth_mode;
            if((event->type == InputTypeShort) || (event->type == InputTypeRepeat)) {
                if(model->idx < LOCK_MENU_TOGGLE_COUNT) {
                    // Grid navigation (2 columns of toggles)
                    if(event->key == InputKeyUp || event->key == InputKeyDown) {
                        if(model->idx % 2) {
                            model->idx--;
                        } else {
                            model->idx++;
                        }
                    } else if(event->key == InputKeyLeft) {
                        if(model->idx < 2) {
                            // wrap to the last (Volume) bar
                            model->idx = DesktopLockMenuIndexTotalCount - 1;
                        } else {
                            model->idx -= 2;
                        }
                    } else if(event->key == InputKeyRight) {
                        if(model->idx >= 2) {
                            // jump to the first (Brightness) bar
                            model->idx = DesktopLockMenuIndexBrightness;
                        } else {
                            model->idx += 2;
                        }
                    }
                } else {
                    // Bar navigation (Left/Right move between bars and back to grid)
                    if(event->key == InputKeyLeft) {
                        if(model->idx == DesktopLockMenuIndexBrightness) {
                            // back to right-hand column of the grid
                            model->idx = DesktopLockMenuIndexBt;
                        } else {
                            model->idx--;
                        }
                    } else if(event->key == InputKeyRight) {
                        if(model->idx >= DesktopLockMenuIndexTotalCount - 1) {
                            // wrap to top-left toggle
                            model->idx = DesktopLockMenuIndexLock;
                        } else {
                            model->idx++;
                        }
                    }
                }
            }
            idx = model->idx;
        },
        true);

    DesktopEvent desktop_event = 0;
    if(event->key == InputKeyBack) {
        consumed = false;
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        switch(idx) {
        case DesktopLockMenuIndexLock:
            desktop_event = DesktopLockMenuEventLock;
            break;
        case DesktopLockMenuIndexStealth:
            desktop_event = stealth_mode ? DesktopLockMenuEventStealthModeOff :
                                           DesktopLockMenuEventStealthModeOn;
            break;
        case DesktopLockMenuIndexBt:
            desktop_event = DesktopLockMenuEventBt;
            break;
        case DesktopLockMenuIndexSettings:
            desktop_event = DesktopLockMenuEventSettings;
            break;
        case DesktopLockMenuIndexVolume:
            desktop_event = stealth_mode ? DesktopLockMenuEventStealthModeOff :
                                           DesktopLockMenuEventStealthModeOn;
            break;
        default:
            break;
        }
    } else if(
        idx >= LOCK_MENU_TOGGLE_COUNT &&
        (event->type == InputTypeShort || event->type == InputTypeRepeat)) {
        int8_t offset = 0;
        if(event->key == InputKeyUp) {
            offset = 1;
        } else if(event->key == InputKeyDown) {
            offset = -1;
        }
        if(offset) {
            float value;
            switch(idx) {
            case DesktopLockMenuIndexBrightness:
                value = lock_menu->notification->settings.display_brightness + 0.05f * offset;
                lock_menu->notification->settings.display_brightness =
                    value < 0.00f ? 0.00f : (value > 1.00f ? 1.00f : value);
                lock_menu->save_notification = true;
                notification_message(lock_menu->notification, &sequence_display_backlight_on);
                break;
            case DesktopLockMenuIndexVolume:
                value = lock_menu->notification->settings.speaker_volume + 0.05f * offset;
                lock_menu->notification->settings.speaker_volume =
                    value < 0.00f ? 0.00f : (value > 1.00f ? 1.00f : value);
                lock_menu->save_notification = true;
                notification_message(lock_menu->notification, &sequence_note_c);
                break;
            default:
                break;
            }
        }
    }

    if(desktop_event) {
        lock_menu->callback(desktop_event, lock_menu->context);
    }

    return consumed;
}

DesktopLockMenuView* desktop_lock_menu_alloc(void) {
    DesktopLockMenuView* lock_menu = malloc(sizeof(DesktopLockMenuView));
    lock_menu->notification = furi_record_open(RECORD_NOTIFICATION);
    lock_menu->save_notification = false;
    lock_menu->view = view_alloc();
    view_allocate_model(lock_menu->view, ViewModelTypeLocking, sizeof(DesktopLockMenuViewModel));
    with_view_model(
        lock_menu->view,
        DesktopLockMenuViewModel * model,
        { model->lock_menu = lock_menu; },
        false);
    view_set_context(lock_menu->view, lock_menu);
    view_set_draw_callback(lock_menu->view, (ViewDrawCallback)desktop_lock_menu_draw_callback);
    view_set_input_callback(lock_menu->view, desktop_lock_menu_input_callback);

    return lock_menu;
}

void desktop_lock_menu_free(DesktopLockMenuView* lock_menu_view) {
    furi_assert(lock_menu_view);

    desktop_lock_menu_save_settings(lock_menu_view);

    view_free(lock_menu_view->view);
    furi_record_close(RECORD_NOTIFICATION);
    free(lock_menu_view);
}

// Visual lock menu adapted from Momentum Firmware (GPLv3)
#include <furi.h>
#include <furi_hal_bt.h>
#include <gui/elements.h>
#include <assets_icons.h>

#include "../desktop_i.h"
#include "desktop_view_lock_menu.h"
// Full Bt definition (bt_settings field). bt_i.h only pulls in the guarded
// notification/notification.h, so it does not re-introduce notification_app.h.
#include <bt/bt_service/bt_i.h>

static const NotificationSequence sequence_note_c = {
    &message_note_c5,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

// Index order is COLUMN-MAJOR so idx/2 = column, idx%2 = row. This yields the
// on-screen layout:
//   row0:  Lock       SubGhz       Bluetooth
//   row1:  Sound      ProtoPirate  Settings
typedef enum {
    DesktopLockMenuIndexLock, // col0 row0
    DesktopLockMenuIndexStealth, // col0 row1 (Sound/Mute)
    DesktopLockMenuIndexSubGhz, // col1 row0
    DesktopLockMenuIndexProtoPirate, // col1 row1
    DesktopLockMenuIndexBluetooth, // col2 row0
    DesktopLockMenuIndexSettings, // col2 row1

    DesktopLockMenuIndexBrightness, // bar 0
    DesktopLockMenuIndexVolume, // bar 1

    DesktopLockMenuIndexTotalCount
} DesktopLockMenuIndex;

// Number of grid toggle buttons (the rest are vertical bars).
#define LOCK_MENU_TOGGLE_COUNT 6

// Leave room for the Flipper status bar (battery, etc.) at the top.
#define LOCK_MENU_TOP_Y (STATUS_BAR_Y_SHIFT + 2)

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
    if(lock_menu->save_bt) {
        bt_settings_save(&lock_menu->bt->bt_settings);
        lock_menu->save_bt = false;
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
    // Fillable inner height of a bar (bar h=48, minus 1px top/bottom border).
    const int8_t total = 46;
    const Icon* icon = NULL;

    for(size_t i = 0; i < DesktopLockMenuIndexTotalCount; ++i) {
        selected = m->idx == i;
        toggle = i < LOCK_MENU_TOGGLE_COUNT;
        // Screen is 64px tall; the status bar occupies the top ~13px, leaving
        // ~49px for two toggle rows + a small gap. Use 23px cells with a 25px
        // row pitch so both rows and the bars fit under the visible header.
        if(toggle) {
            x = 2 + 32 * (i / 2);
            y = LOCK_MENU_TOP_Y + 25 * (i % 2);
            w = 28;
            h = 23;
            enabled = false;
        } else {
            uint8_t bar = i - LOCK_MENU_TOGGLE_COUNT;
            x = 98 + 16 * bar;
            y = LOCK_MENU_TOP_Y;
            w = 12;
            h = 48;
            value = 0;
        }

        switch(i) {
        case DesktopLockMenuIndexLock:
            icon = &I_CC_Lock_16x16;
            break;
        case DesktopLockMenuIndexBluetooth:
            icon = &I_CC_Bluetooth_16x16;
            enabled = m->lock_menu->bt->bt_settings.enabled;
            break;
        case DesktopLockMenuIndexStealth:
            icon = m->stealth_mode ? &I_Muted_8x8 : &I_Volup_8x6;
            enabled = m->stealth_mode;
            break;
        case DesktopLockMenuIndexSubGhz:
            icon = &I_MHz_25x11;
            break;
        case DesktopLockMenuIndexProtoPirate:
            icon = &I_Cos_9x7;
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
                    // Grid navigation: 3 columns x 2 rows (column-major idx).
                    // idx/2 = column (0,1,2), idx%2 = row (0 top, 1 bottom).
                    if(event->key == InputKeyUp || event->key == InputKeyDown) {
                        // Toggle between the two rows of the current column.
                        if(model->idx % 2) {
                            model->idx--; // bottom -> top
                        } else {
                            model->idx++; // top -> bottom
                        }
                    } else if(event->key == InputKeyLeft) {
                        if(model->idx < 2) {
                            // leftmost column -> wrap to the last (Volume) bar
                            model->idx = DesktopLockMenuIndexTotalCount - 1;
                        } else {
                            model->idx -= 2; // one column left, same row
                        }
                    } else if(event->key == InputKeyRight) {
                        if(model->idx >= LOCK_MENU_TOGGLE_COUNT - 2) {
                            // rightmost column -> jump to the first (Brightness) bar
                            model->idx = DesktopLockMenuIndexBrightness;
                        } else {
                            model->idx += 2; // one column right, same row
                        }
                    }
                } else {
                    // Bar navigation (Left/Right move between bars and back to grid)
                    if(event->key == InputKeyLeft) {
                        if(model->idx == DesktopLockMenuIndexBrightness) {
                            // back to the rightmost grid column (top row: Bluetooth)
                            model->idx = DesktopLockMenuIndexBluetooth;
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
            // Simple ARF lock behavior: leave the menu and lock.
            desktop_event = DesktopLockMenuEventLock;
            break;
        case DesktopLockMenuIndexBluetooth:
            // Toggle BT in place; stay in the menu (Momentum behavior).
            lock_menu->bt->bt_settings.enabled = !lock_menu->bt->bt_settings.enabled;
            if(lock_menu->bt->bt_settings.enabled) {
                furi_hal_bt_start_advertising();
            } else {
                furi_hal_bt_stop_advertising();
            }
            lock_menu->save_bt = true;
            break;
        case DesktopLockMenuIndexStealth:
            // Stealth toggle stays in the menu; the scene updates RTC state only.
            desktop_event = stealth_mode ? DesktopLockMenuEventStealthModeOff :
                                           DesktopLockMenuEventStealthModeOn;
            break;
        case DesktopLockMenuIndexSubGhz:
            desktop_event = DesktopLockMenuEventSubGhz;
            break;
        case DesktopLockMenuIndexProtoPirate:
            desktop_event = DesktopLockMenuEventProtoPirate;
            break;
        case DesktopLockMenuIndexSettings:
            desktop_event = DesktopLockMenuEventSettings;
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
    lock_menu->bt = furi_record_open(RECORD_BT);
    lock_menu->notification = furi_record_open(RECORD_NOTIFICATION);
    lock_menu->save_notification = false;
    lock_menu->save_bt = false;
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
    furi_record_close(RECORD_BT);
    free(lock_menu_view);
}

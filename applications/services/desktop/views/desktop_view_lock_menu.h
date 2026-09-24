// Visual lock menu adapted from Momentum Firmware (GPLv3)
#pragma once

#include <gui/view.h>
#include "desktop_events.h"
// Use the guarded, lightweight notification.h (it has #pragma once and provides
// the NotificationApp typedef). Do NOT include notification_app.h here: it has
// NO include guard and is already pulled in via desktop_i.h, so including it
// again caused mass redefinition errors. We only store a NotificationApp*.
#include <notification/notification.h>
// Guarded header providing the Bt typedef (we only store a Bt*). The .c pulls in
// bt_i.h for access to the bt_settings field.
#include <bt/bt_service/bt.h>

#define HINT_TIMEOUT 2

typedef struct DesktopLockMenuView DesktopLockMenuView;

typedef void (*DesktopLockMenuViewCallback)(DesktopEvent event, void* context);

struct DesktopLockMenuView {
    View* view;
    DesktopLockMenuViewCallback callback;
    void* context;

    NotificationApp* notification;
    Bt* bt;
    bool save_notification;
    bool save_bt;
};

typedef struct {
    uint8_t idx;
    bool stealth_mode;
    DesktopLockMenuView* lock_menu;
} DesktopLockMenuViewModel;

void desktop_lock_menu_set_callback(
    DesktopLockMenuView* lock_menu,
    DesktopLockMenuViewCallback callback,
    void* context);

View* desktop_lock_menu_get_view(DesktopLockMenuView* lock_menu);
void desktop_lock_menu_set_stealth_mode_state(DesktopLockMenuView* lock_menu, bool stealth_mode);
void desktop_lock_menu_set_idx(DesktopLockMenuView* lock_menu, uint8_t idx);
void desktop_lock_menu_save_settings(DesktopLockMenuView* lock_menu);
DesktopLockMenuView* desktop_lock_menu_alloc(void);
void desktop_lock_menu_free(DesktopLockMenuView* lock_menu);

#include <gui/scene_manager.h>
#include <applications.h>
#include <furi_hal.h>
#include <toolbox/saved_struct.h>
#include <stdbool.h>
#include <loader/loader.h>

#include "../desktop_i.h"
#include <desktop/desktop_settings.h>
#include "../views/desktop_view_lock_menu.h"
#include "desktop_scene.h"

#define TAG "DesktopSceneLock"

void desktop_scene_lock_menu_callback(DesktopEvent event, void* context) {
    Desktop* desktop = (Desktop*)context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, event);
}

void desktop_scene_lock_menu_on_enter(void* context) {
    Desktop* desktop = (Desktop*)context;

    scene_manager_set_scene_state(desktop->scene_manager, DesktopSceneLockMenu, 0);
    desktop_lock_menu_set_callback(desktop->lock_menu, desktop_scene_lock_menu_callback, desktop);
    desktop_lock_menu_set_stealth_mode_state(
        desktop->lock_menu, furi_hal_rtc_is_flag_set(FuriHalRtcFlagStealthMode));
    desktop_lock_menu_set_idx(desktop->lock_menu, 0);

    view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdLockMenu);
}

bool desktop_scene_lock_menu_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = (Desktop*)context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DesktopLockMenuEventLock:
            desktop_lock_menu_save_settings(desktop->lock_menu);
            scene_manager_set_scene_state(desktop->scene_manager, DesktopSceneLockMenu, 0);
            desktop_lock(desktop);
            consumed = true;
            break;
        case DesktopLockMenuEventStealthModeOn:
            // Stays in the menu: only update the RTC/notification stealth state.
            desktop_set_stealth_mode_state(desktop, true);
            desktop_lock_menu_set_stealth_mode_state(desktop->lock_menu, true);
            consumed = true;
            break;
        case DesktopLockMenuEventStealthModeOff:
            // Stays in the menu: only update the RTC/notification stealth state.
            desktop_set_stealth_mode_state(desktop, false);
            desktop_lock_menu_set_stealth_mode_state(desktop->lock_menu, false);
            consumed = true;
            break;
        case DesktopLockMenuEventSubGhz:
            desktop_lock_menu_save_settings(desktop->lock_menu);
            loader_start_detached_with_gui_error(desktop->loader, "subghz", "read");
            consumed = true;
            break;
        case DesktopLockMenuEventProtoPirate:
            desktop_lock_menu_save_settings(desktop->lock_menu);
            loader_start_detached_with_gui_error(desktop->loader, "proto_pirate", NULL);
            consumed = true;
            break;
        case DesktopLockMenuEventSettings:
            desktop_lock_menu_save_settings(desktop->lock_menu);
            loader_start_detached_with_gui_error(desktop->loader, LOADER_APPLICATIONS_NAME, NULL);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_set_scene_state(desktop->scene_manager, DesktopSceneLockMenu, 0);
    }
    return consumed;
}

void desktop_scene_lock_menu_on_exit(void* context) {
    Desktop* desktop = (Desktop*)context;
    desktop_lock_menu_save_settings(desktop->lock_menu);
}

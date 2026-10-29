#include "../subghz_i.h"
#include "../helpers/subghz_custom_event.h"
#include "../helpers/subghz_debug_log.h"
#include <furi.h>
#include <furi/core/memmgr.h>

#define SUBGHZ_LOW_RAM_RECOVER_TICKS 5
static uint32_t s_low_ram_warning_recover_tick_count = 0;

#define SUBGHZ_LOW_RAM_WARNING_MIN_DWELL_MS 1500
static uint32_t s_low_ram_warning_shown_at_ms = 0;

static void subghz_scene_low_ram_warning_widget_cb(
    GuiButtonType result,
    InputType type,
    void* context) {
    SubGhz* subghz = context;
    if(type != InputTypeShort) return;

    if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventLowRamWarningExit);
    }
}

static void subghz_scene_low_ram_warning_exit_to_start(SubGhz* subghz) {

    subghz_txrx_release_protocol_group(subghz->txrx);

    scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneReceiver, 0);
    subghz_return_to_launcher(subghz);
}

void subghz_scene_low_ram_warning_on_enter(void* context) {
    SubGhz* subghz = context;

    subghz_debug_log_write(
        "low_ram_warning: on_enter, free heap %zu, qFlipper screen-stream active=%d",
        memmgr_get_free_heap(),
        (int)rpc_gui_screen_stream_is_active());

    s_low_ram_warning_recover_tick_count = 0;
    s_low_ram_warning_shown_at_ms = furi_get_tick();

    rpc_gui_screen_stream_set_suppressed(true);

    subghz_ensure_widget(subghz);
    Widget* widget = subghz->widget;

    widget_add_string_multiline_element(
        widget, 64, 8, AlignCenter, AlignTop, FontPrimary, "Uh Oh!");

    widget_add_string_multiline_element(
        widget,
        64,
        20,
        AlignCenter,
        AlignTop,
        FontSecondary,
        rpc_gui_screen_stream_is_active() ? "SubGhz READ needs more\nRAM - close qFlipper to\ncontinue."
                                           : "SubGhz READ needs more\nRAM - close USB/CLI to\ncontinue.");
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Close", subghz_scene_low_ram_warning_widget_cb, subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_low_ram_warning_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeBack) {

        subghz_scene_low_ram_warning_exit_to_start(subghz);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventLowRamWarningExit) {
            subghz_scene_low_ram_warning_exit_to_start(subghz);
            return true;
        }
    }

    if(event.type == SceneManagerEventTypeTick) {
        size_t tick_free_heap = memmgr_get_free_heap();
        if(tick_free_heap >= SUBGHZ_LOW_RAM_RECOVER_FREE_HEAP) {
            s_low_ram_warning_recover_tick_count++;
        } else {
            s_low_ram_warning_recover_tick_count = 0;
        }

        if(s_low_ram_warning_recover_tick_count >= SUBGHZ_LOW_RAM_RECOVER_TICKS) {
            uint32_t shown_for_ms = furi_get_tick() - s_low_ram_warning_shown_at_ms;
            if(shown_for_ms < SUBGHZ_LOW_RAM_WARNING_MIN_DWELL_MS) {

                return true;
            }
            subghz_debug_log_write(
                "low_ram_warning: tick, free heap %zu sustained >= recover threshold for %lu ticks, recovering",
                tick_free_heap,
                (unsigned long)s_low_ram_warning_recover_tick_count);
            s_low_ram_warning_recover_tick_count = 0;

            if(!rpc_gui_screen_stream_is_active()) {
                subghz_debug_log_write("low_ram_warning: locking CLI sessions before recovery");
                subghz_lock_cli_sessions(subghz);
                subghz_debug_log_write("low_ram_warning: CLI sessions locked");
            } else {
                subghz_debug_log_write(
                    "low_ram_warning: qFlipper screen-stream still active, skipping CLI lock this time");
            }

            subghz->low_ram_grace_until_ms = furi_get_tick() + SUBGHZ_LOW_RAM_GRACE_MS;
            subghz_debug_log_write("low_ram_warning: switching to previous scene");
            scene_manager_previous_scene(subghz->scene_manager);
            subghz_debug_log_write("low_ram_warning: previous scene switch returned");
        }
        return true;
    }

    return false;
}

void subghz_scene_low_ram_warning_on_exit(void* context) {
    SubGhz* subghz = context;

    rpc_gui_screen_stream_set_suppressed(false);

    if(subghz->widget) {
        widget_reset(subghz->widget);
    }
}

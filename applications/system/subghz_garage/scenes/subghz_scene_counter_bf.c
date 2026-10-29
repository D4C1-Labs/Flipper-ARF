#include "../subghz_i.h"
#include "../helpers/subghz_txrx_i.h"
#include "../helpers/subghz_lib_ext_compat.h"
#include "../helpers/subghz_generic_global_compat.h"
#include "lib/subghz/blocks/generic.h"

#define TAG "SubGhzCounterBf"

#define COUNTER_BF_TX_INTERVAL_TICKS 5

typedef enum {
    CounterBfStateWarning,
    CounterBfStateIdle,
    CounterBfStateRunning,
    CounterBfStateStopped,
} CounterBfState;

typedef struct {
    uint32_t current_cnt;
    uint32_t start_cnt;
    uint32_t step;
    CounterBfState state;
    uint32_t packets_sent;
    uint32_t tick_wait;
} CounterBfContext;

#define CounterBfEventStart     (0xC0)
#define CounterBfEventStop      (0xC1)
#define CounterBfEventWarningOk (0xC2)

static void counter_bf_warning_callback(GuiButtonType result, InputType type, void* context) {
    SubGhz* subghz = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, CounterBfEventWarningOk);
    }
}

static void counter_bf_widget_callback(GuiButtonType result, InputType type, void* context) {
    SubGhz* subghz = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, CounterBfEventStart);
    }
}

static void counter_bf_draw_warning(SubGhz* subghz) {
    subghz_ensure_widget(subghz);
    widget_reset(subghz->widget);
    widget_add_string_multiline_element(
        subghz->widget,
        64,
        20,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        "WARNING:\nThis may desync\nyour fob!");
    widget_add_button_element(
        subghz->widget,
        GuiButtonTypeCenter,
        "OK",
        counter_bf_warning_callback,
        subghz);
}

static void counter_bf_draw(SubGhz* subghz, CounterBfContext* ctx) {
    subghz_ensure_widget(subghz);
    widget_reset(subghz->widget);
    FuriString* str = furi_string_alloc();
    furi_string_printf(
        str,
        "Counter BruteForce\n"
        "Cnt: 0x%06lX\n"
        "Start: 0x%06lX\n"
        "Sent: %lu",
        ctx->current_cnt & 0xFFFFFF,
        ctx->start_cnt & 0xFFFFFF,
        ctx->packets_sent);
    widget_add_string_multiline_element(
        subghz->widget, 0, 0, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(str));
    furi_string_free(str);
    const char* btn_label = ctx->state == CounterBfStateRunning ? "Stop" : "Start";
    widget_add_button_element(
        subghz->widget,
        GuiButtonTypeCenter,
        btn_label,
        counter_bf_widget_callback,
        subghz);
}

static void counter_bf_save(SubGhz* subghz, CounterBfContext* ctx) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file_fff = flipper_format_buffered_file_alloc(storage);
    if(flipper_format_buffered_file_open_existing(
           file_fff, furi_string_get_cstr(subghz->file_path))) {
        uint32_t cnt = ctx->current_cnt & 0xFFFFFF;
        if(!flipper_format_update_uint32(file_fff, "Cnt", &cnt, 1)) {
            FURI_LOG_E(TAG, "Failed to update Cnt in .sub file");
        }
    } else {
        FURI_LOG_E(TAG, "Failed to open .sub file for Cnt write");
    }
    flipper_format_free(file_fff);
    furi_record_close(RECORD_STORAGE);
}

static void counter_bf_send(SubGhz* subghz, CounterBfContext* ctx) {
    subghz_txrx_stop(subghz->txrx);

    uint32_t delta = (ctx->current_cnt - ctx->start_cnt) & 0xFFFFFF;
    subghz_garage_set_rolling_counter_mult((int32_t)delta);
    subghz_garage_counter_override_set(ctx->current_cnt & 0xFFFFFF);

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    uint32_t repeat = 20;
    flipper_format_rewind(fff);
    flipper_format_update_uint32(fff, "Repeat", &repeat, 1);

    subghz_tx_start(subghz, fff);

    ctx->packets_sent++;
    ctx->tick_wait = COUNTER_BF_TX_INTERVAL_TICKS;
}

void subghz_scene_counter_bf_on_enter(void* context) {
    SubGhz* subghz = context;

    CounterBfContext* ctx = malloc(sizeof(CounterBfContext));
    memset(ctx, 0, sizeof(CounterBfContext));
    ctx->state = CounterBfStateWarning;
    ctx->step = 1;
    subghz_garage_set_rolling_counter_mult(0);
    subghz_key_load(subghz, furi_string_get_cstr(subghz->file_path), false);

    {
        FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
        flipper_format_rewind(fff);
        uint32_t cnt = 0;
        if(flipper_format_read_uint32(fff, "Cnt", &cnt, 1)) {
            ctx->current_cnt = cnt & 0xFFFFFF;
            ctx->start_cnt   = cnt & 0xFFFFFF;
        } else {
            FURI_LOG_W(TAG, "Cnt not in fff after key_load, reading from disk");
            Storage* storage = furi_record_open(RECORD_STORAGE);
            FlipperFormat* file_fff = flipper_format_buffered_file_alloc(storage);
            if(flipper_format_buffered_file_open_existing(
                   file_fff, furi_string_get_cstr(subghz->file_path))) {
                if(flipper_format_read_uint32(file_fff, "Cnt", &cnt, 1)) {
                    ctx->current_cnt = cnt & 0xFFFFFF;
                    ctx->start_cnt   = cnt & 0xFFFFFF;
                }
            }
            flipper_format_free(file_fff);
            furi_record_close(RECORD_STORAGE);
        }
    }

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneCounterBf, (uint32_t)(uintptr_t)ctx);

    counter_bf_draw_warning(subghz);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_counter_bf_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    CounterBfContext* ctx = (CounterBfContext*)(uintptr_t)
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneCounterBf);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == CounterBfEventWarningOk) {
            ctx->state = CounterBfStateIdle;
            counter_bf_draw(subghz, ctx);
            return true;
        }

        if(event.event == CounterBfEventStart) {
            if(ctx->state == CounterBfStateWarning) return true;

            if(ctx->state != CounterBfStateRunning) {
                ctx->state = CounterBfStateRunning;
                ctx->tick_wait = 0;
                subghz->state_notifications = SubGhzNotificationStateTx;
                counter_bf_send(subghz, ctx);
            } else {
                ctx->state = CounterBfStateStopped;
                subghz_txrx_stop(subghz->txrx);
                subghz->state_notifications = SubGhzNotificationStateIDLE;
                counter_bf_save(subghz, ctx);
            }
            counter_bf_draw(subghz, ctx);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(ctx->state == CounterBfStateRunning) {
            notification_message(subghz->notifications, &sequence_blink_magenta_10);
            if(ctx->tick_wait > 0) {
                ctx->tick_wait--;
            } else {
                ctx->current_cnt = (ctx->current_cnt + ctx->step) & 0xFFFFFF;
                counter_bf_send(subghz, ctx);
                counter_bf_save(subghz, ctx);
                counter_bf_draw(subghz, ctx);
            }
        }
        return true;
    } else if(event.type == SceneManagerEventTypeBack) {
        if(ctx->state == CounterBfStateWarning) {
            subghz_garage_set_rolling_counter_mult(1);
            free(ctx);
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }

        subghz_txrx_stop(subghz->txrx);
        subghz->state_notifications = SubGhzNotificationStateIDLE;
        counter_bf_save(subghz, ctx);
        subghz_garage_set_rolling_counter_mult(1);
        free(ctx);
        scene_manager_previous_scene(subghz->scene_manager);
        return true;
    }
    return false;
}

void subghz_scene_counter_bf_on_exit(void* context) {
    SubGhz* subghz = context;
    widget_reset(subghz->widget);
    subghz->state_notifications = SubGhzNotificationStateIDLE;
}

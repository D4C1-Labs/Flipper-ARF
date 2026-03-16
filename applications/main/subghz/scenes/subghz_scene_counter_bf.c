#include "../subghz_i.h"
#include "applications/main/subghz/helpers/subghz_txrx_i.h"
#include "lib/subghz/blocks/generic.h"

#define TAG "SubGhzCounterBf"

// How many ticks to wait between transmissions (1 tick ~100ms)
#define COUNTER_BF_TX_INTERVAL_TICKS 3

typedef enum {
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

#define CounterBfEventStart (0xC0)
#define CounterBfEventStop  (0xC1)

static void counter_bf_widget_callback(GuiButtonType result, InputType type, void* context) {
    SubGhz* subghz = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(subghz->view_dispatcher, CounterBfEventStart);
    }
}

static void counter_bf_draw(SubGhz* subghz, CounterBfContext* ctx) {
    widget_reset(subghz->widget);
    FuriString* str = furi_string_alloc();
    furi_string_printf(
        str,
        "WARNING: THIS MAY BE DESYNC YOUR FOB\n"
        "Counter BruteForce\n"
        "Cnt: 0x%08lX\n"
        "Start: 0x%08lX\n"
        "Sent: %lu",
        ctx->current_cnt,
        ctx->start_cnt,
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
    // Escribir el Cnt final directamente en el archivo .sub en disco.
    // No usar subghz_save_protocol_to_file() porque ese serializa el estado
    // actual del encoder (que puede tener el Cnt ya incrementado internamente).
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* file_fff = flipper_format_buffered_file_alloc(storage);
    if(flipper_format_buffered_file_open_existing(
           file_fff, furi_string_get_cstr(subghz->file_path))) {
        if(!flipper_format_update_uint32(file_fff, "Cnt", &ctx->current_cnt, 1)) {
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

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);

    uint32_t repeat = 20;
    flipper_format_rewind(fff);
    flipper_format_update_uint32(fff, "Repeat", &repeat, 1);

    // Actualizar Cnt DESPUES de Repeat (update es secuencial en el buffer)
    flipper_format_rewind(fff);
    flipper_format_update_uint32(fff, "Cnt", &ctx->current_cnt, 1);

    subghz_tx_start(subghz, fff);

    ctx->packets_sent++;
    ctx->tick_wait = COUNTER_BF_TX_INTERVAL_TICKS;
}

void subghz_scene_counter_bf_on_enter(void* context) {
    SubGhz* subghz = context;

    CounterBfContext* ctx = malloc(sizeof(CounterBfContext));
    memset(ctx, 0, sizeof(CounterBfContext));
    ctx->state = CounterBfStateIdle;
    ctx->step = 1;

    // FIX: Leer el Cnt DIRECTAMENTE del archivo en disco con un FlipperFormat
    // propio, completamente separado del fff en memoria (que puede tener el Cnt
    // modificado por TXs previas y no refleja el estado real del .sub).
    {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        FlipperFormat* file_fff = flipper_format_buffered_file_alloc(storage);
        if(flipper_format_buffered_file_open_existing(
               file_fff, furi_string_get_cstr(subghz->file_path))) {
            uint32_t cnt = 0;
            if(flipper_format_read_uint32(file_fff, "Cnt", &cnt, 1)) {
                ctx->current_cnt = cnt;
                ctx->start_cnt = cnt;
            } else {
                FURI_LOG_W(TAG, "Cnt field not found in file");
            }
        } else {
            FURI_LOG_E(TAG, "Failed to open .sub file for Cnt read");
        }
        flipper_format_free(file_fff);
        furi_record_close(RECORD_STORAGE);
    }

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneCounterBf, (uint32_t)(uintptr_t)ctx);

    // Deshabilitar auto-increment del protocolo para controlar el Cnt manualmente
    furi_hal_subghz_set_rolling_counter_mult(0);

    // Recargar el protocolo DESPUES de haber leído el Cnt del disco,
    // para preparar el fff para TX sin que pise nuestro valor leído.
    subghz_key_load(subghz, furi_string_get_cstr(subghz->file_path), false);

    counter_bf_draw(subghz, ctx);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_counter_bf_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    CounterBfContext* ctx = (CounterBfContext*)(uintptr_t)
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneCounterBf);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == CounterBfEventStart) {
            if(ctx->state != CounterBfStateRunning) {
                ctx->state = CounterBfStateRunning;
                ctx->tick_wait = 0;
                subghz->state_notifications = SubGhzNotificationStateTx;
                counter_bf_send(subghz, ctx);
            } else {
                // FIX 2: Al detener, guardar el contador actual en el .sub
                // para que al volver a emular manualmente continúe desde acá.
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
                ctx->current_cnt += ctx->step;
                counter_bf_send(subghz, ctx);
                counter_bf_draw(subghz, ctx);
            }
        }
        return true;
    } else if(event.type == SceneManagerEventTypeBack) {
        subghz_txrx_stop(subghz->txrx);
        subghz->state_notifications = SubGhzNotificationStateIDLE;

        // FIX 2 (también en Back): guardar siempre al salir
        counter_bf_save(subghz, ctx);

        furi_hal_subghz_set_rolling_counter_mult(1);
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

#include "jammer_app.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <jammer_app_icons.h>
#include <subghz/devices/devices.h>
#include <lib/subghz/subghz_tx_rx_worker.h>

#include "helpers/radio_device_loader.h"

#define TAG "JammerApp"

#define JAMMER_DATA_LEN 1024u

#define JAMMER_FREQ_TOP 14
#define JAMMER_STATUS_Y 45

#define JAMMER_RSSI_X   46
#define JAMMER_RSSI_W   80
#define JAMMER_RSSI_MIN (-100.0f)
#define JAMMER_RSSI_MAX (-50.0f)

static const uint32_t jammer_freqs[] = {
    300000000u, 303875000u, 304250000u, 310000000u, 315000000u, 318000000u, 330000000u,
    345000000u, 390000000u, 418000000u, 433075000u, 433420000u, 433920000u, 434420000u,
    434775000u, 438900000u, 464000000u, 779000000u, 868350000u, 915000000u, 925000000u,
};
#define JAMMER_FREQ_COUNT (sizeof(jammer_freqs) / sizeof(jammer_freqs[0]))

static uint8_t jammer_freq_index_for(uint32_t frequency) {
    uint8_t best = 0;
    uint32_t best_gap = UINT32_MAX;
    for(uint8_t i = 0; i < JAMMER_FREQ_COUNT; i++) {
        const uint32_t gap = (jammer_freqs[i] > frequency) ? (jammer_freqs[i] - frequency) :
                                                             (frequency - jammer_freqs[i]);
        if(gap < best_gap) {
            best_gap = gap;
            best = i;
        }
    }
    return best;
}

static FuriHalSubGhzPreset jammer_preset(JammerModulation mod) {
    return (mod == JammerModAm) ? FuriHalSubGhzPresetOok650Async :
                                  FuriHalSubGhzPreset2FSKDev476Async;
}

static const char* jammer_mod_name(JammerModulation mod) {
    return (mod == JammerModAm) ? "AM" : "FM";
}

static void jammer_yield_radio(JammerApp* app) {
    if(!app->ext_radio || !app->radio_open) return;
    subghz_devices_idle(app->device);
    subghz_devices_sleep(app->device);
    subghz_devices_end(app->device);
    app->radio_open = false;
}

static bool jammer_claim_radio(JammerApp* app) {
    if(!app->ext_radio || app->radio_open) return true;
    if(!subghz_devices_begin(app->device)) {
        FURI_LOG_E(TAG, "External radio begin failed");
        subghz_devices_end(app->device);
        return false;
    }
    subghz_devices_reset(app->device);
    subghz_devices_idle(app->device);
    subghz_devices_load_preset(app->device, jammer_preset(app->modulation), NULL);
    app->radio_open = true;
    return true;
}

static int32_t jammer_tx_thread(void* context) {
    JammerApp* app = context;
    uint8_t data[JAMMER_DATA_LEN];

    if(app->modulation == JammerModAm) {
        memset(data, 0xFF, sizeof(data));
    } else {
        for(size_t i = 0; i < sizeof(data); i++)
            data[i] = (i & 1u) ? 0x55u : 0xAAu;
    }

    while(app->tx_running) {
        if(!subghz_tx_rx_worker_write(app->subghz_txrx, data, sizeof(data))) {
            furi_delay_ms(20);
        }
        furi_delay_ms(1);
    }
    return 0;
}

static void jammer_stop(JammerApp* app) {
    if(!app->jamming) return;
    app->tx_running = false;
    if(app->tx_thread) {
        furi_thread_join(app->tx_thread);
        furi_thread_free(app->tx_thread);
        app->tx_thread = NULL;
    }
    if(app->subghz_txrx && subghz_tx_rx_worker_is_running(app->subghz_txrx)) {
        subghz_tx_rx_worker_stop(app->subghz_txrx);
    }
    app->jamming = false;

    if(app->ext_radio && !jammer_claim_radio(app)) return;
    if(app->device) subghz_devices_idle(app->device);
}

static void jammer_start(JammerApp* app) {
    if(app->jamming || !app->device) return;

    jammer_yield_radio(app);

    if(!subghz_tx_rx_worker_start(app->subghz_txrx, app->device, app->frequency)) {
        FURI_LOG_E(TAG, "TX worker failed to start");
        if(subghz_tx_rx_worker_is_running(app->subghz_txrx)) {
            subghz_tx_rx_worker_stop(app->subghz_txrx);
        }
        jammer_claim_radio(app);
        return;
    }

    app->jamming = true;
    app->tx_running = true;
    app->tx_thread = furi_thread_alloc();
    furi_thread_set_name(app->tx_thread, "JammerTx");
    furi_thread_set_stack_size(app->tx_thread, 2048);
    furi_thread_set_context(app->tx_thread, app);
    furi_thread_set_callback(app->tx_thread, jammer_tx_thread);
    furi_thread_start(app->tx_thread);
}

static void jammer_poll_rssi(JammerApp* app) {
    if(app->jamming || !app->device) return;
    if(app->ext_radio && !app->radio_open) return;
    subghz_devices_idle(app->device);
    subghz_devices_set_frequency(app->device, app->frequency);
    subghz_devices_flush_rx(app->device);
    subghz_devices_set_rx(app->device);
    furi_delay_ms(3);
    app->rssi = subghz_devices_get_rssi(app->device);
    subghz_devices_idle(app->device);
}

static void jammer_step_frequency(JammerApp* app, bool up) {
    if(up) {
        if(app->freq_index + 1u < JAMMER_FREQ_COUNT) app->freq_index++;
    } else {
        if(app->freq_index > 0) app->freq_index--;
    }
    app->frequency = jammer_freqs[app->freq_index];

    if(app->jamming) {
        subghz_tx_rx_worker_stop(app->subghz_txrx);
        if(!subghz_tx_rx_worker_start(app->subghz_txrx, app->device, app->frequency)) {
            FURI_LOG_E(TAG, "retune failed");
        }
    }
}

static void jammer_toggle_modulation(JammerApp* app) {
    app->modulation = (app->modulation + 1u) % JammerModCount;
    if(app->jamming) {
        jammer_stop(app);
        jammer_start(app);
    }
}

static void jammer_input(JammerApp* app, InputKey key, InputType type) {
    const bool step = (type == InputTypePress) || (type == InputTypeRepeat);

    switch(key) {
    case InputKeyOk:
        if(type == InputTypeShort) {
            if(app->jamming) {
                jammer_stop(app);
            } else {
                jammer_start(app);
            }
        }
        break;
    case InputKeyLeft:
        if(step) jammer_step_frequency(app, false);
        break;
    case InputKeyRight:
        if(step) jammer_step_frequency(app, true);
        break;
    case InputKeyUp:
    case InputKeyDown:
        if(type == InputTypeShort) jammer_toggle_modulation(app);
        break;
    default:
        break;
    }
}

static uint8_t jammer_rssi_bar(float rssi) {
    if(rssi <= JAMMER_RSSI_MIN) return 0;
    if(rssi >= JAMMER_RSSI_MAX) return JAMMER_RSSI_W - 2;
    const float f = (rssi - JAMMER_RSSI_MIN) / (JAMMER_RSSI_MAX - JAMMER_RSSI_MIN);
    return (uint8_t)(f * (float)(JAMMER_RSSI_W - 2));
}

static void jammer_arrow(Canvas* canvas, int apex_x, int cy, bool point_left) {
    const int w = 5, hh = 3;
    for(int i = 0; i <= w; i++) {
        const int half = (hh * i + w / 2) / w;
        const int col = point_left ? (apex_x + i) : (apex_x - i);
        canvas_draw_line(canvas, col, cy - half, col, cy + half);
    }
}

static void jammer_vchevron(Canvas* canvas, int cx, int y, bool up) {
    const int dy = up ? 1 : -1;
    canvas_draw_line(canvas, cx - 3, y + dy * 2, cx, y - dy);
    canvas_draw_line(canvas, cx + 3, y + dy * 2, cx, y - dy);
}

static void jammer_draw(Canvas* canvas, void* context) {
    JammerApp* app = context;
    char buf[24];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 8, "Jammer");
    canvas_draw_icon(
        canvas, 109, 0, app->ext_radio ? &I_External_ant_1_9x11 : &I_Internal_ant_1_9x11);
    canvas_draw_line(canvas, 0, 10, 127, 10);

    snprintf(
        buf,
        sizeof(buf),
        "%03lu.%02lu",
        app->frequency / 1000000u,
        (app->frequency % 1000000u) / 10000u);

    canvas_set_font(canvas, FontBigNumbers);
    const int text_w = canvas_string_width(canvas, buf);
    const int text_x = 64 - text_w / 2;
    canvas_draw_str_aligned(canvas, 64, JAMMER_FREQ_TOP, AlignCenter, AlignTop, buf);

    const int digit_h = (int)canvas_get_font_params(canvas, FontBigNumbers)->height;
    const int cy = JAMMER_FREQ_TOP + digit_h / 2;
    jammer_arrow(canvas, text_x - 8, cy, true);
    jammer_arrow(canvas, text_x + text_w + 8, cy, false);

    if(app->jamming) {
        canvas_draw_box(canvas, 0, JAMMER_STATUS_Y - 9, 128, 12);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, JAMMER_STATUS_Y, AlignCenter, AlignBottom, "TRANSMITTING");
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_set_font(canvas, FontSecondary);

        const char* mod = jammer_mod_name(app->modulation);
        const int mod_w = canvas_string_width(canvas, mod);
        const int box_x = 2, box_w = mod_w + 6;
        const int box_cx = box_x + box_w / 2;
        canvas_draw_frame(canvas, box_x, JAMMER_STATUS_Y - 9, box_w, 11);
        canvas_draw_str(canvas, box_x + 3, JAMMER_STATUS_Y - 1, mod);
        jammer_vchevron(canvas, box_cx, JAMMER_STATUS_Y - 12, true);
        jammer_vchevron(canvas, box_cx, JAMMER_STATUS_Y + 4, false);

        snprintf(buf, sizeof(buf), "%d", (int)app->rssi);
        canvas_draw_str_aligned(canvas, 127, JAMMER_STATUS_Y - 8, AlignRight, AlignBottom, buf);
        canvas_draw_frame(canvas, JAMMER_RSSI_X, JAMMER_STATUS_Y - 6, JAMMER_RSSI_W, 6);
        const uint8_t bar = jammer_rssi_bar(app->rssi);
        if(bar) canvas_draw_box(canvas, JAMMER_RSSI_X + 1, JAMMER_STATUS_Y - 5, bar, 4);
    }

    elements_button_center(canvas, app->jamming ? "Stop" : "Jam");
}

static void jammer_input_callback(InputEvent* event, void* context) {
    JammerApp* app = context;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

JammerApp* jammer_app_alloc(void) {
    JammerApp* app = malloc(sizeof(JammerApp));
    memset(app, 0, sizeof(*app));

    app->modulation = JammerModAm;
    app->freq_index = jammer_freq_index_for(433920000u);
    app->frequency = jammer_freqs[app->freq_index];
    app->rssi = JAMMER_RSSI_MIN;

    app->gui = furi_record_open(RECORD_GUI);
    app->event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, jammer_draw, app);
    view_port_input_callback_set(app->view_port, jammer_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    furi_hal_power_suppress_charge_enter();

    subghz_devices_init();
    app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);
    if(!app->device) {
        app->device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeInternal);
    }
    app->ext_radio = radio_device_loader_is_external(app->device);
    app->radio_open = app->ext_radio;
    if(app->device) {
        subghz_devices_reset(app->device);
        subghz_devices_idle(app->device);
        subghz_devices_load_preset(app->device, jammer_preset(app->modulation), NULL);
    }

    app->subghz_txrx = subghz_tx_rx_worker_alloc();
    return app;
}

void jammer_app_free(JammerApp* app) {
    furi_assert(app);

    jammer_stop(app);

    if(app->subghz_txrx) {
        if(subghz_tx_rx_worker_is_running(app->subghz_txrx)) {
            subghz_tx_rx_worker_stop(app->subghz_txrx);
        }
        subghz_tx_rx_worker_free(app->subghz_txrx);
    }
    if(app->device) {
        if(app->ext_radio && !app->radio_open) jammer_claim_radio(app);
        if(!app->ext_radio || app->radio_open) {
            subghz_devices_sleep(app->device);
            radio_device_loader_end(app->device);
            app->radio_open = false;
        }
    }
    subghz_devices_deinit();

    furi_hal_power_suppress_charge_exit();

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t jammer_app(void* p) {
    UNUSED(p);
    JammerApp* app = jammer_app_alloc();

    if(!app->device) {
        FURI_LOG_E(TAG, "no radio device");
        jammer_app_free(app);
        return 0;
    }

    bool running = true;
    InputEvent event;
    while(running) {
        if(furi_message_queue_get(app->event_queue, &event, 100) == FuriStatusOk) {
            if(event.key == InputKeyBack && event.type == InputTypeShort) {
                running = false;
                continue;
            }
            jammer_input(app, event.key, event.type);
        } else {
            jammer_poll_rssi(app);
        }
        view_port_update(app->view_port);
    }

    jammer_app_free(app);
    return 0;
}

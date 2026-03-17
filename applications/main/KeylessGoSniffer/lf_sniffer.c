#include "lf_sniffer.h"

#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#include <furi_hal_cortex.h>
#include <gui/gui.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TAG "LFSniffer"

#define DWT_CYCCNT  (*(volatile uint32_t*)0xE0001004)
#define DWT_CTRL    (*(volatile uint32_t*)0xE0001000)
#define DEMCR       (*(volatile uint32_t*)0xE000EDFC)

static inline void dwt_init(void) {
    DEMCR     |= (1U << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL  |= 1U;
}

static inline uint32_t dwt_us(void) {
    return DWT_CYCCNT / 64;
}

static volatile LFEdge*   g_edges      = NULL;
static volatile uint32_t  g_edge_count = 0;
static volatile uint32_t  g_last_time  = 0;
static volatile bool      g_active     = false;
static volatile bool      g_overflow   = false;

static void lf_gpio_isr(void* ctx) {
    UNUSED(ctx);

    if(!g_active) return;

    uint32_t now   = dwt_us();
    uint32_t delta = now - g_last_time;
    g_last_time    = now;

    if(g_edge_count >= LF_MAX_EDGES) {
        g_overflow = true;
        g_active   = false;
        return;
    }

    bool current_level = furi_hal_gpio_read(LF_INPUT_PIN);

    g_edges[g_edge_count].duration_us = delta;
    g_edges[g_edge_count].level       = !current_level;
    g_edge_count++;
}

static void draw_callback(Canvas* canvas, void* ctx) {
    LFSnifferApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Keyless Sniffer - KeylessGO");

    canvas_set_font(canvas, FontSecondary);

    switch(app->state) {
    case LFStateIdle:
        canvas_draw_str(canvas, 2, 26, "Pin: PB2 (header pin 6)");
        canvas_draw_str(canvas, 2, 36, "OK  = Start capture");
        canvas_draw_str(canvas, 2, 46, "Approach car without key");
        canvas_draw_str(canvas, 2, 58, "Back = Exit");
        break;

    case LFStateCapturing: {
        char buf[32];
        canvas_draw_str(canvas, 2, 26, "CAPTURING...");
        snprintf(buf, sizeof(buf), "Edges: %lu", (unsigned long)app->edge_count);
        canvas_draw_str(canvas, 2, 36, buf);
        snprintf(buf, sizeof(buf), "Packets: %lu", (unsigned long)app->packet_count);
        canvas_draw_str(canvas, 2, 46, buf);
        canvas_draw_str(canvas, 2, 58, "OK/Back = Stop");
        break;
    }

    case LFStateDone: {
        char buf[48];
        snprintf(buf, sizeof(buf), "Edges:%lu  Pkts:%lu",
                 (unsigned long)app->edge_count,
                 (unsigned long)app->packet_count);
        canvas_draw_str(canvas, 2, 26, buf);
        snprintf(buf, sizeof(buf), "Min:%luus Max:%luus",
                 (unsigned long)app->min_pulse_us,
                 (unsigned long)app->max_pulse_us);
        canvas_draw_str(canvas, 2, 36, buf);
        canvas_draw_str(canvas, 2, 46, "OK = Save to SD");
        canvas_draw_str(canvas, 2, 58, "Back = Discard");
        break;
    }

    case LFStateSaving:
        canvas_draw_str(canvas, 2, 26, "Saving...");
        canvas_draw_str(canvas, 2, 36, app->filename);
        break;

    case LFStateSaved:
        canvas_draw_str(canvas, 2, 26, "Saved OK:");
        canvas_draw_str(canvas, 2, 36, app->filename);
        canvas_draw_str(canvas, 2, 46, app->status_msg);
        canvas_draw_str(canvas, 2, 58, "Back = New capture");
        break;

    case LFStateError:
        canvas_draw_str(canvas, 2, 26, "ERROR:");
        canvas_draw_str(canvas, 2, 36, app->status_msg);
        canvas_draw_str(canvas, 2, 58, "Back = Retry");
        break;
    }

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* ctx) {
    LFSnifferApp* app = ctx;
    furi_message_queue_put(app->queue, event, FuriWaitForever);
}

static void lf_analyze_packets(LFSnifferApp* app) {
    app->packet_count   = 0;
    app->min_pulse_us   = 0xFFFFFFFF;
    app->max_pulse_us   = 0;
    app->carrier_pulses = 0;
    app->data_pulses    = 0;

    if(app->edge_count < 4) return;

    uint32_t pkt_start = 0;
    bool     in_packet = false;

    for(uint32_t i = 1; i < app->edge_count; i++) {
        uint32_t dur = app->edges[i].duration_us;

        if(dur < app->min_pulse_us) app->min_pulse_us = dur;
        if(dur > app->max_pulse_us) app->max_pulse_us = dur;

        if(dur < 15) {
        } else if(dur < 12) {
            app->carrier_pulses++;
        } else {
            app->data_pulses++;
        }

        if(dur > LF_GAP_THRESHOLD_US) {
            if(in_packet && i > pkt_start + 8) {
                if(app->packet_count < LF_MAX_PACKETS) {
                    app->packets[app->packet_count].start_idx   = pkt_start;
                    app->packets[app->packet_count].edge_count  = i - pkt_start;
                    app->packets[app->packet_count].duration_us = 0;
                    for(uint32_t j = pkt_start; j < i; j++)
                        app->packets[app->packet_count].duration_us +=
                            app->edges[j].duration_us;
                    app->packet_count++;
                }
            }
            in_packet = false;
            pkt_start = i;
        } else {
            if(!in_packet) {
                in_packet = true;
                pkt_start = i;
            }
        }
    }

    if(in_packet && app->edge_count > pkt_start + 8) {
        if(app->packet_count < LF_MAX_PACKETS) {
            app->packets[app->packet_count].start_idx   = pkt_start;
            app->packets[app->packet_count].edge_count  = app->edge_count - pkt_start;
            app->packets[app->packet_count].duration_us = 0;
            for(uint32_t j = pkt_start; j < app->edge_count; j++)
                app->packets[app->packet_count].duration_us +=
                    app->edges[j].duration_us;
            app->packet_count++;
        }
    }
}

static bool lf_save_csv(LFSnifferApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    storage_common_mkdir(storage, "/ext/keyless_sniffer");

    snprintf(app->filename, sizeof(app->filename),
             "/ext/keyless_sniffer/capture_%04lu.csv",
             (unsigned long)app->file_index);

    File* file = storage_file_alloc(storage);
    bool  ok   = false;

    if(storage_file_open(file, app->filename, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const char* header = "index,duration_us,level,note\n";
        storage_file_write(file, header, strlen(header));

        char meta[128];
        snprintf(meta, sizeof(meta),
                 "# Keyless Sniffer capture -- edges:%lu packets:%lu\n",
                 (unsigned long)app->edge_count,
                 (unsigned long)app->packet_count);
        storage_file_write(file, meta, strlen(meta));

        snprintf(meta, sizeof(meta),
                 "# min_us:%lu max_us:%lu carrier:%lu data:%lu\n",
                 (unsigned long)app->min_pulse_us,
                 (unsigned long)app->max_pulse_us,
                 (unsigned long)app->carrier_pulses,
                 (unsigned long)app->data_pulses);
        storage_file_write(file, meta, strlen(meta));

        uint32_t pkt_idx = 0;
        char     line[64];

        for(uint32_t i = 0; i < app->edge_count; i++) {
            const char* note = "";
            if(pkt_idx < app->packet_count &&
               app->packets[pkt_idx].start_idx == i) {
                note = "PKT_START";
                pkt_idx++;
            }
            snprintf(line, sizeof(line),
                     "%lu,%lu,%d,%s\n",
                     (unsigned long)i,
                     (unsigned long)app->edges[i].duration_us,
                     app->edges[i].level ? 1 : 0,
                     note);
            storage_file_write(file, line, strlen(line));
        }

        storage_file_write(file, "# PACKETS\n", 10);
        for(uint32_t p = 0; p < app->packet_count; p++) {
            snprintf(meta, sizeof(meta),
                     "# PKT %lu: start=%lu edges=%lu dur=%luus\n",
                     (unsigned long)p,
                     (unsigned long)app->packets[p].start_idx,
                     (unsigned long)app->packets[p].edge_count,
                     (unsigned long)app->packets[p].duration_us);
            storage_file_write(file, meta, strlen(meta));
        }

        storage_file_close(file);
        ok = true;

        snprintf(app->status_msg, sizeof(app->status_msg),
                 "%lu edges, %lu packets",
                 (unsigned long)app->edge_count,
                 (unsigned long)app->packet_count);
    } else {
        snprintf(app->status_msg, sizeof(app->status_msg), "Failed to open file");
    }

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static void lf_start_capture(LFSnifferApp* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    app->edge_count     = 0;
    app->packet_count   = 0;
    app->total_time_us  = 0;
    app->min_pulse_us   = 0xFFFFFFFF;
    app->max_pulse_us   = 0;
    app->carrier_pulses = 0;
    app->data_pulses    = 0;
    g_overflow          = false;

    g_edges      = app->edges;
    g_edge_count = 0;
    g_last_time  = dwt_us();
    g_active     = true;

    furi_hal_gpio_init(LF_INPUT_PIN, GpioModeInterruptRiseFall,
                       GpioPullUp, GpioSpeedVeryHigh);
    furi_hal_gpio_add_int_callback(LF_INPUT_PIN, lf_gpio_isr, NULL);

    app->state = LFStateCapturing;
    furi_mutex_release(app->mutex);

    notification_message(app->notif, &sequence_blink_green_100);
    FURI_LOG_I(TAG, "Capture started");
}

static void lf_stop_capture(LFSnifferApp* app) {
    g_active = false;

    furi_hal_gpio_remove_int_callback(LF_INPUT_PIN);
    furi_hal_gpio_init(LF_INPUT_PIN, GpioModeInput, GpioPullUp, GpioSpeedLow);

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    app->edge_count = g_edge_count;

    lf_analyze_packets(app);
    app->state = LFStateDone;

    furi_mutex_release(app->mutex);

    notification_message(app->notif, &sequence_blink_blue_100);
    FURI_LOG_I(TAG, "Capture stopped: %lu edges, %lu packets",
               (unsigned long)app->edge_count,
               (unsigned long)app->packet_count);
}

int32_t lf_sniffer_app(void* p) {
    UNUSED(p);

    dwt_init();

    LFSnifferApp* app = malloc(sizeof(LFSnifferApp));
    furi_check(app != NULL);
    memset(app, 0, sizeof(LFSnifferApp));

    app->edges = malloc(LF_MAX_EDGES * sizeof(LFEdge));
    furi_check(app->edges != NULL);

    app->mutex      = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue      = furi_message_queue_alloc(16, sizeof(InputEvent));
    app->notif      = furi_record_open(RECORD_NOTIFICATION);
    app->view_port  = view_port_alloc();
    app->state      = LFStateIdle;
    app->file_index = 0;

    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    FURI_LOG_I(TAG, "Keyless Sniffer started");

    InputEvent event;
    bool running = true;

    while(running) {
        if(app->state == LFStateCapturing) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->edge_count = g_edge_count;

            if(g_overflow) {
                furi_mutex_release(app->mutex);
                lf_stop_capture(app);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                snprintf(app->status_msg, sizeof(app->status_msg),
                         "Buffer full (%d edges)", LF_MAX_EDGES);
            }
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
        }

        FuriStatus status = furi_message_queue_get(app->queue, &event, 100);

        if(status != FuriStatusOk) continue;
        if(event.type != InputTypeShort && event.type != InputTypeLong) continue;

        switch(app->state) {
        case LFStateIdle:
            if(event.key == InputKeyOk) {
                lf_start_capture(app);
                view_port_update(app->view_port);
            } else if(event.key == InputKeyBack) {
                running = false;
            }
            break;

        case LFStateCapturing:
            if(event.key == InputKeyOk || event.key == InputKeyBack) {
                lf_stop_capture(app);
                view_port_update(app->view_port);
            }
            break;

        case LFStateDone:
            if(event.key == InputKeyOk) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->state = LFStateSaving;
                furi_mutex_release(app->mutex);
                view_port_update(app->view_port);

                bool saved = lf_save_csv(app);

                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->state = saved ? LFStateSaved : LFStateError;
                app->file_index++;
                furi_mutex_release(app->mutex);
                view_port_update(app->view_port);

                if(saved) {
                    notification_message(app->notif, &sequence_success);
                    FURI_LOG_I(TAG, "Saved: %s", app->filename);
                }
            } else if(event.key == InputKeyBack) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->state    = LFStateIdle;
                app->edge_count   = 0;
                app->packet_count = 0;
                furi_mutex_release(app->mutex);
                view_port_update(app->view_port);
            }
            break;

        case LFStateSaved:
        case LFStateError:
            if(event.key == InputKeyBack) {
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->state    = LFStateIdle;
                app->edge_count   = 0;
                app->packet_count = 0;
                furi_mutex_release(app->mutex);
                view_port_update(app->view_port);
            }
            break;

        default:
            break;
        }
    }

    if(app->state == LFStateCapturing) {
        g_active = false;
        furi_hal_gpio_remove_int_callback(LF_INPUT_PIN);
        furi_hal_gpio_init(LF_INPUT_PIN, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    }

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_mutex_free(app->mutex);
    furi_message_queue_free(app->queue);
    free(app->edges);
    free(app);

    return 0;
}
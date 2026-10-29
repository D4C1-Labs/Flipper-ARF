#include "nfc_sniffer_monitor.h"

#include <gui/elements.h>
#include <input/input.h>

#include <stdio.h>
#include <string.h>

#define NFC_SNIFFER_MONITOR_LINES    (3U)
#define NFC_SNIFFER_MONITOR_SNAP_MAX (16U)
#define NFC_SNIFFER_MONITOR_LINE_H   (12U)
#define NFC_SNIFFER_MONITOR_LIST_TOP (12U)

struct NfcSnifferMonitor {
    View* view;
    NfcSnifferTrace* trace;
    NfcSnifferWorker* worker;
    NfcSnifferMonitorCallback callback;
    void* callback_context;
};

typedef struct {
    NfcSnifferFrame frames[NFC_SNIFFER_MONITOR_SNAP_MAX];
    size_t frame_count;

    uint32_t total_pcd;
    uint32_t total_picc;

    NfcSnifferState state;
    bool running;
    uint8_t coupling;

    size_t scroll;
} NfcSnifferMonitorModel;

static const char* nfc_sniffer_state_label(NfcSnifferState state) {
    switch(state) {
    case NfcSnifferStateWaitField:
        return "WAIT";
    case NfcSnifferStateSniffing:
        return "SNIF";
    case NfcSnifferStateError:
        return "ERR";
    case NfcSnifferStateStopped:
    default:
        return "STOP";
    }
}

static void nfc_sniffer_monitor_fill_model(NfcSnifferMonitor* monitor, NfcSnifferMonitorModel* m) {
    m->frame_count =
        nfc_sniffer_trace_snapshot(monitor->trace, m->frames, NFC_SNIFFER_MONITOR_SNAP_MAX);
    m->total_pcd = nfc_sniffer_trace_count_dir(monitor->trace, NfcSnifferDirPcd);
    m->total_picc = nfc_sniffer_trace_count_dir(monitor->trace, NfcSnifferDirPicc);
    m->state = nfc_sniffer_worker_get_state(monitor->worker);
    m->running = nfc_sniffer_worker_is_running(monitor->worker);
    m->coupling = nfc_sniffer_worker_coupling(monitor->worker);

    size_t max_scroll = 0;
    if(m->frame_count > NFC_SNIFFER_MONITOR_LINES) {
        max_scroll = m->frame_count - NFC_SNIFFER_MONITOR_LINES;
    }
    if(m->scroll > max_scroll) m->scroll = max_scroll;
}

static char nfc_sniffer_monitor_format_frame(
    const NfcSnifferFrame* f,
    uint32_t gap_us,
    char* out,
    size_t out_size) {
    char dir = '?';
    if(f->dir == NfcSnifferDirPcd)
        dir = 'R';
    else if(f->dir == NfcSnifferDirPicc)
        dir = 'C';
    else
        dir = 'L';

    char gap[12];
    if(gap_us == 0) {
        snprintf(gap, sizeof(gap), "   0");
    } else if(gap_us < 10000) {
        snprintf(gap, sizeof(gap), "%4lu", (unsigned long)gap_us);
    } else if(gap_us < 1000000) {
        snprintf(gap, sizeof(gap), "%3lum", (unsigned long)(gap_us / 1000));
    } else {
        snprintf(gap, sizeof(gap), "%3lus", (unsigned long)(gap_us / 1000000));
    }

    if(f->dir == NfcSnifferDirEvent) {
        int w = snprintf(out, out_size, "%s %c ", gap, dir);
        if(w < 0) w = 0;
        if((size_t)w < out_size && f->len) {
            size_t avail = out_size - (size_t)w - 1;
            size_t n = f->len < avail ? f->len : avail;
            memcpy(out + w, f->data, n);
            out[w + n] = '\0';
        }
        return dir;
    }

    int written = snprintf(out, out_size, "%s %c", gap, dir);
    if(written < 0) written = 0;
    size_t max_bytes = 14;
    for(size_t i = 0; i < f->len && i < max_bytes && (size_t)written + 3 < out_size; i++) {
        written += snprintf(out + written, out_size - (size_t)written, " %02X", f->data[i]);
    }
    if((f->truncated || f->len > max_bytes) && (size_t)written + 3 < out_size) {
        snprintf(out + written, out_size - (size_t)written, "..");
    }
    return dir;
}

static void nfc_sniffer_monitor_draw_status(Canvas* canvas, NfcSnifferMonitorModel* m) {
    canvas_set_font(canvas, FontSecondary);
    canvas_set_color(canvas, ColorBlack);

    canvas_draw_str(canvas, 0, 8, nfc_sniffer_state_label(m->state));

    if(m->running) {
        const char* cpl = (m->coupling == 0) ? "GOOD" :
                          (m->coupling == 1) ? "OK" :
                          (m->coupling == 2) ? "WEAK" :
                                               "--";
        if(m->coupling == 2) {
            canvas_draw_box(canvas, 28, 0, 26, 10);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 31, 8, cpl);
            canvas_set_color(canvas, ColorBlack);
        } else {
            canvas_draw_str(canvas, 31, 8, cpl);
        }
    }

    char line[24];
    snprintf(
        line, sizeof(line), "R%lu C%lu", (unsigned long)m->total_pcd, (unsigned long)m->total_picc);
    canvas_draw_str_aligned(canvas, 128, 8, AlignRight, AlignBottom, line);
}

static void nfc_sniffer_monitor_draw_frames(Canvas* canvas, NfcSnifferMonitorModel* m) {
    canvas_set_font(canvas, FontSecondary);

    if(m->frame_count == 0) {
        canvas_set_color(canvas, ColorBlack);
        if(m->running) {
            canvas_draw_str(canvas, 2, 26, "Waiting for field...");
            canvas_draw_str(canvas, 2, 38, "Hold on the reader");
            canvas_draw_str(canvas, 2, 50, "until it reads GOOD");
        } else {
            canvas_draw_str(canvas, 2, 30, "Passive NFC sniffer");
            canvas_draw_str(canvas, 2, 42, "OK: start sniffing");
        }
        return;
    }

    size_t end = m->frame_count - m->scroll;
    size_t start = (end > NFC_SNIFFER_MONITOR_LINES) ? (end - NFC_SNIFFER_MONITOR_LINES) : 0;

    char text[48];
    for(size_t i = start; i < end; i++) {
        uint8_t row = (uint8_t)(i - start);
        uint8_t y = (uint8_t)(NFC_SNIFFER_MONITOR_LIST_TOP + row * NFC_SNIFFER_MONITOR_LINE_H);

        uint32_t gap = 0;
        if(i > 0) {
            uint32_t prev = m->frames[i - 1].timestamp_us;
            uint32_t cur = m->frames[i].timestamp_us;
            gap = (cur >= prev) ? (cur - prev) : 0;
        }

        char dir = nfc_sniffer_monitor_format_frame(&m->frames[i], gap, text, sizeof(text));

        if(dir == 'R') {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y, 122, NFC_SNIFFER_MONITOR_LINE_H - 1);
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_str(canvas, 1, y + 8, text);
        } else {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_str(canvas, 1, y + 8, text);
        }
    }

    if(m->frame_count > NFC_SNIFFER_MONITOR_LINES) {
        size_t max_scroll = m->frame_count - NFC_SNIFFER_MONITOR_LINES;
        size_t pos = max_scroll - m->scroll;
        elements_scrollbar_pos(
            canvas,
            128,
            NFC_SNIFFER_MONITOR_LIST_TOP,
            NFC_SNIFFER_MONITOR_LINES * NFC_SNIFFER_MONITOR_LINE_H,
            pos,
            max_scroll + 1);
    }
}

static void nfc_sniffer_monitor_draw(Canvas* canvas, void* model) {
    NfcSnifferMonitorModel* m = model;
    canvas_clear(canvas);

    nfc_sniffer_monitor_draw_status(canvas, m);
    canvas_draw_line(canvas, 0, 10, 127, 10);
    nfc_sniffer_monitor_draw_frames(canvas, m);

    canvas_set_color(canvas, ColorBlack);
    if(m->running) {
        elements_button_center(canvas, "Stop");
    } else {
        elements_button_center(canvas, "Sniff");
    }
}

static void nfc_sniffer_monitor_emit(NfcSnifferMonitor* monitor, NfcSnifferMonitorEvent event) {
    if(monitor->callback) monitor->callback(event, monitor->callback_context);
}

static bool nfc_sniffer_monitor_input(InputEvent* event, void* context) {
    furi_assert(context);
    NfcSnifferMonitor* monitor = context;

    if(event->key == InputKeyBack) {
        return false;
    }

    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            with_view_model(
                monitor->view,
                NfcSnifferMonitorModel * m,
                {
                    size_t max_scroll = 0;
                    if(m->frame_count > NFC_SNIFFER_MONITOR_LINES) {
                        max_scroll = m->frame_count - NFC_SNIFFER_MONITOR_LINES;
                    }
                    if(m->scroll < max_scroll) m->scroll++;
                },
                true);
            consumed = true;
        } else if(event->key == InputKeyDown) {
            with_view_model(
                monitor->view,
                NfcSnifferMonitorModel * m,
                {
                    if(m->scroll > 0) m->scroll--;
                },
                true);
            consumed = true;
        }
    }

    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        nfc_sniffer_monitor_emit(monitor, NfcSnifferMonitorEventToggleRun);
        consumed = true;
    }

    if(consumed) {
        with_view_model(
            monitor->view,
            NfcSnifferMonitorModel * m,
            {
                size_t keep_scroll = m->scroll;
                nfc_sniffer_monitor_fill_model(monitor, m);
                m->scroll = keep_scroll;
            },
            true);
    }

    return consumed;
}

NfcSnifferMonitor* nfc_sniffer_monitor_alloc(NfcSnifferTrace* trace, NfcSnifferWorker* worker) {
    furi_assert(trace);
    furi_assert(worker);

    NfcSnifferMonitor* monitor = malloc(sizeof(NfcSnifferMonitor));
    monitor->trace = trace;
    monitor->worker = worker;
    monitor->callback = NULL;
    monitor->callback_context = NULL;

    monitor->view = view_alloc();
    view_allocate_model(monitor->view, ViewModelTypeLocking, sizeof(NfcSnifferMonitorModel));
    view_set_context(monitor->view, monitor);
    view_set_draw_callback(monitor->view, nfc_sniffer_monitor_draw);
    view_set_input_callback(monitor->view, nfc_sniffer_monitor_input);

    with_view_model(
        monitor->view,
        NfcSnifferMonitorModel * m,
        {
            memset(m, 0, sizeof(NfcSnifferMonitorModel));
            nfc_sniffer_monitor_fill_model(monitor, m);
        },
        true);

    return monitor;
}

void nfc_sniffer_monitor_free(NfcSnifferMonitor* monitor) {
    furi_assert(monitor);
    view_free(monitor->view);
    free(monitor);
}

View* nfc_sniffer_monitor_get_view(NfcSnifferMonitor* monitor) {
    furi_assert(monitor);
    return monitor->view;
}

void nfc_sniffer_monitor_set_callback(
    NfcSnifferMonitor* monitor,
    NfcSnifferMonitorCallback callback,
    void* context) {
    furi_assert(monitor);
    monitor->callback = callback;
    monitor->callback_context = context;
}

void nfc_sniffer_monitor_refresh(NfcSnifferMonitor* monitor) {
    furi_assert(monitor);
    with_view_model(
        monitor->view,
        NfcSnifferMonitorModel * m,
        {
            bool follow = (m->scroll == 0);
            size_t keep = m->scroll;
            nfc_sniffer_monitor_fill_model(monitor, m);
            m->scroll = follow ? 0 : keep;
        },
        true);
}

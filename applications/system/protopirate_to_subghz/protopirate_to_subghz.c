// protopirate_to_subghz.c - app shell: alloc/free/entry, submenu, views,
// worker thread, custom events, summary.
#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>
#include <toolbox/dir_walk.h>
#include <string.h>
#include <strings.h>

#include "converter.h"
#include "paths.h"

#define TAG "P2s"

typedef enum {
    P2sSubmenuIndexPsfToSub,
    P2sSubmenuIndexSubToPsf,
    P2sSubmenuIndexAbout,
} P2sSubmenuIndex;

typedef enum {
    P2sViewSubmenu,
    P2sViewProgress,
    P2sViewSummary,
} P2sView;

typedef enum {
    P2sDirectionPsfToSub,
    P2sDirectionSubToPsf,
} P2sDirection;

// Custom events posted by the worker thread to the UI thread.
typedef enum {
    P2sEventProgress, // update the progress view from the model
    P2sEventFinished, // switch to the summary view
} P2sEvent;

// Worker thread control flag.
#define P2S_WORKER_FLAG_STOP (1u << 0)

typedef struct {
    // progress
    uint32_t total;
    uint32_t current;
    // results
    uint32_t converted;
    uint32_t skipped;
    uint32_t errors;
} P2sProgressModel;

typedef struct {
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    View* view_progress;
    Widget* widget_summary;

    FuriThread* worker;
    volatile bool worker_running;
    P2sDirection direction;
} P2sApp;

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------
static uint32_t p2s_nav_exit_callback(void* ctx) {
    UNUSED(ctx);
    return VIEW_NONE;
}

static uint32_t p2s_nav_submenu_callback(void* ctx) {
    UNUSED(ctx);
    return P2sViewSubmenu;
}

// ---------------------------------------------------------------------------
// Progress view drawing
// ---------------------------------------------------------------------------
static void p2s_progress_draw_callback(Canvas* canvas, void* model) {
    P2sProgressModel* m = (P2sProgressModel*)model;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Converting...");
    canvas_set_font(canvas, FontSecondary);

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu / %lu", (unsigned long)m->current, (unsigned long)m->total);
    canvas_draw_str(canvas, 2, 28, buf);

    snprintf(
        buf,
        sizeof(buf),
        "OK:%lu Skip:%lu Err:%lu",
        (unsigned long)m->converted,
        (unsigned long)m->skipped,
        (unsigned long)m->errors);
    canvas_draw_str(canvas, 2, 44, buf);

    canvas_draw_str(canvas, 2, 60, "Back to cancel");
}

// Back on the progress view: request the worker to stop.
static bool p2s_progress_input_callback(InputEvent* event, void* context) {
    P2sApp* app = (P2sApp*)context;
    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        if(app->worker_running) {
            furi_thread_flags_set(furi_thread_get_id(app->worker), P2S_WORKER_FLAG_STOP);
        }
        return true; // swallow back; the worker will post Finished
    }
    return false;
}

// ---------------------------------------------------------------------------
// dir_walk filter callbacks
// ---------------------------------------------------------------------------
typedef struct {
    const char* ext; // extension to accept
} P2sWalkFilter;

static bool p2s_has_ext(const char* name, const char* ext) {
    size_t nl = strlen(name);
    size_t el = strlen(ext);
    if(nl < el) return false;
    return strcasecmp(name + (nl - el), ext) == 0;
}

static bool p2s_filter_cb(const char* name, FileInfo* fileinfo, void* ctx) {
    P2sWalkFilter* f = (P2sWalkFilter*)ctx;
    if(file_info_is_dir(fileinfo)) {
        return false; // don't emit directories
    }
    return p2s_has_ext(name, f->ext);
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------
static bool p2s_worker_should_stop(void) {
    return (furi_thread_flags_get() & P2S_WORKER_FLAG_STOP) != 0;
}

static void p2s_worker_post_progress(P2sApp* app) {
    view_dispatcher_send_custom_event(app->view_dispatcher, P2sEventProgress);
}

static int32_t p2s_worker_thread(void* context) {
    P2sApp* app = (P2sApp*)context;

    const char* root = (app->direction == P2sDirectionPsfToSub) ? PP_SAVED_DIR : SUBGHZ_DIR;
    P2sWalkFilter filter = {
        .ext = (app->direction == P2sDirectionPsfToSub) ? PP_EXTENSION : SUB_EXTENSION};

    Storage* storage = furi_record_open(RECORD_STORAGE);

    // First pass: count matching files for the total.
    uint32_t total = 0;
    {
        DirWalk* dir_walk = dir_walk_alloc(storage);
        dir_walk_set_recursive(dir_walk, true);
        dir_walk_set_filter_cb(dir_walk, p2s_filter_cb, &filter);
        if(dir_walk_open(dir_walk, root)) {
            FuriString* path = furi_string_alloc();
            FileInfo info;
            while(dir_walk_read(dir_walk, path, &info) == DirWalkOK) {
                total++;
            }
            furi_string_free(path);
        }
        dir_walk_close(dir_walk);
        dir_walk_free(dir_walk);
    }

    with_view_model(
        app->view_progress,
        P2sProgressModel * m,
        {
            m->total = total;
            m->current = 0;
            m->converted = 0;
            m->skipped = 0;
            m->errors = 0;
        },
        false);
    p2s_worker_post_progress(app);

    // Second pass: convert.
    DirWalk* dir_walk = dir_walk_alloc(storage);
    dir_walk_set_recursive(dir_walk, true);
    dir_walk_set_filter_cb(dir_walk, p2s_filter_cb, &filter);

    if(dir_walk_open(dir_walk, root)) {
        FuriString* path = furi_string_alloc();
        FileInfo info;
        while(dir_walk_read(dir_walk, path, &info) == DirWalkOK) {
            if(p2s_worker_should_stop()) {
                break;
            }

            P2sResult r;
            if(app->direction == P2sDirectionPsfToSub) {
                r = p2s_convert_psf_to_sub(furi_string_get_cstr(path));
            } else {
                r = p2s_convert_sub_to_psf(furi_string_get_cstr(path));
            }

            with_view_model(
                app->view_progress,
                P2sProgressModel * m,
                {
                    m->current++;
                    if(r == P2sResultOk) {
                        m->converted++;
                    } else if(r == P2sResultSkipped) {
                        m->skipped++;
                    } else {
                        m->errors++;
                    }
                },
                false);
            p2s_worker_post_progress(app);
        }
        furi_string_free(path);
    }
    dir_walk_close(dir_walk);
    dir_walk_free(dir_walk);

    furi_record_close(RECORD_STORAGE);

    app->worker_running = false;
    view_dispatcher_send_custom_event(app->view_dispatcher, P2sEventFinished);
    return 0;
}

static void p2s_start_worker(P2sApp* app, P2sDirection direction) {
    app->direction = direction;
    app->worker_running = true;

    // Reset the progress model.
    with_view_model(
        app->view_progress,
        P2sProgressModel * m,
        {
            m->total = 0;
            m->current = 0;
            m->converted = 0;
            m->skipped = 0;
            m->errors = 0;
        },
        true);

    app->worker = furi_thread_alloc_ex("P2sWorker", 4 * 1024, p2s_worker_thread, app);
    furi_thread_start(app->worker);

    view_dispatcher_switch_to_view(app->view_dispatcher, P2sViewProgress);
}

static void p2s_join_worker(P2sApp* app) {
    if(app->worker) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------
static void p2s_show_summary(P2sApp* app) {
    p2s_join_worker(app);

    uint32_t converted = 0, skipped = 0, errors = 0;
    with_view_model(
        app->view_progress,
        P2sProgressModel * m,
        {
            converted = m->converted;
            skipped = m->skipped;
            errors = m->errors;
        },
        false);

    FuriString* text = furi_string_alloc();
    furi_string_printf(
        text,
        "Done!\n\nConverted: %lu\nSkipped: %lu\nErrors: %lu",
        (unsigned long)converted,
        (unsigned long)skipped,
        (unsigned long)errors);

    widget_reset(app->widget_summary);
    widget_add_text_scroll_element(
        app->widget_summary, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, P2sViewSummary);
}

// ---------------------------------------------------------------------------
// Custom events (UI thread)
// ---------------------------------------------------------------------------
static bool p2s_custom_event_callback(void* context, uint32_t event) {
    P2sApp* app = (P2sApp*)context;
    switch(event) {
    case P2sEventProgress: {
        bool redraw = true;
        with_view_model(
            app->view_progress, P2sProgressModel * m, { UNUSED(m); }, redraw);
        return true;
    }
    case P2sEventFinished:
        p2s_show_summary(app);
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Submenu
// ---------------------------------------------------------------------------
static void p2s_submenu_callback(void* context, uint32_t index) {
    P2sApp* app = (P2sApp*)context;
    switch(index) {
    case P2sSubmenuIndexPsfToSub:
        p2s_start_worker(app, P2sDirectionPsfToSub);
        break;
    case P2sSubmenuIndexSubToPsf:
        p2s_start_worker(app, P2sDirectionSubToPsf);
        break;
    case P2sSubmenuIndexAbout:
        widget_reset(app->widget_summary);
        widget_add_text_scroll_element(
            app->widget_summary,
            0,
            0,
            128,
            64,
            "ProtoPirate <-> SubGhz\n\n"
            "Converts car captures\n"
            "between ProtoPirate .psf\n"
            "and standard .sub files.\n\n"
            ".psf -> .sub writes to\n"
            "/ext/subghz/cars/<Brand>/\n"
            "with translated protocol\n"
            "names so they load in the\n"
            "main Sub-GHz app.\n\n"
            ".sub -> .psf writes to\n"
            "/ext/apps_data/\n"
            "proto_pirate/saved/");
        view_dispatcher_switch_to_view(app->view_dispatcher, P2sViewSummary);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Alloc / free
// ---------------------------------------------------------------------------
static P2sApp* p2s_app_alloc(void) {
    P2sApp* app = malloc(sizeof(P2sApp));
    app->worker = NULL;
    app->worker_running = false;
    app->direction = P2sDirectionPsfToSub;

    Gui* gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, p2s_custom_event_callback);

    // Submenu
    app->submenu = submenu_alloc();
    submenu_add_item(
        app->submenu,
        "ProtoPirate -> SubGhz (cars/)",
        P2sSubmenuIndexPsfToSub,
        p2s_submenu_callback,
        app);
    submenu_add_item(
        app->submenu, "SubGhz -> ProtoPirate", P2sSubmenuIndexSubToPsf, p2s_submenu_callback, app);
    submenu_add_item(app->submenu, "About", P2sSubmenuIndexAbout, p2s_submenu_callback, app);
    view_set_previous_callback(submenu_get_view(app->submenu), p2s_nav_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, P2sViewSubmenu, submenu_get_view(app->submenu));

    // Progress view
    app->view_progress = view_alloc();
    view_set_context(app->view_progress, app);
    view_set_draw_callback(app->view_progress, p2s_progress_draw_callback);
    view_set_input_callback(app->view_progress, p2s_progress_input_callback);
    view_allocate_model(
        app->view_progress, ViewModelTypeLocking, sizeof(P2sProgressModel));
    view_dispatcher_add_view(app->view_dispatcher, P2sViewProgress, app->view_progress);

    // Summary widget
    app->widget_summary = widget_alloc();
    view_set_previous_callback(widget_get_view(app->widget_summary), p2s_nav_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, P2sViewSummary, widget_get_view(app->widget_summary));

    view_dispatcher_switch_to_view(app->view_dispatcher, P2sViewSubmenu);
    return app;
}

static void p2s_app_free(P2sApp* app) {
    // Make sure the worker is stopped and joined before tearing down views.
    if(app->worker) {
        if(app->worker_running) {
            furi_thread_flags_set(furi_thread_get_id(app->worker), P2S_WORKER_FLAG_STOP);
        }
        p2s_join_worker(app);
    }

    view_dispatcher_remove_view(app->view_dispatcher, P2sViewSummary);
    widget_free(app->widget_summary);
    view_dispatcher_remove_view(app->view_dispatcher, P2sViewProgress);
    view_free(app->view_progress);
    view_dispatcher_remove_view(app->view_dispatcher, P2sViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    free(app);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int32_t protopirate_to_subghz_app(void* p) {
    UNUSED(p);
    P2sApp* app = p2s_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    p2s_app_free(app);
    return 0;
}

// [HITAG2_BF] Hitag2 Bruteforce view - mirrors PSA Decrypt layout with level info

#include "subghz_hitag2_bf.h"

#include <gui/elements.h>
#include <furi.h>

struct SubGhzViewHitag2Bf {
    View* view;
    SubGhzViewHitag2BfCallback callback;
    void* context;
};

typedef struct {
    uint8_t level;                // 1..5
    char level_name[16];
    uint8_t progress;             // 0..100
    uint64_t keys_tested;
    uint32_t keys_per_sec;
    uint32_t elapsed_sec;
    uint32_t eta_sec;
    uint8_t captures;
    bool done;
    bool success;
    FuriString* result_str;
    char status_line[40];
} SubGhzHitag2BfModel;

static void
    subghz_view_hitag2_bf_format_count(char* buf, size_t len, uint64_t count) {
    if(count >= 1000000000ULL) {
        snprintf(
            buf,
            len,
            "%lu.%luG",
            (unsigned long)(count / 1000000000ULL),
            (unsigned long)((count % 1000000000ULL) / 100000000ULL));
    } else if(count >= 1000000ULL) {
        snprintf(
            buf,
            len,
            "%lu.%luM",
            (unsigned long)(count / 1000000ULL),
            (unsigned long)((count % 1000000ULL) / 100000ULL));
    } else if(count >= 1000ULL) {
        snprintf(buf, len, "%luK", (unsigned long)(count / 1000ULL));
    } else {
        snprintf(buf, len, "%lu", (unsigned long)count);
    }
}

static void subghz_view_hitag2_bf_draw(Canvas* canvas, void* _model) {
    SubGhzHitag2BfModel* model = (SubGhzHitag2BfModel*)_model;

    canvas_clear(canvas);

    if(!model->done) {
        // Title with level
        canvas_set_font(canvas, FontPrimary);
        char title[40];
        if(model->status_line[0]) {
            canvas_draw_str_aligned(
                canvas, 64, 2, AlignCenter, AlignTop, model->status_line);
        } else {
            snprintf(
                title,
                sizeof(title),
                "L%u: %s",
                model->level,
                model->level_name[0] ? model->level_name : "Cracking...");
            canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, title);
        }

        // Progress bar
        canvas_draw_rframe(canvas, 3, 15, 122, 12, 2);
        uint8_t fill = (uint8_t)((uint16_t)model->progress * 116U / 100U);
        if(fill > 2) {
            canvas_draw_rbox(canvas, 5, 17, fill, 8, 1);
        } else if(fill > 0) {
            canvas_draw_box(canvas, 5, 17, fill, 8);
        }

        canvas_set_font(canvas, FontSecondary);

        // Keys line: "42% - 13.4M keys | 2 caps"
        char keys_str[40];
        char tested_buf[16];
        subghz_view_hitag2_bf_format_count(tested_buf, sizeof(tested_buf), model->keys_tested);
        snprintf(
            keys_str,
            sizeof(keys_str),
            "%u%% - %s keys | %u cap%s",
            model->progress,
            tested_buf,
            model->captures,
            model->captures == 1 ? "" : "s");
        canvas_draw_str(canvas, 2, 38, keys_str);

        // Speed + ETA
        char speed_str[40];
        char speed_buf[12];
        subghz_view_hitag2_bf_format_count(
            speed_buf, sizeof(speed_buf), model->keys_per_sec);
        uint32_t eta_m = model->eta_sec / 60U;
        uint32_t eta_s = model->eta_sec % 60U;
        if(model->eta_sec >= 3600U) {
            uint32_t eta_h = model->eta_sec / 3600U;
            snprintf(
                speed_str,
                sizeof(speed_str),
                "%s/s  ETA %luh %lum",
                speed_buf,
                (unsigned long)eta_h,
                (unsigned long)((model->eta_sec % 3600U) / 60U));
        } else if(eta_m > 0) {
            snprintf(
                speed_str,
                sizeof(speed_str),
                "%s/s  ETA %lum %lus",
                speed_buf,
                (unsigned long)eta_m,
                (unsigned long)eta_s);
        } else {
            snprintf(
                speed_str,
                sizeof(speed_str),
                "%s/s  ETA %lus",
                speed_buf,
                (unsigned long)eta_s);
        }
        canvas_draw_str(canvas, 2, 48, speed_str);

        // Elapsed
        char elapsed_str[24];
        uint32_t el_m = model->elapsed_sec / 60U;
        uint32_t el_s = model->elapsed_sec % 60U;
        if(model->elapsed_sec >= 3600U) {
            snprintf(
                elapsed_str,
                sizeof(elapsed_str),
                "Elapsed: %luh %lum",
                (unsigned long)(model->elapsed_sec / 3600U),
                (unsigned long)((model->elapsed_sec % 3600U) / 60U));
        } else if(el_m > 0) {
            snprintf(
                elapsed_str,
                sizeof(elapsed_str),
                "Elapsed: %lum %lus",
                (unsigned long)el_m,
                (unsigned long)el_s);
        } else {
            snprintf(
                elapsed_str,
                sizeof(elapsed_str),
                "Elapsed: %lus",
                (unsigned long)el_s);
        }
        canvas_draw_str(canvas, 2, 58, elapsed_str);

        // Cancel hint
        canvas_draw_str_aligned(canvas, 126, 64, AlignRight, AlignBottom, "Hold BACK");
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, 64, 4, AlignCenter, AlignTop,
            model->success ? "Key Found!" : "Not Cracked");

        if(model->result_str) {
            canvas_set_font(canvas, FontSecondary);
            elements_multiline_text_aligned(
                canvas,
                64,
                20,
                AlignCenter,
                AlignTop,
                furi_string_get_cstr(model->result_str));
        }

        elements_button_center(canvas, "Ok");
    }
}

static bool subghz_view_hitag2_bf_input(InputEvent* event, void* context) {
    SubGhzViewHitag2Bf* instance = (SubGhzViewHitag2Bf*)context;

    if(event->key == InputKeyBack || event->key == InputKeyOk) {
        if(instance->callback) {
            instance->callback(SubGhzCustomEventViewTransmitterBack, instance->context);
        }
        return true;
    }
    return false;
}

SubGhzViewHitag2Bf* subghz_view_hitag2_bf_alloc(void) {
    SubGhzViewHitag2Bf* instance = malloc(sizeof(SubGhzViewHitag2Bf));
    instance->view = view_alloc();
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(SubGhzHitag2BfModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, subghz_view_hitag2_bf_draw);
    view_set_input_callback(instance->view, subghz_view_hitag2_bf_input);

    with_view_model(
        instance->view,
        SubGhzHitag2BfModel * model,
        {
            model->result_str = furi_string_alloc();
            model->level = 1;
            model->level_name[0] = '\0';
            model->progress = 0;
            model->keys_tested = 0;
            model->keys_per_sec = 0;
            model->elapsed_sec = 0;
            model->eta_sec = 0;
            model->captures = 1;
            model->done = false;
            model->success = false;
        },
        false);

    return instance;
}

void subghz_view_hitag2_bf_free(SubGhzViewHitag2Bf* instance) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzHitag2BfModel * model,
        { furi_string_free(model->result_str); },
        false);
    view_free(instance->view);
    free(instance);
}

View* subghz_view_hitag2_bf_get_view(SubGhzViewHitag2Bf* instance) {
    furi_check(instance);
    return instance->view;
}

void subghz_view_hitag2_bf_set_callback(
    SubGhzViewHitag2Bf* instance,
    SubGhzViewHitag2BfCallback callback,
    void* context) {
    furi_check(instance);
    instance->callback = callback;
    instance->context = context;
}

void subghz_view_hitag2_bf_update_stats(
    SubGhzViewHitag2Bf* instance,
    uint8_t level,
    const char* level_name,
    uint8_t progress,
    uint64_t keys_tested,
    uint32_t keys_per_sec,
    uint32_t elapsed_sec,
    uint32_t eta_sec,
    uint8_t captures) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzHitag2BfModel * model,
        {
            model->level = level;
            if(level_name) {
                strlcpy(model->level_name, level_name, sizeof(model->level_name));
            }
            model->progress = progress;
            model->keys_tested = keys_tested;
            model->keys_per_sec = keys_per_sec;
            model->elapsed_sec = elapsed_sec;
            model->eta_sec = eta_sec;
            model->captures = captures;
        },
        true);
}

void subghz_view_hitag2_bf_set_result(
    SubGhzViewHitag2Bf* instance,
    bool success,
    const char* result) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzHitag2BfModel * model,
        {
            model->done = true;
            model->success = success;
            furi_string_set_str(model->result_str, result ? result : "");
        },
        true);
}

void subghz_view_hitag2_bf_reset(SubGhzViewHitag2Bf* instance) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzHitag2BfModel * model,
        {
            model->level = 1;
            model->level_name[0] = '\0';
            model->progress = 0;
            model->keys_tested = 0;
            model->keys_per_sec = 0;
            model->elapsed_sec = 0;
            model->eta_sec = 0;
            model->captures = 1;
            model->done = false;
            model->success = false;
            furi_string_reset(model->result_str);
            model->status_line[0] = '\0';
        },
        false);
}

void subghz_view_hitag2_bf_set_status(SubGhzViewHitag2Bf* instance, const char* status) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzHitag2BfModel * model,
        {
            if(status) {
                strlcpy(model->status_line, status, sizeof(model->status_line));
            } else {
                model->status_line[0] = '\0';
            }
        },
        true);
}

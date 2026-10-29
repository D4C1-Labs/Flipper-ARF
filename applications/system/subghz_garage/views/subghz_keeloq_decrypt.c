#include "subghz_keeloq_decrypt.h"

#include <gui/elements.h>
#include <furi.h>

#define KL_TOTAL_KEYS 0x100000000ULL

struct SubGhzViewKeeloqDecrypt {
    View* view;
    SubGhzViewKeeloqDecryptCallback callback;
    void* context;
};

typedef struct {
    uint8_t progress;
    uint32_t keys_tested;
    uint32_t keys_per_sec;
    uint32_t elapsed_sec;
    uint32_t eta_sec;
    bool done;
    bool success;
    uint32_t candidates;
    FuriString* result_str;
    char status_line[40];
} SubGhzKeeloqDecryptModel;

static void subghz_view_keeloq_decrypt_format_count(char* buf, size_t len, uint32_t count) {
    if(count >= 1000000) {
        snprintf(buf, len, "%lu.%luM", count / 1000000, (count % 1000000) / 100000);
    } else if(count >= 1000) {
        snprintf(buf, len, "%luK", count / 1000);
    } else {
        snprintf(buf, len, "%lu", count);
    }
}

static void subghz_view_keeloq_decrypt_draw(Canvas* canvas, void* _model) {
    SubGhzKeeloqDecryptModel* model = (SubGhzKeeloqDecryptModel*)_model;

    canvas_clear(canvas);

    if(!model->done) {
        canvas_set_font(canvas, FontPrimary);
        if(model->status_line[0]) {
            canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, model->status_line);
        } else {
            canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "KeeLoq BF");
        }

        canvas_draw_rframe(canvas, 3, 15, 122, 12, 2);
        uint8_t fill = (uint8_t)((uint16_t)model->progress * 116 / 100);
        if(fill > 0) {
            canvas_draw_rbox(canvas, 5, 17, fill, 8, 1);
        }

        canvas_set_font(canvas, FontSecondary);

        char keys_str[32];
        char tested_buf[12];
        subghz_view_keeloq_decrypt_format_count(tested_buf, sizeof(tested_buf), model->keys_tested);
        snprintf(keys_str, sizeof(keys_str), "%d%% - %s / 4G keys", model->progress, tested_buf);
        canvas_draw_str(canvas, 2, 38, keys_str);

        char speed_str[40];
        char speed_buf[12];
        subghz_view_keeloq_decrypt_format_count(speed_buf, sizeof(speed_buf), model->keys_per_sec);
        uint32_t eta_m = model->eta_sec / 60;
        uint32_t eta_s = model->eta_sec % 60;
        if(eta_m > 0) {
            snprintf(speed_str, sizeof(speed_str), "%s keys/sec  ETA %lum %lus", speed_buf, eta_m, eta_s);
        } else {
            snprintf(speed_str, sizeof(speed_str), "%s keys/sec  ETA %lus", speed_buf, eta_s);
        }
        canvas_draw_str(canvas, 2, 48, speed_str);

        if(model->candidates > 0) {
            char cand_str[32];
            snprintf(cand_str, sizeof(cand_str), "Candidates: %lu", model->candidates);
            canvas_draw_str(canvas, 2, 58, cand_str);
        } else {
            char elapsed_str[24];
            uint32_t el_m = model->elapsed_sec / 60;
            uint32_t el_s = model->elapsed_sec % 60;
            if(el_m > 0) {
                snprintf(elapsed_str, sizeof(elapsed_str), "Elapsed: %lum %lus", el_m, el_s);
            } else {
                snprintf(elapsed_str, sizeof(elapsed_str), "Elapsed: %lus", el_s);
            }
            canvas_draw_str(canvas, 2, 58, elapsed_str);
        }

        canvas_draw_str_aligned(canvas, 126, 64, AlignRight, AlignBottom, "Hold BACK");
    } else {
        canvas_set_font(canvas, FontSecondary);
        if(model->result_str) {
            elements_multiline_text_aligned(
                canvas, 0, 0, AlignLeft, AlignTop, furi_string_get_cstr(model->result_str));
        }
    }
}

static bool subghz_view_keeloq_decrypt_input(InputEvent* event, void* context) {
    SubGhzViewKeeloqDecrypt* instance = (SubGhzViewKeeloqDecrypt*)context;

    if(event->key == InputKeyBack) {
        if(instance->callback) {
            instance->callback(SubGhzCustomEventViewTransmitterBack, instance->context);
        }
        return true;
    }
    return false;
}

SubGhzViewKeeloqDecrypt* subghz_view_keeloq_decrypt_alloc(void) {
    SubGhzViewKeeloqDecrypt* instance = malloc(sizeof(SubGhzViewKeeloqDecrypt));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SubGhzKeeloqDecryptModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, subghz_view_keeloq_decrypt_draw);
    view_set_input_callback(instance->view, subghz_view_keeloq_decrypt_input);

    with_view_model(
        instance->view,
        SubGhzKeeloqDecryptModel * model,
        {
            model->result_str = furi_string_alloc();
            model->progress = 0;
            model->keys_tested = 0;
            model->keys_per_sec = 0;
            model->elapsed_sec = 0;
            model->eta_sec = 0;
            model->done = false;
            model->success = false;
            model->candidates = 0;
        },
        false);

    return instance;
}

void subghz_view_keeloq_decrypt_free(SubGhzViewKeeloqDecrypt* instance) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzKeeloqDecryptModel * model,
        { furi_string_free(model->result_str); },
        false);
    view_free(instance->view);
    free(instance);
}

View* subghz_view_keeloq_decrypt_get_view(SubGhzViewKeeloqDecrypt* instance) {
    furi_check(instance);
    return instance->view;
}

void subghz_view_keeloq_decrypt_set_callback(
    SubGhzViewKeeloqDecrypt* instance,
    SubGhzViewKeeloqDecryptCallback callback,
    void* context) {
    furi_check(instance);
    instance->callback = callback;
    instance->context = context;
}

void subghz_view_keeloq_decrypt_update_stats(
    SubGhzViewKeeloqDecrypt* instance,
    uint8_t progress,
    uint32_t keys_tested,
    uint32_t keys_per_sec,
    uint32_t elapsed_sec,
    uint32_t eta_sec) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzKeeloqDecryptModel * model,
        {
            model->progress = progress;
            model->keys_tested = keys_tested;
            model->keys_per_sec = keys_per_sec;
            model->elapsed_sec = elapsed_sec;
            model->eta_sec = eta_sec;
        },
        true);
}

void subghz_view_keeloq_decrypt_set_result(
    SubGhzViewKeeloqDecrypt* instance,
    bool success,
    const char* result) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzKeeloqDecryptModel * model,
        {
            model->done = true;
            model->success = success;
            furi_string_set_str(model->result_str, result);
        },
        true);
}

void subghz_view_keeloq_decrypt_reset(SubGhzViewKeeloqDecrypt* instance) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzKeeloqDecryptModel * model,
        {
            model->progress = 0;
            model->keys_tested = 0;
            model->keys_per_sec = 0;
            model->elapsed_sec = 0;
            model->eta_sec = 0;
            model->done = false;
            model->success = false;
            model->candidates = 0;
            furi_string_reset(model->result_str);
            model->status_line[0] = '\0';
        },
        false);
}

void subghz_view_keeloq_decrypt_set_status(SubGhzViewKeeloqDecrypt* instance, const char* status) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzKeeloqDecryptModel * model,
        {
            if(status) {
                strlcpy(model->status_line, status, sizeof(model->status_line));
            } else {
                model->status_line[0] = '\0';
            }
        },
        true);
}

void subghz_view_keeloq_decrypt_update_candidates(
    SubGhzViewKeeloqDecrypt* instance, uint32_t count) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzKeeloqDecryptModel * model,
        { model->candidates = count; },
        true);
}

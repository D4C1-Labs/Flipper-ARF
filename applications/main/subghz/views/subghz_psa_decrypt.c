#include "subghz_psa_decrypt.h"

#include <gui/elements.h>
#include <furi.h>
#include <assets_icons.h>

// [ARF_PORT 2026-10-26] Waiting dolphin on the right, matching Hitag2 BF / Seed
// BF. Cosmetic only: the decrypt engine and the BLE offload (both in the scene)
// are untouched. Stats live in the left column (x < PSA_DOLPHIN_X).
#define PSA_DOLPHIN_X 69

#define PSA_TOTAL_KEYS 0x2000000UL // 32M

struct SubGhzViewPsaDecrypt {
    View* view;
    SubGhzViewPsaDecryptCallback callback;
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
    FuriString* result_str;
    char status_line[40];
} SubGhzPsaDecryptModel;

static void subghz_view_psa_decrypt_format_count(char* buf, size_t len, uint32_t count) {
    if(count >= 1000000) {
        snprintf(buf, len, "%lu.%luM", count / 1000000, (count % 1000000) / 100000);
    } else if(count >= 1000) {
        snprintf(buf, len, "%luK", count / 1000);
    } else {
        snprintf(buf, len, "%lu", count);
    }
}

static void subghz_view_psa_decrypt_draw(Canvas* canvas, void* _model) {
    SubGhzPsaDecryptModel* model = (SubGhzPsaDecryptModel*)_model;

    canvas_clear(canvas);

    if(!model->done) {
        // [ARF_PORT] Waiting dolphin on the right while decrypting.
        canvas_draw_icon(canvas, PSA_DOLPHIN_X, 8, &I_DolphinWait_59x54);

        // Title / status (left column)
        canvas_set_font(canvas, FontPrimary);
        if(model->status_line[0]) {
            canvas_draw_str(canvas, 0, 8, model->status_line);
        } else {
            canvas_draw_str(canvas, 0, 8, "PSA");
        }

        canvas_set_font(canvas, FontSecondary);

        // Progress bar (left column width: 0..66)
        canvas_draw_rframe(canvas, 0, 12, 66, 9, 2);
        uint8_t fill = (uint8_t)((uint16_t)model->progress * 62 / 100);
        if(fill > 2) {
            canvas_draw_rbox(canvas, 2, 14, fill, 5, 1);
        } else if(fill > 0) {
            canvas_draw_box(canvas, 2, 14, fill, 5);
        }

        char line[24];

        // Percent
        snprintf(line, sizeof(line), "%d%%", model->progress);
        canvas_draw_str(canvas, 0, 30, line);

        // Keys tested
        char tested_buf[12];
        subghz_view_psa_decrypt_format_count(tested_buf, sizeof(tested_buf), model->keys_tested);
        snprintf(line, sizeof(line), "%s keys", tested_buf);
        canvas_draw_str(canvas, 0, 39, line);

        // Speed
        char speed_buf[12];
        subghz_view_psa_decrypt_format_count(speed_buf, sizeof(speed_buf), model->keys_per_sec);
        snprintf(line, sizeof(line), "%s/s", speed_buf);
        canvas_draw_str(canvas, 0, 48, line);

        // ETA (compact)
        if(model->eta_sec >= 3600U) {
            snprintf(
                line,
                sizeof(line),
                "ETA %luh%lum",
                (unsigned long)(model->eta_sec / 3600U),
                (unsigned long)((model->eta_sec % 3600U) / 60U));
        } else if(model->eta_sec >= 60U) {
            snprintf(
                line,
                sizeof(line),
                "ETA %lum%lus",
                (unsigned long)(model->eta_sec / 60U),
                (unsigned long)(model->eta_sec % 60U));
        } else {
            snprintf(line, sizeof(line), "ETA %lus", (unsigned long)model->eta_sec);
        }
        canvas_draw_str(canvas, 0, 57, line);

        // Cancel hint
        canvas_draw_str(canvas, 0, 64, "Hold BACK");
    } else {
        // [ARF_PORT] Result screen: dolphin on the right, verdict + details left.
        canvas_draw_icon(canvas, PSA_DOLPHIN_X, 8, &I_DolphinWait_59x54);

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 10, "Decrypted!");

        if(model->result_str) {
            canvas_set_font(canvas, FontSecondary);
            elements_multiline_text_aligned(
                canvas, 0, 18, AlignLeft, AlignTop, furi_string_get_cstr(model->result_str));
        }

        elements_button_center(canvas, "Ok");
    }
}

static bool subghz_view_psa_decrypt_input(InputEvent* event, void* context) {
    SubGhzViewPsaDecrypt* instance = (SubGhzViewPsaDecrypt*)context;

    if(event->key == InputKeyBack || event->key == InputKeyOk) {
        if(instance->callback) {
            instance->callback(SubGhzCustomEventViewTransmitterBack, instance->context);
        }
        return true;
    }
    return false;
}

SubGhzViewPsaDecrypt* subghz_view_psa_decrypt_alloc(void) {
    SubGhzViewPsaDecrypt* instance = malloc(sizeof(SubGhzViewPsaDecrypt));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SubGhzPsaDecryptModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, subghz_view_psa_decrypt_draw);
    view_set_input_callback(instance->view, subghz_view_psa_decrypt_input);

    with_view_model(
        instance->view,
        SubGhzPsaDecryptModel * model,
        {
            model->result_str = furi_string_alloc();
            model->progress = 0;
            model->keys_tested = 0;
            model->keys_per_sec = 0;
            model->elapsed_sec = 0;
            model->eta_sec = 0;
            model->done = false;
            model->success = false;
        },
        false);

    return instance;
}

void subghz_view_psa_decrypt_free(SubGhzViewPsaDecrypt* instance) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzPsaDecryptModel * model,
        { furi_string_free(model->result_str); },
        false);
    view_free(instance->view);
    free(instance);
}

View* subghz_view_psa_decrypt_get_view(SubGhzViewPsaDecrypt* instance) {
    furi_check(instance);
    return instance->view;
}

void subghz_view_psa_decrypt_set_callback(
    SubGhzViewPsaDecrypt* instance,
    SubGhzViewPsaDecryptCallback callback,
    void* context) {
    furi_check(instance);
    instance->callback = callback;
    instance->context = context;
}

void subghz_view_psa_decrypt_update_stats(
    SubGhzViewPsaDecrypt* instance,
    uint8_t progress,
    uint32_t keys_tested,
    uint32_t keys_per_sec,
    uint32_t elapsed_sec,
    uint32_t eta_sec) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzPsaDecryptModel * model,
        {
            model->progress = progress;
            model->keys_tested = keys_tested;
            model->keys_per_sec = keys_per_sec;
            model->elapsed_sec = elapsed_sec;
            model->eta_sec = eta_sec;
        },
        true);
}

void subghz_view_psa_decrypt_set_result(
    SubGhzViewPsaDecrypt* instance,
    bool success,
    const char* result) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzPsaDecryptModel * model,
        {
            model->done = true;
            model->success = success;
            furi_string_set_str(model->result_str, result);
        },
        true);
}

void subghz_view_psa_decrypt_reset(SubGhzViewPsaDecrypt* instance) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzPsaDecryptModel * model,
        {
            model->progress = 0;
            model->keys_tested = 0;
            model->keys_per_sec = 0;
            model->elapsed_sec = 0;
            model->eta_sec = 0;
            model->done = false;
            model->success = false;
            furi_string_reset(model->result_str);
            model->status_line[0] = '\0';
        },
        false);
}

void subghz_view_psa_decrypt_set_status(SubGhzViewPsaDecrypt* instance, const char* status) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzPsaDecryptModel * model,
        {
            if(status) {
                strlcpy(model->status_line, status, sizeof(model->status_line));
            } else {
                model->status_line[0] = '\0';
            }
        },
        true);
}

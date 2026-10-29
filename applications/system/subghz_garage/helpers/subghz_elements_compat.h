#pragma once

#include <gui/canvas.h>
#include <furi/core/string.h>

static inline void subghz_garage_scrollable_text_line_str(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    const char* string,
    size_t scroll,
    bool ellipsis,
    bool centered) {
    FuriString* line = furi_string_alloc_set_str(string);

    size_t len_px = canvas_string_width(canvas, furi_string_get_cstr(line));
    if(len_px > width) {
        if(centered) {
            centered = false;
            x -= width / 2;
        }

        if(ellipsis) {
            uint8_t ew = width - (uint8_t)canvas_string_width(canvas, "...");
            size_t scroll_size = furi_string_size(line);
            size_t right_width = 0;
            for(size_t i = scroll_size - 1; i > 0; i--) {
                right_width += canvas_glyph_width(canvas, furi_string_get_char(line, i));
                if(right_width > ew) break;
                scroll_size--;
                if(!scroll_size) break;
            }
            if(scroll_size) {
                scroll_size += 3;
                scroll = scroll % scroll_size;
                furi_string_right(line, scroll);
            }
            len_px = canvas_string_width(canvas, furi_string_get_cstr(line));
            while(len_px > ew) {
                furi_string_left(line, furi_string_size(line) - 1);
                len_px = canvas_string_width(canvas, furi_string_get_cstr(line));
            }
            furi_string_cat(line, "...");
        } else {

            size_t scroll_size = furi_string_size(line);
            size_t right_width = 0;
            for(size_t i = scroll_size - 1; i > 0; i--) {
                right_width += canvas_glyph_width(canvas, furi_string_get_char(line, i));
                if(right_width > width) break;
                scroll_size--;
                if(!scroll_size) break;
            }
            if(scroll_size) {
                size_t pause = 2;
                size_t cycle = 2 * (pause + scroll_size);
                size_t t = scroll % cycle;
                size_t s;
                if(t < pause)
                    s = 0;
                else if(t < pause + scroll_size)
                    s = t - pause;
                else if(t < 2 * pause + scroll_size)
                    s = scroll_size;
                else
                    s = 2 * pause + 2 * scroll_size - 1 - t;
                furi_string_right(line, s);
            }
            len_px = canvas_string_width(canvas, furi_string_get_cstr(line));
            while(len_px > width) {
                furi_string_left(line, furi_string_size(line) - 1);
                len_px = canvas_string_width(canvas, furi_string_get_cstr(line));
            }
        }
    }

    if(centered) {
        canvas_draw_str_aligned(
            canvas, x, y, AlignCenter, AlignBottom, furi_string_get_cstr(line));
    } else {
        canvas_draw_str(canvas, x, y, furi_string_get_cstr(line));
    }
    furi_string_free(line);
}

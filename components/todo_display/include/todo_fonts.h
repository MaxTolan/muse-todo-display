/*
 * The screen's fonts: LVGL's built-in Montserrat, which stops at ASCII, with a
 * fallback for everything else people write (accented letters, em/en dashes,
 * curly quotes, the ellipsis, ...): the same Montserrat Medium, subset to
 * those characters (todo_font_extra.c) and rendered at the matching size by
 * LVGL's TinyTTF. So "Call Mom — re: café plans…" draws in one typeface.
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The font for a pixel size the screen uses (16, 20, 24, 28, 32, 40, 48). */
const lv_font_t *todo_font(int px);

/* True if every character of the UTF-8 text has a glyph in that font (or its fallback). */
bool todo_font_can_render(const lv_font_t *font, const char *utf8);

#ifdef __cplusplus
}
#endif

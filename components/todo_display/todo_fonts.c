/*
 * The screen's fonts: see todo_fonts.h.
 */
#include "todo_fonts.h"

#include <stddef.h>
#include <stdint.h>

#include "src/libs/tiny_ttf/lv_tiny_ttf.h"

extern const uint8_t todo_font_extra_ttf[];
extern const size_t todo_font_extra_ttf_len;

static const int SIZES[] = { 16, 20, 24, 28, 32, 40, 48 };
#define N_SIZES ((int)(sizeof(SIZES) / sizeof(SIZES[0])))

static const lv_font_t *const BUILTIN[N_SIZES] = {
    &lv_font_montserrat_16, &lv_font_montserrat_20, &lv_font_montserrat_24, &lv_font_montserrat_28,
    &lv_font_montserrat_32, &lv_font_montserrat_40, &lv_font_montserrat_48,
};

/* Copies of the built-in fonts (those are const) that carry the fallback. */
static lv_font_t s_fonts[N_SIZES];
static bool s_ready;

static void init(void)
{
    for (int i = 0; i < N_SIZES; i++) {
        s_fonts[i] = *BUILTIN[i];
        s_fonts[i].fallback = lv_tiny_ttf_create_data(todo_font_extra_ttf, todo_font_extra_ttf_len, SIZES[i]);
    }
    s_ready = true;
}

const lv_font_t *todo_font(int px)
{
    if (!s_ready) {
        init();
    }
    for (int i = 0; i < N_SIZES; i++) {
        if (SIZES[i] == px) {
            return &s_fonts[i];
        }
    }
    return &s_fonts[1]; /* 20 */
}

/* Next code point of UTF-8 text (malformed bytes come back as U+FFFD). */
static uint32_t next_cp(const unsigned char **p)
{
    const unsigned char *s = *p;
    uint32_t cp;
    int extra;
    if (s[0] < 0x80) {
        cp = s[0], extra = 0;
    } else if ((s[0] & 0xe0) == 0xc0) {
        cp = s[0] & 0x1f, extra = 1;
    } else if ((s[0] & 0xf0) == 0xe0) {
        cp = s[0] & 0x0f, extra = 2;
    } else if ((s[0] & 0xf8) == 0xf0) {
        cp = s[0] & 0x07, extra = 3;
    } else {
        *p = s + 1;
        return 0xfffd;
    }
    for (int k = 1; k <= extra; k++) {
        if ((s[k] & 0xc0) != 0x80) {
            *p = s + k;
            return 0xfffd;
        }
        cp = (cp << 6) | (s[k] & 0x3f);
    }
    *p = s + 1 + extra;
    return cp;
}

bool todo_font_can_render(const lv_font_t *font, const char *utf8)
{
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p) {
        uint32_t cp = next_cp(&p);
        if (cp == '\n' || cp == '\r' || cp == ' ') {
            continue;
        }
        lv_font_glyph_dsc_t dsc;
        if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0) || dsc.resolved_font == NULL) {
            return false;
        }
    }
    return true;
}

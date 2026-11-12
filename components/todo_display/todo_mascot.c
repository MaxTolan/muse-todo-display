/*
 * Chief: see todo_mascot.h.
 *
 * The mascot is a plain container holding two canvases, back to front:
 * the war bonnet's feather crown (behind the head, with room above the
 * avatar's frame so it can lift and lean), and the avatar with the
 * browband, rosettes and fur drops painted on. Everything is drawn in avatar-pixel space so it stays
 * crisp pixel art.
 */
#include "todo_mascot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "muse_pixel.h"
/* lv_image_cache_drop: not exported by lvgl.h in 9.5. */
#include "src/misc/cache/instance/lv_image_cache.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define TODO_EXT_BSS EXT_RAM_BSS_ATTR
#else
#define TODO_EXT_BSS
#endif

/* RGB565 of the avatar's fixed outline colour (0x3a2b22), used to find the top of the head. */
#define OUTLINE_565 0x3944u

/*
 * The headdress: a war bonnet. A crown of long feathers fans out around the
 * head from one point inside it (the body hides everything up to the head's
 * edge), each one a black quill wrapped in white, a tuft of white fluff,
 * then a blue, red or near-black vane ending in a dark tip. In front, a
 * beaded browband with a rosette at each temple and white fur drops
 * hanging beside the face.
 */
#define CROWN_N 15          /* feathers, spread evenly across the crown */
#define CROWN_SPAN 128.0f   /* degrees at the base, shoulder to shoulder */
/* The feathers sweep back: seen from the front the crown looks shorter
 * (CROWN_SQUASH of its height) and each feather's outer half curls outward
 * and down, up to SWEEP_DEG at the tip (the middle one stays upright). */
#define CROWN_SQUASH 0.85f
#define SWEEP_DEG 16.0f
#define FEATHER_LEN 27.5f   /* avatar pixels from the base point */
#define QUILL_END 14.5f     /* black-and-white wrapped quill up to here */
#define FLUFF_END 17.5f     /* then white fluff */
#define FEATHER_TIP 0x1b2b33u

typedef struct {
    uint32_t vane;
    uint32_t shaft;
} feather_color_t;

/* By distance from the centre feather, so the crown is symmetric. */
static const feather_color_t CROWN_COLORS[] = {
    { 0xd2302a, 0x8f1e18 }, /* red */
    { 0x2f8fd8, 0x1d5d94 }, /* blue */
    { 0x2f5c63, 0x173238 }, /* near-black teal */
    { 0x2f8fd8, 0x1d5d94 },
};
#define CROWN_COLORS_N ((int)(sizeof(CROWN_COLORS) / sizeof(CROWN_COLORS[0])))

/* The crown's base point, in avatar pixels below the head top: about the
 * middle of the head, so the feathers ring it evenly. */
#define CROWN_DEPTH 12

/* Space above the avatar for the crown, in avatar pixels: TOP_MARGIN inside
 * the mascot's box, CROWN_RISE more above it (the box overflows visibly). */
#define TOP_MARGIN 6
#define CROWN_RISE 8
/* The most the pose lifts the crown (the celebration's tip), in avatar pixels. */
#define CROWN_LIFT_MAX 5
/* The browband: its first row below the head top, and its height. */
#define BAND_ROW 4
#define BAND_ROWS 3
/* White fur drops below each rosette, in avatar pixels. */
#define DROP_LEN 11
#define PI_F 3.14159265f

typedef struct {
    int px;
    int cell;
    lv_draw_buf_t *body_buf;
    lv_draw_buf_t *crest_buf;
    lv_obj_t *body;
    lv_obj_t *crest;
    /* The browband's ends, steadied: the head's outline wobbles a pixel
     * from frame to frame, and the rosettes and crown hang off these. */
    int band_l, band_r;
} mascot_t;

static uint16_t rgb565(uint32_t c)
{
    return (uint16_t)((((c >> 16) & 0xff) >> 3) << 11 | (((c >> 8) & 0xff) >> 2) << 5 | ((c & 0xff) >> 3));
}

static uint16_t dim565(uint16_t c)
{
    unsigned r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    return (uint16_t)(((r * 184) >> 8) << 11 | ((g * 184) >> 8) << 5 | ((b * 184) >> 8));
}

/* The crown rises above the mascot's box: tell LVGL to redraw it there too,
 * or it skips the box (and its overflowing children) for areas above it. */
static void mascot_ext_draw_cb(lv_event_t *e)
{
    mascot_t *m = lv_event_get_user_data(e);
    lv_event_set_ext_draw_size(e, (CROWN_RISE + CROWN_LIFT_MAX) * m->cell);
}

static void mascot_delete_cb(lv_event_t *e)
{
    mascot_t *m = lv_event_get_user_data(e);
    lv_draw_buf_destroy(m->body_buf);
    lv_draw_buf_destroy(m->crest_buf);
    free(m);
}

static lv_obj_t *make_canvas(lv_obj_t *parent, lv_draw_buf_t *buf)
{
    memset(buf->data, 0, buf->data_size);
    lv_obj_t *c = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(c, buf);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

/* One repainted layer. The canvas is its draw buffer's image cache entry:
 * drop it (as lv_canvas_set_draw_buf does) or the first frame shows forever. */
static void layer_dirty(lv_draw_buf_t *buf, lv_obj_t *canvas)
{
    lv_image_cache_drop(buf);
    lv_obj_invalidate(canvas);
}

lv_obj_t *todo_mascot_create(lv_obj_t *parent, int px)
{
    mascot_t *m = calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    m->px = px;
    m->cell = px / MUSE_PX_W;
    m->band_l = m->band_r = -1;
    m->body_buf = lv_draw_buf_create((uint32_t)px, (uint32_t)px, LV_COLOR_FORMAT_RGB565A8, LV_STRIDE_AUTO);
    m->crest_buf = lv_draw_buf_create((uint32_t)px, (uint32_t)px, LV_COLOR_FORMAT_RGB565A8, LV_STRIDE_AUTO);
    if (!m->body_buf || !m->crest_buf) {
        if (m->body_buf) {
            lv_draw_buf_destroy(m->body_buf);
        }
        if (m->crest_buf) {
            lv_draw_buf_destroy(m->crest_buf);
        }
        free(m);
        return NULL;
    }

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(root, px, px + TOP_MARGIN * m->cell);
    lv_obj_add_flag(root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_user_data(root, m);
    lv_obj_add_event_cb(root, mascot_delete_cb, LV_EVENT_DELETE, m);
    lv_obj_add_event_cb(root, mascot_ext_draw_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, m);
    lv_obj_refresh_ext_draw_size(root);

    /* Stacking: feathers behind the body (their quills hide in the fur). */
    m->crest = make_canvas(root, m->crest_buf);
    lv_obj_set_pos(m->crest, 0, TOP_MARGIN * m->cell);
    m->body = make_canvas(root, m->body_buf);
    lv_obj_set_pos(m->body, 0, TOP_MARGIN * m->cell);
    return root;
}

/* A 1:1 copy of the current avatar frame (find_head fills it). */
static TODO_EXT_BSS uint16_t grid[MUSE_PX_W * MUSE_PX_H];

/* Top of the head and its centre, in avatar pixels, from a 1:1 copy of the frame. */
static void find_head(int *top, int *cx)
{
    muse_pixel_set_size(MUSE_PX_W);
    muse_pixel_scale(grid, MUSE_PX_W, 0, MUSE_PX_W - 1, 0, MUSE_PX_H - 1);
    for (int y = 0; y < MUSE_PX_H; y++) {
        int first = -1, last = -1;
        for (int x = 16; x < MUSE_PX_W - 16; x++) {
            if (grid[y * MUSE_PX_W + x] == OUTLINE_565) {
                if (first < 0) {
                    first = x;
                }
                last = x;
            }
        }
        if (first >= 0) {
            *top = y;
            *cx = (first + last + 1) / 2;
            return;
        }
    }
    *top = 14;
    *cx = MUSE_PX_W / 2;
}

/* Fill one avatar-pixel cell of an RGB565A8 buffer, with the avatar's faint grid at 3x and up. */
static void put_cell(lv_draw_buf_t *buf, int size_px, int cell, int gx, int gy, uint16_t color)
{
    uint32_t stride = buf->header.stride;
    uint8_t *alpha_plane = buf->data + stride * (uint32_t)size_px;
    uint32_t alpha_stride = stride / 2;
    uint16_t dim = dim565(color);
    for (int dy = 0; dy < cell; dy++) {
        int y = gy * cell + dy;
        if (y < 0 || y >= size_px) {
            continue;
        }
        uint16_t *row = (uint16_t *)(buf->data + stride * (uint32_t)y);
        for (int dx = 0; dx < cell; dx++) {
            int x = gx * cell + dx;
            if (x < 0 || x >= size_px) {
                continue;
            }
            bool edge = cell >= 3 && (dx == cell - 1 || dy == cell - 1);
            row[x] = edge ? dim : color;
            alpha_plane[alpha_stride * (uint32_t)y + (uint32_t)x] = 0xff;
        }
    }
}

/*
 * Draw the feather crown, leaning deg (positive: to the right), fanning
 * from (bx, by) in avatar pixels. The crest canvas starts TOP_MARGIN +
 * CROWN_RISE rows above the avatar's frame so the crown has room.
 */
static void draw_crest(mascot_t *m, float bx, float by, float deg)
{
    memset(m->crest_buf->data, 0, m->crest_buf->data_size);
    const float step = CROWN_SPAN / (CROWN_N - 1);
    const float first = -CROWN_SPAN / 2 + deg;
    uint16_t tip = rgb565(FEATHER_TIP), white = rgb565(0xf4f1ea), black = rgb565(0x1e1a1c);
    int rows = (int)by + TOP_MARGIN + CROWN_RISE + 4;
    if (rows > MUSE_PX_H) {
        rows = MUSE_PX_H;
    }
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < MUSE_PX_W; gx++) {
            float qx = gx + 0.5f - bx, qy = gy + 0.5f - (TOP_MARGIN + CROWN_RISE) - by;
            if (qy < 0.0f) {
                qy /= CROWN_SQUASH; /* foreshortened: the crown leans away */
            }
            float r = sqrtf(qx * qx + qy * qy);
            if (r < QUILL_END - 3.0f || r > FEATHER_LEN) {
                continue; /* inside the head, or past the tips */
            }
            /* Angle from straight up, and the nearest feather to it. */
            float a = atan2f(qx, -qy) * 180.0f / PI_F;
            /* Undo the sweep: out here a feather has curled further from upright. */
            float k = (r - FLUFF_END) / (FEATHER_LEN - FLUFF_END);
            if (k > 0.0f) {
                float bend = SWEEP_DEG * k * k;
                a += a > deg ? -bend : bend;
            }
            int i = (int)lroundf((a - first) / step);
            if (i < 0 || i >= CROWN_N) {
                continue;
            }
            float off = (a - (first + (float)i * step)) * PI_F / 180.0f;
            float u = r * cosf(off), v = fabsf(r * sinf(off));
            const feather_color_t *fc = &CROWN_COLORS[abs(i - CROWN_N / 2) % CROWN_COLORS_N];
            uint16_t color;
            if (u < QUILL_END) {
                if (v > 0.75f) {
                    continue;
                }
                color = ((int)u % 2) ? white : black; /* white wraps on a black quill */
            } else if (u < FLUFF_END) {
                if (v > 1.2f) {
                    continue;
                }
                color = white;
            } else {
                float f = (u - FLUFF_END) / (FEATHER_LEN - FLUFF_END);
                float half = 0.6f + 1.0f * sinf(PI_F * (0.25f + 0.75f * f));
                if (v > half) {
                    continue;
                }
                color = f > 0.8f ? tip : v < 0.5f ? rgb565(fc->shaft) : rgb565(fc->vane);
            }
            put_cell(m->crest_buf, m->px, m->cell, gx, gy, color);
        }
    }
    layer_dirty(m->crest_buf, m->crest);
}

/* Painted onto the avatar (it stays on the head through every bob). */
static void paint(mascot_t *m, int x, int y, uint32_t c)
{
    if (x >= 0 && y >= 0 && x < MUSE_PX_W && y < MUSE_PX_H) {
        put_cell(m->body_buf, m->px, m->cell, x, y, rgb565(c));
    }
}

/* One temple: a rosette over the band's end, fur drops hanging below it. */
static void draw_rosette(mascot_t *m, int x, int y, int out)
{
    static const char *const ROSETTE[] = { ".yoy.", "yorOy", "orwro", "yOroy", ".yoy." };
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 5; c++) {
            uint32_t col;
            switch (ROSETTE[r][c]) {
            case 'y': col = 0xf2c230; break; /* gold beads */
            case 'o': col = 0xe0782a; break;
            case 'O': col = 0xa34d16; break;
            case 'r': col = 0xd2302a; break;
            case 'w': col = 0xfaf6ee; break; /* the crystal centre */
            default: continue;
            }
            paint(m, x - 2 + c, y - 2 + r, col);
        }
    }
    /* Drops: white fur strips, with a red one on the outside. */
    for (int d = 0; d < DROP_LEN; d++) {
        int yy = y + 3 + d;
        paint(m, x, yy, (d % 4 == 3) ? 0xd9d2c4 : 0xf4f1ea);
        paint(m, x - out, yy, (d % 4 == 1) ? 0xd9d2c4 : 0xf4f1ea);
        if (d < DROP_LEN - 2) {
            paint(m, x - 2 * out, yy, d > 2 ? 0xd2302a : 0x8f1e18);
        }
    }
}

/*
 * The browband across the forehead between the head's outlines: white bead
 * edges and a red, yellow, blue and purple beaded middle, a rosette at each
 * end.
 */
/* Move a steadied edge only when the measured one is 2+ pixels away. */
static int steady(int held, int seen)
{
    return (held < 0 || abs(seen - held) >= 2) ? seen : held;
}

static void draw_band(mascot_t *m, int top)
{
    static const uint32_t BEADS[] = { 0xd2302a, 0xf2c230, 0x2f6fd8, 0x7a3fb0, 0x2f6fd8, 0xf2c230 };
    int left = -1, right = -1;
    /* The ends, from the middle row (see mascot_t.band_l). */
    {
        int y = top + BAND_ROW + BAND_ROWS / 2;
        for (int x = 8; y >= 0 && y < MUSE_PX_H && x < MUSE_PX_W - 8; x++) {
            if (grid[y * MUSE_PX_W + x] == OUTLINE_565) {
                if (left < 0) {
                    left = x;
                }
                right = x;
            }
        }
        if (left >= 0 && right - left >= 4) {
            m->band_l = steady(m->band_l, left);
            m->band_r = steady(m->band_r, right);
        }
    }
    for (int r = 0; r < BAND_ROWS; r++) {
        int y = top + BAND_ROW + r;
        if (y < 0 || y >= MUSE_PX_H) {
            continue;
        }
        int first = -1, last = -1;
        for (int x = 8; x < MUSE_PX_W - 8; x++) {
            if (grid[y * MUSE_PX_W + x] == OUTLINE_565) {
                if (first < 0) {
                    first = x;
                }
                last = x;
            }
        }
        if (first < 0 || last - first < 4) {
            continue;
        }
        for (int x = first; x <= last; x++) {
            uint32_t c = 0xe9e4d8; /* white bead edge */
            if (r == BAND_ROWS / 2 && m->band_l >= 0) {
                /* The pattern counts from the steady end, so it doesn't crawl. */
                c = BEADS[(x - m->band_l + 60) % 6];
            }
            paint(m, x, y, c);
        }
    }
    if (m->band_l >= 0) {
        int y = top + BAND_ROW + BAND_ROWS / 2;
        draw_rosette(m, m->band_l + 1, y, 1);
        draw_rosette(m, m->band_r - 1, y, -1);
    }
}

void todo_mascot_draw(lv_obj_t *root, const todo_mascot_pose_t *pose)
{
    mascot_t *m = lv_obj_get_user_data(root);
    if (!m) {
        return;
    }
    muse_pose_t p = {
        .mode = MUSE_MODE_IDLE,
        .t = pose->t,
        .mode_t = pose->t,
        .level = 0,
        .happy = pose->happy,
    };
    muse_pixel_render(&p);

    int top, cx;
    find_head(&top, &cx);

    lv_draw_buf_t *buf = m->body_buf;
    uint32_t stride = buf->header.stride;
    uint8_t *alpha_plane = buf->data + stride * (uint32_t)m->px;
    uint32_t alpha_stride = stride / 2;
    muse_pixel_set_size(m->px);
    muse_pixel_scale((uint16_t *)buf->data, (int)(stride / 2), 0, m->px - 1, 0, m->px - 1);
    for (int y = 0; y < m->px; y++) {
        const uint16_t *row = (const uint16_t *)(buf->data + stride * (uint32_t)y);
        uint8_t *a = alpha_plane + alpha_stride * (uint32_t)y;
        for (int x = 0; x < m->px; x++) {
            a[x] = row[x] ? 0xff : 0x00; /* the avatar's background is pure black */
        }
    }

    draw_band(m, top);
    layer_dirty(m->body_buf, m->body);

    /* The crown lifts and leans with the pose; its base stays in the head. */
    lv_obj_set_pos(m->crest, 0, -(int32_t)lroundf((CROWN_RISE + pose->crest_lift) * (float)m->cell));
    /* Centred on the steadied band, so the crown stays put between the rosettes. */
    float crown_x = m->band_l >= 0 ? (float)(m->band_l + m->band_r + 1) / 2.0f : (float)cx;
    draw_crest(m, crown_x, (float)(top + CROWN_DEPTH), pose->crest_tilt * 20.0f);
}

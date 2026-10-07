/*
 * Muse in a top hat: see todo_mascot.h.
 *
 * The mascot is a plain container holding two canvases: the avatar, and the
 * hat on its own canvas so it can lift above the avatar's frame and rotate.
 * The hat is rotated in avatar-pixel space (nearest neighbour) so it stays
 * crisp pixel art at any angle.
 */
#include "todo_mascot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "muse_pixel.h"

/* RGB565 of the avatar's fixed outline colour (0x3a2b22), used to find the top of the head. */
#define OUTLINE_565 0x3944u

/*
 * The hat, one character per avatar pixel, brim at the bottom.
 * o outline, k felt, h felt highlight, p pink band, P band shade.
 */
static const char *const HAT[] = {
    "...oooooooooooo...",
    "...okkkkkkkkkho...",
    "...okkkkkkkkkho...",
    "...okkkkkkkkkho...",
    "...okkkkkkkkkho...",
    "...okkkkkkkkkho...",
    "...oppppppppppo...",
    "...oPPPPPPPPPPo...",
    "...okkkkkkkkkko...",
    "oooooooooooooooooo",
    "okkkkkkkkkkkkkkkho",
    "oooooooooooooooooo",
};
#define HAT_ROWS ((int)(sizeof(HAT) / sizeof(HAT[0])))
#define HAT_COLS 18

/* Hat canvas, in avatar pixels: room for the hat at any angle. */
#define HAT_GRID 26
/* Space above the avatar for a lifted hat, in avatar pixels. */
#define TOP_MARGIN 6
/* How far the brim sinks into the fur, its offset right of centre, and resting tilt. */
#define HAT_SINK 3
#define HAT_NUDGE 2
#define HAT_REST_DEG 9.0f

typedef struct {
    int px;
    int cell;
    lv_draw_buf_t *body_buf;
    lv_draw_buf_t *hat_buf;
    lv_obj_t *body;
    lv_obj_t *hat;
} mascot_t;

static uint16_t rgb565(uint32_t c)
{
    return (uint16_t)((((c >> 16) & 0xff) >> 3) << 11 | (((c >> 8) & 0xff) >> 2) << 5 | ((c & 0xff) >> 3));
}

static uint16_t hat_color(char ch)
{
    switch (ch) {
    case 'o': return rgb565(0x140d18);
    case 'k': return rgb565(0x2b2233);
    case 'h': return rgb565(0x51465e);
    case 'p': return rgb565(0xff8fb8);
    case 'P': return rgb565(0xd8668f);
    default: return 0;
    }
}

static uint16_t dim565(uint16_t c)
{
    unsigned r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    return (uint16_t)(((r * 184) >> 8) << 11 | ((g * 184) >> 8) << 5 | ((b * 184) >> 8));
}

static void mascot_delete_cb(lv_event_t *e)
{
    mascot_t *m = lv_event_get_user_data(e);
    lv_draw_buf_destroy(m->body_buf);
    lv_draw_buf_destroy(m->hat_buf);
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

lv_obj_t *todo_mascot_create(lv_obj_t *parent, int px)
{
    mascot_t *m = calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }
    m->px = px;
    m->cell = px / MUSE_PX_W;
    int hat_px = HAT_GRID * m->cell;
    m->body_buf = lv_draw_buf_create((uint32_t)px, (uint32_t)px, LV_COLOR_FORMAT_RGB565A8, LV_STRIDE_AUTO);
    m->hat_buf = lv_draw_buf_create((uint32_t)hat_px, (uint32_t)hat_px, LV_COLOR_FORMAT_RGB565A8, LV_STRIDE_AUTO);
    if (!m->body_buf || !m->hat_buf) {
        if (m->body_buf) {
            lv_draw_buf_destroy(m->body_buf);
        }
        if (m->hat_buf) {
            lv_draw_buf_destroy(m->hat_buf);
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

    m->body = make_canvas(root, m->body_buf);
    lv_obj_set_pos(m->body, 0, TOP_MARGIN * m->cell);
    m->hat = make_canvas(root, m->hat_buf);
    return root;
}

/* Top of the head and its centre, in avatar pixels, from a 1:1 copy of the frame. */
static void find_head(int *top, int *cx)
{
    static uint16_t grid[MUSE_PX_W * MUSE_PX_H];
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

/* Draw the hat rotated by deg about the middle of its brim, which lands at the canvas centre. */
static void draw_hat(mascot_t *m, float deg)
{
    int size_px = HAT_GRID * m->cell;
    memset(m->hat_buf->data, 0, m->hat_buf->data_size);
    float rad = deg * 3.14159265f / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    /* Pivot: centre of the brim's middle row. */
    float pivot_x = HAT_COLS / 2.0f, pivot_y = HAT_ROWS - 1.5f;
    float centre = HAT_GRID / 2.0f;
    for (int gy = 0; gy < HAT_GRID; gy++) {
        for (int gx = 0; gx < HAT_GRID; gx++) {
            /* Inverse-rotate the cell centre back into hat coordinates. */
            float dx = gx + 0.5f - centre, dy = gy + 0.5f - centre;
            float hx = c * dx + s * dy + pivot_x;
            float hy = -s * dx + c * dy + pivot_y;
            int ix = (int)floorf(hx), iy = (int)floorf(hy);
            if (ix < 0 || iy < 0 || ix >= HAT_COLS || iy >= HAT_ROWS || HAT[iy][ix] == '.') {
                continue;
            }
            put_cell(m->hat_buf, size_px, m->cell, gx, gy, hat_color(HAT[iy][ix]));
        }
    }
    lv_obj_invalidate(m->hat);
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
    lv_obj_invalidate(m->body);

    /* The hat's brim sits on the head; lift and tilt tip it. */
    draw_hat(m, HAT_REST_DEG + pose->hat_tilt * 30.0f);
    float brim_x = (float)(cx + HAT_NUDGE);
    float brim_y = (float)(top + HAT_SINK + TOP_MARGIN) - 1.0f - pose->hat_lift;
    int half = HAT_GRID * m->cell / 2;
    lv_obj_set_pos(m->hat, (int32_t)lroundf(brim_x * m->cell) - half, (int32_t)lroundf(brim_y * m->cell) - half);
}

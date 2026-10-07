/*
 * Todo display screen: see todo_ui.h and docs/03-ui-spec.md.
 *
 * Cozy berry theme to sit with the pink stand: deep plum background, soft
 * rounded cards, pink checkboxes, cream type, and a little top-hat Muse.
 */
#include "todo_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "todo_mascot.h"

#define SCREEN_W 800
#define SCREEN_H 480

/* Palette. */
#define C_BG 0x2b1a2f
#define C_CARD 0x3e2745
#define C_CARD_PRESSED 0x52335b
#define C_PINK 0xff8fb8
#define C_PEACH 0xffc7a8
#define C_CREAM 0xfff3e8
#define C_MUTED 0xb79db5
#define C_FAINT 0x7d6380
#define C_MINT 0x8ee3c8
#define C_BUTTER 0xffe08a
#define C_PLUM_TEXT 0x3b1f3f

#define TOP_H 84
#define ROW_MIN_H 80
#define CHECK_PX 52
#define SQUISH_MS 320u
#define SWOOSH_MS 420u
#define PEEK_MS 4200u
#define PEEK_EVERY_MS (7u * 60u * 1000u)
#define MASCOT_FRAME_MS 80u
#define BAR_SLIDE_MS 260u
#define CONFETTI_N 28

#define PI_F 3.14159265f

typedef struct {
    bool used;
    char id[TODO_ID_MAX + 1];
    lv_obj_t *row;
    lv_obj_t *check;
    lv_obj_t *check_icon;
    lv_obj_t *label;
    lv_obj_t *side;
    lv_obj_t *tag;
    lv_obj_t *tag_label;
    lv_obj_t *note;
    lv_obj_t *undo;
    lv_obj_t *undo_bar;
    lv_obj_t *swoosh;
    /* What is currently applied, to restyle only on change. */
    int shown_state;
    bool shown_online;
    char shown_label[TODO_LABEL_MAX + 1];
    char shown_source[TODO_SOURCE_MAX + 1];
    bool crossed;
    uint32_t cross_ms;
    int32_t leave_h;
} row_t;

static struct {
    todo_model_t *model;
    uint32_t now;
    uint32_t seq;
    lv_obj_t *root;

    lv_obj_t *top;
    lv_obj_t *date;
    lv_obj_t *clock;
    lv_obj_t *pill;
    lv_obj_t *pill_fill;
    lv_obj_t *pill_label;
    lv_obj_t *peek_box;
    lv_obj_t *peek_muse;
    uint32_t peek_ms;
    bool peek_on;

    lv_obj_t *main;
    lv_obj_t *list;
    row_t rows[TODO_MAX_ITEMS];

    lv_obj_t *idle;
    lv_obj_t *idle_muse;
    lv_obj_t *idle_time;
    lv_obj_t *idle_date;

    lv_obj_t *done;
    lv_obj_t *done_muse;
    lv_obj_t *done_title;
    lv_obj_t *done_sub;
    lv_obj_t *done_note;

    lv_obj_t *party;
    lv_obj_t *party_muse;
    lv_obj_t *party_title;
    lv_obj_t *confetti[CONFETTI_N];

    lv_obj_t *bar;
    lv_obj_t *bar_muse;
    lv_obj_t *bar_text;
    uint32_t bar_seq;
    uint32_t bar_ms;
    bool bar_on;

    uint32_t mascot_ms;
    int screen;
} s;

/* ---- small helpers ------------------------------------------------------ */

static lv_color_t col(uint32_t c)
{
    return lv_color_hex(c);
}

static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col(color), 0);
    lv_label_set_text(l, "");
    return l;
}

static void set_text(lv_obj_t *label, const char *t)
{
    const char *cur = lv_label_get_text(label);
    if (!cur || strcmp(cur, t)) {
        lv_label_set_text(label, t);
    }
}

static void show(lv_obj_t *o, bool on)
{
    if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (on) {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static float clamp01(float v)
{
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static float ease_out(float t)
{
    t = clamp01(t);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

static float since(uint32_t then)
{
    return (float)(uint32_t)(s.now - then);
}

static void scale_obj(lv_obj_t *o, float k)
{
    int32_t v = (int32_t)lroundf(256 * k);
    lv_obj_set_style_transform_scale_x(o, v, 0);
    lv_obj_set_style_transform_scale_y(o, v, 0);
}

static void format_clock(time_t t, char *out, size_t len)
{
    struct tm tm;
    if (!t || !localtime_r(&t, &tm)) {
        out[0] = '\0';
        return;
    }
    int h = tm.tm_hour % 12;
    snprintf(out, len, "%d:%02d %s", h ? h : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}

static void format_date(time_t t, char *out, size_t len)
{
    static const char *const DAYS[] = { "Sunday", "Monday", "Tuesday", "Wednesday",
                                        "Thursday", "Friday", "Saturday" };
    static const char *const MONTHS[] = { "January", "February", "March", "April",
                                          "May", "June", "July", "August",
                                          "September", "October", "November", "December" };
    struct tm tm;
    if (!t || !localtime_r(&t, &tm)) {
        out[0] = '\0';
        return;
    }
    snprintf(out, len, "%s, %s %d", DAYS[tm.tm_wday], MONTHS[tm.tm_mon], tm.tm_mday);
}

static time_t wall(void)
{
    const todo_port_t *p = &s.model->port;
    return p->wall_time ? p->wall_time(p->ctx) : 0;
}

/* ---- events ------------------------------------------------------------- */

static void row_clicked(lv_event_t *e)
{
    row_t *r = lv_event_get_user_data(e);
    todo_model_tap(s.model, r->id, s.now);
}

static void undo_clicked(lv_event_t *e)
{
    row_t *r = lv_event_get_user_data(e);
    todo_model_undo(s.model, r->id, s.now);
}

static void bar_clicked(lv_event_t *e)
{
    (void)e;
    todo_model_clear_message(s.model);
}

/* ---- rows --------------------------------------------------------------- */

static row_t *row_find(const char *id)
{
    for (size_t i = 0; i < TODO_MAX_ITEMS; i++) {
        if (s.rows[i].used && !strcmp(s.rows[i].id, id)) {
            return &s.rows[i];
        }
    }
    return NULL;
}

static row_t *row_create(const todo_item_t *it)
{
    row_t *r = NULL;
    for (size_t i = 0; i < TODO_MAX_ITEMS && !r; i++) {
        if (!s.rows[i].used) {
            r = &s.rows[i];
        }
    }
    if (!r) {
        return NULL;
    }
    memset(r, 0, sizeof(*r));
    r->used = true;
    r->shown_state = -1;
    snprintf(r->id, sizeof(r->id), "%s", it->id);

    lv_obj_t *row = box(s.list);
    r->row = row;
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, ROW_MIN_H, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, col(C_CARD), 0);
    lv_obj_set_style_bg_color(row, col(C_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 26, 0);
    lv_obj_set_style_pad_hor(row, 18, 0);
    lv_obj_set_style_pad_ver(row, 12, 0);
    lv_obj_set_style_pad_column(row, 18, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, r);

    r->check = box(row);
    lv_obj_set_size(r->check, CHECK_PX, CHECK_PX);
    lv_obj_set_style_radius(r->check, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r->check, 4, 0);
    lv_obj_set_style_bg_opa(r->check, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(r->check, CHECK_PX / 2, 0);
    lv_obj_set_style_transform_pivot_y(r->check, CHECK_PX / 2, 0);
    r->check_icon = text(r->check, &lv_font_montserrat_28, C_PLUM_TEXT);
    lv_label_set_text(r->check_icon, LV_SYMBOL_OK);
    lv_obj_center(r->check_icon);

    r->label = text(row, &lv_font_montserrat_28, C_CREAM);
    lv_obj_set_flex_grow(r->label, 1);
    lv_label_set_long_mode(r->label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(r->label, LV_SIZE_CONTENT);
    /* Two lines at most; longer labels end in an ellipsis. */
    lv_obj_set_style_max_height(r->label, 2 * lv_font_get_line_height(&lv_font_montserrat_28), 0);

    r->side = box(row);
    lv_obj_set_size(r->side, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r->side, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->side, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r->side, 10, 0);

    r->note = text(r->side, &lv_font_montserrat_20, C_MUTED);

    r->tag = box(r->side);
    lv_obj_set_size(r->tag, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r->tag, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(r->tag, col(C_MINT), 0);
    lv_obj_set_style_radius(r->tag, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(r->tag, 14, 0);
    lv_obj_set_style_pad_ver(r->tag, 6, 0);
    r->tag_label = text(r->tag, &lv_font_montserrat_16, C_MINT);

    r->undo = box(r->side);
    lv_obj_set_size(r->undo, 124, 56);
    lv_obj_set_style_bg_opa(r->undo, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->undo, col(C_PEACH), 0);
    lv_obj_set_style_bg_color(r->undo, col(C_CREAM), LV_STATE_PRESSED);
    lv_obj_set_style_radius(r->undo, 28, 0);
    lv_obj_set_style_clip_corner(r->undo, true, 0);
    lv_obj_add_flag(r->undo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r->undo, undo_clicked, LV_EVENT_CLICKED, r);
    lv_obj_t *ul = text(r->undo, &lv_font_montserrat_24, C_PLUM_TEXT);
    lv_label_set_text(ul, "Undo");
    lv_obj_align(ul, LV_ALIGN_CENTER, 0, -2);
    r->undo_bar = box(r->undo);
    lv_obj_set_height(r->undo_bar, 6);
    lv_obj_align(r->undo_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(r->undo_bar, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(r->undo_bar, col(C_PINK), 0);

    r->swoosh = box(row);
    lv_obj_add_flag(r->swoosh, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_height(r->swoosh, 6);
    lv_obj_set_style_radius(r->swoosh, 3, 0);
    lv_obj_set_style_bg_opa(r->swoosh, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->swoosh, col(C_PINK), 0);
    show(r->swoosh, false);
    return r;
}

static void row_delete(row_t *r)
{
    lv_obj_delete(r->row);
    memset(r, 0, sizeof(*r));
}

static bool crossed_state(todo_row_state_t st)
{
    return st == TODO_ROW_QUEUED || st == TODO_ROW_SENDING || st == TODO_ROW_AUTO_DONE;
}

/* Restyle a row when its state, label or connection note changes. */
static void row_apply(row_t *r, const todo_item_t *it)
{
    todo_row_state_t st = it->state;
    if (st == TODO_ROW_LEAVING) {
        return; /* keep the last look while it hops away */
    }
    bool online = s.model->online;
    if ((int)st == r->shown_state && online == r->shown_online && !strcmp(r->shown_label, it->label)
        && !strcmp(r->shown_source, it->source)) {
        return;
    }
    r->shown_state = (int)st;
    r->shown_online = online;
    snprintf(r->shown_label, sizeof(r->shown_label), "%s", it->label);
    snprintf(r->shown_source, sizeof(r->shown_source), "%s", it->source);

    set_text(r->label, it->label);
    const char *tag = todo_source_label(it->source);
    set_text(r->tag_label, tag);

    bool crossed = crossed_state(st);
    if (crossed && !r->crossed) {
        r->cross_ms = s.now;
    }
    r->crossed = crossed;

    uint32_t ring = C_PINK, fill = C_BG;
    lv_opa_t fill_opa = LV_OPA_TRANSP;
    bool tick = false;
    switch (st) {
    case TODO_ROW_OPEN:
        ring = it->tappable ? C_PINK : C_FAINT;
        break;
    case TODO_ROW_UNDO:
        fill = C_PINK;
        fill_opa = LV_OPA_COVER;
        tick = true;
        break;
    case TODO_ROW_QUEUED:
    case TODO_ROW_SENDING:
        fill = C_PINK;
        fill_opa = LV_OPA_70;
        ring = C_PINK;
        tick = true;
        break;
    case TODO_ROW_AUTO_DONE:
        fill = C_MINT;
        ring = C_MINT;
        fill_opa = LV_OPA_COVER;
        tick = true;
        break;
    default:
        break;
    }
    lv_obj_set_style_border_color(r->check, col(ring), 0);
    lv_obj_set_style_border_opa(r->check, it->tappable || st != TODO_ROW_OPEN ? LV_OPA_COVER : LV_OPA_60, 0);
    lv_obj_set_style_bg_color(r->check, col(fill), 0);
    lv_obj_set_style_bg_opa(r->check, fill_opa, 0);
    show(r->check_icon, tick);

    lv_obj_set_style_text_color(r->label, col(crossed ? C_MUTED : C_CREAM), 0);
    lv_obj_set_style_text_decor(r->label, crossed ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, 0);

    show(r->tag, tag[0] && (st == TODO_ROW_OPEN || st == TODO_ROW_AUTO_DONE));
    show(r->undo, st == TODO_ROW_UNDO);
    const char *note = "";
    if (st == TODO_ROW_SENDING || (st == TODO_ROW_QUEUED && online)) {
        note = "sending...";
    } else if (st == TODO_ROW_QUEUED) {
        note = "waiting for connection";
    }
    set_text(r->note, note);
    show(r->note, note[0] != '\0');

    bool tappable = it->tappable && st == TODO_ROW_OPEN;
    if (tappable) {
        lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_remove_flag(r->row, LV_OBJ_FLAG_CLICKABLE);
    }
}

/* Per-frame motion: squish, undo countdown, strike swoosh, hop away. */
static void row_animate(row_t *r, const todo_item_t *it)
{
    /* Squishy checkbox press. */
    float k = 1.0f;
    if (it->tap_ms && since(it->tap_ms) < SQUISH_MS) {
        float t = since(it->tap_ms) / SQUISH_MS;
        k = t < 0.35f ? 1.0f - 0.22f * (t / 0.35f) : 0.78f + 0.22f * ease_out((t - 0.35f) / 0.65f) + 0.08f * sinf((t - 0.35f) / 0.65f * PI_F);
    }
    scale_obj(r->check, k);

    if (it->state == TODO_ROW_UNDO) {
        float left = 1.0f - clamp01(since(it->state_ms) / TODO_UNDO_MS);
        lv_obj_set_width(r->undo_bar, (int32_t)lroundf(124 * left));
    }

    /* A happy pink swoosh across the label as it's crossed out. */
    bool swoosh = r->crossed && since(r->cross_ms) < SWOOSH_MS && it->state != TODO_ROW_LEAVING;
    show(r->swoosh, swoosh);
    if (swoosh) {
        float t = since(r->cross_ms) / SWOOSH_MS;
        lv_point_t size;
        const lv_font_t *font = &lv_font_montserrat_28;
        int32_t max_w = lv_obj_get_width(r->label);
        lv_text_get_size(&size, it->label, font, 0, 0, max_w, LV_TEXT_FLAG_NONE);
        int32_t w = size.x < max_w ? size.x : max_w;
        lv_obj_set_width(r->swoosh, (int32_t)lroundf((float)(w + 12) * ease_out(t / 0.6f)));
        lv_obj_set_style_bg_opa(r->swoosh, (lv_opa_t)lroundf(255 * (1 - clamp01((t - 0.6f) / 0.4f))), 0);
        lv_obj_set_pos(r->swoosh, lv_obj_get_x(r->label) - 6,
                       lv_obj_get_y(r->label) + lv_font_get_line_height(font) / 2 - 3);
    }

    /* Hop away: a little jump to the right, fade, then the gap closes. */
    if (it->state == TODO_ROW_LEAVING) {
        float t = clamp01(since(it->state_ms) / TODO_LEAVE_MS);
        if (!r->leave_h) {
            r->leave_h = lv_obj_get_height(r->row);
        }
        lv_obj_set_style_translate_x(r->row, (int32_t)lroundf(90 * t * t), 0);
        lv_obj_set_style_translate_y(r->row, (int32_t)lroundf(-18 * sinf(PI_F * clamp01(t / 0.6f))), 0);
        lv_obj_set_style_opa(r->row, (lv_opa_t)lroundf(255 * (1 - clamp01(t / 0.7f))), 0);
        if (t > 0.5f) {
            float c = (t - 0.5f) / 0.5f;
            lv_obj_set_style_min_height(r->row, 0, 0);
            lv_obj_set_height(r->row, (int32_t)lroundf((float)r->leave_h * (1 - ease_out(c))));
            lv_obj_set_style_pad_ver(r->row, (int32_t)lroundf(12 * (1 - c)), 0);
        }
    }
}

static void sync_rows(void)
{
    const todo_model_t *m = s.model;
    /* Drop rows whose item is gone or hidden. */
    for (size_t i = 0; i < TODO_MAX_ITEMS; i++) {
        row_t *r = &s.rows[i];
        if (!r->used) {
            continue;
        }
        const todo_item_t *it = todo_model_find(m, r->id);
        if (!it || !todo_model_item_visible(it)) {
            row_delete(r);
        }
    }
    /* Create, order and style the visible ones. */
    uint32_t index = 0;
    for (size_t i = 0; i < m->count; i++) {
        const todo_item_t *it = &m->items[i];
        if (!todo_model_item_visible(it)) {
            continue;
        }
        row_t *r = row_find(it->id);
        if (!r) {
            r = row_create(it);
            if (!r) {
                continue;
            }
        }
        if (lv_obj_get_index(r->row) != (int32_t)index) {
            lv_obj_move_to_index(r->row, (int32_t)index);
        }
        index++;
        row_apply(r, it);
    }
    lv_obj_update_layout(s.list);
    for (size_t i = 0; i < m->count; i++) {
        const todo_item_t *it = &m->items[i];
        row_t *r = todo_model_item_visible(it) ? row_find(it->id) : NULL;
        if (r) {
            row_animate(r, it);
        }
    }
}

/* ---- building ------------------------------------------------------------ */

static void build_top(void)
{
    s.top = box(s.root);
    lv_obj_set_size(s.top, SCREEN_W, TOP_H);
    lv_obj_set_style_pad_hor(s.top, 28, 0);

    s.date = text(s.top, &lv_font_montserrat_28, C_CREAM);
    lv_obj_align(s.date, LV_ALIGN_TOP_LEFT, 0, 14);
    s.clock = text(s.top, &lv_font_montserrat_20, C_MUTED);
    lv_obj_align(s.clock, LV_ALIGN_TOP_LEFT, 0, 50);

    s.pill = box(s.top);
    lv_obj_set_size(s.pill, 200, 48);
    lv_obj_align(s.pill, LV_ALIGN_RIGHT_MID, 0, 2);
    lv_obj_set_style_radius(s.pill, 24, 0);
    lv_obj_set_style_bg_opa(s.pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s.pill, col(C_CARD), 0);
    lv_obj_set_style_clip_corner(s.pill, true, 0);
    s.pill_fill = box(s.pill);
    lv_obj_set_height(s.pill_fill, 48);
    lv_obj_set_style_bg_opa(s.pill_fill, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(s.pill_fill, col(C_PINK), 0);
    s.pill_label = text(s.pill, &lv_font_montserrat_20, C_CREAM);
    lv_obj_center(s.pill_label);

    /* Muse peeks up from behind the bottom edge of this little window. */
    s.peek_box = box(s.top);
    lv_obj_set_size(s.peek_box, 112, TOP_H);
    lv_obj_align(s.peek_box, LV_ALIGN_RIGHT_MID, -208, 0);
    s.peek_muse = todo_mascot_create(s.peek_box, 128);
    lv_obj_set_pos(s.peek_muse, -8, TOP_H);
}

static void build_idle(lv_obj_t *parent)
{
    s.idle = box(parent);
    lv_obj_set_size(s.idle, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(s.idle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s.idle, col(C_BG), 0);

    s.idle_muse = todo_mascot_create(s.idle, 256);
    lv_obj_align(s.idle_muse, LV_ALIGN_LEFT_MID, 64, -4);

    lv_obj_t *col_box = box(s.idle);
    lv_obj_set_size(col_box, 420, LV_SIZE_CONTENT);
    lv_obj_align(col_box, LV_ALIGN_LEFT_MID, 350, 0);
    lv_obj_set_flex_flow(col_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col_box, 6, 0);
    s.idle_time = text(col_box, &lv_font_montserrat_48, C_CREAM);
    s.idle_date = text(col_box, &lv_font_montserrat_24, C_MUTED);
    lv_obj_t *gap = box(col_box);
    lv_obj_set_size(gap, 1, 18);
    lv_obj_t *wait = text(col_box, &lv_font_montserrat_32, C_PINK);
    lv_label_set_text(wait, "waiting for\ntoday's list");
    lv_obj_t *sub = text(col_box, &lv_font_montserrat_20, C_MUTED);
    lv_label_set_text(sub, "Muse will send it over soon.");
}

static void build_done(void)
{
    s.done = box(s.main);
    lv_obj_set_size(s.done, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(s.done, LV_OBJ_FLAG_IGNORE_LAYOUT);

    s.done_muse = todo_mascot_create(s.done, 256);
    lv_obj_align(s.done_muse, LV_ALIGN_LEFT_MID, 70, -10);

    lv_obj_t *col_box = box(s.done);
    lv_obj_set_size(col_box, 400, LV_SIZE_CONTENT);
    lv_obj_align(col_box, LV_ALIGN_LEFT_MID, 360, -10);
    lv_obj_set_flex_flow(col_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col_box, 8, 0);
    s.done_title = text(col_box, &lv_font_montserrat_48, C_BUTTER);
    s.done_sub = text(col_box, &lv_font_montserrat_24, C_CREAM);
    lv_obj_set_width(s.done_sub, 400);
    lv_label_set_long_mode(s.done_sub, LV_LABEL_LONG_MODE_WRAP);
    s.done_note = text(col_box, &lv_font_montserrat_20, C_MUTED);
}

static const uint32_t CONFETTI_COLORS[] = { C_PINK, C_BUTTER, C_MINT, C_PEACH, C_CREAM };

static void build_party(void)
{
    s.party = box(s.main);
    lv_obj_set_size(s.party, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(s.party, LV_OBJ_FLAG_IGNORE_LAYOUT);
    for (int i = 0; i < CONFETTI_N; i++) {
        lv_obj_t *c = box(s.party);
        lv_obj_set_size(c, 10 + (i % 3) * 3, 16 - (i % 4) * 2);
        lv_obj_set_style_radius(c, 3, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, col(CONFETTI_COLORS[i % 5]), 0);
        lv_obj_set_style_transform_pivot_x(c, 6, 0);
        lv_obj_set_style_transform_pivot_y(c, 7, 0);
        s.confetti[i] = c;
    }
    s.party_muse = todo_mascot_create(s.party, 256);
    lv_obj_align(s.party_muse, LV_ALIGN_CENTER, 0, -26);
    s.party_title = text(s.party, &lv_font_montserrat_48, C_BUTTER);
    lv_label_set_text(s.party_title, "All done!");
    lv_obj_align(s.party_title, LV_ALIGN_BOTTOM_MID, 0, -18);
}

static void build_bar(void)
{
    s.bar = box(s.root);
    lv_obj_set_size(s.bar, SCREEN_W - 32, LV_SIZE_CONTENT);
    lv_obj_add_flag(s.bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_min_height(s.bar, 76, 0);
    lv_obj_set_style_radius(s.bar, 28, 0);
    lv_obj_set_style_bg_opa(s.bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s.bar, col(C_CREAM), 0);
    lv_obj_set_style_pad_left(s.bar, 104, 0);
    lv_obj_set_style_pad_right(s.bar, 24, 0);
    lv_obj_set_style_pad_ver(s.bar, 12, 0);
    lv_obj_set_style_shadow_width(s.bar, 24, 0);
    lv_obj_set_style_shadow_color(s.bar, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s.bar, LV_OPA_40, 0);
    lv_obj_add_flag(s.bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.bar, bar_clicked, LV_EVENT_CLICKED, NULL);

    /* Muse's head pops over the left edge of the bubble. */
    lv_obj_t *head = box(s.bar);
    lv_obj_add_flag(head, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(head, 78, 64);
    lv_obj_set_style_radius(head, 22, 0);
    lv_obj_set_style_clip_corner(head, true, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(head, col(C_BG), 0);
    lv_obj_align(head, LV_ALIGN_LEFT_MID, -92, 0);
    /* Frame just the face and hat. */
    s.bar_muse = todo_mascot_create(head, 128);
    lv_obj_set_pos(s.bar_muse, -26, -20);

    s.bar_text = text(s.bar, &lv_font_montserrat_24, C_PLUM_TEXT);
    lv_obj_set_width(s.bar_text, lv_pct(100));
    lv_label_set_long_mode(s.bar_text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s.bar_text, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s.bar_text, 2 * lv_font_get_line_height(&lv_font_montserrat_24), 0);
    lv_obj_align(s.bar_text, LV_ALIGN_LEFT_MID, 0, 0);
    show(s.bar, false);
}

void todo_ui_create(lv_obj_t *parent, todo_model_t *model)
{
    memset(&s, 0, sizeof(s));
    s.model = model;
    s.screen = -1;
    s.mascot_ms = UINT32_MAX;

    lv_obj_set_style_bg_color(parent, col(C_BG), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    s.root = box(parent);
    lv_obj_set_size(s.root, SCREEN_W, SCREEN_H);
    lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);

    build_top();

    s.main = box(s.root);
    lv_obj_set_width(s.main, SCREEN_W);
    lv_obj_set_flex_grow(s.main, 1);

    s.list = box(s.main);
    lv_obj_set_size(s.list, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(s.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s.list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(s.list, 24, 0);
    lv_obj_set_style_pad_top(s.list, 4, 0);
    lv_obj_set_style_pad_bottom(s.list, 16, 0);
    lv_obj_set_style_pad_row(s.list, 12, 0);

    build_done();
    build_party();
    build_bar();
    build_idle(parent);
}

/* ---- per frame ----------------------------------------------------------- */

static void update_top(todo_screen_t screen)
{
    char buf[64];
    time_t now = wall();
    if (s.model->date[0]) {
        set_text(s.date, s.model->date);
    } else {
        format_date(now, buf, sizeof(buf));
        set_text(s.date, buf);
    }
    format_clock(now, buf, sizeof(buf));
    set_text(s.clock, buf);

    int done, total;
    todo_model_progress(s.model, &done, &total);
    snprintf(buf, sizeof(buf), "%d of %d done", done, total);
    set_text(s.pill_label, buf);
    lv_obj_set_width(s.pill_fill, total ? 200 * done / total : 0);
    show(s.pill, s.model->have_list);
    show(s.top, screen != TODO_SCREEN_IDLE);
}

static void update_peek(bool draw)
{
    if (!s.peek_on && (s.now % PEEK_EVERY_MS) < 40 && s.now > 1000) {
        todo_ui_peek(s.now);
    }
    float t = since(s.peek_ms);
    if (s.peek_on && t >= PEEK_MS) {
        s.peek_on = false;
    }
    show(s.peek_muse, s.peek_on);
    if (!s.peek_on) {
        return;
    }
    /* Slide up, hold, slide down. */
    float up = t < 400 ? ease_out(t / 400) : (t > PEEK_MS - 400 ? 1 - ease_out((t - (PEEK_MS - 400)) / 400) : 1);
    lv_obj_set_y(s.peek_muse, TOP_H - (int32_t)lroundf(78 * up));
    if (draw) {
        todo_mascot_pose_t pose = { .t = s.now / 1000.0f, .happy = 0, .hat_lift = 0, .hat_tilt = 0 };
        todo_mascot_draw(s.peek_muse, &pose);
    }
}

static void update_bar(bool draw)
{
    const todo_model_t *m = s.model;
    bool want = m->message[0] != '\0';
    if (m->message_seq != s.bar_seq) {
        s.bar_seq = m->message_seq;
        if (want) {
            set_text(s.bar_text, m->message);
            if (!s.bar_on) {
                s.bar_ms = s.now;
            }
        }
    }
    s.bar_on = want;
    show(s.bar, want);
    lv_obj_set_style_pad_bottom(s.main, want ? 108 : 0, 0);
    if (!want) {
        return;
    }
    float t = ease_out(since(s.bar_ms) / BAR_SLIDE_MS);
    lv_obj_update_layout(s.bar);
    int32_t h = lv_obj_get_height(s.bar);
    /* Slides up from below the screen to rest 14 px above the bottom edge. */
    lv_obj_set_pos(s.bar, 16, SCREEN_H - (int32_t)lroundf((float)(h + 14) * t));
    if (draw) {
        todo_mascot_pose_t pose = { .t = s.now / 1000.0f + 1.7f };
        todo_mascot_draw(s.bar_muse, &pose);
    }
}

static void update_done(bool draw)
{
    int done, total;
    todo_model_progress(s.model, &done, &total);
    char buf[64];
    if (total == 0) {
        set_text(s.done_title, "All clear!");
        set_text(s.done_sub, "Nothing on the list today.");
    } else {
        set_text(s.done_title, "All done!");
        snprintf(buf, sizeof(buf), "%d of %d. Nice work today.", done, total);
        set_text(s.done_sub, buf);
    }
    int pending = todo_model_outbox_count(s.model);
    if (pending) {
        snprintf(buf, sizeof(buf), s.model->online ? "telling Muse about %d..." : "%d waiting for connection",
                 pending);
    } else {
        buf[0] = '\0';
    }
    set_text(s.done_note, buf);
    show(s.done_note, pending > 0);
    if (draw) {
        todo_mascot_pose_t pose = { .t = s.now / 1000.0f, .happy = 0.35f };
        todo_mascot_draw(s.done_muse, &pose);
    }
}

static void update_party(bool draw)
{
    float t = since(s.model->celebrate_ms) / 1000.0f; /* seconds into the 2 s show */
    int32_t w = lv_obj_get_width(s.party), h = lv_obj_get_height(s.party);
    for (int i = 0; i < CONFETTI_N; i++) {
        float speed = 160.0f + (float)((i * 53) % 90);
        float x0 = (float)((i * 137 + 31) % (w - 20));
        float y = -30.0f - (float)((i * 71) % 160) + speed * t * 1.4f;
        float x = x0 + 18.0f * sinf(t * 4.0f + (float)i);
        lv_obj_set_pos(s.confetti[i], (int32_t)lroundf(x), (int32_t)lroundf(y));
        lv_obj_set_style_transform_rotation(s.confetti[i], (int32_t)lroundf(fmodf(t * 540.0f + i * 40.0f, 3600.0f)), 0);
        show(s.confetti[i], y < h);
    }
    /* Bounce, and a hat tip in the middle. */
    float bounce = fabsf(sinf(t * PI_F * 2.5f)) * 22.0f * (1 - clamp01((t - 1.5f) / 0.5f));
    lv_obj_align(s.party_muse, LV_ALIGN_CENTER, 0, -26 - (int32_t)lroundf(bounce));
    float tip = clamp01((t - 0.35f) / 0.9f);
    float tip_amt = sinf(tip * PI_F);
    lv_obj_set_style_opa(s.party_title, (lv_opa_t)lroundf(255 * clamp01((t - 0.3f) / 0.4f)), 0);
    scale_obj(s.party_title, 0.8f + 0.2f * ease_out((t - 0.3f) / 0.5f));
    lv_obj_set_style_transform_pivot_x(s.party_title, lv_obj_get_width(s.party_title) / 2, 0);
    lv_obj_set_style_transform_pivot_y(s.party_title, lv_obj_get_height(s.party_title) / 2, 0);
    if (draw) {
        todo_mascot_pose_t pose = {
            .t = s.now / 1000.0f,
            .happy = 1.0f,
            .hat_lift = 4.0f * tip_amt,
            .hat_tilt = -0.9f * tip_amt,
        };
        todo_mascot_draw(s.party_muse, &pose);
    }
}

void todo_ui_peek(uint32_t now_ms)
{
    s.peek_on = true;
    s.peek_ms = now_ms;
}

void todo_ui_update(uint32_t now_ms)
{
    s.now = now_ms;
    todo_screen_t screen = todo_model_screen(s.model);
    if ((int)screen != s.screen) {
        s.screen = (int)screen;
        s.mascot_ms = UINT32_MAX; /* draw mascots right away */
    }
    bool draw = s.mascot_ms == UINT32_MAX || since(s.mascot_ms) >= MASCOT_FRAME_MS;
    if (draw) {
        s.mascot_ms = now_ms;
    }

    update_top(screen);
    show(s.idle, screen == TODO_SCREEN_IDLE);
    show(s.list, screen == TODO_SCREEN_LIST);
    show(s.done, screen == TODO_SCREEN_ALL_DONE);
    show(s.party, screen == TODO_SCREEN_CELEBRATE);

    switch (screen) {
    case TODO_SCREEN_IDLE: {
        char buf[48];
        time_t now = wall();
        format_clock(now, buf, sizeof(buf));
        set_text(s.idle_time, buf);
        format_date(now, buf, sizeof(buf));
        set_text(s.idle_date, buf);
        if (draw) {
            todo_mascot_pose_t pose = { .t = s.now / 1000.0f };
            todo_mascot_draw(s.idle_muse, &pose);
        }
        break;
    }
    case TODO_SCREEN_LIST:
        sync_rows();
        break;
    case TODO_SCREEN_ALL_DONE:
        update_done(draw);
        break;
    case TODO_SCREEN_CELEBRATE:
        update_party(draw);
        break;
    }
    /* Keep rows in step even while hidden, so the list is right when it returns. */
    if (screen != TODO_SCREEN_LIST) {
        sync_rows();
    }
    update_peek(draw && screen != TODO_SCREEN_IDLE);
    update_bar(draw);
}

bool todo_ui_point_for(const char *target, lv_point_t *pt)
{
    lv_obj_t *o = NULL;
    if (!strncmp(target, "row:", 4)) {
        row_t *r = row_find(target + 4);
        o = r ? r->row : NULL;
    } else if (!strncmp(target, "undo:", 5)) {
        row_t *r = row_find(target + 5);
        o = r ? r->undo : NULL;
    } else if (!strcmp(target, "message")) {
        o = s.bar;
    }
    if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        return false;
    }
    lv_obj_update_layout(o);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    pt->x = (a.x1 + a.x2) / 2;
    pt->y = (a.y1 + a.y2) / 2;
    return pt->y >= 0 && pt->y < SCREEN_H && pt->x >= 0 && pt->x < SCREEN_W;
}

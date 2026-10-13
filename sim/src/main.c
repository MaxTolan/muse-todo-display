/*
 * 800x480 desktop simulator for the todo display.
 *
 * Runs the real model (todo_model.c) and screen (todo_ui.c) with SDL standing
 * in for the panel and touch, a virtual clock, and a pretend Muse that
 * answers completion turns. Scenarios script it for repeatable screenshots
 * and tests; see sim/README.md.
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"

#include "png_write.h"
#include "todo_fonts.h"
#include "todo_model.h"
#include "todo_ui.h"

/* 800x480 landscape, or 480x800 with --portrait (the board on its stand). */
static int s_screen_w = 800, s_screen_h = 480;
#define SCREEN_W s_screen_w
#define SCREEN_H s_screen_h
#define FRAME_STEP_MS 10u
#define TAP_HOLD_MS 80u

/* ---- simulated platform -------------------------------------------------- */

/* The SDK-style tileview pieces when run with --sdk-layout (see ui_parent()). */
static lv_obj_t *s_tv, *s_face, *s_root;

static struct {
    uint32_t now_ms;
    time_t wall_base;
    uint32_t wall_base_ms;
    bool online;
    bool refuse_turns;
    bool turn_open;
    char auto_reply[TODO_MESSAGE_MAX + 1];
    uint32_t auto_reply_ms;
    uint32_t reply_due_ms;
    bool reply_due;
    char last_turn[8192];
    int turns;
    char *saved;
    const char *state_path;
    const char *list_date;
    char list_date_buf[TODO_DATE_MAX + 1];

    lv_display_t *display;
    lv_indev_t *script_touch;
    lv_point_t touch_pt;
    bool touch_down;

    todo_model_t model;
    int failures;
    volatile sig_atomic_t quit;
} g;

static uint32_t tick_cb(void)
{
    return g.now_ms;
}

static bool port_link_ready(void *ctx)
{
    (void)ctx;
    return g.online;
}

static time_t port_wall(void *ctx)
{
    (void)ctx;
    return g.wall_base ? g.wall_base + (time_t)((g.now_ms - g.wall_base_ms) / 1000u) : 0;
}

static bool port_send_turn(void *ctx, const char *text)
{
    (void)ctx;
    if (g.refuse_turns || g.turn_open) {
        printf("[%7u] turn refused (busy)\n", g.now_ms);
        return false;
    }
    g.turns++;
    g.turn_open = true;
    snprintf(g.last_turn, sizeof(g.last_turn), "%s", text);
    printf("[%7u] TURN >>> Muse\n%s\n", g.now_ms, text);
    if (g.auto_reply[0]) {
        g.reply_due = true;
        g.reply_due_ms = g.now_ms + g.auto_reply_ms;
    }
    return true;
}

static void port_save(void *ctx, const char *blob)
{
    (void)ctx;
    free(g.saved);
    g.saved = strdup(blob);
    if (g.state_path) {
        FILE *f = fopen(g.state_path, "w");
        if (f) {
            fputs(blob, f);
            fclose(f);
        }
    }
}

static const todo_port_t PORT = { NULL, port_link_ready, port_send_turn, port_wall, port_save };

static void muse_reply(const char *text)
{
    if (!g.turn_open) {
        printf("[%7u] (no turn to reply to)\n", g.now_ms);
        return;
    }
    g.turn_open = false;
    g.reply_due = false;
    printf("[%7u] Muse <<< %s\n", g.now_ms, text);
    todo_model_turn_reply(&g.model, text);
    todo_model_turn_done(&g.model, g.now_ms);
}

static void muse_error(void)
{
    if (!g.turn_open) {
        return;
    }
    g.turn_open = false;
    g.reply_due = false;
    printf("[%7u] turn ERROR\n", g.now_ms);
    todo_model_turn_error(&g.model, g.now_ms);
}

/* Scripted finger: a second pointer device next to the SDL mouse. */
static void script_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = g.touch_pt;
    data->state = g.touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/*
 * SDL keeps reporting the mouse while a drag leaves the window, which LVGL
 * logs as a warning on every frame. A real panel can't report off-screen
 * points, so clamp to the window like the touch controller would.
 */
static lv_indev_read_cb_t s_sdl_mouse_read;

static void clamped_mouse_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    s_sdl_mouse_read(indev, data);
    data->point.x = LV_CLAMP(0, data->point.x, SCREEN_W - 1);
    data->point.y = LV_CLAMP(0, data->point.y, SCREEN_H - 1);
}

/* Pixels LVGL marks for redraw, for checking that a still screen stays still. */
static unsigned long long s_redraw_px;

static void invalidate_cb(lv_event_t *e)
{
    const lv_area_t *a = lv_event_get_param(e);
    s_redraw_px += (unsigned long long)lv_area_get_size(a);
}

/* ---- main loop ----------------------------------------------------------- */

static void frame(void)
{
    if (g.reply_due && (int32_t)(g.now_ms - g.reply_due_ms) >= 0) {
        muse_reply(g.auto_reply);
    }
    todo_model_tick(&g.model, g.now_ms);
    todo_ui_update(g.now_ms);
    lv_timer_handler();
}

static void run_for(uint32_t ms, bool real_time)
{
    uint32_t end = g.now_ms + ms;
    while (!g.quit && (int32_t)(end - g.now_ms) > 0) {
        uint32_t step = end - g.now_ms < FRAME_STEP_MS ? end - g.now_ms : FRAME_STEP_MS;
        g.now_ms += step;
        frame();
        if (real_time) {
            SDL_Delay(step);
        }
    }
}

/*
 * Skip ahead through a long quiet stretch: the model and screen state keep
 * up in 1 s steps, but nothing is drawn until the end. For waits measured in
 * minutes, where drawing every 10 ms frame would take ages.
 */
static void wait_for(uint32_t ms)
{
    uint32_t end = g.now_ms + ms;
    while (!g.quit && (int32_t)(end - g.now_ms) > 0) {
        uint32_t step = end - g.now_ms < 1000u ? end - g.now_ms : 1000u;
        g.now_ms += step;
        if (g.reply_due && (int32_t)(g.now_ms - g.reply_due_ms) >= 0) {
            muse_reply(g.auto_reply);
        }
        todo_model_tick(&g.model, g.now_ms);
        todo_ui_update(g.now_ms);
    }
    lv_timer_handler();
}

static bool screenshot(const char *path)
{
    lv_refr_now(g.display);
    SDL_Renderer *renderer = lv_sdl_window_get_renderer(g.display);
    int w = 0, h = 0;
    if (!renderer || SDL_GetRendererOutputSize(renderer, &w, &h) != 0 || w <= 0 || h <= 0) {
        fprintf(stderr, "could not read the simulator screen: %s\n", SDL_GetError());
        return false;
    }
    uint8_t *px = malloc((size_t)w * (size_t)h * 3);
    if (!px) {
        return false;
    }
    bool ok = SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB24, px, w * 3) == 0
              && png_write_rgb(path, px, w, h);
    free(px);
    if (!ok) {
        fprintf(stderr, "%s: screenshot failed\n", path);
    } else {
        printf("[%7u] screenshot %s\n", g.now_ms, path);
    }
    return ok;
}

static void tap_point(lv_point_t pt, bool real_time)
{
    g.touch_pt = pt;
    g.touch_down = true;
    run_for(TAP_HOLD_MS, real_time);
    g.touch_down = false;
    run_for(40, real_time);
}

/* A finger drag from (x1,y1) to (x2,y2) over ~300 ms, like a scroll. */
static void drag(int x1, int y1, int x2, int y2, bool real_time)
{
    g.touch_pt = (lv_point_t){ x1, y1 };
    g.touch_down = true;
    run_for(40, real_time);
    for (int i = 1; i <= 15; i++) {
        g.touch_pt = (lv_point_t){ x1 + (x2 - x1) * i / 15, y1 + (y2 - y1) * i / 15 };
        run_for(20, real_time);
    }
    g.touch_down = false;
    run_for(40, real_time);
}

static void tap(const char *target, bool real_time)
{
    lv_point_t pt;
    if (!todo_ui_point_for(target, &pt)) {
        fprintf(stderr, "tap: %s is not on screen\n", target);
        g.failures++;
        return;
    }
    tap_point(pt, real_time);
}

/*
 * Type text on the on-screen keyboard, one key press at a time, switching
 * between its letter/CAPS/symbol pages as a person would. The sheet opens on
 * the lowercase page.
 */
static bool type_text(const char *text, bool real_time)
{
    enum { LOWER, UPPER, SPECIAL } page = LOWER;
    for (const char *p = text; *p; p++) {
        char key[2] = { *p, 0 };
        int want;
        if ((*p >= 'a' && *p <= 'z') || strchr(" _-.,:", *p)) {
            want = page == UPPER && !(*p >= 'a' && *p <= 'z') ? UPPER : LOWER;
        } else if (*p >= 'A' && *p <= 'Z') {
            want = UPPER;
        } else {
            want = SPECIAL;
        }
        if (want != (int)page) {
            if (page != LOWER) {
                todo_ui_type("abc");
            }
            if (want == UPPER) {
                todo_ui_type("ABC");
            } else if (want == SPECIAL) {
                todo_ui_type("1#");
            }
            page = want;
        }
        if (!todo_ui_type(key)) {
            fprintf(stderr, "type: no key for '%c' (is the add-task sheet open?)\n", *p);
            return false;
        }
        run_for(30, real_time);
    }
    if (page != LOWER) {
        todo_ui_type("abc");
    }
    return true;
}

/*
 * --sdk-layout: host the screen the way the firmware does (todo_glue.c inside
 * the SDK's muse_ui.c): a tileview with the face tile (our screen) and a
 * settings tile one swipe to the left. Bugs in how our list scrolls inside
 * that tileview only show up here.
 */
static bool s_sdk_layout;

static lv_obj_t *ui_parent(void)
{
    lv_obj_t *scr = lv_screen_active();
    if (!s_sdk_layout) {
        return scr;
    }
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *tv = s_tv = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(tv, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *face = s_face = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_set_scrollbar_mode(face, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(face, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(face, LV_OPA_COVER, 0);
    lv_obj_t *settings = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT);
    lv_obj_t *menu = lv_list_create(settings);
    lv_obj_set_size(menu, lv_pct(100), lv_pct(100));
    for (int i = 0; i < 20; i++) {
        lv_list_add_text(menu, "setting");
    }
    /* What todo_glue.c's ui_build() puts on the face tile. */
    lv_obj_t *root = s_root = lv_obj_create(face);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    return root;
}

static void reboot(void)
{
    printf("[%7u] reboot\n", g.now_ms);
    char *blob = g.saved ? strdup(g.saved) : NULL;
    g.turn_open = false;
    g.reply_due = false;
    lv_obj_clean(lv_screen_active());
    todo_model_init(&g.model, &PORT);
    g.now_ms += 1000;
    if (blob) {
        todo_model_restore(&g.model, blob, g.now_ms);
        free(blob);
    }
    todo_ui_create(ui_parent(), &g.model);
    todo_ui_set_wifi(TODO_WIFI_CONNECTED);
}

/* "2026-10-07 09:14" in America/Chicago. */
static bool set_clock(const char *v)
{
    struct tm tm = { 0 };
    if (sscanf(v, "%d-%d-%d %d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min) != 5) {
        return false;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    g.wall_base = mktime(&tm);
    g.wall_base_ms = g.now_ms;
    return g.wall_base != (time_t)-1;
}

static bool parse_bool(const char *v, bool *out)
{
    if (!strcmp(v, "true") || !strcmp(v, "1") || !strcmp(v, "on")) {
        *out = true;
    } else if (!strcmp(v, "false") || !strcmp(v, "0") || !strcmp(v, "off")) {
        *out = false;
    } else {
        return false;
    }
    return true;
}

static const char *screen_name(todo_screen_t s)
{
    switch (s) {
    case TODO_SCREEN_IDLE: return "idle";
    case TODO_SCREEN_LIST: return "list";
    case TODO_SCREEN_CELEBRATE: return "celebrate";
    case TODO_SCREEN_ALL_DONE: return "all_done";
    }
    return "?";
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        buf[n] = '\0';
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static void check(bool ok, int line, const char *what, const char *got)
{
    if (!ok) {
        fprintf(stderr, "line %d: expected %s, got \"%s\"\n", line, what, got);
        g.failures++;
    }
}

static void join_path(char *out, size_t len, const char *dir, const char *name)
{
    if (name[0] == '/' || !dir || !dir[0]) {
        snprintf(out, len, "%s", name);
    } else {
        snprintf(out, len, "%s/%s", dir, name);
    }
}

/*
 * One `key=value` per line; `#` comments. Applied in order, so `advance`
 * and `screenshot` can capture each state along the way.
 */
static bool run_scenario(const char *path, const char *out_dir, bool real_time)
{
    char *src = read_file(path);
    if (!src) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return false;
    }
    char base[1024];
    snprintf(base, sizeof(base), "%s", path);
    char *slash = strrchr(base, '/');
    if (slash) {
        *slash = '\0';
    } else {
        snprintf(base, sizeof(base), ".");
    }

    int line_no = 0;
    bool ok = true;
    for (char *line = strtok(src, "\n"); line && ok && !g.quit; line = strtok(NULL, "\n")) {
        line_no++;
        while (isspace((unsigned char)*line)) {
            line++;
        }
        size_t n = strlen(line);
        while (n && isspace((unsigned char)line[n - 1])) {
            line[--n] = '\0';
        }
        if (!*line || *line == '#') {
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "%s:%d: expected key=value\n", path, line_no);
            ok = false;
            break;
        }
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        char err[128] = "";
        bool b;

        if (!strcmp(k, "clock")) {
            ok = set_clock(v);
        } else if (!strcmp(k, "wifi")) {
            todo_wifi_t w = !strcmp(v, "connected")    ? TODO_WIFI_CONNECTED
                          : !strcmp(v, "connecting")   ? TODO_WIFI_CONNECTING
                          : !strcmp(v, "none")         ? TODO_WIFI_NONE
                          : !strcmp(v, "unknown")      ? TODO_WIFI_UNKNOWN
                                                       : (todo_wifi_t)-1;
            ok = w != (todo_wifi_t)-1;
            if (ok) {
                todo_ui_set_wifi(w);
            }
        } else if (!strcmp(k, "online")) {
            ok = parse_bool(v, &b);
            g.online = b;
        } else if (!strcmp(k, "refuse_turns")) {
            ok = parse_bool(v, &b);
            g.refuse_turns = b;
        } else if (!strcmp(k, "list_date")) {
            snprintf(g.list_date_buf, sizeof(g.list_date_buf), "%s", v);
            g.list_date = g.list_date_buf[0] ? g.list_date_buf : NULL;
        } else if (!strcmp(k, "set_list") || !strcmp(k, "set_list_file")) {
            char *json = NULL;
            if (!strcmp(k, "set_list_file")) {
                char p[1024];
                join_path(p, sizeof(p), base, v);
                json = read_file(p);
                if (!json) {
                    fprintf(stderr, "%s:%d: cannot read %s\n", path, line_no, p);
                    ok = false;
                    break;
                }
            }
            if (!todo_model_set_list(&g.model, json ? json : v, g.list_date, g.now_ms, err, sizeof(err))) {
                fprintf(stderr, "%s:%d: set_list rejected: %s\n", path, line_no, err);
                ok = false;
            } else {
                char res[512];
                todo_model_list_result(&g.model, false, res, sizeof(res));
                printf("[%7u] todo.set_list -> %s\n", g.now_ms, res);
            }
            free(json);
        } else if (!strcmp(k, "set_ongoing") || !strcmp(k, "set_ongoing_file")) {
            char *json = NULL;
            if (!strcmp(k, "set_ongoing_file")) {
                char p[1024];
                join_path(p, sizeof(p), base, v);
                json = read_file(p);
                if (!json) {
                    fprintf(stderr, "%s:%d: cannot read %s\n", path, line_no, p);
                    ok = false;
                    break;
                }
            }
            if (!todo_model_set_ongoing(&g.model, json ? json : v, g.now_ms, err, sizeof(err))) {
                fprintf(stderr, "%s:%d: set_ongoing rejected: %s\n", path, line_no, err);
                ok = false;
            } else {
                char res[512];
                todo_model_list_result(&g.model, true, res, sizeof(res));
                printf("[%7u] todo.set_ongoing -> %s\n", g.now_ms, res);
            }
            free(json);
        } else if (!strcmp(k, "set_list_expect_error")) {
            check(!todo_model_set_list(&g.model, v, g.list_date, g.now_ms, err, sizeof(err)), line_no,
                  "set_list to be rejected", "accepted");
            printf("[%7u] todo.set_list rejected: %s\n", g.now_ms, err);
        } else if (!strcmp(k, "message")) {
            ok = todo_model_show_message(&g.model, v, g.now_ms, err, sizeof(err));
        } else if (!strcmp(k, "tap")) {
            tap(v, real_time);
        } else if (!strcmp(k, "type")) {
            ok = type_text(v, real_time);
        } else if (!strcmp(k, "key")) {
            /* A single key by label; names for the special ones. */
            const char *label = !strcmp(v, "backspace") ? LV_SYMBOL_BACKSPACE
                              : !strcmp(v, "ok")        ? LV_SYMBOL_OK
                              : !strcmp(v, "enter")     ? LV_SYMBOL_NEW_LINE
                              : !strcmp(v, "close")     ? LV_SYMBOL_KEYBOARD
                                                        : v;
            ok = todo_ui_type(label);
            run_for(30, real_time);
        } else if (!strcmp(k, "expect_sheet")) {
            lv_point_t pt;
            bool open = todo_ui_point_for("sheet_add", &pt);
            check(open == !strcmp(v, "open"), line_no, v, open ? "open" : "closed");
        } else if (!strcmp(k, "drag")) {
            int x1, y1, x2, y2;
            ok = sscanf(v, "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4;
            if (ok) {
                drag(x1, y1, x2, y2, real_time);
            }
        } else if (!strcmp(k, "expect_renders") || !strcmp(k, "expect_not_renders")) {
            /* Every character of the text has a glyph in the row font (with its fallback). */
            bool can = todo_font_can_render(todo_font(28), v);
            bool want = !strcmp(k, "expect_renders");
            check(can == want, line_no, want ? "renders" : "missing glyphs", can ? "renders" : "missing glyphs");
        } else if (!strcmp(k, "info")) {
            char info[200];
            todo_ui_scroll_info(info, sizeof(info));
            printf("[%7u] %s\n", g.now_ms, info);
            if (s_tv) {
                lv_area_t a;
                lv_obj_get_coords(s_root, &a);
                printf("          tv scroll=%d,%d face scroll=%d,%d root y=%d..%d\n",
                       (int)lv_obj_get_scroll_x(s_tv), (int)lv_obj_get_scroll_y(s_tv),
                       (int)lv_obj_get_scroll_x(s_face), (int)lv_obj_get_scroll_y(s_face), (int)a.y1, (int)a.y2);
            }
        } else if (!strcmp(k, "expect_visible")) {
            /* expect_visible=row:<id>: that row lies fully inside the list's visible area */
            check(todo_ui_fully_visible(v), line_no, v, "cut off or off screen");
        } else if (!strcmp(k, "expect_scrolled_max")) {
            /* expect_scrolled_max=<px>: the list is scrolled down no further than that */
            int32_t y = todo_ui_list_scroll_y();
            char got[32];
            snprintf(got, sizeof(got), "%d px", (int)y);
            check(y <= (int32_t)strtol(v, NULL, 10), line_no, v, got);
        } else if (!strcmp(k, "expect_scrolled")) {
            /* expect_scrolled=<min px>: the list has scrolled down at least that far */
            int32_t y = todo_ui_list_scroll_y();
            char got[32];
            snprintf(got, sizeof(got), "%d px", (int)y);
            check(y >= (int32_t)strtol(v, NULL, 10), line_no, v, got);
            printf("[%7u] list scrolled %s\n", g.now_ms, got);
        } else if (!strcmp(k, "tap_at")) {
            int x, y;
            ok = sscanf(v, "%d,%d", &x, &y) == 2;
            if (ok) {
                tap_point((lv_point_t){ x, y }, real_time);
            }
        } else if (!strcmp(k, "reply")) {
            muse_reply(v);
        } else if (!strcmp(k, "turn_error")) {
            muse_error();
        } else if (!strcmp(k, "auto_reply")) {
            snprintf(g.auto_reply, sizeof(g.auto_reply), "%s", strcmp(v, "off") ? v : "");
        } else if (!strcmp(k, "auto_reply_ms")) {
            g.auto_reply_ms = (uint32_t)strtoul(v, NULL, 10);
        } else if (!strcmp(k, "advance")) {
            run_for((uint32_t)strtoul(v, NULL, 10), real_time);
        } else if (!strcmp(k, "wait")) {
            wait_for((uint32_t)strtoul(v, NULL, 10));
        } else if (!strcmp(k, "redraw_reset")) {
            s_redraw_px = 0;
        } else if (!strcmp(k, "expect_redraw_max")) {
            char got[32];
            snprintf(got, sizeof(got), "%llu px", s_redraw_px);
            check(s_redraw_px <= strtoull(v, NULL, 10), line_no, v, got);
            printf("[%7u] redrawn since reset: %s\n", g.now_ms, got);
        } else if (!strcmp(k, "peek")) {
            todo_ui_peek(g.now_ms);
        } else if (!strcmp(k, "reboot")) {
            reboot();
        } else if (!strcmp(k, "screenshot")) {
            char p[1024];
            join_path(p, sizeof(p), out_dir, v);
            run_for(FRAME_STEP_MS, real_time);
            ok = screenshot(p);
        } else if (!strcmp(k, "expect_screen")) {
            const char *got = screen_name(todo_model_screen(&g.model));
            check(!strcmp(got, v), line_no, v, got);
        } else if (!strcmp(k, "expect_turn")) {
            check(strstr(g.last_turn, v) != NULL, line_no, v, g.last_turn);
        } else if (!strcmp(k, "expect_turns")) {
            char got[16];
            snprintf(got, sizeof(got), "%d", g.turns);
            check(!strcmp(got, v), line_no, v, got);
        } else if (!strcmp(k, "expect_message")) {
            check(!strcmp(g.model.message, v), line_no, v, g.model.message);
        } else if (!strcmp(k, "expect_state")) {
            /* expect_state=<id> <open|undo|queued|sending|auto_done|leaving|hidden|gone> */
            static const char *const NAMES[] = { "open", "undo", "queued", "sending",
                                                 "auto_done", "leaving", "hidden" };
            char id[TODO_ID_MAX + 1], want[16];
            if (sscanf(v, "%96s %15s", id, want) != 2) {
                ok = false;
                break;
            }
            const todo_item_t *it = todo_model_find(&g.model, id);
            const char *got = it ? NAMES[it->state] : "gone";
            check(!strcmp(got, want), line_no, want, got);
        } else {
            fprintf(stderr, "%s:%d: unknown key \"%s\"\n", path, line_no, k);
            ok = false;
        }
        if (!ok) {
            fprintf(stderr, "%s:%d: bad value for %s\n", path, line_no, k);
        }
    }
    free(src);
    return ok;
}

/* ---- interactive helpers ------------------------------------------------- */

static const char *const SAMPLE_LIST =
    "[{\"id\":\"rosary@2026-10-07\",\"label\":\"Rosary\",\"source\":\"manual\"},"
    "{\"id\":\"bible@2026-10-07\",\"label\":\"Bible in a Year podcast\",\"source\":\"manual\"},"
    "{\"id\":\"steps@2026-10-07\",\"label\":\"10,000 steps\",\"source\":\"healthkit\"},"
    "{\"id\":\"vit-am@2026-10-07\",\"label\":\"Vitamins AM\",\"source\":\"manual\"},"
    "{\"id\":\"cardio@2026-10-07\",\"label\":\"30 min cardio (140+ bpm)\",\"source\":\"healthkit\"},"
    "{\"id\":\"sun@2026-10-07\",\"label\":\"Sunlight\",\"source\":\"manual\"},"
    "{\"id\":\"pt@2026-10-07\",\"label\":\"PT exercises\",\"source\":\"manual\"},"
    "{\"id\":\"vit-pm@2026-10-07\",\"label\":\"Vitamins PM\",\"source\":\"manual\"},"
    "{\"id\":\"ups@2026-10-07\",\"label\":\"Return the library book and drop the shoe box at the UPS store\",\"source\":\"manual\"}]";

static const char *const MESSAGES[] = {
    "Good morning! Nine things today. You've got this.",
    "Don't forget to drink some water.",
    "Halfway there! Sunlight break?",
};

static int event_watch(void *userdata, SDL_Event *e)
{
    (void)userdata;
    if (e->type == SDL_QUIT) {
        g.quit = 1;
        return 0;
    }
    if (e->type != SDL_KEYDOWN) {
        return 0;
    }
    static int msg;
    char err[96];
    switch (e->key.keysym.sym) {
    case SDLK_l:
        todo_model_set_list(&g.model, SAMPLE_LIST, "Wednesday, October 7", g.now_ms, err, sizeof(err));
        break;
    case SDLK_o:
        g.online = !g.online;
        printf("online: %s\n", g.online ? "yes" : "no");
        break;
    case SDLK_r:
        muse_reply("Nice! Logged it.");
        break;
    case SDLK_e:
        muse_error();
        break;
    case SDLK_m:
        todo_model_show_message(&g.model, MESSAGES[msg++ % 3], g.now_ms, err, sizeof(err));
        break;
    case SDLK_k:
        todo_ui_peek(g.now_ms);
        break;
    case SDLK_w: {
        static int w = TODO_WIFI_CONNECTED;
        w = w == TODO_WIFI_CONNECTED ? TODO_WIFI_CONNECTING : w == TODO_WIFI_CONNECTING ? TODO_WIFI_NONE
                                                                                     : TODO_WIFI_CONNECTED;
        todo_ui_set_wifi((todo_wifi_t)w);
        break;
    }
    case SDLK_p:
        screenshot("todo-sim.png");
        break;
    case SDLK_ESCAPE:
        g.quit = 1;
        break;
    default:
        break;
    }
    return 0;
}

static void on_signal(int sig)
{
    (void)sig;
    g.quit = 1;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [--headless] [--portrait] [--sdk-layout] [--scenario FILE] [--out DIR] [--run-ms N]\n"
            "          [--screenshot FILE.png] [--state FILE]\n"
            "Keys: L load sample list, O toggle online, R Muse replies, E turn error,\n"
            "      M message, K peek, P screenshot, Esc quit. The mouse is your finger.\n",
            argv0);
}

int main(int argc, char **argv)
{
    bool headless = false;
    const char *scenario = NULL, *shot = NULL, *out_dir = ".";
    uint32_t run_ms = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) {
            headless = true;
        } else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) {
            scenario = argv[++i];
        } else if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            shot = argv[++i];
        } else if (!strcmp(argv[i], "--run-ms") && i + 1 < argc) {
            run_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--portrait")) {
            s_screen_w = 480;
            s_screen_h = 800;
        } else if (!strcmp(argv[i], "--sdk-layout")) {
            s_sdk_layout = true;
        } else if (!strcmp(argv[i], "--state") && i + 1 < argc) {
            g.state_path = argv[++i];
        } else {
            usage(argv[0]);
            return strcmp(argv[i], "--help") ? 2 : 0;
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (headless) {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
    }
    setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1); /* America/Chicago, like the device */
    tzset();
    signal(SIGINT, on_signal);

    g.now_ms = 1000;
    g.auto_reply_ms = 1500;
    g.online = true;
    /* Scripted runs get a fixed clock; interactive runs use the real one. */
    if (headless || scenario) {
        set_clock("2026-10-07 09:14");
    } else {
        g.wall_base = time(NULL);
        g.wall_base_ms = g.now_ms;
        snprintf(g.auto_reply, sizeof(g.auto_reply), "Nice! Logged it.");
    }

    lv_init();
    g.display = lv_sdl_window_create(SCREEN_W, SCREEN_H);
    if (!g.display) {
        fprintf(stderr, "could not open the simulator window\n");
        return 1;
    }
    /* After the window: LVGL's SDL driver installs its own real-time tick. */
    lv_tick_set_cb(tick_cb);
    lv_sdl_window_set_title(g.display, SCREEN_W > SCREEN_H ? "Muse Todo Display (800x480)"
                                                           : "Muse Todo Display (480x800 portrait)");
    lv_sdl_window_set_resizeable(g.display, false);
    lv_display_add_event_cb(g.display, invalidate_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_indev_t *mouse = lv_sdl_mouse_create();
    s_sdl_mouse_read = lv_indev_get_read_cb(mouse);
    lv_indev_set_read_cb(mouse, clamped_mouse_read);
    g.script_touch = lv_indev_create();
    lv_indev_set_type(g.script_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g.script_touch, script_touch_read);
    lv_indev_set_display(g.script_touch, g.display);

    todo_model_init(&g.model, &PORT);
    if (g.state_path) {
        char *blob = read_file(g.state_path);
        if (blob) {
            todo_model_restore(&g.model, blob, g.now_ms);
            free(blob);
        }
    }
    todo_ui_create(ui_parent(), &g.model);
    todo_ui_set_wifi(TODO_WIFI_CONNECTED); /* scenarios change it with wifi= */
    SDL_AddEventWatch(event_watch, NULL);

    bool ok = true;
    if (scenario) {
        ok = run_scenario(scenario, out_dir, !headless);
    }
    if (ok && run_ms) {
        run_for(run_ms, !headless);
    }
    if (ok && shot) {
        run_for(FRAME_STEP_MS, !headless);
        ok = screenshot(shot);
    }
    if (ok && !headless) {
        while (!g.quit) {
            uint32_t before = SDL_GetTicks();
            g.now_ms += FRAME_STEP_MS;
            frame();
            uint32_t spent = SDL_GetTicks() - before;
            if (spent < FRAME_STEP_MS) {
                SDL_Delay(FRAME_STEP_MS - spent);
            }
        }
    }
    SDL_DelEventWatch(event_watch, NULL);
    free(g.saved);
    if (g.failures) {
        fprintf(stderr, "%d expectation(s) failed\n", g.failures);
        return 1;
    }
    return ok ? 0 : 1;
}

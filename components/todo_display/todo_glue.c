/*
 * Device glue for the todo display (ESP-IDF only): hooks the model and screen
 * into the Muse SDK through muse_ext.h.
 *
 *   - todo.set_list / todo.show_message commands (docs/04-agent-integration.md)
 *   - the todo screen on the face tile once the device is paired; the SDK's
 *     own screens handle pairing and Wi-Fi setup
 *   - completions and task requests go out as typed chat turns
 *     (muse_hatch_text_turn); their "@chat" results come back through
 *     muse_ext's chat_event hook
 *   - state persisted in NVS; wall clock from SNTP, America/Chicago
 *
 * Locking: every model call holds the display lock (todo_model.h), which is
 * recursive, so the LVGL task (already holding it) can call in directly.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"

#include "muse_ble.h"
#include "muse_board.h"
#include "muse_chat.h"
#include "muse_chat_priv.h"
#include "muse_ext.h"
#include "muse_link.h"
#include "muse_settings.h"

#include "todo_model.h"
#include "todo_ui.h"

#if CONFIG_TODO_DISPLAY

static const char *TAG = "todo";

#define NVS_NS "todo"
#define NVS_STATE "state"
#define NVS_SETUP "setup" /* first-boot defaults applied */
#define FRAME_MS 40
#define TIMEZONE "CST6CDT,M3.2.0,M11.1.0" /* America/Chicago */

static EXT_RAM_BSS_ATTR todo_model_t s_model;
static lv_obj_t *s_root;
static bool s_visible;

/* The typed turn this device started, and its reply as it streams in. */
static bool s_turn_active;
static bool s_in_send;
static bool s_send_refused;
static char *s_reply;      /* finished messages, joined */
static char *s_message;    /* the message streaming now */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool lock(void)
{
    return muse_board->display_lock(-1);
}

static void unlock(void)
{
    muse_board->display_unlock();
}

/* ---- platform port for the model ------------------------------------------- */

static bool port_link_ready(void *ctx)
{
    (void)ctx;
    return muse_hatch_configured() && muse_hatch_ready();
}

static bool port_send_turn(void *ctx, const char *text)
{
    (void)ctx;
    char *copy = strdup(text);
    if (!copy) {
        return false;
    }
    /* A refusal ("MUSE NOT SET UP", "BUSY") is reported synchronously as an
     * "@chat error" from inside this call; chat_event notices it. */
    s_in_send = true;
    s_send_refused = false;
    s_turn_active = true;
    muse_hatch_text_turn(copy); /* takes and frees it */
    s_in_send = false;
    if (s_send_refused) {
        s_turn_active = false;
        return false;
    }
    free(s_reply);
    free(s_message);
    s_reply = s_message = NULL;
    ESP_LOGI(TAG, "sent to Muse:\n%s", text);
    return true;
}

static time_t port_wall_time(void *ctx)
{
    (void)ctx;
    time_t t = time(NULL);
    return t > 1700000000 ? t : 0; /* 0 until SNTP has set the clock */
}

static void port_save(void *ctx, const char *blob)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "can't open NVS to save");
        return;
    }
    esp_err_t err = nvs_set_blob(h, NVS_STATE, blob, strlen(blob) + 1);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(err));
    }
}

static const todo_port_t PORT = {
    .ctx = NULL,
    .link_ready = port_link_ready,
    .send_turn = port_send_turn,
    .wall_time = port_wall_time,
    .save = port_save,
};

static void restore(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = 0;
    if (nvs_get_blob(h, NVS_STATE, NULL, &len) == ESP_OK && len) {
        char *blob = malloc(len);
        if (blob && nvs_get_blob(h, NVS_STATE, blob, &len) == ESP_OK) {
            bool ok = todo_model_restore(&s_model, blob, now_ms());
            ESP_LOGI(TAG, "restored state: %s (%u items)", ok ? "ok" : "unreadable", (unsigned)s_model.count);
        }
        free(blob);
    }
    nvs_close(h);
}

/* A desk display stays on: turn auto-sleep off once, on the first boot of this
 * firmware. The owner can still set it in the SDK's settings afterwards. */
static void first_boot_defaults(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    uint8_t done = 0;
    if (nvs_get_u8(h, NVS_SETUP, &done) != ESP_OK || !done) {
        muse_settings_set_sleep_s(0);
        nvs_set_u8(h, NVS_SETUP, 1);
        nvs_commit(h);
        ESP_LOGI(TAG, "first boot: screen auto-sleep off");
    }
    nvs_close(h);
}

/* ---- chat turn results -------------------------------------------------------- */

static char *append(char *dst, const char *sep, const char *src)
{
    size_t a = dst ? strlen(dst) : 0, b = strlen(src), c = (a && sep) ? strlen(sep) : 0;
    char *out = realloc(dst, a + c + b + 1);
    if (!out) {
        return dst;
    }
    if (c) {
        memcpy(out + a, sep, c);
    }
    memcpy(out + a + c, src, b + 1);
    return out;
}

/* Runs on whichever task reports the turn; holds the display lock (recursive,
 * so a refusal reported from inside port_send_turn on the LVGL task is fine). */
static void chat_event_locked(const char *type, const char *text)
{
    if (!s_turn_active) {
        return; /* someone else's typed turn (tools/muse/chat.py) */
    }
    if (!strcmp(type, "error")) {
        if (s_in_send) {
            s_send_refused = true; /* refused before it started; the model retries */
            return;
        }
        ESP_LOGW(TAG, "turn failed: %s", text ? text : "?");
        s_turn_active = false;
        todo_model_turn_error(&s_model, now_ms());
        return;
    }
    if (!strcmp(type, "text") && text) {
        s_message = append(s_message, NULL, text);
    } else if (!strcmp(type, "final") && text) {
        free(s_message); /* the pieces didn't add up: use the whole text */
        s_message = strdup(text);
    } else if (!strcmp(type, "message_done") || !strcmp(type, "done")) {
        if (s_message) {
            s_reply = append(s_reply, " ", s_message);
            free(s_message);
            s_message = NULL;
        }
        if (!strcmp(type, "done")) {
            s_turn_active = false;
            ESP_LOGI(TAG, "Muse replied: %s", s_reply ? s_reply : "(nothing)");
            if (s_reply) {
                todo_model_turn_reply(&s_model, s_reply);
            }
            todo_model_turn_done(&s_model, now_ms());
            free(s_reply);
            s_reply = NULL;
        }
    }
}

static void chat_event(const char *type, const char *text)
{
    if (lock()) {
        chat_event_locked(type, text);
        unlock();
    }
}

/* ---- commands ----------------------------------------------------------------- */

static cJSON *string_param(const char *description)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description", description);
    return p;
}

static void add_command(cJSON *commands, const char *name, const char *description,
                        cJSON *required, cJSON *optional)
{
    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "description", description);
    cJSON_AddItemToObject(c, "required", required ? required : cJSON_CreateObject());
    cJSON_AddItemToObject(c, "optional", optional ? optional : cJSON_CreateObject());
    cJSON_AddItemToObject(commands, name, c);
}

static void register_commands(cJSON *commands)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddItemToObject(req, "items", string_param(
        "JSON array (as a string) of {id, label, source, done}. id is unique per occurrence "
        "(include the date for daily items); source is manual, healthkit, myfitnesspal, etc.; "
        "done is true for items already complete. Max 40 items, labels 120 chars."));
    cJSON *opt = cJSON_CreateObject();
    cJSON_AddItemToObject(opt, "date", string_param("The day the list is for, shown at the top."));
    add_command(commands, "todo.set_list",
                "Replace the owner's desk todo list. Send the whole list each morning and whenever "
                "anything changes. Returns how many are shown and the ids still being reported back.",
                req, opt);

    cJSON *mreq = cJSON_CreateObject();
    cJSON_AddItemToObject(mreq, "text", string_param("The message, at most 280 characters."));
    add_command(commands, "todo.show_message",
                "Show a short message on the owner's desk todo display: a reminder, encouragement "
                "or a note. Replaces any message already showing.",
                mreq, NULL);
}

static cJSON *result_ok(cJSON *payload)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    if (payload) {
        cJSON_AddItemToObject(r, "payload", payload);
    }
    return r;
}

static cJSON *result_error(const char *message)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "code", "invalid_params");
    cJSON_AddStringToObject(e, "message", message);
    cJSON_AddItemToObject(r, "error", e);
    return r;
}

static const char *param_str(cJSON *params, const char *key)
{
    cJSON *v = params ? cJSON_GetObjectItem(params, key) : NULL;
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static cJSON *command(const char *name, cJSON *params)
{
    char err[96] = "";
    if (!strcmp(name, "todo.set_list")) {
        const char *items = param_str(params, "items");
        const char *date = param_str(params, "date");
        if (!items) {
            return result_error("items is required (a JSON array, as a string)");
        }
        char payload[1024];
        bool ok = false;
        if (lock()) {
            ok = todo_model_set_list(&s_model, items, date, now_ms(), err, sizeof(err));
            if (ok && !todo_model_list_result(&s_model, payload, sizeof(payload))) {
                snprintf(payload, sizeof(payload), "{}");
            }
            unlock();
        }
        if (!ok) {
            ESP_LOGW(TAG, "todo.set_list rejected: %s", err);
            return result_error(err[0] ? err : "busy");
        }
        ESP_LOGI(TAG, "todo.set_list: %s", payload);
        return result_ok(cJSON_Parse(payload));
    }
    if (!strcmp(name, "todo.show_message")) {
        const char *text = param_str(params, "text");
        bool ok = false;
        if (lock()) {
            ok = todo_model_show_message(&s_model, text, now_ms(), err, sizeof(err));
            unlock();
        }
        if (!ok) {
            return result_error(err[0] ? err : "busy");
        }
        ESP_LOGI(TAG, "todo.show_message: %s", text);
        return result_ok(NULL);
    }
    return NULL;
}

/* ---- screen -------------------------------------------------------------------- */

/*
 * The SDK's own screens take over while pairing is under way (the app is
 * connected, or it's asking for a tap to confirm). Before that, the todo
 * screen shows how to pair.
 */
static bool want_visible(void)
{
    muse_link_state_t st = muse_link_state();
    return st != MUSE_LINK_PAIRING && st != MUSE_LINK_CONFIRM;
}

static void update_hint(void)
{
    static char hint[160];
    static int shown = -1;
    muse_link_state_t st = muse_link_state();
    if ((int)st == shown) {
        return;
    }
    shown = (int)st;
    if (st == MUSE_LINK_UNPAIRED) {
        muse_ble_status_t ble;
        muse_ble_status(&ble);
        snprintf(hint, sizeof(hint),
                 "Not paired yet: in the Muse app, Settings > Devices > Add Device, then pick %s.",
                 ble.name[0] ? ble.name : "this display");
    } else if (st == MUSE_LINK_CONNECTING || st == MUSE_LINK_OFFLINE || st == MUSE_LINK_ERROR) {
        snprintf(hint, sizeof(hint), "Connecting to Muse...");
    } else {
        hint[0] = '\0';
    }
    todo_ui_set_idle_hint(hint);
}

static void frame(lv_timer_t *t)
{
    (void)t;
    uint32_t now = now_ms();
    todo_model_tick(&s_model, now);
    bool visible = want_visible();
    if (visible != s_visible) {
        s_visible = visible;
        if (visible) {
            lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        }
        ESP_LOGI(TAG, "todo screen %s", visible ? "shown" : "hidden for pairing");
    }
    if (visible) {
        update_hint();
        todo_ui_update(now);
    }
}

static void ui_build(lv_obj_t *face)
{
    setenv("TZ", TIMEZONE, 1);
    tzset();
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    sntp.start = true;
    sntp.wait_for_sync = false;
    if (esp_netif_sntp_init(&sntp) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP didn't start: the clock stays unset");
    }

    todo_model_init(&s_model, &PORT);
    restore();
    first_boot_defaults();

    s_root = lv_obj_create(face);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    todo_ui_create(s_root, &s_model);
    s_visible = true;
    lv_timer_create(frame, FRAME_MS, NULL);
    ESP_LOGI(TAG, "todo screen built");
}

static bool ui_covers_face(void)
{
    return s_visible;
}

static const muse_ext_t EXT = {
    .ui_build = ui_build,
    .ui_covers_face = ui_covers_face,
    .register_commands = register_commands,
    .command = command,
    .chat_event = chat_event,
};

/* Registered before app_main, so it's in place before the UI starts. */
__attribute__((constructor)) static void todo_register(void)
{
    muse_ext_register(&EXT);
}

#endif /* CONFIG_TODO_DISPLAY */

/*
 * Todo display model: see todo_model.h.
 */
#include "todo_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

/* Retry delays after a failed turn (docs/04-agent-integration.md); the last one repeats. */
static const uint32_t RETRY_MS[] = { 10000u, 30000u, 120000u, 300000u };
#define RETRY_STAGES (sizeof(RETRY_MS) / sizeof(RETRY_MS[0]))

static const char *const COULDNT_REACH = "couldn't reach Muse";

/* ---- helpers ---------------------------------------------------------- */

static bool elapsed(uint32_t now, uint32_t since, uint32_t ms)
{
    return (uint32_t)(now - since) >= ms;
}

static void set_err(char *err, size_t len, const char *fmt, const char *arg)
{
    if (err && len) {
        snprintf(err, len, fmt, arg ? arg : "");
    }
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool has_control(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || *s == 0x7f) {
            return true;
        }
    }
    return false;
}

/* Labels are shown and sent on one line: turn control characters into spaces. */
static void flatten(char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || *s == 0x7f) {
            *s = ' ';
        }
    }
}

static time_t wall_now(const todo_model_t *m)
{
    return m->port.wall_time ? m->port.wall_time(m->port.ctx) : 0;
}

static bool is_outbox(const todo_item_t *it)
{
    return it->state == TODO_ROW_QUEUED || it->state == TODO_ROW_SENDING;
}

static bool is_open(const todo_item_t *it)
{
    return it->in_list && (it->state == TODO_ROW_OPEN || it->state == TODO_ROW_UNDO);
}

static int open_count(const todo_model_t *m)
{
    int n = 0;
    for (size_t i = 0; i < m->count; i++) {
        n += is_open(&m->items[i]);
    }
    return n;
}

static void set_state(todo_item_t *it, todo_row_state_t st, uint32_t now)
{
    it->state = st;
    it->state_ms = now;
}

static void start_celebration(todo_model_t *m, uint32_t now)
{
    m->celebrating = true;
    m->celebrate_ms = now;
}

uint32_t todo_message_hold_ms(const char *text)
{
    size_t chars = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        chars += (*p & 0xc0) != 0x80; /* count UTF-8 lead bytes only */
    }
    return chars < TODO_MESSAGE_SHORT_CHARS ? TODO_MESSAGE_SHORT_MS : TODO_MESSAGE_LONG_MS;
}

static void set_message(todo_model_t *m, const char *text, uint32_t now)
{
    copy_str(m->message, sizeof(m->message), text);
    flatten(m->message);
    m->message_ms = now;
    m->message_hold_ms = todo_message_hold_ms(m->message);
    m->message_seq++;
    m->seq++;
}

static void remove_at(todo_model_t *m, size_t i)
{
    memmove(&m->items[i], &m->items[i + 1], (m->count - i - 1) * sizeof(m->items[0]));
    m->count--;
}

const char *todo_source_label(const char *source)
{
    if (!source || !strcmp(source, "manual")) {
        return "";
    }
    if (!strcmp(source, "healthkit")) {
        return "HealthKit";
    }
    if (!strcmp(source, "myfitnesspal")) {
        return "MyFitnessPal";
    }
    return source;
}

/* ---- persistence -------------------------------------------------------- */

static const char *state_name(const todo_item_t *it)
{
    switch (it->state) {
    case TODO_ROW_QUEUED:
    case TODO_ROW_SENDING:
        return "queued";
    case TODO_ROW_AUTO_DONE:
    case TODO_ROW_LEAVING:
    case TODO_ROW_HIDDEN:
        return "done";
    case TODO_ROW_OPEN:
    case TODO_ROW_UNDO: /* nothing was sent: after a reboot it's simply open */
    default:
        return "open";
    }
}

static void save(todo_model_t *m)
{
    if (!m->port.save) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    if (!root || !items) {
        cJSON_Delete(root);
        cJSON_Delete(items);
        return;
    }
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddBoolToObject(root, "have_list", m->have_list);
    cJSON_AddStringToObject(root, "date", m->date);
    cJSON_AddNumberToObject(root, "first_fail", (double)m->first_fail_at);
    for (size_t i = 0; i < m->count; i++) {
        const todo_item_t *it = &m->items[i];
        const char *st = state_name(it);
        if (!it->in_list && strcmp(st, "queued")) {
            continue; /* finished and no longer on Muse's list */
        }
        cJSON *o = cJSON_CreateObject();
        if (!o) {
            continue;
        }
        cJSON_AddStringToObject(o, "id", it->id);
        cJSON_AddStringToObject(o, "label", it->label);
        cJSON_AddStringToObject(o, "source", it->source);
        cJSON_AddBoolToObject(o, "in_list", it->in_list);
        cJSON_AddBoolToObject(o, "done", it->muse_done);
        cJSON_AddStringToObject(o, "st", st);
        cJSON_AddNumberToObject(o, "at", (double)it->completed_at);
        cJSON_AddItemToArray(items, o);
    }
    cJSON_AddItemToObject(root, "items", items);
    char *blob = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (blob) {
        m->port.save(m->port.ctx, blob);
        cJSON_free(blob);
    }
}

static const char *get_str(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static bool get_bool(const cJSON *o, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, key));
}

static double get_num(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}

bool todo_model_restore(todo_model_t *m, const char *blob, uint32_t now_ms)
{
    todo_port_t port = m->port;
    todo_model_init(m, &port);
    cJSON *root = blob ? cJSON_Parse(blob) : NULL;
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsObject(root) || get_num(root, "v") != 1 || !cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return false;
    }
    m->have_list = get_bool(root, "have_list");
    const char *date = get_str(root, "date");
    copy_str(m->date, sizeof(m->date), date ? date : "");
    m->first_fail_at = (time_t)get_num(root, "first_fail");

    const cJSON *o;
    cJSON_ArrayForEach(o, items) {
        const char *id = get_str(o, "id");
        const char *label = get_str(o, "label");
        const char *source = get_str(o, "source");
        const char *st = get_str(o, "st");
        if (!id || !label || !source || !st || m->count >= TODO_MAX_ITEMS) {
            continue;
        }
        todo_item_t *it = &m->items[m->count++];
        memset(it, 0, sizeof(*it));
        copy_str(it->id, sizeof(it->id), id);
        copy_str(it->label, sizeof(it->label), label);
        copy_str(it->source, sizeof(it->source), source);
        it->manual = !strcmp(it->source, "manual");
        it->in_list = get_bool(o, "in_list");
        it->muse_done = get_bool(o, "done");
        it->completed_at = (time_t)get_num(o, "at");
        if (!strcmp(st, "queued")) {
            set_state(it, TODO_ROW_QUEUED, now_ms);
        } else if (!strcmp(st, "done")) {
            set_state(it, TODO_ROW_HIDDEN, now_ms);
        } else {
            set_state(it, TODO_ROW_OPEN, now_ms);
        }
    }
    cJSON_Delete(root);
    m->seq++;
    return true;
}

/* ---- commands from Muse ------------------------------------------------ */

void todo_model_init(todo_model_t *m, const todo_port_t *port)
{
    memset(m, 0, sizeof(*m));
    if (port) {
        m->port = *port;
    }
}

const todo_item_t *todo_model_find(const todo_model_t *m, const char *id)
{
    for (size_t i = 0; i < m->count; i++) {
        if (!strcmp(m->items[i].id, id)) {
            return &m->items[i];
        }
    }
    return NULL;
}

static todo_item_t *find_mut(todo_model_t *m, const char *id)
{
    return (todo_item_t *)todo_model_find(m, id);
}

static bool check_str(const cJSON *o, const char *key, size_t max, bool allow_control,
                      const char **out, char *err, size_t err_len)
{
    const char *s = get_str(o, key);
    if (!s || !*s) {
        set_err(err, err_len, "item missing \"%s\"", key);
        return false;
    }
    if (strlen(s) > max) {
        set_err(err, err_len, "item \"%s\" too long", key);
        return false;
    }
    if (!allow_control && has_control(s)) {
        set_err(err, err_len, "item \"%s\" has control characters", key);
        return false;
    }
    *out = s;
    return true;
}

bool todo_model_set_list(todo_model_t *m, const char *items_json, const char *date,
                         uint32_t now_ms, char *err, size_t err_len)
{
    if (!items_json) {
        set_err(err, err_len, "%s", "items is required");
        return false;
    }
    if (date && strlen(date) > TODO_DATE_MAX) {
        set_err(err, err_len, "%s", "date too long");
        return false;
    }
    cJSON *arr = cJSON_Parse(items_json);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        set_err(err, err_len, "%s", "items must be a JSON array");
        return false;
    }
    int n = cJSON_GetArraySize(arr);
    if (n > TODO_MAX_LIST_ITEMS) {
        cJSON_Delete(arr);
        set_err(err, err_len, "%s", "too many items (max 40)");
        return false;
    }

    /* Build into a scratch array so a bad item leaves the current list alone. */
    static todo_item_t next[TODO_MAX_ITEMS];
    size_t count = 0;
    bool ok = true;
    bool completed_by_muse = false;
    const cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        const char *id, *label, *source;
        if (!cJSON_IsObject(o)) {
            set_err(err, err_len, "%s", "each item must be an object");
            ok = false;
            break;
        }
        if (!check_str(o, "id", TODO_ID_MAX, false, &id, err, err_len)
            || !check_str(o, "label", TODO_LABEL_MAX, true, &label, err, err_len)
            || !check_str(o, "source", TODO_SOURCE_MAX, false, &source, err, err_len)) {
            ok = false;
            break;
        }
        const cJSON *done = cJSON_GetObjectItemCaseSensitive(o, "done");
        if (done && !cJSON_IsBool(done)) {
            set_err(err, err_len, "%s", "item \"done\" must be true or false");
            ok = false;
            break;
        }
        for (size_t j = 0; j < count; j++) {
            if (!strcmp(next[j].id, id)) {
                set_err(err, err_len, "duplicate id %s", id);
                ok = false;
                break;
            }
        }
        if (!ok) {
            break;
        }

        todo_item_t *it = &next[count++];
        const todo_item_t *old = todo_model_find(m, id);
        bool muse_done = cJSON_IsTrue(done);
        if (old) {
            *it = *old;
        } else {
            memset(it, 0, sizeof(*it));
            copy_str(it->id, sizeof(it->id), id);
            set_state(it, muse_done ? TODO_ROW_HIDDEN : TODO_ROW_OPEN, now_ms);
        }
        copy_str(it->label, sizeof(it->label), label);
        flatten(it->label);
        copy_str(it->source, sizeof(it->source), source);
        it->manual = !strcmp(source, "manual");
        it->in_list = true;
        it->muse_done = muse_done;

        if (old) {
            switch (old->state) {
            case TODO_ROW_UNDO:
            case TODO_ROW_QUEUED:
            case TODO_ROW_SENDING:
                break; /* local state wins until Muse has replied */
            case TODO_ROW_OPEN:
                if (muse_done) {
                    set_state(it, TODO_ROW_AUTO_DONE, now_ms);
                    completed_by_muse = true;
                } else if (!old->in_list) {
                    it->state_ms = now_ms;
                }
                break;
            case TODO_ROW_AUTO_DONE:
            case TODO_ROW_LEAVING:
            case TODO_ROW_HIDDEN:
                if (!muse_done) {
                    set_state(it, TODO_ROW_OPEN, now_ms); /* Muse reopened it */
                }
                break;
            }
            if (!it->manual && it->state == TODO_ROW_UNDO) {
                set_state(it, TODO_ROW_OPEN, now_ms); /* became display-only */
            }
        }
    }
    cJSON_Delete(arr);
    if (!ok) {
        return false;
    }

    /* Completions Muse no longer lists still go out. */
    for (size_t i = 0; i < m->count; i++) {
        const todo_item_t *old = &m->items[i];
        bool listed = false;
        for (size_t j = 0; j < count && !listed; j++) {
            listed = !strcmp(next[j].id, old->id);
        }
        if (listed || !is_outbox(old)) {
            continue;
        }
        if (count >= TODO_MAX_ITEMS) {
            set_err(err, err_len, "%s", "too many items waiting to send");
            return false;
        }
        next[count] = *old;
        next[count].in_list = false;
        count++;
    }

    int open_before = m->have_list ? open_count(m) : 0;
    memcpy(m->items, next, count * sizeof(next[0]));
    m->count = count;
    m->have_list = true;
    copy_str(m->date, sizeof(m->date), date ? date : "");
    if (open_before > 0 && completed_by_muse && open_count(m) == 0) {
        start_celebration(m, now_ms);
    }
    m->seq++;
    save(m);
    return true;
}

bool todo_model_show_message(todo_model_t *m, const char *text, uint32_t now_ms,
                             char *err, size_t err_len)
{
    if (!text || !*text) {
        set_err(err, err_len, "%s", "text is required");
        return false;
    }
    if (strlen(text) > TODO_MESSAGE_MAX) {
        set_err(err, err_len, "%s", "text too long (max 280)");
        return false;
    }
    set_message(m, text, now_ms);
    return true;
}

/* ---- touch -------------------------------------------------------------- */

bool todo_model_tap(todo_model_t *m, const char *id, uint32_t now_ms)
{
    todo_item_t *it = find_mut(m, id);
    if (!it || !it->manual || it->state != TODO_ROW_OPEN) {
        return false;
    }
    if (it->tap_ms && !elapsed(now_ms, it->tap_ms, TODO_TAP_DEBOUNCE_MS)) {
        return false;
    }
    it->tap_ms = now_ms ? now_ms : 1;
    it->completed_at = wall_now(m);
    set_state(it, TODO_ROW_UNDO, now_ms);
    m->seq++;
    return true;
}

bool todo_model_undo(todo_model_t *m, const char *id, uint32_t now_ms)
{
    todo_item_t *it = find_mut(m, id);
    if (!it || it->state != TODO_ROW_UNDO) {
        return false;
    }
    it->completed_at = 0;
    set_state(it, TODO_ROW_OPEN, now_ms);
    m->seq++;
    return true;
}

void todo_model_clear_message(todo_model_t *m)
{
    if (m->message[0]) {
        m->message[0] = '\0';
        m->message_seq++;
        m->seq++;
    }
}

/* ---- outbox ------------------------------------------------------------- */

static void format_clock(time_t t, char *out, size_t len)
{
    struct tm tm;
    if (!t || !localtime_r(&t, &tm)) {
        out[0] = '\0';
        return;
    }
    int h = tm.tm_hour % 12;
    snprintf(out, len, " at %d:%02d %s", h ? h : 12, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}

/* One line per completion, ID included so Muse can ignore repeats. */
static char *build_turn(const todo_model_t *m)
{
    size_t cap = 1;
    for (size_t i = 0; i < m->count; i++) {
        if (m->items[i].state == TODO_ROW_QUEUED) {
            cap += 64 + strlen(m->items[i].label) + strlen(m->items[i].id);
        }
    }
    char *text = malloc(cap);
    if (!text) {
        return NULL;
    }
    size_t used = 0;
    text[0] = '\0';
    for (size_t i = 0; i < m->count; i++) {
        const todo_item_t *it = &m->items[i];
        if (it->state != TODO_ROW_QUEUED) {
            continue;
        }
        char when[24];
        format_clock(it->completed_at, when, sizeof(when));
        used += (size_t)snprintf(text + used, cap - used, "%s[todo-display] Completed: %s (id %s)%s.",
                                 used ? "\n" : "", it->label, it->id, when);
    }
    return text;
}

static void give_up(todo_model_t *m, uint32_t now)
{
    for (size_t i = 0; i < m->count; i++) {
        todo_item_t *it = &m->items[i];
        if (is_outbox(it)) {
            set_state(it, TODO_ROW_LEAVING, now);
        }
    }
    m->retry_stage = 0;
    m->first_fail_at = 0;
    set_message(m, COULDNT_REACH, now);
}

void todo_model_turn_reply(todo_model_t *m, const char *chunk)
{
    if (!m->in_flight || !chunk) {
        return;
    }
    size_t used = strlen(m->reply);
    copy_str(m->reply + used, sizeof(m->reply) - used, chunk);
}

void todo_model_turn_done(todo_model_t *m, uint32_t now_ms)
{
    if (!m->in_flight) {
        return;
    }
    m->in_flight = false;
    for (size_t i = 0; i < m->count; i++) {
        todo_item_t *it = &m->items[i];
        if (it->state == TODO_ROW_SENDING) {
            it->muse_done = true;
            set_state(it, TODO_ROW_LEAVING, now_ms);
        }
    }
    if (m->reply[0]) {
        set_message(m, m->reply, now_ms);
    }
    m->reply[0] = '\0';
    m->retry_stage = 0;
    m->first_fail_at = 0;
    m->next_send_ms = now_ms;
    m->seq++;
    save(m);
}

void todo_model_turn_error(todo_model_t *m, uint32_t now_ms)
{
    if (!m->in_flight) {
        return;
    }
    m->in_flight = false;
    m->reply[0] = '\0';
    for (size_t i = 0; i < m->count; i++) {
        if (m->items[i].state == TODO_ROW_SENDING) {
            set_state(&m->items[i], TODO_ROW_QUEUED, now_ms);
        }
    }
    time_t wall = wall_now(m);
    if (!m->first_fail_at) {
        m->first_fail_at = wall;
    }
    if (wall && m->first_fail_at && wall - m->first_fail_at >= TODO_GIVE_UP_S) {
        give_up(m, now_ms);
    } else {
        unsigned stage = m->retry_stage < RETRY_STAGES ? m->retry_stage : RETRY_STAGES - 1;
        m->next_send_ms = now_ms + RETRY_MS[stage];
        if (m->retry_stage < RETRY_STAGES) {
            m->retry_stage++;
        }
    }
    m->seq++;
    save(m);
}

static void try_send(todo_model_t *m, uint32_t now)
{
    if (m->in_flight || !m->online || !m->port.send_turn
        || (int32_t)(now - m->next_send_ms) < 0) {
        return;
    }
    bool any = false;
    for (size_t i = 0; i < m->count && !any; i++) {
        any = m->items[i].state == TODO_ROW_QUEUED;
    }
    if (!any) {
        return;
    }
    char *text = build_turn(m);
    if (!text) {
        return;
    }
    bool sent = m->port.send_turn(m->port.ctx, text);
    free(text);
    if (!sent) {
        m->next_send_ms = now + TODO_REFUSED_RETRY_MS; /* busy or not set up: not a failure */
        return;
    }
    m->in_flight = true;
    m->in_flight_ms = now;
    m->reply[0] = '\0';
    for (size_t i = 0; i < m->count; i++) {
        if (m->items[i].state == TODO_ROW_QUEUED) {
            set_state(&m->items[i], TODO_ROW_SENDING, now);
        }
    }
    m->seq++;
}

/* ---- timers -------------------------------------------------------------- */

void todo_model_tick(todo_model_t *m, uint32_t now_ms)
{
    bool committed = false;
    bool online = m->port.link_ready && m->port.link_ready(m->port.ctx);
    if (online != m->online) {
        m->online = online;
        m->seq++;
    }

    for (size_t i = 0; i < m->count;) {
        todo_item_t *it = &m->items[i];
        switch (it->state) {
        case TODO_ROW_UNDO:
            if (elapsed(now_ms, it->state_ms, TODO_UNDO_MS)) {
                set_state(it, TODO_ROW_QUEUED, now_ms);
                committed = true;
                m->seq++;
            }
            break;
        case TODO_ROW_AUTO_DONE:
            if (elapsed(now_ms, it->state_ms, TODO_AUTO_DONE_SHOW_MS)) {
                set_state(it, TODO_ROW_LEAVING, now_ms);
                m->seq++;
            }
            break;
        case TODO_ROW_LEAVING:
            if (elapsed(now_ms, it->state_ms, TODO_LEAVE_MS)) {
                m->seq++;
                if (!it->in_list) {
                    remove_at(m, i);
                    continue;
                }
                set_state(it, TODO_ROW_HIDDEN, now_ms);
            }
            break;
        default:
            break;
        }
        i++;
    }
    if (committed) {
        if (m->have_list && open_count(m) == 0) {
            start_celebration(m, now_ms);
        }
        save(m);
    }

    if (m->celebrating && elapsed(now_ms, m->celebrate_ms, TODO_CELEBRATE_MS)) {
        m->celebrating = false;
        m->seq++;
    }
    if (m->message[0] && elapsed(now_ms, m->message_ms, m->message_hold_ms)) {
        todo_model_clear_message(m);
    }
    if (m->in_flight && elapsed(now_ms, m->in_flight_ms, TODO_TURN_TIMEOUT_MS)) {
        todo_model_turn_error(m, now_ms);
    }
    try_send(m, now_ms);
}

/* ---- queries ------------------------------------------------------------- */

bool todo_model_item_visible(const todo_item_t *it)
{
    return it->state != TODO_ROW_HIDDEN;
}

int todo_model_outbox_count(const todo_model_t *m)
{
    int n = 0;
    for (size_t i = 0; i < m->count; i++) {
        n += is_outbox(&m->items[i]);
    }
    return n;
}

todo_screen_t todo_model_screen(const todo_model_t *m)
{
    if (m->celebrating) {
        return TODO_SCREEN_CELEBRATE;
    }
    if (!m->have_list) {
        return todo_model_outbox_count(m) ? TODO_SCREEN_LIST : TODO_SCREEN_IDLE;
    }
    return open_count(m) ? TODO_SCREEN_LIST : TODO_SCREEN_ALL_DONE;
}

void todo_model_progress(const todo_model_t *m, int *done, int *total)
{
    int d = 0, t = 0;
    for (size_t i = 0; i < m->count; i++) {
        const todo_item_t *it = &m->items[i];
        if (!it->in_list) {
            continue;
        }
        t++;
        d += it->state != TODO_ROW_OPEN;
    }
    *done = d;
    *total = t;
}

bool todo_model_list_result(const todo_model_t *m, char *out, size_t len)
{
    int shown = 0;
    for (size_t i = 0; i < m->count; i++) {
        shown += m->items[i].in_list && todo_model_item_visible(&m->items[i]);
    }
    size_t used = (size_t)snprintf(out, len, "{\"shown\":%d,\"pending\":[", shown);
    bool first = true;
    for (size_t i = 0; i < m->count && used < len; i++) {
        if (!is_outbox(&m->items[i])) {
            continue;
        }
        char *esc = NULL;
        cJSON *s = cJSON_CreateString(m->items[i].id);
        if (s) {
            esc = cJSON_PrintUnformatted(s);
            cJSON_Delete(s);
        }
        if (!esc) {
            return false;
        }
        used += (size_t)snprintf(out + used, len - used, "%s%s", first ? "" : ",", esc);
        cJSON_free(esc);
        first = false;
    }
    if (used < len) {
        used += (size_t)snprintf(out + used, len - used, "]}");
    }
    return used < len;
}

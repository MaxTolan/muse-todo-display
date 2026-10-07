/*
 * Host unit tests for the todo model (components/todo_display/todo_model.c).
 * Run through ctest from the sim/ build: `ctest --test-dir build/sim`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "todo_model.h"

static int s_failures;
static const char *s_test;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, s_test, #cond); \
            s_failures++;                                                              \
        }                                                                              \
    } while (0)

#define CHECK_STR(a, b)                                                                     \
    do {                                                                                    \
        if (strcmp((a), (b))) {                                                             \
            fprintf(stderr, "FAIL %s:%d [%s] \"%s\" != \"%s\"\n", __FILE__, __LINE__, s_test, \
                    (a), (b));                                                              \
            s_failures++;                                                                   \
        }                                                                                   \
    } while (0)

/* ---- fake platform ------------------------------------------------------ */

typedef struct {
    bool online;
    bool refuse;
    time_t wall;
    int sends;
    char last_turn[4096];
    char saved[16384];
    int saves;
} fake_t;

static bool fake_link(void *ctx) { return ((fake_t *)ctx)->online; }
static time_t fake_wall(void *ctx) { return ((fake_t *)ctx)->wall; }

static bool fake_send(void *ctx, const char *text)
{
    fake_t *f = ctx;
    if (f->refuse) {
        return false;
    }
    f->sends++;
    snprintf(f->last_turn, sizeof(f->last_turn), "%s", text);
    return true;
}

static void fake_save(void *ctx, const char *blob)
{
    fake_t *f = ctx;
    f->saves++;
    snprintf(f->saved, sizeof(f->saved), "%s", blob);
}

static fake_t F;
static todo_model_t M;
static uint32_t T;

/* 2026-10-07 09:14:00 America/Chicago (CDT, UTC-5) */
#define WALL_0914 ((time_t)1791382440)

static void setup(const char *name)
{
    s_test = name;
    memset(&F, 0, sizeof(F));
    F.online = true;
    F.wall = WALL_0914;
    todo_port_t port = { &F, fake_link, fake_send, fake_wall, fake_save };
    todo_model_init(&M, &port);
    T = 1000;
}

static void run(uint32_t ms)
{
    /* Tick like a 25 fps UI so timers see realistic steps. */
    uint32_t end = T + ms;
    while (T < end) {
        uint32_t step = end - T < 40 ? end - T : 40;
        T += step;
        todo_model_tick(&M, T);
    }
}

static const char *DAY =
    "[{\"id\":\"rosary@1007\",\"label\":\"Rosary\",\"source\":\"manual\",\"done\":false},"
    "{\"id\":\"vit-am@1007\",\"label\":\"Vitamins AM\",\"source\":\"manual\"},"
    "{\"id\":\"steps@1007\",\"label\":\"10,000 steps\",\"source\":\"healthkit\",\"done\":false}]";

static bool push(const char *json)
{
    char err[96] = "";
    bool ok = todo_model_set_list(&M, json, "Wednesday, October 7", T, err, sizeof(err));
    if (!ok) {
        fprintf(stderr, "  (set_list: %s)\n", err);
    }
    return ok;
}

static todo_row_state_t state_of(const char *id)
{
    const todo_item_t *it = todo_model_find(&M, id);
    return it ? it->state : (todo_row_state_t)-1;
}

/* ---- tests -------------------------------------------------------------- */

static void test_idle_until_list(void)
{
    setup("idle_until_list");
    CHECK(todo_model_screen(&M) == TODO_SCREEN_IDLE);
    run(10 * 60 * 1000);
    CHECK(todo_model_screen(&M) == TODO_SCREEN_IDLE);
    CHECK(push(DAY));
    CHECK(todo_model_screen(&M) == TODO_SCREEN_LIST);
    int done, total;
    todo_model_progress(&M, &done, &total);
    CHECK(done == 0 && total == 3);
    CHECK_STR(M.date, "Wednesday, October 7");
}

static void test_validation(void)
{
    setup("validation");
    char err[96];
    CHECK(!todo_model_set_list(&M, "{\"id\":1}", NULL, T, err, sizeof(err)));
    CHECK(!todo_model_set_list(&M, "not json", NULL, T, err, sizeof(err)));
    CHECK(!todo_model_set_list(&M, "[{\"label\":\"x\",\"source\":\"manual\"}]", NULL, T, err, sizeof(err)));
    CHECK(strstr(err, "id") != NULL);
    CHECK(!todo_model_set_list(&M, "[{\"id\":\"a\",\"label\":\"x\"}]", NULL, T, err, sizeof(err)));
    CHECK(!todo_model_set_list(&M, "[{\"id\":\"a\",\"label\":\"x\",\"source\":\"manual\",\"done\":\"yes\"}]",
                               NULL, T, err, sizeof(err)));
    CHECK(!todo_model_set_list(&M,
                               "[{\"id\":\"a\",\"label\":\"x\",\"source\":\"manual\"},"
                               "{\"id\":\"a\",\"label\":\"y\",\"source\":\"manual\"}]",
                               NULL, T, err, sizeof(err)));
    CHECK(strstr(err, "duplicate") != NULL);
    CHECK(!todo_model_set_list(&M, "[{\"id\":\"a\\nb\",\"label\":\"x\",\"source\":\"manual\"}]", NULL, T,
                               err, sizeof(err)));

    char big[200];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    char json[512];
    snprintf(json, sizeof(json), "[{\"id\":\"a\",\"label\":\"%s\",\"source\":\"manual\"}]", big);
    CHECK(!todo_model_set_list(&M, json, NULL, T, err, sizeof(err)));

    char many[8192] = "[";
    for (int i = 0; i < 41; i++) {
        char one[96];
        snprintf(one, sizeof(one), "%s{\"id\":\"i%d\",\"label\":\"L\",\"source\":\"manual\"}", i ? "," : "", i);
        strcat(many, one);
    }
    strcat(many, "]");
    CHECK(!todo_model_set_list(&M, many, NULL, T, err, sizeof(err)));

    /* Nothing above replaced anything. */
    CHECK(!M.have_list && M.count == 0);

    /* A bad push leaves the current list alone. */
    CHECK(push(DAY));
    CHECK(!todo_model_set_list(&M, "[1]", NULL, T, err, sizeof(err)));
    CHECK(M.count == 3);

    /* Newlines in labels are flattened, not rejected. */
    CHECK(push("[{\"id\":\"a\",\"label\":\"two\\nlines\",\"source\":\"manual\"}]"));
    CHECK_STR(todo_model_find(&M, "a")->label, "two lines");

    char err2[32];
    CHECK(!todo_model_show_message(&M, "", T, err2, sizeof(err2)));
    char longmsg[300];
    memset(longmsg, 'm', 299);
    longmsg[299] = '\0';
    CHECK(!todo_model_show_message(&M, longmsg, T, err2, sizeof(err2)));
}

static void test_tap_undo(void)
{
    setup("tap_undo");
    push(DAY);
    CHECK(todo_model_tap(&M, "rosary@1007", T));
    CHECK(state_of("rosary@1007") == TODO_ROW_UNDO);
    run(2000);
    CHECK(todo_model_undo(&M, "rosary@1007", T));
    CHECK(state_of("rosary@1007") == TODO_ROW_OPEN);
    run(10000);
    CHECK(state_of("rosary@1007") == TODO_ROW_OPEN);
    CHECK(F.sends == 0); /* undo never sends anything */

    /* Auto items aren't tappable. */
    CHECK(!todo_model_tap(&M, "steps@1007", T));
    CHECK(!todo_model_tap(&M, "nope", T));
}

static void test_debounce(void)
{
    setup("debounce");
    push(DAY);
    CHECK(todo_model_tap(&M, "rosary@1007", T));
    run(100);
    todo_model_undo(&M, "rosary@1007", T);
    run(100);
    CHECK(!todo_model_tap(&M, "rosary@1007", T)); /* 200 ms after the first tap */
    run(150);
    CHECK(todo_model_tap(&M, "rosary@1007", T));
}

static void test_commit_send_reply(void)
{
    setup("commit_send_reply");
    push(DAY);
    todo_model_tap(&M, "vit-am@1007", T);
    run(TODO_UNDO_MS - 100);
    CHECK(state_of("vit-am@1007") == TODO_ROW_UNDO);
    CHECK(F.sends == 0);
    run(200);
    CHECK(state_of("vit-am@1007") == TODO_ROW_SENDING);
    CHECK(F.sends == 1);
    CHECK_STR(F.last_turn, "[todo-display] Completed: Vitamins AM (id vit-am@1007) at 9:14 AM.");
    CHECK(todo_model_outbox_count(&M) == 1);
    CHECK(strstr(F.saved, "\"st\":\"queued\"") != NULL); /* persisted before sending */

    todo_model_turn_reply(&M, "Nice, ");
    todo_model_turn_reply(&M, "vitamins logged!");
    todo_model_turn_done(&M, T);
    CHECK_STR(M.message, "Nice, vitamins logged!");
    CHECK(state_of("vit-am@1007") == TODO_ROW_LEAVING);
    run(TODO_LEAVE_MS + 40);
    CHECK(state_of("vit-am@1007") == TODO_ROW_HIDDEN);
    CHECK(todo_model_outbox_count(&M) == 0);
    int done, total;
    todo_model_progress(&M, &done, &total);
    CHECK(done == 1 && total == 3);
    CHECK(F.sends == 1);
}

static void test_batching(void)
{
    setup("batching");
    F.online = false;
    push(DAY);
    todo_model_tap(&M, "rosary@1007", T);
    todo_model_tap(&M, "vit-am@1007", T);
    run(TODO_UNDO_MS + 100);
    CHECK(state_of("rosary@1007") == TODO_ROW_QUEUED);
    CHECK(state_of("vit-am@1007") == TODO_ROW_QUEUED);
    run(60000);
    CHECK(F.sends == 0); /* held while offline */
    F.online = true;
    run(40);
    CHECK(F.sends == 1);
    CHECK_STR(F.last_turn,
              "[todo-display] Completed: Rosary (id rosary@1007) at 9:14 AM.\n"
              "[todo-display] Completed: Vitamins AM (id vit-am@1007) at 9:14 AM.");
}

static void test_refused_retry(void)
{
    setup("refused_retry");
    F.refuse = true;
    push(DAY);
    todo_model_tap(&M, "rosary@1007", T);
    run(TODO_UNDO_MS + 100);
    CHECK(state_of("rosary@1007") == TODO_ROW_QUEUED);
    F.refuse = false;
    run(TODO_REFUSED_RETRY_MS + 100);
    CHECK(F.sends == 1);
    CHECK(state_of("rosary@1007") == TODO_ROW_SENDING);
}

static void test_error_backoff(void)
{
    setup("error_backoff");
    push(DAY);
    todo_model_tap(&M, "rosary@1007", T);
    run(TODO_UNDO_MS + 100);
    CHECK(F.sends == 1);
    todo_model_turn_error(&M, T);
    CHECK(state_of("rosary@1007") == TODO_ROW_QUEUED);
    run(9000);
    CHECK(F.sends == 1);
    run(1100);
    CHECK(F.sends == 2); /* 10 s */
    todo_model_turn_error(&M, T);
    run(29000);
    CHECK(F.sends == 2);
    run(1100);
    CHECK(F.sends == 3); /* 30 s */

    /* No DONE within the timeout counts as an error. */
    run(TODO_TURN_TIMEOUT_MS + 100);
    CHECK(state_of("rosary@1007") == TODO_ROW_QUEUED);
    run(120000);
    CHECK(F.sends == 4); /* 2 min */
}

static void test_give_up(void)
{
    setup("give_up");
    push(DAY);
    todo_model_tap(&M, "rosary@1007", T);
    run(TODO_UNDO_MS + 100);
    todo_model_turn_error(&M, T);
    F.wall += TODO_GIVE_UP_S;
    run(10100);
    CHECK(F.sends == 2);
    todo_model_turn_error(&M, T);
    CHECK_STR(M.message, "couldn't reach Muse");
    CHECK(state_of("rosary@1007") == TODO_ROW_LEAVING);
    run(1000);
    CHECK(todo_model_outbox_count(&M) == 0);
}

static void test_events_without_turn_ignored(void)
{
    setup("events_without_turn");
    push(DAY);
    todo_model_turn_reply(&M, "stray");
    todo_model_turn_done(&M, T);
    todo_model_turn_error(&M, T);
    CHECK(M.message[0] == '\0');
    CHECK(M.retry_stage == 0);
}

static void test_push_keeps_local_state(void)
{
    setup("push_keeps_local_state");
    F.online = false;
    push(DAY);
    todo_model_tap(&M, "rosary@1007", T);      /* will be queued */
    run(TODO_UNDO_MS + 100);
    todo_model_tap(&M, "vit-am@1007", T);      /* in its undo window */
    CHECK(push(DAY));                           /* mid-day push, both still open per Muse */
    CHECK(state_of("rosary@1007") == TODO_ROW_QUEUED);
    CHECK(state_of("vit-am@1007") == TODO_ROW_UNDO);

    /* Muse drops the queued item from the list: it stays and still sends. */
    CHECK(push("[{\"id\":\"vit-am@1007\",\"label\":\"Vitamins AM\",\"source\":\"manual\"}]"));
    const todo_item_t *r = todo_model_find(&M, "rosary@1007");
    CHECK(r && r->state == TODO_ROW_QUEUED && !r->in_list);
    int done, total;
    todo_model_progress(&M, &done, &total);
    CHECK(total == 1);

    /* An undo-window item that disappears is just gone; nothing is sent for it. */
    CHECK(push("[]"));
    CHECK(todo_model_find(&M, "vit-am@1007") == NULL);
    F.online = true;
    run(100);
    CHECK(F.sends == 1);
    CHECK(strstr(F.last_turn, "rosary@1007") && !strstr(F.last_turn, "vit-am"));
    todo_model_turn_done(&M, T);
    run(1000);
    CHECK(todo_model_find(&M, "rosary@1007") == NULL);
}

static void test_muse_marks_done(void)
{
    setup("muse_marks_done");
    push(DAY);
    CHECK(push("[{\"id\":\"rosary@1007\",\"label\":\"Rosary\",\"source\":\"manual\"},"
               "{\"id\":\"vit-am@1007\",\"label\":\"Vitamins AM\",\"source\":\"manual\"},"
               "{\"id\":\"steps@1007\",\"label\":\"10,000 steps\",\"source\":\"healthkit\",\"done\":true}]"));
    CHECK(state_of("steps@1007") == TODO_ROW_AUTO_DONE);
    run(TODO_AUTO_DONE_SHOW_MS - 100);
    CHECK(state_of("steps@1007") == TODO_ROW_AUTO_DONE);
    run(200);
    CHECK(state_of("steps@1007") == TODO_ROW_LEAVING);
    run(TODO_LEAVE_MS);
    CHECK(state_of("steps@1007") == TODO_ROW_HIDDEN);
    CHECK(F.sends == 0); /* auto items never go through the outbox */

    /* Already-done items on first sight are simply hidden. */
    setup("muse_marks_done/new");
    CHECK(push("[{\"id\":\"x\",\"label\":\"X\",\"source\":\"healthkit\",\"done\":true}]"));
    CHECK(state_of("x") == TODO_ROW_HIDDEN);

    /* Muse can reopen something. */
    CHECK(push("[{\"id\":\"x\",\"label\":\"X\",\"source\":\"healthkit\",\"done\":false}]"));
    CHECK(state_of("x") == TODO_ROW_OPEN);
}

static void test_celebration(void)
{
    setup("celebration/tap");
    push("[{\"id\":\"a\",\"label\":\"A\",\"source\":\"manual\"},"
         "{\"id\":\"b\",\"label\":\"B\",\"source\":\"healthkit\",\"done\":true}]");
    todo_model_tap(&M, "a", T);
    CHECK(todo_model_screen(&M) == TODO_SCREEN_LIST); /* not until the undo window ends */
    run(TODO_UNDO_MS + 40);
    CHECK(todo_model_screen(&M) == TODO_SCREEN_CELEBRATE);
    run(TODO_CELEBRATE_MS + 40);
    CHECK(todo_model_screen(&M) == TODO_SCREEN_ALL_DONE);

    /* A later push with new open items brings the list back; finishing them celebrates again. */
    push("[{\"id\":\"a\",\"label\":\"A\",\"source\":\"manual\",\"done\":true},"
         "{\"id\":\"c\",\"label\":\"C\",\"source\":\"manual\"}]");
    CHECK(todo_model_screen(&M) == TODO_SCREEN_LIST);
    todo_model_tap(&M, "c", T);
    run(TODO_UNDO_MS + 40);
    CHECK(todo_model_screen(&M) == TODO_SCREEN_CELEBRATE);

    setup("celebration/by_muse");
    push("[{\"id\":\"s\",\"label\":\"Steps\",\"source\":\"healthkit\"}]");
    push("[{\"id\":\"s\",\"label\":\"Steps\",\"source\":\"healthkit\",\"done\":true}]");
    CHECK(todo_model_screen(&M) == TODO_SCREEN_CELEBRATE);

    setup("celebration/arrives_done");
    push("[{\"id\":\"s\",\"label\":\"Steps\",\"source\":\"healthkit\",\"done\":true}]");
    CHECK(todo_model_screen(&M) == TODO_SCREEN_ALL_DONE);

    setup("celebration/removed_not_completed");
    push("[{\"id\":\"s\",\"label\":\"Steps\",\"source\":\"healthkit\"}]");
    push("[]");
    CHECK(todo_model_screen(&M) == TODO_SCREEN_ALL_DONE);

    setup("celebration/undo_cancels");
    push("[{\"id\":\"a\",\"label\":\"A\",\"source\":\"manual\"}]");
    todo_model_tap(&M, "a", T);
    run(1000);
    todo_model_undo(&M, "a", T);
    run(TODO_UNDO_MS + 40);
    CHECK(todo_model_screen(&M) == TODO_SCREEN_LIST);
}

static void test_message_bar(void)
{
    setup("message_bar");
    char err[32];
    /* Durations are written out (not the constants) to pin the owner's choice:
     * under 50 characters -> 30 s, otherwise 5 minutes. */
    CHECK(todo_model_show_message(&M, "Drink some water!", T, err, sizeof(err)));
    CHECK_STR(M.message, "Drink some water!");
    run(29900);
    CHECK(M.message[0]);
    run(200);
    CHECK(!M.message[0]);

    static const char LONG_MSG[] = "Halfway there! Maybe step outside for some sunlight?"; /* 52 chars */
    todo_model_show_message(&M, LONG_MSG, T, err, sizeof(err));
    run(5 * 60 * 1000 - 100);
    CHECK_STR(M.message, LONG_MSG);
    run(200);
    CHECK(!M.message[0]);

    /* The boundary: 49 characters is short, 50 is long. */
    char text[64];
    memset(text, 'a', 49);
    text[49] = '\0';
    CHECK(todo_message_hold_ms(text) == 30000);
    text[49] = 'a';
    text[50] = '\0';
    CHECK(todo_message_hold_ms(text) == 300000);
    /* Characters, not bytes: 40 x "é" is 80 bytes but still short. */
    char accents[96] = "";
    for (int i = 0; i < 40; i++) {
        strcat(accents, "\xc3\xa9");
    }
    CHECK(todo_message_hold_ms(accents) == 30000);

    /* Replacing restarts the timer with the new message's length. */
    todo_model_show_message(&M, LONG_MSG, T, err, sizeof(err));
    run(60000);
    todo_model_show_message(&M, "two", T, err, sizeof(err));
    run(29900);
    CHECK_STR(M.message, "two");
    run(200);
    CHECK(!M.message[0]);

    /* Muse's replies follow the same rule. */
    push(DAY);
    todo_model_tap(&M, "rosary@1007", T);
    run(TODO_UNDO_MS + 100);
    todo_model_turn_reply(&M, "Logged!");
    todo_model_turn_done(&M, T);
    run(29900);
    CHECK_STR(M.message, "Logged!");
    run(200);
    CHECK(!M.message[0]);

    /* Tapping clears it right away. */
    todo_model_show_message(&M, LONG_MSG, T, err, sizeof(err));
    todo_model_clear_message(&M);
    CHECK(!M.message[0]);
}

static void test_reboot_keeps_outbox_and_list(void)
{
    setup("reboot");
    F.online = false;
    push(DAY);
    todo_model_tap(&M, "rosary@1007", T);
    run(TODO_UNDO_MS + 100);
    todo_model_tap(&M, "vit-am@1007", T); /* undo window at power loss: nothing sent, reopens */
    char blob[16384];
    snprintf(blob, sizeof(blob), "%s", F.saved);

    todo_port_t port = M.port;
    todo_model_init(&M, &port);
    T = 50; /* monotonic clock restarts */
    CHECK(todo_model_restore(&M, blob, T));
    CHECK(state_of("rosary@1007") == TODO_ROW_QUEUED);
    CHECK(state_of("vit-am@1007") == TODO_ROW_OPEN);
    CHECK(todo_model_screen(&M) == TODO_SCREEN_LIST);
    CHECK_STR(M.date, "Wednesday, October 7");
    F.online = true;
    run(100);
    CHECK(F.sends == 1);
    CHECK_STR(F.last_turn, "[todo-display] Completed: Rosary (id rosary@1007) at 9:14 AM.");

    CHECK(!todo_model_restore(&M, "{garbage", T));
    CHECK(!todo_model_restore(&M, NULL, T));
    CHECK(todo_model_screen(&M) == TODO_SCREEN_IDLE);
}

static void test_list_result(void)
{
    setup("list_result");
    F.online = false;
    push(DAY);
    todo_model_tap(&M, "rosary@1007", T);
    run(TODO_UNDO_MS + 100);
    char out[256];
    CHECK(todo_model_list_result(&M, out, sizeof(out)));
    CHECK_STR(out, "{\"shown\":3,\"pending\":[\"rosary@1007\"]}");
    char tiny[8];
    CHECK(!todo_model_list_result(&M, tiny, sizeof(tiny)));
}

static void test_source_labels(void)
{
    setup("source_labels");
    CHECK_STR(todo_source_label("manual"), "");
    CHECK_STR(todo_source_label("healthkit"), "HealthKit");
    CHECK_STR(todo_source_label("myfitnesspal"), "MyFitnessPal");
    CHECK_STR(todo_source_label("calendar"), "calendar");
}

int main(void)
{
    setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1); /* America/Chicago */
    tzset();
    test_idle_until_list();
    test_validation();
    test_tap_undo();
    test_debounce();
    test_commit_send_reply();
    test_batching();
    test_refused_retry();
    test_error_backoff();
    test_give_up();
    test_events_without_turn_ignored();
    test_push_keeps_local_state();
    test_muse_marks_done();
    test_celebration();
    test_message_bar();
    test_reboot_keeps_outbox_and_list();
    test_list_result();
    test_source_labels();
    if (s_failures) {
        fprintf(stderr, "%d check(s) failed\n", s_failures);
        return 1;
    }
    printf("todo model: all tests passed\n");
    return 0;
}

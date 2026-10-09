/*
 * Out-of-memory paths in the todo model. Builds todo_model.c with a malloc
 * that can be told to fail (cJSON keeps the real one).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

static bool s_fail;
static int s_calls;

static void *test_malloc(size_t n)
{
    s_calls++;
    return s_fail ? NULL : malloc(n);
}

#define TODO_MALLOC test_malloc
#include "../components/todo_display/todo_model.c"

#include <stdio.h>

static int s_failures;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c);  \
            s_failures++;                                                \
        }                                                                \
    } while (0)

static int s_sends;
static bool link_up(void *ctx) { (void)ctx; return true; }
static bool send_turn(void *ctx, const char *t) { (void)ctx; (void)t; s_sends++; return true; }

int main(void)
{
    todo_model_t *m = calloc(1, sizeof(*m));
    todo_port_t port = { NULL, link_up, send_turn, NULL, NULL };
    todo_model_init(m, &port);
    const char *list = "[{\"id\":\"a\",\"label\":\"A\",\"source\":\"manual\"}]";
    char err[64] = "";
    uint32_t t = 1000;

    /* set_list: a failed scratch allocation is reported and changes nothing. */
    s_fail = true;
    CHECK(!todo_model_set_list(m, list, NULL, t, err, sizeof(err)));
    CHECK(strcmp(err, "out of memory") == 0);
    CHECK(!m->have_list && m->count == 0);
    s_fail = false;
    CHECK(todo_model_set_list(m, list, NULL, t, err, sizeof(err)));
    CHECK(m->count == 1);

    /* Sending: an allocation failure backs off instead of retrying every frame. */
    CHECK(todo_model_tap(m, "a", t));
    t += TODO_UNDO_MS - 10;
    todo_model_tick(m, t);
    s_fail = true;
    s_calls = 0;
    for (int i = 0; i < 100; i++) { /* ~1 s of 10 ms frames */
        t += 10;
        todo_model_tick(m, t);
    }
    CHECK(s_calls == 1);       /* one attempt, then wait */
    CHECK(s_sends == 0);
    CHECK(m->items[0].state == TODO_ROW_QUEUED);
    s_fail = false;
    for (int i = 0; i < 150; i++) {
        t += 10;
        todo_model_tick(m, t);
    }
    CHECK(s_sends == 1);       /* recovered once memory is back */
    CHECK(m->items[0].state == TODO_ROW_SENDING);

    free(m);
    if (s_failures) {
        fprintf(stderr, "%d check(s) failed\n", s_failures);
        return 1;
    }
    printf("todo model out-of-memory: all tests passed\n");
    return 0;
}

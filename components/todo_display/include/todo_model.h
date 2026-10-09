/*
 * Todo display: list, item lifecycle and completion outbox.
 *
 * Plain C with no LVGL or ESP-IDF dependency, so the same code runs in the
 * firmware, the desktop simulator and the host unit tests. All time comes in
 * through arguments (a monotonic millisecond clock) or the port (wall clock),
 * which keeps every state change deterministic and testable.
 *
 * Behaviour follows docs/03-ui-spec.md and docs/04-agent-integration.md.
 *
 * Threading: the model is not thread-safe. Every todo_model_* call -- from
 * the command handlers, the chat-turn event reader and the UI -- must hold
 * the same lock (on the device, the SDK's display/LVGL lock), because the UI
 * reads the model while it draws.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Limits for what Muse may send (validated in todo_model_set_list). */
#define TODO_MAX_LIST_ITEMS 40
#define TODO_MAX_ITEMS 64 /* list items plus completions still in the outbox */
#define TODO_ID_MAX 96
#define TODO_LABEL_MAX 120
#define TODO_SOURCE_MAX 32
#define TODO_DATE_MAX 40
#define TODO_MESSAGE_MAX 280

/* Timing (docs/03-ui-spec.md, docs/04-agent-integration.md). */
#define TODO_UNDO_MS 5000u
#define TODO_TAP_DEBOUNCE_MS 300u
#define TODO_AUTO_DONE_SHOW_MS 3000u
#define TODO_LEAVE_MS 450u
#define TODO_CELEBRATE_MS 2000u
/* Message bar: short messages clear sooner than long ones (owner's choice). */
#define TODO_MESSAGE_SHORT_CHARS 50
#define TODO_MESSAGE_SHORT_MS 30000u
#define TODO_MESSAGE_LONG_MS (5u * 60u * 1000u)
#define TODO_TURN_TIMEOUT_MS 60000u
#define TODO_REFUSED_RETRY_MS 2000u
#define TODO_GIVE_UP_S (24 * 60 * 60)

typedef enum {
    TODO_ROW_OPEN,      /* waiting to be done (tappable if the source allows) */
    TODO_ROW_UNDO,      /* tapped; undo window running, nothing sent */
    TODO_ROW_QUEUED,    /* crossed out, in the outbox, not yet sent */
    TODO_ROW_SENDING,   /* crossed out, part of the chat turn in flight */
    TODO_ROW_AUTO_DONE, /* Muse marked it done: crossed out with its tag */
    TODO_ROW_LEAVING,   /* finished: plays its exit, then is hidden */
    TODO_ROW_HIDDEN,    /* done and settled; counted but not shown */
} todo_row_state_t;

typedef struct {
    char id[TODO_ID_MAX + 1];
    char label[TODO_LABEL_MAX + 1];
    char source[TODO_SOURCE_MAX + 1];
    bool manual;     /* source == "manual" */
    bool tappable;   /* the owner can tick it off (see todo_source_tappable) */
    bool in_list;    /* present in Muse's latest push */
    bool muse_done;  /* Muse says it's done */
    todo_row_state_t state;
    uint32_t state_ms; /* monotonic time the current state began */
    uint32_t tap_ms;   /* last accepted tap, for debounce and the squish */
    time_t completed_at; /* wall clock at the tap, 0 if unknown */
} todo_item_t;

/* What the screen should show as a whole. */
typedef enum {
    TODO_SCREEN_IDLE,      /* no list yet: "waiting for today's list" */
    TODO_SCREEN_LIST,      /* checklist */
    TODO_SCREEN_CELEBRATE, /* two-second all-done animation over the list area */
    TODO_SCREEN_ALL_DONE,  /* nothing left open */
} todo_screen_t;

/* Platform services. Any callback may be NULL (treated as offline / no clock / no storage). */
typedef struct {
    void *ctx;
    /* Muse is set up and the link is up, so a chat turn could go out now. */
    bool (*link_ready)(void *ctx);
    /*
     * Start a typed chat turn. Return false if it was refused (Muse not set
     * up, or another turn running); the model retries shortly. The text is
     * only borrowed for the call.
     */
    bool (*send_turn)(void *ctx, const char *text);
    /* Wall clock in seconds, or 0 while it isn't known (no SNTP yet). */
    time_t (*wall_time)(void *ctx);
    /* Persist the state blob (NUL-terminated JSON). Called after changes that must survive a reboot. */
    void (*save)(void *ctx, const char *blob);
} todo_port_t;

typedef struct {
    todo_port_t port;

    todo_item_t items[TODO_MAX_ITEMS];
    size_t count;
    bool have_list;
    char date[TODO_DATE_MAX + 1];

    /* Message bar. */
    char message[TODO_MESSAGE_MAX + 1];
    uint32_t message_ms;
    uint32_t message_hold_ms; /* how long this message stays up */
    uint32_t message_seq; /* bumps whenever the message changes */

    /* Outbox sending. */
    bool online;
    bool in_flight;
    uint32_t in_flight_ms;
    uint32_t next_send_ms;
    unsigned retry_stage;
    time_t first_fail_at;
    char reply[TODO_MESSAGE_MAX + 1];

    /* All-done celebration. */
    bool celebrating;
    uint32_t celebrate_ms;

    /* Bumps on any change the UI should redraw for. */
    uint32_t seq;
} todo_model_t;

/* Initialise an empty model (idle screen). */
void todo_model_init(todo_model_t *m, const todo_port_t *port);

/*
 * Restore state saved through port.save (after a reboot). Returns false and
 * leaves the model empty if the blob is malformed.
 */
bool todo_model_restore(todo_model_t *m, const char *blob, uint32_t now_ms);

/*
 * todo.set_list: replace the list. items_json is the JSON array Muse sent (as
 * a string), date may be NULL. On error returns false and writes a short
 * reason into err; the current list is left untouched.
 */
bool todo_model_set_list(todo_model_t *m, const char *items_json, const char *date,
                         uint32_t now_ms, char *err, size_t err_len);

/* todo.show_message. Returns false (with err) if the text is missing or too long. */
bool todo_model_show_message(todo_model_t *m, const char *text, uint32_t now_ms,
                             char *err, size_t err_len);

/* Touch input. Return true if the tap did something. */
bool todo_model_tap(todo_model_t *m, const char *id, uint32_t now_ms);
bool todo_model_undo(todo_model_t *m, const char *id, uint32_t now_ms);
void todo_model_clear_message(todo_model_t *m);

/* Chat turn events for the turn this model started. Ignored if none is in flight. */
void todo_model_turn_reply(todo_model_t *m, const char *chunk);
void todo_model_turn_done(todo_model_t *m, uint32_t now_ms);
void todo_model_turn_error(todo_model_t *m, uint32_t now_ms);

/* Advance timers and try to send. Call regularly (every frame is fine). */
void todo_model_tick(todo_model_t *m, uint32_t now_ms);

/* Queries for the UI. */
todo_screen_t todo_model_screen(const todo_model_t *m);
bool todo_model_item_visible(const todo_item_t *it);
const todo_item_t *todo_model_find(const todo_model_t *m, const char *id);
void todo_model_progress(const todo_model_t *m, int *done, int *total);
/* Completions waiting in the outbox (queued or sending). */
int todo_model_outbox_count(const todo_model_t *m);

/*
 * The todo.set_list return payload: {"shown":N,"pending":[ids]}. Writes at
 * most len bytes; returns false if it didn't fit.
 */
bool todo_model_list_result(const todo_model_t *m, char *out, size_t len);

/*
 * How long a message stays in the bar: 30 s if it is under 50 characters,
 * otherwise 5 minutes. Characters, not bytes, so accented text counts fairly.
 */
uint32_t todo_message_hold_ms(const char *text);

/*
 * Whether items from a source can be ticked off by hand: "manual", plus
 * "healthkit" as a backup for when HealthKit is slow to sync (owner's choice).
 * Other sources stay display-only until Muse marks them.
 */
bool todo_source_tappable(const char *source);

/* Friendly name for a source tag ("healthkit" -> "HealthKit"). */
const char *todo_source_label(const char *source);

#ifdef __cplusplus
}
#endif

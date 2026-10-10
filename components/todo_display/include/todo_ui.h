/*
 * Todo display screen (LVGL 9.5), 800x480 landscape.
 *
 * The UI only renders the model and turns touches into model calls; it owns
 * no todo state. All animation is driven from the time passed to
 * todo_ui_update(), so a scripted run renders the same frames every time.
 * Call everything from the LVGL thread (under the display lock on device).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "todo_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the screen on `parent` (normally lv_screen_active()). */
void todo_ui_create(lv_obj_t *parent, todo_model_t *model);

/* Redraw for the model's current state. Call every frame after todo_model_tick(). */
void todo_ui_update(uint32_t now_ms);

/*
 * The line under "waiting for today's list" (e.g. how to pair). NULL or ""
 * restores the default, "Muse will send it over soon."
 */
void todo_ui_set_idle_hint(const char *hint);

/* Have the little top-hat Muse peek in from the top bar for a few seconds. */
void todo_ui_peek(uint32_t now_ms);

/*
 * Screen point to touch for a target, for scripted input in the simulator:
 * "row:<id>", "undo:<id>", "message", "add" (the + button), or while the
 * add-task sheet is open "sheet_add" / "sheet_cancel". Returns false if it
 * isn't on screen.
 */
bool todo_ui_point_for(const char *target, lv_point_t *pt);

/*
 * Press an on-screen keyboard key by its label ("a", " ", LV_SYMBOL_BACKSPACE,
 * LV_SYMBOL_OK ...) while the add-task sheet is open. For scripted input.
 */
bool todo_ui_type(const char *key);

#ifdef __cplusplus
}
#endif

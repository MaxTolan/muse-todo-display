/*
 * Muse in a top hat: the SDK's pixel avatar (avatar/muse_pixel.c) drawn on
 * an LVGL canvas, with a pixel-art top hat on its head. Black background
 * pixels are transparent so Muse sits directly on the todo screen.
 *
 * muse_pixel keeps one global frame, so draw mascots one after another from
 * the LVGL thread (never concurrently).
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float t;          /* seconds, drives blinking and bobbing */
    float happy;      /* 0..1: hop, hearts and big smile */
    float hat_lift;   /* hat raised off the head, in avatar pixels (hat tip) */
    float hat_tilt;   /* extra lean, -1..1 (negative tips it back) */
} todo_mascot_pose_t;

/* px must be a multiple of 64 (the avatar grid) and at most 512. */
lv_obj_t *todo_mascot_create(lv_obj_t *parent, int px);
void todo_mascot_draw(lv_obj_t *mascot, const todo_mascot_pose_t *pose);

#ifdef __cplusplus
}
#endif

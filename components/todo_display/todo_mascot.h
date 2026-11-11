/*
 * Chief: the SDK's pixel avatar (avatar/muse_pixel.c) drawn on an LVGL
 * canvas, wearing his current look: a war bonnet, a crown of blue, red and
 * near-black feathers on a beaded browband with rosettes and fur drops.
 * Black background pixels are transparent so he sits directly on the todo
 * screen.
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
    float happy;       /* 0..1: hop, hearts and big smile */
    float crest_lift;  /* bonnet crown raised off the head, in avatar pixels */
    float crest_tilt;  /* extra lean, -1..1 (about 20 degrees; negative tips it forward) */
} todo_mascot_pose_t;

/* px must be a multiple of 64 (the avatar grid) and at most 512. */
lv_obj_t *todo_mascot_create(lv_obj_t *parent, int px);
void todo_mascot_draw(lv_obj_t *mascot, const todo_mascot_pose_t *pose);

#ifdef __cplusplus
}
#endif

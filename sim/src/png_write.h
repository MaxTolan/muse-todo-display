/*
 * Tiny dependency-free PNG writer (uncompressed deflate) for simulator screenshots.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Write w x h tightly packed 8-bit RGB pixels to path. */
bool png_write_rgb(const char *path, const uint8_t *rgb, int w, int h);

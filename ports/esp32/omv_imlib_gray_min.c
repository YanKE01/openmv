/*
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>

#include "omv_imlib_gray_min.h"

static uint8_t *omv_esp32_gray_buf = NULL;
static size_t omv_esp32_gray_buf_cap = 0;
static uint32_t omv_esp32_gray_cache_version = 0;
static uint32_t omv_esp32_gray_current_version = 1;
static int omv_esp32_gray_cached_w = 0;
static int omv_esp32_gray_cached_h = 0;
static uint8_t *omv_esp32_gray_cached_pixels = NULL;

void omv_esp32_imlib_gray_mark_dirty(void) {
    omv_esp32_gray_current_version += 1;
    if (omv_esp32_gray_current_version == 0) {
        omv_esp32_gray_current_version = 1;
    }
}

static bool omv_esp32_imlib_gray_ensure_capacity(size_t count) {
    if (omv_esp32_gray_buf_cap >= count) {
        return true;
    }

    uint8_t *new_buf = realloc(omv_esp32_gray_buf, count);
    if (new_buf == NULL) {
        return false;
    }

    omv_esp32_gray_buf = new_buf;
    omv_esp32_gray_buf_cap = count;
    return true;
}

bool omv_esp32_imlib_gray_get(image_t *img, const uint8_t **gray_out) {
    if ((img == NULL) || (gray_out == NULL) || (img->pixfmt != PIXFORMAT_RGB565) ||
        (img->pixels == NULL) || (img->w <= 0) || (img->h <= 0)) {
        return false;
    }

    size_t count = (size_t) img->w * (size_t) img->h;
    if (!omv_esp32_imlib_gray_ensure_capacity(count)) {
        return false;
    }

    if ((omv_esp32_gray_cache_version != omv_esp32_gray_current_version) ||
        (omv_esp32_gray_cached_w != img->w) ||
        (omv_esp32_gray_cached_h != img->h) ||
        (omv_esp32_gray_cached_pixels != img->pixels)) {
        uint16_t *pixels = (uint16_t *) img->pixels;
        for (size_t i = 0; i < count; i++) {
            omv_esp32_gray_buf[i] = COLOR_RGB565_TO_Y(pixels[i]);
        }

        omv_esp32_gray_cache_version = omv_esp32_gray_current_version;
        omv_esp32_gray_cached_w = img->w;
        omv_esp32_gray_cached_h = img->h;
        omv_esp32_gray_cached_pixels = img->pixels;
    }

    *gray_out = omv_esp32_gray_buf;
    return true;
}

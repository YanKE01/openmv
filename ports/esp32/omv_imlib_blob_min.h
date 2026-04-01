/*
 * SPDX-License-Identifier: MIT
 */
#ifndef __OMV_ESP32_IMLIB_BLOB_MIN_H__
#define __OMV_ESP32_IMLIB_BLOB_MIN_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "imlib.h"

typedef enum {
    OMV_ESP32_THRESHOLD_MODE_Y = 0,
    OMV_ESP32_THRESHOLD_MODE_LAB,
} omv_esp32_threshold_mode_t;

typedef struct {
    omv_esp32_threshold_mode_t mode;
    int l_lo, l_hi;
    int a_lo, a_hi;
    int b_lo, b_hi;
} omv_esp32_threshold_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int pixels;
    float cx;
    float cy;
    int code;
    int count;
} omv_esp32_blob_t;

bool omv_esp32_find_blobs(image_t *img,
                          rectangle_t *roi,
                          const omv_esp32_threshold_t *thresholds,
                          size_t thresholds_len,
                          bool invert,
                          unsigned int x_stride,
                          unsigned int y_stride,
                          unsigned int area_threshold,
                          unsigned int pixels_threshold,
                          omv_esp32_blob_t **out_blobs,
                          size_t *out_len);

void omv_esp32_free_blobs(omv_esp32_blob_t *blobs);

#endif // __OMV_ESP32_IMLIB_BLOB_MIN_H__

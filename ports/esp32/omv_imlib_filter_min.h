/*
 * SPDX-License-Identifier: MIT
 */
#ifndef __OMV_ESP32_IMLIB_FILTER_MIN_H__
#define __OMV_ESP32_IMLIB_FILTER_MIN_H__

#include <stdbool.h>

#include "imlib.h"

bool omv_esp32_imlib_mean_filter(image_t *img, int ksize, bool threshold, int offset, bool invert);
bool omv_esp32_imlib_median_filter(image_t *img, int ksize, float percentile, bool threshold, int offset, bool invert);
bool omv_esp32_imlib_gaussian_filter(image_t *img, int ksize, bool unsharp, bool threshold, int offset, bool invert);

#endif // __OMV_ESP32_IMLIB_FILTER_MIN_H__

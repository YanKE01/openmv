/*
 * SPDX-License-Identifier: MIT
 */
#ifndef __OMV_ESP32_IMLIB_EDGE_MIN_H__
#define __OMV_ESP32_IMLIB_EDGE_MIN_H__

#include <stdbool.h>

#include "imlib.h"

bool omv_esp32_imlib_edge_simple(image_t *img, rectangle_t *roi, int low_thresh, int high_thresh);
bool omv_esp32_imlib_edge_canny(image_t *img, rectangle_t *roi, int low_thresh, int high_thresh);

#endif // __OMV_ESP32_IMLIB_EDGE_MIN_H__

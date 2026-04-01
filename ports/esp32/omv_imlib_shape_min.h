/*
 * SPDX-License-Identifier: MIT
 */
#ifndef __OMV_ESP32_IMLIB_SHAPE_MIN_H__
#define __OMV_ESP32_IMLIB_SHAPE_MIN_H__

#include <stdbool.h>
#include <stddef.h>

#include "imlib.h"

bool omv_esp32_find_rects(image_t *img,
                          rectangle_t *roi,
                          uint32_t threshold,
                          find_rects_list_lnk_data_t **out_rects,
                          size_t *out_len);

void omv_esp32_free_rects(find_rects_list_lnk_data_t *rects);

bool omv_esp32_find_circles(image_t *img,
                            rectangle_t *roi,
                            unsigned int x_stride,
                            unsigned int y_stride,
                            uint32_t threshold,
                            unsigned int x_margin,
                            unsigned int y_margin,
                            unsigned int r_margin,
                            unsigned int r_min,
                            unsigned int r_max,
                            unsigned int r_step,
                            find_circles_list_lnk_data_t **out_circles,
                            size_t *out_len);

void omv_esp32_free_circles(find_circles_list_lnk_data_t *circles);

#endif // __OMV_ESP32_IMLIB_SHAPE_MIN_H__

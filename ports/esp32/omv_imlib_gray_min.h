/*
 * SPDX-License-Identifier: MIT
 */
#ifndef __OMV_ESP32_IMLIB_GRAY_MIN_H__
#define __OMV_ESP32_IMLIB_GRAY_MIN_H__

#include <stdbool.h>
#include <stdint.h>

#include "imlib.h"

bool omv_esp32_imlib_gray_get(image_t *img, const uint8_t **gray_out);
void omv_esp32_imlib_gray_mark_dirty(void);

#endif // __OMV_ESP32_IMLIB_GRAY_MIN_H__

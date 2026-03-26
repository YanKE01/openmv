/*
 * SPDX-License-Identifier: MIT
 */
#ifndef __OMV_ESP32_PY_IMAGE_LITE_H__
#define __OMV_ESP32_PY_IMAGE_LITE_H__

#include "py/obj.h"

extern const mp_obj_type_t py_esp32_image_type;

void py_esp32_image_init0(void);
mp_obj_t py_esp32_image_from_mainfb(void);

#endif // __OMV_ESP32_PY_IMAGE_LITE_H__

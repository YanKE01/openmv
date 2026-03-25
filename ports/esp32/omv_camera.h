/*
 * SPDX-License-Identifier: MIT
 *
 * OpenMV ESP32 camera integration skeleton.
 */
#ifndef __OMV_ESP32_CAMERA_H__
#define __OMV_ESP32_CAMERA_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void omv_esp32_camera_init0(void);
int omv_esp32_camera_init(void);
void omv_esp32_camera_deinit(void);
bool omv_esp32_camera_is_ready(void);
uint32_t omv_esp32_camera_get_width(void);
uint32_t omv_esp32_camera_get_height(void);
bool omv_esp32_camera_capture_rgb565(uint16_t *pixels, size_t pixel_count);

#endif // __OMV_ESP32_CAMERA_H__

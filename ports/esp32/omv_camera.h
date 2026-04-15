/*
 * SPDX-License-Identifier: MIT
 *
 * OpenMV ESP32 camera helpers.
 */
#ifndef __OMV_ESP32_CAMERA_H__
#define __OMV_ESP32_CAMERA_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OMV_ESP32_CAMERA_SENSOR_ID (0x2710)

void omv_esp32_camera_init0(void);
int omv_esp32_camera_init(void);
void omv_esp32_camera_deinit(void);
bool omv_esp32_camera_is_ready(void);
bool omv_esp32_camera_set_pixformat(uint32_t pixformat);
uint32_t omv_esp32_camera_get_pixformat(void);
bool omv_esp32_camera_set_framesize(uint32_t framesize);
uint32_t omv_esp32_camera_get_width(void);
uint32_t omv_esp32_camera_get_height(void);
uint32_t omv_esp32_camera_get_id(void);
bool omv_esp32_camera_set_hmirror(bool enable);
bool omv_esp32_camera_get_hmirror(void);
bool omv_esp32_camera_set_vflip(bool enable);
bool omv_esp32_camera_get_vflip(void);
bool omv_esp32_camera_capture(uint8_t *pixels, size_t size);

#endif // __OMV_ESP32_CAMERA_H__

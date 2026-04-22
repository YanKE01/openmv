/*
 * This file is part of the OpenMV project.
 *
 * Board configuration and pin definitions.
 */
#ifndef __OMV_BOARDCONFIG_H__
#define __OMV_BOARDCONFIG_H__

#include "omv_pins.h"

#define OMV_BOARD_ARCH             "ESP32P4"
#define OMV_BOARD_TYPE             "ESP32_P4X_EYE"
#define OMV_PORT_ESP32             (1)
#define OMV_BOARD_UID_SIZE         (2)
#define OMV_BOARD_UID_OFFSET       (0)
#define OMV_UMM_BLOCK_SIZE         (16)
#define OMV_TUSBDBG_ENABLE         (1)
#define OMV_TUSBDBG_BUFFER         (2048)
#define OMV_PROFILER_ENABLE        (0)
#define OMV_JPEG_QUALITY_LOW       (60)
#define OMV_JPEG_QUALITY_HIGH      (60)
#define OMV_FB_SIZE                (512 * 1024)
#define OMV_SB_SIZE                (256 * 1024)

// Camera configuration.
#define OMV_ESP32_CAMERA_INPUT_WIDTH            (1280)
#define OMV_ESP32_CAMERA_INPUT_HEIGHT           (720)
#define OMV_ESP32_CAMERA_ACTIVE_INPUT_WIDTH     (640)
#define OMV_ESP32_CAMERA_ACTIVE_INPUT_HEIGHT    (480)
#define OMV_ESP32_CAMERA_ACTIVE_INPUT_OFFSET_X  (320)
#define OMV_ESP32_CAMERA_ACTIVE_INPUT_OFFSET_Y  (120)
#define OMV_ESP32_CAMERA_OUTPUT_QQVGA_WIDTH     (160)
#define OMV_ESP32_CAMERA_OUTPUT_QQVGA_HEIGHT    (120)
#define OMV_ESP32_CAMERA_OUTPUT_QVGA_WIDTH      (320)
#define OMV_ESP32_CAMERA_OUTPUT_QVGA_HEIGHT     (240)
#define OMV_ESP32_CAMERA_BUFFER_COUNT           (2)
#define OMV_ESP32_CAMERA_SCCB_I2C_FREQ          (100000)
#define OMV_ESP32_CAMERA_XCLK_FREQ              (24000000)

// LCD configuration.
#define OMV_ESP32_LCD_H_RES             (240)
#define OMV_ESP32_LCD_V_RES             (240)
#define OMV_ESP32_LCD_PIXEL_CLOCK_HZ    (80 * 1000 * 1000)
#define OMV_ESP32_LCD_CMD_BITS          (8)
#define OMV_ESP32_LCD_PARAM_BITS        (8)
#define OMV_ESP32_LCD_BPP               (16)
#define OMV_ESP32_LCD_BRIGHTNESS_CH     (0)
#define OMV_ESP32_LCD_BRIGHTNESS_TIMER  (0)

#endif // __OMV_BOARDCONFIG_H__

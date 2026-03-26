/*
 * This file is part of the OpenMV project.
 *
 * Board configuration and pin definitions.
 */
#ifndef __OMV_BOARDCONFIG_H__
#define __OMV_BOARDCONFIG_H__

#define OMV_BOARD_ARCH             "ESP32P4"
#define OMV_BOARD_TYPE             "ESP32_GENERIC_P4"
#define OMV_BOARD_UID_SIZE         (2)
#define OMV_BOARD_UID_OFFSET       (0)
#define OMV_UMM_BLOCK_SIZE         (16)

#define OMV_JPEG_QUALITY_LOW       (50)
#define OMV_JPEG_QUALITY_HIGH      (85)
#define OMV_JPEG_QUALITY_THRESHOLD (320 * 240 * 2)

#define OMV_PROTOCOL_MAX_BUFFER_SIZE (4096)

#define OMV_FB_SIZE                (320 * 240 * 2)
#define OMV_SB_SIZE                (262144)

#endif // __OMV_BOARDCONFIG_H__

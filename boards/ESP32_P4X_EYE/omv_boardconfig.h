/*
 * This file is part of the OpenMV project.
 *
 * Board configuration and pin definitions.
 */
#ifndef __OMV_BOARDCONFIG_H__
#define __OMV_BOARDCONFIG_H__

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

#endif // __OMV_BOARDCONFIG_H__

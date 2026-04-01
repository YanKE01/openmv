/*
 * SPDX-License-Identifier: MIT
 *
 * MicroPython port config wrapper for OpenMV ESP32 builds.
 */
#ifndef __OMV_MPCONFIGPORT_H__
#define __OMV_MPCONFIGPORT_H__

#ifndef OPENMV_GIT_TAG
#define OPENMV_GIT_TAG "unknown"
#endif

#define MICROPY_BANNER_NAME_AND_VERSION "OpenMV " OPENMV_GIT_TAG "; MicroPython " MICROPY_GIT_TAG

#include <mpconfigport.h>

#endif // __OMV_MPCONFIGPORT_H__

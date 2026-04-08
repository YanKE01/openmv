/*
 * SPDX-License-Identifier: MIT
 *
 * OpenMV monitor debug helpers.
 */
#ifndef __OMV_DEBUG_H__
#define __OMV_DEBUG_H__

#include <stdarg.h>

void omv_debug_printf(const char *fmt, ...);
void omv_debug_vprintf(const char *fmt, va_list ap);

#define OMV_DEBUG(...) omv_debug_printf(__VA_ARGS__)

#endif // __OMV_DEBUG_H__

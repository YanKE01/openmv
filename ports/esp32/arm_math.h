/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal CMSIS-DSP compatibility header for non-ARM ESP32 builds.
 */
#ifndef __ARM_MATH_H__
#define __ARM_MATH_H__

#include <stdint.h>

typedef float float32_t;
typedef double float64_t;
typedef int8_t q7_t;
typedef int16_t q15_t;
typedef int32_t q31_t;
typedef int64_t q63_t;

#endif // __ARM_MATH_H__

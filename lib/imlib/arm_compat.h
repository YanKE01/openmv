/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal ARM/CMSIS intrinsic compatibility helpers for non-ARM targets.
 */
#ifndef __ARM_COMPAT_H__
#define __ARM_COMPAT_H__

#include <stdint.h>
#include <limits.h>

#if !defined(__ARM_ARCH)

typedef float float32_t;

static inline uint32_t __CLZ(uint32_t value) {
    return value ? __builtin_clz(value) : 32;
}

static inline uint32_t __RBIT(uint32_t value) {
    value = ((value & 0x55555555U) << 1) | ((value >> 1) & 0x55555555U);
    value = ((value & 0x33333333U) << 2) | ((value >> 2) & 0x33333333U);
    value = ((value & 0x0F0F0F0FU) << 4) | ((value >> 4) & 0x0F0F0F0FU);
    value = ((value & 0x00FF00FFU) << 8) | ((value >> 8) & 0x00FF00FFU);
    return (value << 16) | (value >> 16);
}

static inline uint32_t __REV(uint32_t value) {
    return __builtin_bswap32(value);
}

static inline uint32_t __REV16(uint32_t value) {
    return ((value & 0x00FF00FFU) << 8) | ((value >> 8) & 0x00FF00FFU);
}

static inline uint32_t __USAT(int32_t value, uint32_t sat) {
    uint32_t max = (sat >= 31) ? INT32_MAX : ((1U << sat) - 1U);
    if (value < 0) {
        return 0;
    }
    return ((uint32_t) value > max) ? max : (uint32_t) value;
}

static inline int32_t __SSAT(int32_t value, uint32_t sat) {
    int32_t max = (sat >= 31) ? INT32_MAX : ((1U << (sat - 1U)) - 1);
    int32_t min = (sat >= 31) ? INT32_MIN : (-1 - max);
    if (value > max) {
        return max;
    }
    if (value < min) {
        return min;
    }
    return value;
}

static inline uint32_t __USAT16(int32_t value, uint32_t sat) {
    uint32_t lo = __USAT((int16_t) value, sat);
    uint32_t hi = __USAT((int16_t) (value >> 16), sat);
    return lo | (hi << 16);
}

static inline uint32_t __USAT_ASR(int32_t value, uint32_t sat, uint32_t shift) {
    return __USAT(value >> shift, sat);
}

static inline uint32_t __PKHBT(uint32_t a, uint32_t b, uint32_t shift) {
    return (a & 0x0000FFFFU) | ((b << shift) & 0xFFFF0000U);
}

static inline uint32_t __PKHTB(uint32_t a, uint32_t b, uint32_t shift) {
    return (a & 0xFFFF0000U) | ((b >> shift) & 0x0000FFFFU);
}

static inline int32_t __SMUAD(uint32_t a, uint32_t b) {
    return ((int16_t) a * (int16_t) b) + ((int16_t) (a >> 16) * (int16_t) (b >> 16));
}

static inline int32_t __SMUADX(uint32_t a, uint32_t b) {
    return ((int16_t) a * (int16_t) (b >> 16)) + ((int16_t) (a >> 16) * (int16_t) b);
}

static inline int32_t __SMLAD(uint32_t a, uint32_t b, int32_t acc) {
    return __SMUAD(a, b) + acc;
}

static inline int32_t __SMLADX(uint32_t a, uint32_t b, int32_t acc) {
    return __SMUADX(a, b) + acc;
}

static inline int32_t __SMUSD(uint32_t a, uint32_t b) {
    return ((int16_t) a * (int16_t) b) - ((int16_t) (a >> 16) * (int16_t) (b >> 16));
}

static inline int32_t __SMLSD(uint32_t a, uint32_t b, int32_t acc) {
    return __SMUSD(a, b) + acc;
}

static inline uint32_t __UXTB_RORn(uint32_t value, uint32_t rotate) {
    return (value >> rotate) & 0xFFU;
}

static inline uint32_t __UXTB16(uint32_t value) {
    return value & 0x00FF00FFU;
}

static inline uint32_t __SXTB16(uint32_t value) {
    return ((uint32_t) (uint16_t) (int16_t) (int8_t) (value & 0xFFU))
        | (((uint32_t) (uint16_t) (int16_t) (int8_t) ((value >> 16) & 0xFFU)) << 16);
}

static inline uint32_t __UXTB16_RORn(uint32_t value, uint32_t rotate) {
    return __UXTB16((value >> rotate) | (value << (32 - rotate)));
}

static inline uint32_t __SHADD8(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) {
        int16_t sum = (int8_t) (a >> (i * 8)) + (int8_t) (b >> (i * 8));
        r |= ((uint32_t) (uint8_t) (sum >> 1)) << (i * 8);
    }
    return r;
}

static inline uint32_t __SHADD16(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (int i = 0; i < 2; i++) {
        int32_t sum = (int16_t) (a >> (i * 16)) + (int16_t) (b >> (i * 16));
        r |= ((uint32_t) (uint16_t) (sum >> 1)) << (i * 16);
    }
    return r;
}

static inline uint32_t __SADD8(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) {
        int16_t sum = (int8_t) (a >> (i * 8)) + (int8_t) (b >> (i * 8));
        r |= ((uint32_t) (uint8_t) sum) << (i * 8);
    }
    return r;
}

static inline uint32_t __SADD16(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (int i = 0; i < 2; i++) {
        int32_t sum = (int16_t) (a >> (i * 16)) + (int16_t) (b >> (i * 16));
        r |= ((uint32_t) (uint16_t) sum) << (i * 16);
    }
    return r;
}

static inline uint32_t __SSUB8(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) {
        int16_t diff = (int8_t) (a >> (i * 8)) - (int8_t) (b >> (i * 8));
        r |= ((uint32_t) (uint8_t) diff) << (i * 8);
    }
    return r;
}

static inline uint32_t __SSUB16(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (int i = 0; i < 2; i++) {
        int32_t diff = (int16_t) (a >> (i * 16)) - (int16_t) (b >> (i * 16));
        r |= ((uint32_t) (uint16_t) diff) << (i * 16);
    }
    return r;
}

static inline uint32_t __QADD16(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (int i = 0; i < 2; i++) {
        int32_t sum = (int16_t) (a >> (i * 16)) + (int16_t) (b >> (i * 16));
        r |= ((uint32_t) (uint16_t) __SSAT(sum, 16)) << (i * 16);
    }
    return r;
}

static inline uint32_t __QSUB16(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (int i = 0; i < 2; i++) {
        int32_t diff = (int16_t) (a >> (i * 16)) - (int16_t) (b >> (i * 16));
        r |= ((uint32_t) (uint16_t) __SSAT(diff, 16)) << (i * 16);
    }
    return r;
}

#endif

#endif

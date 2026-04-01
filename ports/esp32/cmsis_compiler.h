#ifndef OPENMV_PORTS_ESP32_CMSIS_COMPILER_H
#define OPENMV_PORTS_ESP32_CMSIS_COMPILER_H

#include <stdint.h>

static inline uint32_t __REV(uint32_t value) {
    return __builtin_bswap32(value);
}

static inline uint32_t __REV16(uint32_t value) {
    return ((value & 0x00FF00FFU) << 8) | ((value & 0xFF00FF00U) >> 8);
}

#endif

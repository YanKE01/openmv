#ifndef OPENMV_PORTS_ESP32_CMSIS_COMPILER_H
#define OPENMV_PORTS_ESP32_CMSIS_COMPILER_H

#include <stddef.h>
#include <stdint.h>

#ifndef __ASM
#define __ASM __asm
#endif

#ifndef __COMPILER_BARRIER
#define __COMPILER_BARRIER() __ASM volatile("" ::: "memory")
#endif

#ifndef __INLINE
#define __INLINE inline
#endif

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif

#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#endif

#ifndef __NO_RETURN
#define __NO_RETURN __attribute__((noreturn))
#endif

#ifndef __USED
#define __USED __attribute__((used))
#endif

#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif

#ifndef __PACKED
#define __PACKED __attribute__((packed))
#endif

#ifndef __PACKED_STRUCT
#define __PACKED_STRUCT struct __attribute__((packed))
#endif

#ifndef __PACKED_UNION
#define __PACKED_UNION union __attribute__((packed))
#endif

#ifndef __ALIGNED
#define __ALIGNED(x) __attribute__((aligned(x)))
#endif

#ifndef __WFI
#define __WFI() __COMPILER_BARRIER()
#endif

#ifndef __WFE
#define __WFE() __COMPILER_BARRIER()
#endif

#ifndef __SEV
#define __SEV() __COMPILER_BARRIER()
#endif

static inline uint32_t __REV(uint32_t value) {
    return __builtin_bswap32(value);
}

static inline uint32_t __REV16(uint32_t value) {
    return ((value & 0x00FF00FFU) << 8) | ((value & 0xFF00FF00U) >> 8);
}

static inline uint32_t __ROR(uint32_t op1, uint32_t op2) {
    op2 &= 31U;
    if (op2 == 0U) {
        return op1;
    }
    return (op1 >> op2) | (op1 << (32U - op2));
}

static inline uint32_t __RBIT(uint32_t value) {
    value = ((value & 0x55555555U) << 1) | ((value >> 1) & 0x55555555U);
    value = ((value & 0x33333333U) << 2) | ((value >> 2) & 0x33333333U);
    value = ((value & 0x0F0F0F0FU) << 4) | ((value >> 4) & 0x0F0F0F0FU);
    return __REV(value);
}

static inline uint32_t __CLZ(uint32_t value) {
    return value ? (uint32_t) __builtin_clz(value) : 32U;
}

static inline int32_t __SSAT(int32_t val, uint32_t sat) {
    if ((sat >= 1U) && (sat <= 32U)) {
        const int32_t max = (int32_t) ((1ULL << (sat - 1U)) - 1ULL);
        const int32_t min = -1 - max;
        if (val > max) {
            return max;
        }
        if (val < min) {
            return min;
        }
    }
    return val;
}

static inline uint32_t __USAT(int32_t val, uint32_t sat) {
    if (val < 0) {
        return 0U;
    }
    if (sat <= 31U) {
        const uint32_t max = (1U << sat) - 1U;
        if ((uint32_t) val > max) {
            return max;
        }
    }
    return (uint32_t) val;
}

#endif

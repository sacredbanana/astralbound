/* SPDX-License-Identifier: Unlicense */
#ifndef FPU_H
#define FPU_H
#include <stdint.h>
/* FPU executables (-m68881, or -DSPACE_FPU for host checks) replace 64-bit
   libgcc division and square-root loops with 6888x/040/060 arithmetic.
   Simulation results stay bit-identical; only rendering may round differently. */
#if defined(__HAVE_68881__) && !defined(ASTRALBOUND_FPU)
#define ASTRALBOUND_FPU 1
#endif
#ifdef ASTRALBOUND_FPU
/* The FPU converts only 32-bit integers. Splitting 64-bit values keeps every
   conversion inline instead of calling libgcc's soft-float helpers. */
static inline double fpu_unsigned32(uint32_t v) {
    return (double)(int32_t)(v ^ 0x80000000U) + 2147483648.0;
}
static inline double fpu_double(int64_t v) {
    return (double)(int32_t)(v >> 32) * 4294967296.0 + fpu_unsigned32((uint32_t)v);
}
static inline double fpu_udouble(uint64_t v) {
    return fpu_unsigned32((uint32_t)(v >> 32)) * 4294967296.0 + fpu_unsigned32((uint32_t)v);
}
/* Truncates toward zero, like C integer division; |d| must be below 2^62. */
static inline int64_t fpu_int64(double d) {
    int negative = d < 0;
    double high, low;
    uint32_t low_bits;
    int64_t value;
    if (negative)
        d = -d;
    high = (double)(int32_t)(d * (1.0 / 4294967296.0));
    low = d - high * 4294967296.0;
    low_bits = low >= 2147483648.0 ? (uint32_t)(int32_t)(low - 2147483648.0) + 0x80000000U
                                   : (uint32_t)(int32_t)low;
    value = (int64_t)(((uint64_t)(uint32_t)(int32_t)high << 32) | low_bits);
    return negative ? -value : value;
}
/* Saturating truncation for screen-space values that feed 32-bit fields. */
static inline int fpu_int(double d, int limit) {
    return d < -limit ? -limit : d > limit ? limit : (int)d;
}
#define fpu_sqrt(x) __builtin_sqrt(x)
#endif
#endif

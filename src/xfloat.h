/* SPDX-License-Identifier: Unlicense
   Integer mantissa/exponent numbers for the few per-frame and per-scanline
   calculations whose dynamic range no fixed-point format covers, such as a
   planet's outline seen from an AU away or from 200 m up. Frontier: Elite II
   kept the same split: shifted integers everywhere, floats only for planets.

   value = m * 2^e with 2^29 <= |m| < 2^30 (or m = 0): 30 significant bits.
   Pure integer arithmetic, so every CPU variant gets bit-identical results.
   Not for per-pixel use: a multiply costs one 32x32->64 product and a
   normalise, a divide one 64-bit division. */
#ifndef ASTRALBOUND_XFLOAT_H
#define ASTRALBOUND_XFLOAT_H
#include <stdint.h>
typedef struct {
    int32_t m, e;
} XFloat;
#define XF_ZERO ((XFloat){0, 0})
/* v * 2^s and floor(v / 2^s) without shifting negative values, which C99
   leaves undefined or implementation-defined. */
static inline int64_t xf_up(int64_t v, int s) { return v * ((int64_t)1 << s); }
static inline int64_t xf_down(int64_t v, int s) { return v >= 0 ? v >> s : ~(~v >> s); }
/* Normalise a wide mantissa. */
XFloat xf_make(int64_t m, int e);
static inline XFloat xf_int(int64_t v) { return xf_make(v, 0); }
/* v * 2^e, for fixed-point inputs. */
static inline XFloat xf_fixed(int64_t v, int e) { return xf_make(v, e); }
static inline XFloat xf_neg(XFloat a) { a.m = -a.m; return a; }
XFloat xf_mul(XFloat a, XFloat b);
XFloat xf_add(XFloat a, XFloat b);
XFloat xf_sub(XFloat a, XFloat b);
/* a / b, b nonzero. */
XFloat xf_div(XFloat a, XFloat b);
static inline int xf_sign(XFloat a) { return (a.m > 0) - (a.m < 0); }
int xf_less(XFloat a, XFloat b);
/* Square root of a non-negative value (negative inputs give zero). */
XFloat xf_sqrt(XFloat a);
/* round(a * 2^frac) saturated to +-limit. */
int64_t xf_to_fixed(XFloat a, int frac, int64_t limit);
#endif

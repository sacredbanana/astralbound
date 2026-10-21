/* SPDX-License-Identifier: Unlicense; see xfloat.h. Out of line: these run
   per frame and per scanline, and inlined copies cost the 68000 build tens of
   kilobytes of code. */
#include "xfloat.h"
/* One leading-zero count (BFFFO on a 68020 or later) and a single shift,
   done on 32-bit halves: variable 64-bit shifts are libgcc calls on 68k. */
static int xf_top(uint32_t v) { return 31 - __builtin_clz(v); }
XFloat xf_make(int64_t m, int e) {
    XFloat r;
    uint64_t a = m < 0 ? 0 - (uint64_t)m : (uint64_t)m;
    uint32_t hi = (uint32_t)(a >> 32), lo = (uint32_t)a, v;
    int top, s;
    if (hi) {
        top = 32 + xf_top(hi); s = top - 29;
        v = s >= 32 ? hi >> (s - 32) : lo >> s | hi << (32 - s);
        e += s;
    } else if (lo) {
        top = xf_top(lo);
        if (top >= 29) { v = lo >> (top - 29); e += top - 29; }
        else { v = lo << (29 - top); e -= 29 - top; }
    } else return XF_ZERO;
    r.m = m < 0 ? -(int32_t)v : (int32_t)v;
    r.e = e;
    return r;
}
XFloat xf_mul(XFloat a, XFloat b) {
    return xf_make((int64_t)a.m * b.m, a.e + b.e);
}
XFloat xf_add(XFloat a, XFloat b) {
    int d;
    if (!a.m) return b;
    if (!b.m) return a;
    if (a.e < b.e) { XFloat t = a; a = b; b = t; }
    d = a.e - b.e;
    if (d > 60) return a;
    /* 31 guard bits keep the smaller term's contribution exact enough:
       b * 2^(31 - d), exact for d <= 31 and floored beyond, in 32 bits. */
    return xf_make(xf_up(a.m, 31) + (d <= 31 ? xf_up(b.m, 31 - d) : (int64_t)(b.m >= 0 ? b.m >> (d - 31) : ~(~b.m >> (d - 31)))),
                   a.e - 31);
}
XFloat xf_sub(XFloat a, XFloat b) { return xf_add(a, xf_neg(b)); }
XFloat xf_div(XFloat a, XFloat b) {
    if (!a.m) return XF_ZERO;
    return xf_make(xf_up(a.m, 32) / b.m, a.e - b.e - 32);
}
int xf_less(XFloat a, XFloat b) { return xf_sign(xf_sub(a, b)) < 0; }
XFloat xf_sqrt(XFloat a) {
    uint64_t n, r = 0, bit = 1ULL << 62;
    int e;
    if (a.m <= 0) return XF_ZERO;
    /* Widen to 61 or 62 bits with an even exponent, then root bitwise. */
    n = (uint64_t)a.m << 32;
    e = a.e - 32;
    if (e & 1) { n >>= 1; e++; }
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return xf_make((int64_t)r, e / 2);
}
int64_t xf_to_fixed(XFloat a, int frac, int64_t limit) {
    int s = a.e + frac;
    int64_t v;
    if (!a.m) return 0;
    if (s >= 0) {
        if (s > 32) return a.m < 0 ? -limit : limit;
        v = xf_up(a.m, s);
    } else if (s < -40) return 0;
    else v = xf_down(xf_down(a.m, -s - 1) + 1, 1);
    return v > limit ? limit : v < -limit ? -limit : v;
}

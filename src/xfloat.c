/* SPDX-License-Identifier: Unlicense; see xfloat.h. Out of line: these run
   per frame and per scanline, and inlined copies cost the 68000 build tens of
   kilobytes of code. */
#include "xfloat.h"
XFloat xf_make(int64_t m, int e) {
    XFloat r;
    uint64_t a = (uint64_t)(m < 0 ? -m : m);
    int step;
    if (!a) return XF_ZERO;
    for (step = 32; step; step /= 2)
        if (a >> (29 + step)) { a >>= step; e += step; }
    for (step = 16; step; step /= 2)
        if (a < 1ULL << (30 - step)) { a <<= step; e -= step; }
    r.m = m < 0 ? -(int32_t)a : (int32_t)a;
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
    /* 31 guard bits keep the smaller term's contribution exact enough. */
    return xf_make(xf_up(a.m, 31) + xf_down(xf_up(b.m, 31), d), a.e - 31);
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

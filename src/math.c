/* SPDX-License-Identifier: Unlicense */
#include "astralbound.h"
#include "trig.h"
#include "fpu.h"
uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}
uint32_t sector_seed(uint32_t seed, Sector s) {
    return hash32(seed ^ hash32((uint32_t)s.x) ^ hash32((uint32_t)s.y + 0x9e3779b9U) ^
                  hash32((uint32_t)s.z + 0x85ebca6bU));
}
#ifdef ASTRALBOUND_FPU
/* FSQRT lands within one of the root; the integer correction keeps results
   bit-identical to the shift-subtract loop, so cross-CPU golden values hold. */
uint64_t isqrt64(uint64_t n) {
    double s = fpu_sqrt(fpu_udouble(n));
    uint32_t r = s >= 4294967295.0 ? 0xffffffffU
               : s >= 2147483648.0 ? (uint32_t)(int32_t)(s - 2147483648.0) + 0x80000000U
                                   : (uint32_t)(int32_t)s;
    while ((uint64_t)r * r > n)
        r--;
    while (r < 0xffffffffU && (uint64_t)(r + 1) * (r + 1) <= n)
        r++;
    return r;
}
#else
static uint32_t isqrt32(uint32_t n) {
    uint32_t r = 0, b = 1U << 30;
    while (b > n)
        b >>= 2;
    while (b) {
        if (n >= r + b) {
            n -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}
/* The floor root is unique, so any route to it is bit-identical: 32-bit
   digit-by-digit below 2^32; above, the root of the top 32 bits, one Newton
   step and an exact correction. 64-bit shift-subtract loops cost the 68k
   thousands of cycles. */
uint64_t isqrt64(uint64_t n) {
    uint32_t high = (uint32_t)(n >> 32), r;
    int k;
    if (!high) return isqrt32((uint32_t)n);
    k = (33 - __builtin_clz(high)) / 2; /* n >> 2k holds 31 or 32 bits */
    r = isqrt32((uint32_t)(n >> (2 * k))) << k;
    {
        uint64_t q;
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__)
        if (high < r) { /* DIVU.L: the quotient fits 32 bits */
            uint32_t hi = high, lo = (uint32_t)n;
            __asm__("divu.l %2,%0:%1" : "+d"(hi), "+d"(lo) : "d"(r) : "cc");
            q = lo;
        } else
#endif
            q = n / r;
        r = (uint32_t)(((uint64_t)r + q) / 2 > 0xffffffffU ? 0xffffffffU : ((uint64_t)r + q) / 2);
    }
    while ((uint64_t)r * r > n)
        r--;
    while (r < 0xffffffffU && (uint64_t)(r + 1) * (r + 1) <= n)
        r++;
    return r;
}
#endif
Vec vec(int64_t x, int64_t y, int64_t z) {
    Vec a = {x, y, z};
    return a;
}
Vec add(Vec a, Vec b) {
    return vec(a.x + b.x, a.y + b.y, a.z + b.z);
}
Vec sub(Vec a, Vec b) {
    return vec(a.x - b.x, a.y - b.y, a.z - b.z);
}
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__)
/* DIVS.L <ea>,Dr:Dq divides a 64-bit value by a 32-bit one in a single
   instruction, truncating toward zero as C does, when the quotient fits 32
   bits. The 68060 traps it, and the 68000 lacks it. */
#define NATIVE_DIVIDE 1
/* a n / d truncated, all three in 32 bits: one MULS.L and one DIVS.L. */
static int native_muldiv(int32_t a, int32_t n, int32_t d, int32_t *q) {
    int64_t p = (int64_t)a * n;
    uint64_t magnitude = p < 0 ? 0 - (uint64_t)p : (uint64_t)p;
    uint32_t divisor = d < 0 ? 0 - (uint32_t)d : (uint32_t)d;
    int32_t high = (int32_t)(uint32_t)((uint64_t)p >> 32), low = (int32_t)(uint32_t)p;
    if (!divisor || magnitude >= (uint64_t)divisor << 31) return 0;
    __asm__("divs.l %2,%0:%1" : "+d"(high), "+d"(low) : "d"(d) : "cc");
    *q = low;
    return 1;
}
static int fits32(int64_t v) { return v >= -INT32_MAX && v <= INT32_MAX; }
static int native_divide(int64_t value, int64_t d, int64_t *q) {
    uint64_t magnitude = value < 0 ? 0 - (uint64_t)value : (uint64_t)value;
    uint64_t divisor = d < 0 ? 0 - (uint64_t)d : (uint64_t)d;
    int32_t high, low;
    if (d < INT32_MIN || d > INT32_MAX || !divisor || magnitude >= divisor << 31) return 0;
    high = (int32_t)(uint32_t)((uint64_t)value >> 32); low = (int32_t)(uint32_t)value;
    __asm__("divs.l %2,%0:%1" : "+d"(high), "+d"(low) : "d"((int32_t)d) : "cc");
    *q = low;
    return 1;
}
#endif
/* Common fixed-point divisors compile to shifts on 68k, preserving signed
   truncation toward zero. General 64-bit division is reserved for other ratios. */
static int64_t divide(int64_t value, int64_t d) {
    switch (d) {
    case 1:
        return value;
    case 2:
        return value / 2;
    case HZ:
        return value / HZ;
    case FP:
        return value / FP;
    case 16384:
        return value / 16384;
    case 65536:
        return value / 65536;
    case (1LL << 30):
        return value / (1LL << 30);
    default:
#ifdef NATIVE_DIVIDE
        {
            int64_t q;
            if (native_divide(value, d, &q)) return q;
        }
#endif
        return value / d;
    }
}
/* Significant bits (1 for zero): a leading-zero count, BFFFO on 020+. */
static int bits(uint64_t x) {
    uint32_t high = (uint32_t)(x >> 32), low = (uint32_t)x;
    if (high) return 64 - __builtin_clz(high);
    return low ? 32 - __builtin_clz(low) : 1;
}
/* Multiply/divide without overflowing intermediate astronomical coordinates.
   Fast path covers ordinary local physics and all terrain work. */
int64_t muldiv(int64_t a, int64_t n, int64_t d) {
#ifdef NATIVE_DIVIDE
    int32_t native;
    if (fits32(a) && fits32(n) && fits32(d) && native_muldiv((int32_t)a, (int32_t)n, (int32_t)d, &native)) return native;
#endif
    uint64_t u = a < 0 ? (uint64_t)(-(a + 1)) + 1 : (uint64_t)a;
    uint64_t v = n < 0 ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
    uint64_t den = d < 0 ? (uint64_t)(-(d + 1)) + 1 : (uint64_t)d;
    uint64_t q, rem, fraction = 0, remainder = 0, bit;
    int negative = (a < 0) ^ (n < 0) ^ (d < 0);
    if (!den)
        return 0;
    if (!a || !n)
        return 0;
    if (n == 1)
        return divide(a, d);
    if (bits(u) + bits(v) <= 63) {
        /* Both factors in 32 bits: a widening MULS.L, not a libgcc call. */
        if (u <= INT32_MAX && v <= INT32_MAX)
            return divide((int64_t)(int32_t)a * (int32_t)n, d);
        return divide(a * n, d);
    }
    q = u / den;
    rem = u % den;
    for (bit = (uint64_t)1 << 63; bit; bit >>= 1) {
        fraction *= 2;
        remainder *= 2; /* Inputs here are bounded below 2^62 by the world limits. */
        if (v & bit)
            remainder += rem;
        /* The invariant remainder < den bounds this step below 3*den.
           Two subtractions replace expensive division/modulo on every bit. */
        if (remainder >= den) {
            remainder -= den;
            fraction++;
        }
        if (remainder >= den) {
            remainder -= den;
            fraction++;
        }
    }
    q = q * v + fraction;
    return negative ? -(int64_t)q : (int64_t)q;
}
Vec scale(Vec a, int64_t n, int64_t d) {
#ifdef NATIVE_DIVIDE
    /* Local geometry fits 32 bits: three native products and quotients
       instead of three general 64-bit calls, with identical truncation. */
    if (fits32(a.x) && fits32(a.y) && fits32(a.z) && fits32(n) && fits32(d)) {
        int32_t x, y, z;
        if (native_muldiv((int32_t)a.x, (int32_t)n, (int32_t)d, &x) && native_muldiv((int32_t)a.y, (int32_t)n, (int32_t)d, &y) &&
            native_muldiv((int32_t)a.z, (int32_t)n, (int32_t)d, &z))
            return vec(x, y, z);
    }
#endif
    return vec(muldiv(a.x, n, d), muldiv(a.y, n, d), muldiv(a.z, n, d));
}
/* Scale before squaring, keeping lengths safe across the supported local frame. */
int64_t length(Vec a) {
    int shift = 0;
    while (a.x > 1000000000LL || a.x < -1000000000LL || a.y > 1000000000LL || a.y < -1000000000LL ||
           a.z > 1000000000LL || a.z < -1000000000LL) {
        a.x /= 2;
        a.y /= 2;
        a.z /= 2;
        shift++;
    }
    /* Components now fit 31 bits: three widening MULS.L, not libgcc calls. */
    return (int64_t)isqrt64((uint64_t)((int64_t)(int32_t)a.x * (int32_t)a.x + (int64_t)(int32_t)a.y * (int32_t)a.y +
                                       (int64_t)(int32_t)a.z * (int32_t)a.z)) << shift;
}
Vec unit(Vec a, int64_t magnitude) {
    int64_t l = length(a);
    return l ? scale(a, magnitude, l) : vec(0, 0, 0);
}
/* Renormalize after expansion: a Q14 direction's tiny norm error would
   otherwise bury a surface port hundreds of metres inside an Earth-size body. */
Vec radial(Vec direction, int64_t radius) {
    int64_t l = length(direction);
    Vec p = l ? scale(direction, radius, l) : vec(0, 0, 0);
    l = length(p);
    return l ? scale(p, radius, l) : p;
}
int sini(int a) {
    return sine_table[(unsigned)a & 1023];
}
int cosi(int a) {
    return sini(a + 256);
}
Vec forward(int yaw, int pitch) {
    return vec((int64_t)sini(yaw) * cosi(pitch) / 16384, sini(pitch),
               (int64_t)cosi(yaw) * cosi(pitch) / 16384);
}
void aim_angles(Vec direction, int *yaw, int *pitch) {
    Vec d = unit(direction, 16384);
    int a, best_yaw = *yaw, best_pitch = 0;
    int64_t best = INT64_MIN;
    int64_t horizontal = isqrt64((uint64_t)(d.x * d.x + d.z * d.z));
    if (horizontal > 32) {
        for (a = 0; a < 1024; a += 4) {
            int64_t dot = d.x * sini(a) + d.z * cosi(a);
            if (dot > best) {
                best = dot;
                best_yaw = a;
            }
        }
        /* Refine the coarse search to individual table entries. Shallow
           bearings must not flatten to the horizon at four-entry precision. */
        {
            int coarse = best_yaw;
            for (a = coarse - 3; a <= coarse + 3; a++) {
                int64_t dot = d.x * sini(a) + d.z * cosi(a);
                if (dot > best) {
                    best = dot;
                    best_yaw = a & 1023;
                }
            }
        }
    }
    best = INT64_MIN;
    for (a = -256; a <= 256; a += 4) {
        int64_t dot = d.y * sini(a) + horizontal * cosi(a);
        if (dot > best) {
            best = dot;
            best_pitch = a;
        }
    }
    {
        int coarse = best_pitch;
        for (a = coarse - 3; a <= coarse + 3; a++) {
            int64_t dot;
            if (a < -256 || a > 256)
                continue;
            dot = d.y * sini(a) + horizontal * cosi(a);
            if (dot > best) {
                best = dot;
                best_pitch = a;
            }
        }
    }
    *yaw = best_yaw;
    *pitch = best_pitch;
}
/* All coordinates for this query are metres. Projection clamps to the segment. */
int swept_sphere(Vec start, Vec end, Vec center, int64_t radius) {
    Vec d = sub(end, start), r = sub(center, start);
    int64_t l = length(d), t;
    Vec n;
    if (!l)
        return length(r) <= radius;
    n = unit(d, 16384);
    t = (r.x * n.x + r.y * n.y + r.z * n.z) / 16384;
    if (t < 0)
        t = 0;
    if (t > l)
        t = l;
    return length(sub(r, scale(n, t, 16384))) <= radius;
}
/* Scale the larger factor by powers of four before multiplying. This
   extends braking roots past 2^32 without overflowing the squared speed. */
uint64_t isqrt_product(uint64_t a, uint64_t b) {
    int shift = 0;
    while (a && b > UINT64_MAX / a) {
        if (a > b) a /= 4;
        else b /= 4;
        shift++;
    }
    return isqrt64(a * b) << shift;
}
Vec guidance_step(Vec p, Vec v, Vec target, int64_t accel, int64_t maxspeed, int ticks) {
    Vec delta = sub(target, p), desired;
    int64_t d = length(delta), speed;
    int64_t envelope = muldiv(maxspeed, maxspeed, 2LL * (accel ? accel : 1));
    if (d > envelope + 4 * FP)
        speed = maxspeed;
    else if (maxspeed > INT32_MAX)
        speed = (int64_t)isqrt_product(2ULL * (accel / FP),
            (uint64_t)(d > 4 * FP ? (d - 4 * FP) / FP : 0)) * FP;
    else
        speed = isqrt64((uint64_t)(2LL * accel * (d > 4 * FP ? d - 4 * FP : 0)));
    if (speed > maxspeed)
        speed = maxspeed; /* Conservative braking envelope, with a four-metre capture zone. */
    speed = speed * 3 / 4;
    desired = scale(sub(speed <= INT32_MAX ? unit(delta, (int)speed) : radial(delta, speed), v), HZ, ticks);
    return length(desired) > accel ? unit(desired, accel) : desired;
}
Vec guidance(Vec p, Vec v, Vec target, int64_t accel, int64_t maxspeed) {
    return guidance_step(p, v, target, accel, maxspeed, 1);
}
Vec avoidance(Vec r, Vec v, int64_t accel, int radius) {
    int64_t dot, t = 0, closing;
    Vec direction;
    if (length(r) > 1000000000LL)
        return vec(0, 0, 0);
    direction = unit(v, 16384);
    closing = length(v);
    dot = r.x * direction.x + r.y * direction.y + r.z * direction.z;
    Vec closest, side;
    if (closing && dot < 0)
        t = (-dot * HZ) / (closing * 16384);
    if (t > 12 * HZ)
        t = 12 * HZ;
    closest = add(r, scale(v, t, HZ));
    closing = length(v);
    if (length(closest) > radius &&
        (uint64_t)closing < isqrt_product(2ULL * (accel ? accel : 1),
                                        (uint64_t)(length(closest) - radius)))
        return vec(0, 0, 0);
    if (dot >= 0 && length(r) > radius)
        return vec(0, 0, 0); /* Deterministic lateral break for a perfectly head-on approach. */
    side = vec(r.z, 0, -r.x);
    if (length(side) < FP)
        side = vec(FP, 0, 0);
    return unit(add(unit(scale(r, -1, 1), 16384), unit(side, 32768)), accel);
}

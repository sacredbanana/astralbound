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
uint64_t isqrt64(uint64_t n) {
    uint64_t r = 0, b = (uint64_t)1 << 62;
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
        return value / d;
    }
}
static int bits(uint64_t x) {
    uint32_t word = (uint32_t)x;
    int count = 0;
    if (x >> 32) {
        word = (uint32_t)(x >> 32);
        count = 32;
    }
    if (word >> 16) {
        word >>= 16;
        count += 16;
    }
    if (word >> 8) {
        word >>= 8;
        count += 8;
    }
    if (word >> 4) {
        word >>= 4;
        count += 4;
    }
    if (word >> 2) {
        word >>= 2;
        count += 2;
    }
    if (word >> 1)
        count++;
    return count + 1;
}
/* Multiply/divide without overflowing intermediate astronomical coordinates.
   Fast path covers ordinary local physics and all terrain work. */
int64_t muldiv(int64_t a, int64_t n, int64_t d) {
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
    if (bits(u) + bits(v) <= 63)
        return divide(a * n, d);
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
    return (int64_t)isqrt64((uint64_t)(a.x * a.x + a.y * a.y + a.z * a.z)) << shift;
}
Vec unit(Vec a, int magnitude) {
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
Vec guidance_step(Vec p, Vec v, Vec target, int accel, int64_t maxspeed, int ticks) {
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
Vec guidance(Vec p, Vec v, Vec target, int accel, int64_t maxspeed) {
    return guidance_step(p, v, target, accel, maxspeed, 1);
}
Vec avoidance(Vec r, Vec v, int accel, int radius) {
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

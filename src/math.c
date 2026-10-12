/* SPDX-License-Identifier: Unlicense */
#include "space.h"
#include "trig.h"
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
Vec scale(Vec a, int64_t n, int64_t d) {
    return vec(a.x * n / d, a.y * n / d, a.z * n / d);
}
/* Scale before squaring, keeping lengths safe across the supported local frame. */
int64_t length(Vec a) {
    int shift = 0;
    while (a.x > 1000000000LL || a.x < -1000000000LL || a.y > 1000000000LL || a.y < -1000000000LL ||
           a.z > 1000000000LL || a.z < -1000000000LL) {
        a = scale(a, 1, 2);
        shift++;
    }
    return (int64_t)isqrt64((uint64_t)(a.x * a.x + a.y * a.y + a.z * a.z)) << shift;
}
Vec unit(Vec a, int magnitude) {
    int64_t l = length(a);
    return l ? scale(a, magnitude, l) : vec(0, 0, 0);
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
    }
    best = INT64_MIN;
    for (a = -256; a <= 256; a += 4) {
        int64_t dot = d.y * sini(a) + horizontal * cosi(a);
        if (dot > best) {
            best = dot;
            best_pitch = a;
        }
    }
    *yaw = best_yaw;
    *pitch = best_pitch;
}
/* All coordinates for this query are metres. Projection clamps to the segment. */
int swept_sphere(Vec start, Vec end, Vec center, int radius) {
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
Vec guidance(Vec p, Vec v, Vec target, int accel, int maxspeed) {
    Vec delta = sub(target, p), desired;
    int64_t d = length(delta),
            speed = isqrt64((uint64_t)(2LL * accel * (d > 4 * FP ? d - 4 * FP : 0)));
    if (speed > maxspeed)
        speed = maxspeed; /* Conservative braking envelope, with a four-metre capture zone. */
    speed = speed * 3 / 4;
    desired = scale(sub(unit(delta, (int)speed), v), HZ, 1);
    return length(desired) > accel ? unit(desired, accel) : desired;
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
    if (length(closest) > radius + closing * closing / (2 * (accel ? accel : 1)))
        return vec(0, 0, 0);
    if (dot >= 0 && length(r) > radius)
        return vec(0, 0, 0); /* Deterministic lateral break for a perfectly head-on approach. */
    side = vec(r.z, 0, -r.x);
    if (length(side) < FP)
        side = vec(FP, 0, 0);
    return unit(add(unit(scale(r, -1, 1), 16384), unit(side, 32768)), accel);
}

/* SPDX-License-Identifier: Unlicense
   Shared ring-plane geometry. A 45-degree cubic arc uses tangent handles
   of 4/3 tan(pi/16), approximately 0.2652 of its radius. No projection,
   framebuffer or backend state lives here. Ring-plane coordinates use
   16384 units per planetary radius. */
#ifndef ASTRALBOUND_PLANET_RING_H
#define ASTRALBOUND_PLANET_RING_H
#include "astralbound.h"
static inline void planet_ring_control(int32_t radius, int angle, int handle,
                                       int direction, int32_t *x, int32_t *y) {
    int32_t s = sini(angle), c = cosi(angle);
    *x = s * radius / 16384;
    *y = c * radius / 16384;
    if (handle) {
        int32_t h = radius / 16 * 4345 / 1024 * handle * direction;
        *x += c * h / 16384;
        *y -= s * h / 16384;
    }
}
#endif

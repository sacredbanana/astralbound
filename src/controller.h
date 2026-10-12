/* SPDX-License-Identifier: Unlicense; normalized seven-button controller. */
#ifndef SPACE_CONTROLLER_H
#define SPACE_CONTROLLER_H
#include "flight_input.h"
enum { PAD_FIRE=1, PAD_BRAKE=2, PAD_TARGET=4, PAD_AUTO=8,
       PAD_THRUST=16, PAD_REVERSE=32, PAD_PAUSE=64 };
typedef struct { unsigned held; int x, y, calibrated; } Controller;
/* The centre is explicit: press C with the analogue stick released. */
static inline void controller_calibrate(Controller *p, int x, int y) {
    p->x = x; p->y = y; p->calibrated = 1;
}
static inline int analogue_axis(int value, int centre) {
    int d = value - centre, extent;
    if (d >= -12 && d <= 12) return 0;
    extent = d > 0 ? 255 - centre - 12 : centre - 12;
    if (extent < 1) return d > 0 ? 256 : -256;
    d = (d > 0 ? d - 12 : d + 12) * 256 / extent;
    return d > 256 ? 256 : d < -256 ? -256 : d;
}
static inline void controller_apply(Controller *p, Game *g, Controls *c, unsigned buttons,
                                    int x, int y, int analogue) {
    unsigned pressed = buttons & ~p->held;
    p->held = buttons;
    if (pressed & PAD_PAUSE) game_action(g, ' ');
    if (g->page != FLIGHT || g->paused) return;
    if (analogue && p->calibrated) {
        c->axis_yaw = analogue_axis(x, p->x);
        c->axis_pitch = -analogue_axis(y, p->y);
    }
    c->fire |= !!(buttons & PAD_FIRE);
    c->thrust = flight_axis(c->thrust + !!(buttons & PAD_THRUST) - !!(buttons & PAD_REVERSE), 0);
    if (pressed & PAD_TARGET) game_action(g, 'r');
    if (pressed & PAD_AUTO) game_action(g, 'p');
    if (buttons & PAD_BRAKE) { g->throttle = 0; c->thrust = 0; }
}
#endif

/* SPDX-License-Identifier: Unlicense; normalized seven-button controller. */
#ifndef ASTRALBOUND_CONTROLLER_H
#define ASTRALBOUND_CONTROLLER_H
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
/* On foot one analogue stick walks and turns; with a second (analogue == 2,
   look_x/look_y on the same 0..255 scale centred at 128) the first walks and
   sidesteps and the second looks. The shoulders sidestep, green/Y and
   yellow/X look up and down and blue/B boards. Fire belongs to the platform's
   FootStick, which also reads the directional pad. */
static inline void controller_foot(Controller *p, Game *g, Controls *c, unsigned buttons,
                                   unsigned pressed, int x, int y, int analogue,
                                   int look_x, int look_y) {
    if (analogue && p->calibrated) {
        int ax = analogue_axis(x, p->x);
        c->axis_walk = -analogue_axis(y, p->y);
        if (analogue == 2) {
            c->axis_strafe = ax;
            c->axis_roll = analogue_axis(look_x, 128);
            c->axis_pitch = analogue_axis(look_y, 128);
        } else c->axis_roll = ax;
    }
    c->strafe = flight_axis(c->strafe + !!(buttons & PAD_THRUST) - !!(buttons & PAD_REVERSE), 0);
    c->pitch = flight_axis(c->pitch + !!(buttons & PAD_TARGET) - !!(buttons & PAD_AUTO), 0);
    if (pressed & PAD_BRAKE) game_action(g, 'k');
}
static inline void controller_apply(Controller *p, Game *g, Controls *c, unsigned buttons,
                                    int x, int y, int analogue, int look_x, int look_y) {
    unsigned pressed = buttons & ~p->held;
    p->held = buttons;
    if (pressed & PAD_PAUSE) game_action(g, ' ');
    if (g->page != FLIGHT || g->paused) return;
    if (g->on_foot) {
        controller_foot(p, g, c, buttons, pressed, x, y, analogue, look_x, look_y);
        return;
    }
    if (analogue && p->calibrated) {
        c->axis_roll = analogue_axis(x, p->x);
        c->axis_pitch = analogue_axis(y, p->y);
    }
    c->fire |= !!(buttons & PAD_FIRE);
    c->thrust = flight_axis(c->thrust + !!(buttons & PAD_THRUST) - !!(buttons & PAD_REVERSE), 0);
    if (pressed & PAD_TARGET) game_action(g, 'r');
    if (pressed & PAD_AUTO) game_action(g, 'p');
    if (buttons & PAD_BRAKE) { g->throttle = 0; c->thrust = 0; }
}
#endif

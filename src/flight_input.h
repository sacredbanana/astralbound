/* SPDX-License-Identifier: Unlicense */
#ifndef FLIGHT_INPUT_H
#define FLIGHT_INPUT_H
/* Right-drag acts as a virtual stick. Release to recenter. */
typedef struct { int x, y; } MouseStick;
static inline int flight_axis(int v, int deadzone) {
    return v > deadzone ? 1 : v < -deadzone ? -1 : 0;
}
static inline void mouse_stick(MouseStick *m, int dx, int dy, int held) {
    if (!held) { m->x = m->y = 0; return; }
    m->x += dx; m->y += dy;
    if (m->x > 64) m->x = 64;
    if (m->x < -64) m->x = -64;
    if (m->y > 64) m->y = 64;
    if (m->y < -64) m->y = -64;
}
static inline void flight_input(Controls *c, const MouseStick *m, int roll, int pitch, int fire) {
    c->roll = flight_axis(c->roll + roll + flight_axis(m->x, 8), 0);
    c->pitch = flight_axis(c->pitch + pitch + flight_axis(m->y, 8), 0);
    c->fire = c->fire || fire;
}
/* On foot a digital stick walks: up/down walk, left/right turn. Holding fire
   shifts it to look (up/down) and sidestep (left/right), so a one-button
   stick reaches every move; a tap of fire alone returns 1 to use what is in
   reach. Directions are -1..1 with down positive, as joystick_pitch. */
typedef struct { int held, shifted; } FootStick;
static inline int foot_stick(FootStick *s, Controls *c, int x, int y, int fire) {
    int tap = 0;
    if (fire) {
        s->shifted |= x || y;
        c->strafe = flight_axis(c->strafe + x, 0);
        c->pitch = flight_axis(c->pitch + y, 0);
    } else {
        tap = s->held && !s->shifted;
        s->shifted = 0;
        c->roll = flight_axis(c->roll + x, 0);
        c->thrust = flight_axis(c->thrust - y, 0);
    }
    s->held = fire;
    return tap;
}
/* Amiga quadrature joystick directions: vertical switches XOR adjacent bits. */
static inline int joystick_roll(unsigned joy) {
    return ((joy >> 1) & 1) - ((joy >> 9) & 1);
}
static inline int joystick_pitch(unsigned joy) {
    return ((joy ^ (joy >> 1)) & 1) - (((joy >> 8) ^ (joy >> 9)) & 1);
}
#endif

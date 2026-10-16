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
    c->pitch = flight_axis(c->pitch + pitch - flight_axis(m->y, 8), 0);
    c->fire = c->fire || fire;
}
/* Amiga quadrature joystick directions: vertical switches XOR adjacent bits. */
static inline int joystick_roll(unsigned joy) {
    return ((joy >> 1) & 1) - ((joy >> 9) & 1);
}
static inline int joystick_pitch(unsigned joy) {
    return (((joy >> 8) ^ (joy >> 9)) & 1) - ((joy ^ (joy >> 1)) & 1);
}
#endif

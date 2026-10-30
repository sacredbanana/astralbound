/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_TOUR_H
#define ASTRALBOUND_TOUR_H
#include "intro.h"
/* The Solar System tour, started from the setup menu: one unbroken camera
   flight past the Sun, every planet and the major moons, with facts typed
   beneath a letterboxed view. Like the title sequence it borrows the
   commander's Game as a stage with world time frozen and never runs flight
   physics; ending or skipping leaves a freshly initialised Game. */
#define TOUR_STOPS 33
#define TOUR_LINES 4
#define TOUR_TEXT 50 /* characters per fact: 300 pixels at 320 x 200 */
#define TOUR_TITLE_TICKS 2 /* per title letter; facts type a letter a tick */
typedef struct {
    const char *body; /* catalog name, "Sun" for the star, NULL for deep space */
    const char *title, *subtitle;
    const char *fact[TOUR_LINES];
    /* Camera: distance in tenths of a radius (deep space: of an AU), degrees
       round the frame pole from the sunward direction (moons: turned from
       the planet's far side towards the Sun), its change over the hold and
       the elevation. */
    short reach, azimuth, drift, elevation;
    /* A landmark the camera faces, degrees north and east on the body (when
       face): the tour's moment is chosen so it is in afternoon light. */
    short face, lat, lon;
} TourStop;
extern const TourStop tour_stops[TOUR_STOPS];
typedef struct {
    Intro view; /* camera, glows and fade for the cinematic renderer */
    int tick, length, running;
    uint32_t seed;
    /* Presentation for the current tick. */
    int stop;     /* stop being shown, or departed from while in transit */
    int shown;    /* ticks into the stop's hold: drives the typing */
    int transit;  /* ticks into the flight onwards, 0 while holding */
    int flight;   /* ticks the whole flight takes */
    int fade;     /* ordered dither 0..16 over the whole frame */
    int32_t pan;  /* in transit: the yaw still to turn, unwrapped tick by tick */
    Vec up;       /* Q30 camera up, carried from tick to tick */
    int64_t distance, speed; /* in transit: metres to the next surface, m/s */
    char stats[64]; /* measured radius and gravity of the stop's body */
} Tour;
void tour_start(Tour *t, Game *g, uint32_t seed);
/* Advances by real ticks; returns 0 once finished. */
int tour_frame(Tour *t, Game *g, int ticks);
/* Ends the tour at once (skip) and restores a fresh Game. */
void tour_finish(Tour *t, Game *g);
int tour_skippable(const Tour *t);
/* First tick of each stop's hold, for previews and tests. */
int tour_stop_tick(int stop);
void render_tour(const Tour *t, const Game *g, Frame *f);
#endif

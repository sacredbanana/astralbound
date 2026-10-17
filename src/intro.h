/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_INTRO_H
#define ASTRALBOUND_INTRO_H
#include "astralbound.h"
/* The opening title sequence. It borrows the commander's Game as its stage:
   real systems, ports, hulls and effects, positioned by a fixed script rather
   than by physics. Ending or skipping leaves a freshly initialised Game. */
#define INTRO_TICKS (HZ * 198)
#define INTRO_BEAMS 16
#define INTRO_GLOWS 32
typedef struct { Vec a, b; int colour; } IntroBeam; /* Q8 system positions */
typedef struct { Vec p; int64_t radius; int core, halo; } IntroGlow; /* radius in metres */
/* A directed hull with full attitude, for ships that bank (NPC hulls cannot). */
typedef struct { Vec pos, front, up; int ship; } IntroHull; /* Q8 position, Q14 axes */
typedef struct {
    int tick, running;
    uint32_t seed;
    /* Presentation for the current tick, rebuilt by every script step. */
    Vec eye; /* Q8 system position */
    int yaw, pitch, roll;
    int hero; /* draw the commander's hull at g->pos */
    int escort; /* draw the ally hull */
    IntroHull ally;
    int cockpit, guns; /* crosshair; gun lasers converging on it */
    int letterbox, fade, flash; /* fade/flash: ordered dither 0..16 */
    int beams, glows;
    IntroBeam beam[INTRO_BEAMS];
    IntroGlow glow[INTRO_GLOWS];
    const char *caption;
    int caption_chars;
    char caption_text[40];
    int title; /* ticks into the title card, 0 before it */
} Intro;
void intro_start(Intro *in, Game *g, uint32_t seed);
/* Advances the script by real ticks; returns 0 once finished. */
int intro_frame(Intro *in, Game *g, int ticks);
/* Ends the sequence at once (skip) and restores a fresh Game. */
void intro_finish(Intro *in, Game *g);
/* Any key, button or fire during the sequence skips it. */
int intro_skippable(const Intro *in);
void render_intro(const Intro *in, const Game *g, Frame *f);
#endif

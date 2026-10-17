/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_MUSIC_H
#define ASTRALBOUND_MUSIC_H
#include "astralbound.h"
enum { MUSIC_BATTLE, MUSIC_PLANETARY, MUSIC_SPACE, MUSIC_DOCKED, MUSIC_GAME_OVER, MUSIC_INTRO, MUSIC_SCENES };
/* One player, shared by the platform's single audio mixer. Load/close with the
   OS active and the mixer stopped. Observe/mix require the host's audio lock. */
int music_load(const char *root, uint32_t seed);
void music_close(void);
int music_context(const Game *g);
void music_observe(const Game *g);
void music_mix(int8_t *out, unsigned count);
const char *music_track(void);
#endif

/* SPDX-License-Identifier: Unlicense; procedural engine and effects. */
#ifndef ASTRALBOUND_AUDIO_H
#define ASTRALBOUND_AUDIO_H
#include "astralbound.h"
#define AUDIO_RATE 8000
#define EFFECT_SAMPLES 4000
enum { SOUND_NONE, SOUND_LASER, SOUND_HIT, SOUND_EXPLOSION, SOUND_DOCK, SOUND_JUMP, SOUND_HYPERSPACE, SOUND_COUNT };
typedef struct {
    uint32_t cursor, shots, seed;
    int effect, effect_pos, hull, shield, kills, docked, throttle, active, paused, hyperspace;
} SpaceAudio;
void audio_init(SpaceAudio *s, const Game *g);
int audio_observe(SpaceAudio *s, const Game *g);
/* Samples in each effect; the charge and tunnel span their sequence phases. */
unsigned audio_effect_length(int effect);
int8_t audio_effect(int effect, unsigned sample);
int8_t audio_engine(unsigned sample);
void audio_mix(SpaceAudio *s, int8_t *out, unsigned count);
#endif

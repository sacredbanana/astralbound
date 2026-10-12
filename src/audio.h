/* SPDX-License-Identifier: Unlicense; original integer PCM score and effects. */
#ifndef SPACE_AUDIO_H
#define SPACE_AUDIO_H
#include "space.h"
#define AUDIO_RATE 8000
#define MUSIC_SAMPLES 64000
#define EFFECT_SAMPLES 4000
enum { SOUND_NONE, SOUND_LASER, SOUND_HIT, SOUND_EXPLOSION, SOUND_DOCK, SOUND_JUMP, SOUND_COUNT };
typedef struct {
    uint32_t cursor, shots, seed;
    int effect, effect_pos, hull, shield, kills, docked, throttle, active, paused;
} SpaceAudio;
void audio_init(SpaceAudio *s, const Game *g);
int audio_observe(SpaceAudio *s, const Game *g);
int8_t audio_music(uint32_t sample);
int8_t audio_effect(int effect, unsigned sample);
int8_t audio_engine(unsigned sample);
void audio_mix(SpaceAudio *s, int8_t *out, unsigned count);
#endif

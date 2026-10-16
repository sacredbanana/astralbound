/* SPDX-License-Identifier: Unlicense. Procedural effects and MOD music mix. */
#include "audio.h"
#include "music.h"
#include <string.h>
static int triangle(unsigned phase) {
    int p = phase & 255;
    return p < 128 ? p * 2 - 127 : 383 - p * 2;
}
int8_t audio_engine(unsigned sample) {
    return (int8_t)(triangle(sample * 3) / 3 + (int)(hash32(sample & 255) & 31) - 16);
}
int8_t audio_effect(int effect, unsigned t) {
    int envelope, value;
    unsigned duration;
    if (effect <= SOUND_NONE || effect >= SOUND_COUNT) return 0;
    duration = effect == SOUND_LASER ? 1200 : EFFECT_SAMPLES;
    if (t >= duration) return 0;
    envelope = (int)(duration - t) * 96 / (int)duration;
    switch (effect) {
    case SOUND_LASER: value = triangle(t * (80 - t / 20)); break;
    case SOUND_HIT: case SOUND_EXPLOSION:
        value = (int)(hash32(t / (effect == SOUND_EXPLOSION ? 4 : 1)) & 255) - 128; break;
    case SOUND_DOCK: value = triangle(t * (t < 1300 ? 17 : t < 2600 ? 21 : 26)); break;
    default: value = triangle(t * (4 + t / 80)); break;
    }
    return (int8_t)(value * envelope / 128);
}
void audio_init(SpaceAudio *s, const Game *g) {
    memset(s, 0, sizeof(*s));
    s->shots = g->shots; s->seed = g->system.seed;
    s->hull = g->hull; s->shield = g->shield; s->kills = g->kills; s->docked = g->docked;
    s->effect_pos = EFFECT_SAMPLES;
}
int audio_observe(SpaceAudio *s, const Game *g) {
    int effect = SOUND_NONE;
    if (s->shots != g->shots) effect = SOUND_LASER;
    if (g->docked != s->docked) effect = SOUND_DOCK;
    if (s->seed != g->system.seed) effect = SOUND_JUMP;
    if (g->hull < s->hull || g->shield < s->shield) effect = SOUND_HIT;
    if (g->kills > s->kills || (g->dead && s->hull > 0)) effect = SOUND_EXPLOSION;
    s->shots = g->shots; s->seed = g->system.seed;
    s->hull = g->hull; s->shield = g->shield; s->kills = g->kills; s->docked = g->docked;
    s->active = g->page == FLIGHT && g->docked == -1 && !g->dead;
    s->paused = g->paused;
    music_observe(g);
    s->throttle = g->throttle < 4000 ? g->throttle : 4000;
    if (effect) { s->effect = effect; s->effect_pos = 0; }
    return effect;
}
void audio_mix(SpaceAudio *s, int8_t *out, unsigned count) {
    unsigned i;
    music_mix(out, count);
    for (i = 0; i < count; i++) {
        int value = 0;
        if (!s->paused) {
            value = out[i];
            if (s->active) value += audio_engine(s->cursor & 255) * s->throttle / 12000;
            value += audio_effect(s->effect, (unsigned)s->effect_pos);
            if (s->effect_pos < EFFECT_SAMPLES) s->effect_pos++;
            s->cursor++;
        }
        out[i] = (int8_t)(value > 127 ? 127 : value < -128 ? -128 : value);
    }
}

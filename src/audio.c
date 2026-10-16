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
unsigned audio_effect_length(int effect) {
    if (effect <= SOUND_NONE || effect >= SOUND_COUNT) return 0;
    if (effect == SOUND_LASER) return 1200;
    if (effect == SOUND_EXPLOSION) return AUDIO_RATE * 3 / 2;
    if (effect == SOUND_HYPERSPACE) return AUDIO_RATE * HYPER_CHARGE / HZ;
    if (effect == SOUND_JUMP) return AUDIO_RATE * HYPER_TUNNEL / HZ;
    return EFFECT_SAMPLES;
}
int8_t audio_effect(int effect, unsigned t) {
    int envelope, value;
    unsigned duration = audio_effect_length(effect);
    if (t >= duration) return 0;
    envelope = (int)(duration - t) * 96 / (int)duration;
    switch (effect) {
    case SOUND_LASER: value = triangle(t * (80 - t / 20)); break;
    case SOUND_HIT:
        value = (int)(hash32(t) & 255) - 128; break;
    case SOUND_EXPLOSION:
        /* Sharp crack, descending bass and a rolling noisy tail. */
        value = ((int)(hash32(t / 5) & 255) - 128) * 2 / 3 +
                triangle(t * (9 - t * 6 / duration)) / 2;
        break;
    case SOUND_DOCK: value = triangle(t * (t < 1300 ? 17 : t < 2600 ? 21 : 26)); break;
    case SOUND_HYPERSPACE: {
        /* Spool-up: a rising root and fifth that throb faster as it charges. */
        unsigned pitch = 4 + t * 24 / duration, pulse = t * (2 + t * 6 / duration) / 64;
        value = triangle(t * pitch) * 2 / 3 + triangle(t * pitch * 3 / 2) / 3;
        envelope = 20 + (int)(t * 76 / duration);
        if (pulse & 64) envelope = envelope * 3 / 4;
        break;
    }
    case SOUND_JUMP: {
        /* Tunnel: a breaking-wave hiss on entry over a falling, rumbling
           drone, with a second rush swelling towards the drop-out. */
        unsigned swell = duration - 8000;
        int hiss = t < 6000 ? (int)(6000 - t) * 64 / 6000 :
                   t > swell ? (int)(t - swell) * 48 / 8000 : 0;
        value = ((int)(hash32(t / 3) & 255) - 128) * hiss / 64 +
                triangle(t * (14 - t * 9 / duration)) * (160 + triangle(t / 24)) / 384;
        envelope = t + 2000 < duration ? 96 : (int)(duration - t) * 96 / 2000;
        break;
    }
    default: value = triangle(t * (4 + t / 80)); break;
    }
    value = value * envelope / 128;
    return (int8_t)(value > 127 ? 127 : value < -128 ? -128 : value);
}
void audio_init(SpaceAudio *s, const Game *g) {
    memset(s, 0, sizeof(*s));
    s->shots = g->shots; s->seed = g->system.seed;
    s->hull = g->hull; s->shield = g->shield; s->kills = g->kills; s->docked = g->docked;
    s->hyperspace = g->hyperspace != 0;
    s->blast_serial = g->blast_serial;
}
int audio_observe(SpaceAudio *s, const Game *g) {
    int effect = SOUND_NONE;
    if (s->shots != g->shots) effect = SOUND_LASER;
    if (g->docked != s->docked) effect = SOUND_DOCK;
    if (g->hyperspace && !s->hyperspace) effect = SOUND_HYPERSPACE;
    if (s->seed != g->system.seed) effect = SOUND_JUMP;
    /* An aborted charge cuts its spool-up rather than letting it finish. */
    if (!g->hyperspace && s->hyperspace && s->effect == SOUND_HYPERSPACE && s->seed == g->system.seed)
        s->effect = SOUND_NONE;
    if (g->hull < s->hull || g->shield < s->shield) effect = SOUND_HIT;
    if ((g->blast_serial && g->blast_serial != s->blast_serial) ||
        (!g->death_started && g->dead && s->hull > 0))
        effect = SOUND_EXPLOSION;
    s->blast_serial = g->blast_serial;
    s->shots = g->shots; s->seed = g->system.seed; s->hyperspace = g->hyperspace != 0;
    s->hull = g->hull; s->shield = g->shield; s->kills = g->kills; s->docked = g->docked;
    s->active = g->page == FLIGHT && g->docked == -1 && !g->dead;
    s->paused = g->paused;
    music_observe(g);
    /* A dry tank has no engine note, whatever the set speed. */
    s->throttle = g->fuel <= 0 ? 0 : g->throttle < 4000 ? g->throttle : 4000;
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
            if ((unsigned)s->effect_pos < audio_effect_length(s->effect)) s->effect_pos++;
            s->cursor++;
        }
        out[i] = (int8_t)(value > 127 ? 127 : value < -128 ? -128 : value);
    }
}

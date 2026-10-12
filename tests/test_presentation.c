/* SPDX-License-Identifier: Unlicense */
#include "space.h"
#include "audio.h"
#include "controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
unsigned long __stack = 131072;
static Game g;
static void sound_tests(void) {
    SpaceAudio a, b;
    int8_t whole[8192], chunks[8192];
    unsigned i, effect;
    int nonzero = 0;
    game_init(&g, 0x53504143);
    audio_init(&a, &g); b = a;
    assert(audio_observe(&a, &g) == SOUND_NONE);
    g.shots++;
    assert(audio_observe(&a, &g) == SOUND_LASER);
    assert(audio_observe(&a, &g) == SOUND_NONE);
    g.hull--;
    assert(audio_observe(&a, &g) == SOUND_HIT);
    g.kills++;
    assert(audio_observe(&a, &g) == SOUND_EXPLOSION);
    g.docked = -1;
    assert(audio_observe(&a, &g) == SOUND_DOCK);
    g.system.seed++;
    assert(audio_observe(&a, &g) == SOUND_JUMP);
    b = a;
    audio_mix(&a, whole, sizeof(whole));
    for (i = 0; i < sizeof(chunks); i += 256) audio_mix(&b, chunks + i, 256);
    assert(!memcmp(whole, chunks, sizeof(whole)));
    assert(a.cursor == b.cursor && a.effect_pos == b.effect_pos);
    a.paused = 1;
    audio_mix(&a, whole, sizeof(whole));
    for (i = 0; i < sizeof(whole); i++) assert(whole[i] == 0);
    for (i = 0; i < MUSIC_SAMPLES; i++) {
        assert(audio_music(i) == audio_music(i + MUSIC_SAMPLES));
        nonzero |= audio_music(i) != 0;
    }
    assert(nonzero);
    for (effect = 1; effect < SOUND_COUNT; effect++) {
        nonzero = 0;
        for (i = 0; i < EFFECT_SAMPLES; i++) nonzero |= audio_effect(effect, i) != 0;
        assert(nonzero && audio_effect(effect, EFFECT_SAMPLES) == 0);
    }
    puts("PASS original PCM music, effects, event priority, chunk continuity and pause");
}
static void controller_tests(void) {
    Controller p = {0};
    Controls c = {0};
    int i, yaw;
    game_init(&g, 0x53504143);
    assert(analogue_axis(128,128) == 0 && analogue_axis(140,128) == 0);
    assert(analogue_axis(0,128) == -256 && analogue_axis(255,128) == 256);
    assert(analogue_axis(0,0) == 0 && analogue_axis(255,255) == 0);
    controller_calibrate(&p, 100, 150);
    g.docked = -1; g.pos = add(g.pos, vec(0, 18000 * FP, 0)); g.page = FLIGHT;
    controller_apply(&p, &g, &c, PAD_FIRE | PAD_THRUST, 255, 0, 1);
    assert(c.axis_yaw == 256 && c.axis_pitch == 256 && c.fire && c.thrust == 1);
    c = (Controls){0}; c.axis_yaw = 64;
    yaw = g.yaw;
    for (i = 0; i < 4; i++) game_tick(&g, c);
    assert(g.yaw == ((yaw + 3) & 1023));
    c = (Controls){0}; c.yaw = -1; c.axis_yaw = 256;
    game_tick(&g, c);
    assert(g.yaw == yaw); /* digital wins */
    controller_apply(&p, &g, &c, PAD_PAUSE, 0, 0, 0); assert(g.paused);
    controller_apply(&p, &g, &c, PAD_PAUSE, 0, 0, 0); assert(g.paused);
    controller_apply(&p, &g, &c, 0, 0, 0, 0);
    controller_apply(&p, &g, &c, PAD_PAUSE, 0, 0, 0); assert(!g.paused);
    g.throttle = 4000;
    controller_apply(&p, &g, &c, PAD_BRAKE, 0, 0, 0); assert(!g.throttle);
    g.page = MARKET; c = (Controls){0};
    controller_apply(&p, &g, &c, PAD_FIRE | PAD_THRUST, 255, 0, 1);
    assert(!c.fire && !c.thrust && !c.axis_yaw);
    puts("PASS analogue calibration/rates, digital precedence, button edges and menu isolation");
}
int main(void) { sound_tests(); controller_tests(); return 0; }

/* SPDX-License-Identifier: Unlicense. Synthetic MODs exist only in test scratch
   space; no test tune is distributed as a game music asset. */
#include "audio.h"
#include "music.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

unsigned long __stack = 131072;
static Game g;
#ifdef __AMIGAOS3__
#define ROOT "RAM:AstralboundMusicTests"
#else
static char music_root[128];
#define ROOT music_root
#endif
static const char *folders[] = {"battle", "planetary-flight", "space-flight", "docked", "game-over"};

static void fixture(const char *relative, int kind) {
    unsigned char data[1084 + 1024 + 32] = {0};
    char path[256];
    FILE *f;
    int i;
    memcpy(data, "Astralbound test", 16);
    data[42] = 0; data[43] = 16; /* instrument 1: 32 signed samples */
    data[45] = 64;
    data[49] = 16; /* loop all 32 */
    data[950] = 1;
    memcpy(data + 1080, "M.K.", 4);
    data[1084] = 1; data[1085] = 172; data[1086] = 0x1f; data[1087] = 1;
    data[1102] = 0x0b; /* row 1/channel 0: B00 ends song after 320 samples */
    for (i = 0; i < 32; i++) data[2108 + i] = (unsigned char)(i < 16 ? 100 : -100);
    if (kind == 1) memset(data + 1080, 0xff, 4);
    if (kind == 4) data[950] = 128;
    if (kind == 3) { data[46] = 0xff; data[47] = 0xff; } /* invalid loop start */
    snprintf(path, sizeof(path), "%s/%s", ROOT, relative);
    f = fopen(path, "wb"); assert(f);
    assert(fwrite(data, 1, kind == 2 ? 1084 : sizeof(data), f) == (kind == 2 ? 1084 : sizeof(data)));
    assert(fclose(f) == 0);
}

static void contexts(void) {
    int i;
    game_init(&g, 0x53504143);
    assert(music_context(&g) == MUSIC_DOCKED);
    g.docked = -2; assert(music_context(&g) == MUSIC_DOCKED);
    g.docked = -1;
    for (i = 0; i < MAX_NPC; i++) g.npc[i].active = 0;
    g.pos = add(scale(g.system.planets[0].center, FP, 1),
                vec(0, (g.system.planets[0].radius + 1000LL) * FP, 0));
    assert(music_context(&g) == MUSIC_PLANETARY);
    g.npc[0] = (Npc){add(g.pos, vec(0, 0, 2000 * FP)), vec(0, 0, 0), 100, 1, 1, -1, 0};
    assert(music_context(&g) == MUSIC_BATTLE);
    g.npc[0].pos = add(g.pos, vec(0, 0, 15000 * FP));
    assert(music_context(&g) == MUSIC_PLANETARY);
    g.npc[0].pos = g.pos;
    g.npc[0].hostile = 0; assert(music_context(&g) == MUSIC_PLANETARY);
    g.npc[0].hostile = 1; g.npc[0].hull = 0;
    assert(music_context(&g) == MUSIC_PLANETARY);
    g.npc[0].active = 0;
    g.pos = vec(900000000000LL, 800000000000LL, 700000000000LL);
    assert(music_context(&g) == MUSIC_SPACE);
    g.page = MARKET; assert(music_context(&g) == MUSIC_SPACE);
    g.dead = 1; assert(music_context(&g) == MUSIC_GAME_OVER);
    puts("PASS music context priority, hostile range, landing and menu continuity");
}

static void playback(void) {
    char first[256], path[256];
    int8_t whole[8000], chunks[8000];
    int i, nonzero = 0;
#ifndef __AMIGAOS3__
    snprintf(music_root, sizeof(music_root), "build/music-test-files-%ld", (long)getpid());
#endif
    mkdir(ROOT, 0700);
    for (i = 0; i < MUSIC_SCENES; i++) {
        snprintf(path, sizeof(path), "%s/%s", ROOT, folders[i]); mkdir(path, 0700);
    }
    fixture("docked/a.mod", 0); fixture("docked/MOD.b", 0);
    fixture("battle/loop.MOD", 3); fixture("space-flight/full.mod", 4);
    fixture("docked/not.mod", 1); fixture("docked/short.mod", 2);
    fixture("docked/ignored.txt", 0);
    assert(music_load(ROOT, 77) == 4);
    game_init(&g, 0x53504143);
    music_observe(&g); assert(music_track());
    strcpy(first, music_track());
    music_mix(whole, 320);
    for (i = 0; i < 320; i++) nonzero |= whole[i] != 0;
    assert(nonzero); /* A parsed note actually reaches the PCM output. */
    assert(!strcmp(first, music_track()));
    music_mix(whole, 1); assert(strcmp(first, music_track()));
    music_observe(&g); /* observing same context must not restart it */
    strcpy(first, music_track());
    g.paused = 1; music_observe(&g); music_mix(whole, sizeof(whole));
    for (i = 0; i < (int)sizeof(whole); i++) assert(whole[i] == 0);
    assert(!strcmp(first, music_track()));
    g.paused = 0; music_observe(&g);
    music_mix(whole, 319); assert(!strcmp(first, music_track()));
    music_mix(whole, 1); assert(strcmp(first, music_track()));
    assert(music_load(ROOT, 77) == 4); music_observe(&g);
    music_mix(whole, sizeof(whole));
    assert(music_load(ROOT, 77) == 4); music_observe(&g);
    for (i = 0; i < (int)sizeof(chunks); i += 125) music_mix(chunks + i, 125);
    assert(!memcmp(whole, chunks, sizeof(whole))); /* tick/song boundaries independent of buffer size */
    g.docked = -1; g.npc[0] = (Npc){g.pos, vec(0,0,0), 100, 1, 1, -1, 0};
    music_observe(&g); assert(strstr(music_track(), "battle/loop.MOD"));
    music_mix(whole, sizeof(whole)); /* malformed sample loop is clamped safely */
    assert(strstr(music_track(), "battle/loop.MOD"));
    g.dead = 1; music_observe(&g); assert(!music_track());
    music_mix(whole, sizeof(whole));
    for (i = 0; i < (int)sizeof(whole); i++) assert(whole[i] == 0);
    music_close();
    snprintf(path, sizeof(path), "%s/missing", ROOT);
    assert(music_load(path, 1) == -1);
    music_mix(whole, sizeof(whole));
    for (i = 0; i < (int)sizeof(whole); i++) assert(whole[i] == 0);
    {
        SpaceAudio sound;
        audio_init(&sound, &g); audio_mix(&sound, whole, sizeof(whole));
        for (i = 0; i < (int)sizeof(whole); i++) assert(whole[i] == 0);
        g.shots++; audio_observe(&sound, &g); audio_mix(&sound, whole, sizeof(whole));
        nonzero = 0;
        for (i = 0; i < (int)sizeof(whole); i++) nonzero |= whole[i] != 0;
        assert(nonzero); /* no music still permits effects */
    }
    {
        const char *files[] = {"docked/a.mod", "docked/MOD.b", "docked/not.mod",
            "docked/short.mod", "docked/ignored.txt", "battle/loop.MOD", "space-flight/full.mod"};
        for (i = 0; i < (int)(sizeof(files)/sizeof(files[0])); i++) {
            snprintf(path, sizeof(path), "%s/%s", ROOT, files[i]); remove(path);
        }
    }
    for (i = 0; i < MUSIC_SCENES; i++) {
        snprintf(path, sizeof(path), "%s/%s", ROOT, folders[i]); rmdir(path);
    }
    rmdir(ROOT);
    puts("PASS MOD decode, random rotation, no immediate repeat, single track loop, pause,");
    puts("     chunk continuity, invalid/truncated files, empty folders and effects without music");
}
int main(void) { contexts(); playback(); return 0; }

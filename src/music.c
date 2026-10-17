/* SPDX-License-Identifier: Unlicense. Preload MODs while DOS is available;
   selection/replay performs no allocation or filesystem calls during takeover. */
#include "music.h"
#include "audio.h"
#include "third_party/micromod/micromod.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACKS 128
#define MAX_MODULE (4UL * 1024 * 1024)
#define MUSIC_BUDGET (16UL * 1024 * 1024)
typedef struct {
    signed char *data;
    char *path;
    long duration;
    int scene;
} Track;
static Track tracks[MAX_TRACKS];
static int track_count, scene = -1, current = -1, paused;
static int previous[MUSIC_SCENES] = {-1, -1, -1, -1, -1, -1};
static uint32_t random_state;
static unsigned long remaining;
static const char *folders[MUSIC_SCENES] = {
    "battle", "planetary-flight", "space-flight", "docked", "game-over", "intro"
};

static int is_mod(const char *name) {
    size_t n = strlen(name);
    /* Accept both conventional Amiga MOD.name and name.mod, any case. */
    char a[4], b[4];
    int i;
    if (n < 4) return 0;
    for (i = 0; i < 4; i++) {
        a[i] = name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 'a' - 'A' : name[i];
        b[i] = name[n - 4 + i] >= 'A' && name[n - 4 + i] <= 'Z'
             ? name[n - 4 + i] + 'a' - 'A' : name[n - 4 + i];
    }
    return !memcmp(a, "mod.", 4) || !memcmp(b, ".mod", 4);
}

void music_close(void) {
    int i;
    current = scene = -1;
    remaining = 0;
    paused = 0;
    for (i = 0; i < track_count; i++) { free(tracks[i].data); free(tracks[i].path); }
    memset(tracks, 0, sizeof(tracks));
    track_count = 0;
    for (i = 0; i < MUSIC_SCENES; i++) previous[i] = -1;
}

int music_load(const char *root, uint32_t seed) {
    unsigned long used = 0;
    int context;
    DIR *base;
    music_close();
    random_state = seed ? seed : 1;
    base = opendir(root);
    if (!base) return -1;
    closedir(base);
    for (context = 0; context < MUSIC_SCENES; context++) {
        char folder[1024];
        struct dirent *entry;
        DIR *dir;
        int n = snprintf(folder, sizeof(folder), "%s/%s", root, folders[context]);
        if (n < 0 || (size_t)n >= sizeof(folder)) continue;
        dir = opendir(folder);
        if (!dir) continue;
        while ((entry = readdir(dir)) != NULL) {
            signed char header[1084], *data;
            char path[1024], *name;
            long bytes, duration;
            FILE *file;
            if (!is_mod(entry->d_name)) continue;
            n = snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
            if (n < 0 || (size_t)n >= sizeof(path)) continue;
            file = fopen(path, "rb");
            if (!file) continue;
            if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
                fclose(file); continue;
            }
            bytes = micromod_calculate_mod_file_len(header);
            if (bytes < 1084 || (unsigned long)bytes > MAX_MODULE ||
                (!(header[950] & 255) || (header[950] & 255) > 128) || track_count == MAX_TRACKS ||
                used + (unsigned long)bytes > MUSIC_BUDGET) {
                fprintf(stderr, "Music skipped (format or preload limit): %s\n", path);
                fclose(file); continue;
            }
            data = malloc((size_t)bytes);
            name = malloc(strlen(path) + 1);
            if (!data || !name) { free(data); free(name); fclose(file); continue; }
            memcpy(data, header, sizeof(header));
            if (fread(data + sizeof(header), 1, (size_t)bytes - sizeof(header), file) !=
                (size_t)bytes - sizeof(header)) {
                fprintf(stderr, "Music skipped (truncated): %s\n", path);
                free(data); free(name); fclose(file); continue;
            }
            fclose(file);
            if (micromod_initialise(data, AUDIO_RATE) != 0) {
                free(data); free(name); continue;
            }
            duration = micromod_calculate_song_duration();
            if (duration <= 0) { free(data); free(name); continue; }
            strcpy(name, path);
            tracks[track_count++] = (Track){data, name, duration, context};
            used += (unsigned long)bytes;
        }
        closedir(dir);
    }
    return track_count;
}

int music_context(const Game *g) {
    int i;
    if (g->cinematic) return MUSIC_INTRO;
    if (g->dead) return MUSIC_GAME_OVER;
    if (g->docked != -1) return MUSIC_DOCKED;
    for (i = 0; i < MAX_NPC; i++)
        if (g->npc[i].active && g->npc[i].hostile && g->npc[i].hull > 0 &&
            length(sub(g->npc[i].pos, g->pos)) < 12000LL * FP) return MUSIC_BATTLE;
    /* Include moons/airless bodies; navigation's selected body need not be near. */
    if (altitude_at(g, g->pos) < 12000) return MUSIC_PLANETARY;
    return MUSIC_SPACE;
}

static void next_track(void) {
    int i, count = 0, choice;
    current = -1;
    remaining = 0;
    if (scene < 0) return;
    for (i = 0; i < track_count; i++)
        if (tracks[i].scene == scene && i != previous[scene]) count++;
    if (!count) { /* A folder with a single track repeats that track. */
        if (previous[scene] >= 0 && previous[scene] < track_count) current = previous[scene];
    } else {
        random_state = random_state * 1664525U + 1013904223U;
        choice = (int)((random_state >> 1) % (unsigned)count);
        for (i = 0; i < track_count; i++)
            if (tracks[i].scene == scene && i != previous[scene] && choice-- == 0) {
                current = i; break;
            }
    }
    if (current >= 0) {
        micromod_initialise(tracks[current].data, AUDIO_RATE);
        remaining = (unsigned long)tracks[current].duration;
        previous[scene] = current;
    }
}

void music_observe(const Game *g) {
    int context = music_context(g), i;
    paused = g->paused;
    /* Without an intro score the title sequence plays space-flight music. */
    if (context == MUSIC_INTRO) {
        for (i = 0; i < track_count && tracks[i].scene != MUSIC_INTRO; i++) ;
        if (i == track_count) context = MUSIC_SPACE;
    }
    if (context != scene) { scene = context; next_track(); }
}

const char *music_track(void) { return current < 0 ? NULL : tracks[current].path; }

void music_mix(int8_t *out, unsigned count) {
    short pcm[256 * 2];
    unsigned i, n;
    memset(out, 0, count);
    if (paused) return;
    while (count && current >= 0) {
        if (!remaining) next_track();
        n = count > 256 ? 256 : count;
        if (n > remaining) n = (unsigned)remaining;
        if (!n) break;
        memset(pcm, 0, n * 2 * sizeof(*pcm));
        micromod_get_audio(pcm, n);
        for (i = 0; i < n; i++) out[i] = (int8_t)(((int)pcm[i * 2] + pcm[i * 2 + 1]) / 1024);
        remaining -= n;
        out += n;
        count -= n;
    }
}

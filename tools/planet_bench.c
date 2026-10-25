/* SPDX-License-Identifier: Unlicense; planetary rendering fixtures.
   Renders the same bodies from deep space down to low flight, each from a
   fixed lit-side camera, timing the host renderer and optionally writing
   full-frame PPM evidence. The identical camera set is used before and
   after renderer changes, so images and timings compare like for like. */
#include "performance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct {
    const char *name;
    uint32_t seed;
    int body;
    int64_t distance;  /* centre distance in radius/16, or 0 for a horizon view */
    int64_t altitude;  /* metres above the surface for a horizon view */
    int phase;         /* camera bearing from the sun line, table angles */
    int spin;          /* extra planet rotation, fraction of a day in 1024ths */
} Fixture;
static const Fixture fixtures[] = {
    {"ocean-distant", 0x53504143, 0, 200 * 16, 0, 170, 0},
    {"ocean-far", 0x53504143, 0, 40 * 16, 0, 170, 0},
    {"ocean-orbit", 0x53504143, 0, 8 * 16, 0, 170, 0},
    {"ocean-close", 0x53504143, 0, 3 * 16, 0, 170, 0},
    {"ocean-terminator", 0x53504143, 0, 3 * 16, 0, 300, 0},
    {"ocean-low-orbit", 0x53504143, 0, 22, 0, 150, 0},
    {"ocean-high-altitude", 0x53504143, 0, 0, 150000, 120, 0},
    {"ocean-low-altitude", 0x53504143, 0, 0, 3000, 120, 0},
    {"desert-orbit", 0x53504143, 2, 4 * 16, 0, 200, 0},
    {"moon-orbit", 0x53504143, 3, 4 * 16, 0, 200, 0},
    {"gas-orbit", 2, 1, 6 * 16, 0, 170, 0},
    {"gas-close", 2, 1, 40, 0, 120, 0},
};
#define FIXTURES (int)(sizeof(fixtures) / sizeof(fixtures[0]))
static Game g;
static Vec cross14(Vec a, Vec b) {
    return vec((a.y * b.z - a.z * b.y) / 16384, (a.z * b.x - a.x * b.z) / 16384,
               (a.x * b.y - a.y * b.x) / 16384);
}
static int64_t dot14(Vec a, Vec b) { return (a.x * b.x + a.y * b.y + a.z * b.z) / 16384; }
/* Roll that levels the view against a local up direction (Q14). */
static int level_roll(int yaw, int pitch, Vec up) {
    Vec right0 = vec(cosi(yaw), 0, -sini(yaw)), up0;
    int roll, unused;
    up0 = vec(-(int64_t)sini(pitch) * sini(yaw) / 16384, cosi(pitch),
              -(int64_t)sini(pitch) * cosi(yaw) / 16384);
    aim_angles(vec(dot14(up, right0), 0, dot14(up, up0)), &roll, &unused);
    return roll;
}
static void place(const Fixture *x) {
    Planet *p;
    Vec sun, side, dir;
    game_init(&g, x->seed);
    g.docked = -1; g.page = FLIGHT; g.ui_visible = 0; g.target = -1;
    g.body = g.target_body = g.frame_body = x->body;
    memset(g.npc, 0, sizeof(g.npc));
    p = &g.system.planets[x->body];
    if (x->spin) {
        g.tick += (uint32_t)((uint64_t)rotation_period(p) * HZ * (unsigned)x->spin / 1024);
        update_world(&g);
    }
    /* The phase is the exact sun-planet-camera angle. The camera swings
       away from the sun line towards the planet's north (+Z, its spin axis)
       by 30 degrees, so poles and rings tilt into view. */
    sun = unit(sub(g.system.star[0].center, p->center), 16384);
    {
        Vec north = unit(sub(vec(0, 0, 16384), scale(sun, sun.z, 16384)), 16384), across;
        if (!length(north)) north = vec(16384, 0, 0);
        across = cross14(sun, north);
        side = add(scale(across, cosi(85), 16384), scale(north, sini(85), 16384));
        dir = unit(add(scale(sun, cosi(x->phase), 16384), scale(side, sini(x->phase), 16384)), 16384);
    }
    if (x->distance) {
        Vec offset = scale(dir, (int64_t)p->radius * x->distance, 16384 * 16LL);
        g.pos = scale(add(p->center, offset), FP, 1);
        aim_angles(scale(offset, -1, 1), &g.yaw, &g.pitch);
        g.roll = level_roll(g.yaw, g.pitch, vec(0, 0, 16384));
    } else {
        /* Above a lit ground point, looking along the surface towards the
           terminator with the horizon about a third of the way down. */
        Vec local = radial(dir, p->radius), up = unit(local, 16384);
        Vec tangent = unit(cross14(up, cross14(sun, up)), 16384), aim;
        int64_t ground = p->radius + terrain_height(p, local);
        int dip = (int)isqrt64((uint64_t)(2 * x->altitude * 16384 / p->radius * 16384));
        if (!length(tangent)) tangent = side;
        tangent = scale(tangent, -1, 1);
        aim = unit(sub(scale(tangent, 16384, 16384), scale(up, dip + 1400, 16384)), 16384);
        g.pos = scale(add(p->center, radial(local, ground + x->altitude)), FP, 1);
        g.vel = surface_velocity(p, sub(scale(g.pos, 1, FP), p->center));
        aim_angles(aim, &g.yaw, &g.pitch);
        g.roll = level_roll(g.yaw, g.pitch, up);
    }
}
static void write_ppm(const Frame *f, const char *path) {
    FILE *out = fopen(path, "wb");
    int i;
    if (!out) { perror(path); exit(1); }
    fprintf(out, "P6\n%d %d\n255\n", f->w, f->h);
    for (i = 0; i < f->w * f->h; i++) {
        uint32_t c = f->palette[f->pixels[i]];
        fputc((int)(c >> 16 & 255), out); fputc((int)(c >> 8 & 255), out); fputc((int)(c & 255), out);
    }
    fclose(out);
}
static double frame_ms(Frame *f, int frames) {
    clock_t start = clock();
    int i;
    for (i = 0; i < frames; i++) render_performance_scene(&g, f);
    return (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC / frames;
}
int main(int argc, char **argv) {
    const char *images = NULL;
    int timing = 1, i, k, preset, mode, renderer = RENDERER_ENHANCED;
    static const struct { const char *name; int w, h, enhanced; } modes[] = {
        {"ecs", 320, 256, 0}, {"aga", 320, 256, 1}, {"rtg", 640, 480, 1}};
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--images") && i + 1 < argc) images = argv[++i];
        else if (!strcmp(argv[i], "--no-timing")) timing = 0;
        else if (!strcmp(argv[i], "--classic")) renderer = RENDERER_CLASSIC;
    }
    if (images) {
        Settings s;
        settings_defaults(&s, 0); settings_preset(&s, QUALITY_HIGH); render_graphics(&s.gfx);
        render_bezier(s.bezier);
        render_renderer(renderer);
        for (mode = 0; mode < 3; mode++) {
            Frame f = {modes[mode].w, modes[mode].h, modes[mode].enhanced, NULL, {0}};
            f.pixels = malloc((size_t)f.w * f.h);
            if (!f.pixels || !render_init(&f)) return 1;
            for (k = 0; k < FIXTURES; k++) {
                char path[512];
                place(&fixtures[k]);
                render_performance_scene(&g, &f);
                snprintf(path, sizeof(path), "%s/%s-%s.ppm", images, fixtures[k].name, modes[mode].name);
                write_ppm(&f, path);
            }
            free(f.pixels); render_shutdown();
        }
    }
    if (!timing) return 0;
    printf("Host CPU, %s renderer, AGA 320x256, ms/frame (min of 3 runs of 8 frames).\n", renderer_names[renderer]);
    printf("%-22s %8s %8s\n", "scene", "LOW", "HIGH");
    for (k = 0; k < FIXTURES; k++) {
        double best[2];
        for (preset = 0; preset < 2; preset++) {
            Settings s;
            Frame f = {320, 256, 1, NULL, {0}};
            int run;
            settings_defaults(&s, 0); settings_preset(&s, preset ? QUALITY_HIGH : QUALITY_LOW);
            render_graphics(&s.gfx);
            render_bezier(s.bezier);
            render_renderer(renderer);
            f.pixels = malloc((size_t)f.w * f.h);
            if (!f.pixels || !render_init(&f)) return 1;
            place(&fixtures[k]);
            render_performance_scene(&g, &f);
            best[preset] = 1e9;
            for (run = 0; run < 3; run++) {
                double ms = frame_ms(&f, 8);
                if (ms < best[preset]) best[preset] = ms;
            }
            free(f.pixels); render_shutdown();
        }
        printf("%-22s %8.3f %8.3f\n", fixtures[k].name, best[0], best[1]);
    }
    return 0;
}

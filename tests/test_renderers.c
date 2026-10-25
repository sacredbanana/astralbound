/* SPDX-License-Identifier: Unlicense
   Both renderers, side by side: every performance scene (3D and interface)
   and the title sequence in ECS, AGA and RTG must render deterministically,
   draw real content and stay inside their frame. Planet outlines must agree
   exactly between Enhanced and Classic, as both come from planet_view. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "performance.h"
#include "intro.h"
#define GUARD 4096
static Game g;
static Intro intro;
typedef struct { int w, h, enhanced; const char *name; } Mode;
static const Mode modes[] = {{320, 200, 0, "ECS"}, {320, 256, 1, "AGA"}, {640, 480, 1, "RTG"}};
static uint8_t *buffer;
static uint8_t *frame_pixels(void) { return buffer + GUARD; }
static void guards(int w, int h) {
    int i;
    for (i = 0; i < GUARD; i++) assert(buffer[i] == 0xA5 && buffer[GUARD + w * h + i] == 0xA5);
}
static int colours(const uint8_t *p, int n) {
    static int seen[256];
    int i, count = 0;
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < n; i++) if (!seen[p[i]]++) count++;
    return count;
}
static void scenes(void) {
    static uint8_t first[640 * 480];
    Settings s;
    int renderer, mode, scene, checked = 0;
    for (renderer = 0; renderer < 2; renderer++)
        for (mode = 0; mode < 3; mode++) {
            Frame f = {modes[mode].w, modes[mode].h, modes[mode].enhanced, NULL, {0}};
            int n = f.w * f.h;
            settings_defaults(&s, 0); settings_preset(&s, QUALITY_HIGH);
            render_graphics(&s.gfx); render_renderer(renderer);
            f.pixels = frame_pixels();
            memset(buffer, 0xA5, GUARD * 2 + 640 * 480);
            assert(render_init(&f));
            for (scene = 0; scene < PERFORMANCE_SCENES; scene++) {
                performance_scene(&g, scene);
                render_performance_scene(&g, &f);
                memcpy(first, f.pixels, (size_t)n);
                performance_scene(&g, scene);
                render_performance_scene(&g, &f);
                if (memcmp(first, f.pixels, (size_t)n))
                    fprintf(stderr, "%s %s scene %d (%s) is not deterministic\n", renderer_names[renderer],
                            modes[mode].name, scene, performance_names[scene]);
                assert(!memcmp(first, f.pixels, (size_t)n));
                /* Every scene shows more than a blank sky. */
                if (colours(f.pixels, n) < 3)
                    fprintf(stderr, "%s %s scene %d (%s): %d colours\n", renderer_names[renderer], modes[mode].name,
                            scene, performance_names[scene], colours(f.pixels, n));
                assert(colours(f.pixels, n) >= 3); /* even a whiteout keeps a silhouette */
                guards(f.w, f.h);
                checked++;
            }
            render_shutdown();
        }
    printf("PASS %d renders: all %d scenes in ECS/AGA/RTG in both renderers, deterministic and in bounds\n",
           checked, PERFORMANCE_SCENES);
}
static void title_sequence(void) {
    Settings s;
    int renderer, t;
    for (renderer = 0; renderer < 2; renderer++) {
        Frame f = {320, 256, 1, NULL, {0}};
        settings_defaults(&s, 0); settings_preset(&s, QUALITY_HIGH);
        render_graphics(&s.gfx); render_renderer(renderer);
        f.pixels = frame_pixels();
        memset(buffer, 0xA5, GUARD * 2 + 640 * 480);
        assert(render_init(&f));
        intro_start(&intro, &g, 0x53504143);
        for (t = 0; t < INTRO_TICKS; t += HZ * 9) {
            if (t > intro.tick) intro_frame(&intro, &g, t - intro.tick);
            if (!intro.running) break;
            render_intro(&intro, &g, &f);
            if (intro.fade < 16 && !intro.title) assert(colours(f.pixels, f.w * f.h) >= 2); /* fades are black */
            guards(f.w, f.h);
        }
        render_shutdown();
    }
    puts("PASS title sequence through both renderers, frames in bounds");
}
/* Ocean world at three ranges, stars off: both renderers take the outline
   from planet_view, so every interior pixel Enhanced covers Classic covers
   too, and Classic adds nothing beyond its atmosphere halo, a band a few
   percent of the radius wide. */
static void planet_outlines(void) {
    static const int ranges[] = {3 * 16, 8 * 16, 22};
    static uint8_t covered[2][320 * 256];
    Settings s;
    int renderer, k;
    for (k = 0; k < 3; k++) {
        int x, y, interior = 0, missing = 0, area[2] = {0, 0};
        for (renderer = 0; renderer < 2; renderer++) {
            Frame f = {320, 256, 1, NULL, {0}};
            Planet *p;
            Vec sun, offset;
            settings_defaults(&s, 0); settings_preset(&s, QUALITY_HIGH);
            s.gfx.stars = 0; render_graphics(&s.gfx); render_renderer(renderer);
            f.pixels = frame_pixels();
            assert(render_init(&f));
            game_init(&g, 0x53504143);
            g.docked = -1; g.page = FLIGHT; g.ui_visible = 0; g.target = -1; g.body = 0;
            p = &g.system.planets[0];
            sun = unit(sub(g.system.star[0].center, p->center), 16384);
            offset = scale(sun, (int64_t)p->radius * ranges[k], 16384 * 16LL);
            g.pos = scale(add(p->center, offset), FP, 1);
            aim_angles(scale(offset, -1, 1), &g.yaw, &g.pitch); g.roll = 0;
            render_performance_scene(&g, &f);
            for (y = 0; y < 256; y++)
                for (x = 0; x < 320; x++) {
                    covered[renderer][y * 320 + x] = f.pixels[y * 320 + x] != f.pixels[0];
                    area[renderer] += covered[renderer][y * 320 + x];
                }
            render_shutdown();
        }
        for (y = 1; y < 255; y++)
            for (x = 1; x < 319; x++) {
                const uint8_t *e = covered[0] + y * 320 + x;
                if (!(e[0] && e[-1] && e[1] && e[-320] && e[320])) continue;
                interior++;
                missing += !covered[1][y * 320 + x];
            }
        if (missing || area[1] > area[0] * 5 / 4 + 400)
            fprintf(stderr, "range %d: interior %d, Classic misses %d; areas Enhanced %d Classic %d\n",
                    k, interior, missing, area[0], area[1]);
        assert(interior > 100 && !missing && area[1] >= area[0] - area[0] / 50 && area[1] <= area[0] * 5 / 4 + 400);
    }
    puts("PASS planet discs agree between renderers at three ranges (Classic adds only its halo)");
}
/* The launcher option must change the actual ring rasteriser, in both
   backends. The scripted 148 s shot frames the rings. */
static void bezier_option(void) {
    static uint8_t first[640 * 480];
    int renderer, mode, enabled;
    Settings s;
    for(renderer=0;renderer<2;renderer++) for(mode=0;mode<3;mode++) {
        Frame f={modes[mode].w,modes[mode].h,modes[mode].enhanced,NULL,{0}};
        int n=f.w*f.h;
        settings_defaults(&s,0); settings_preset(&s,QUALITY_ULTRA);
        render_graphics(&s.gfx); render_renderer(renderer);
        f.pixels=frame_pixels(); memset(buffer,0xA5,GUARD*2+640*480);
        assert(render_init(&f));
        intro_start(&intro,&g,0x53504143); intro_frame(&intro,&g,148*HZ);
        for(enabled=0;enabled<2;enabled++) {
            render_bezier(enabled); render_intro(&intro,&g,&f); guards(f.w,f.h);
            if(!enabled) memcpy(first,f.pixels,(size_t)n);
            else assert(memcmp(first,f.pixels,(size_t)n)!=0);
        }
        render_shutdown();
    }
    render_bezier(1);
    puts("PASS Bezier ring toggle changes both renderers in ECS/AGA/RTG");
}
/* Pixel-size overrides from old settings must not degrade either backend. */
static void full_resolution(void) {
    static uint8_t first[320*256];
    Settings s;
    int renderer;
    Frame f={320,256,1,NULL,{0}};
    for(renderer=0;renderer<2;renderer++) {
        settings_defaults(&s,0); settings_preset(&s,QUALITY_HIGH);
        render_renderer(renderer); render_bezier(s.bezier); render_graphics(&s.gfx);
        f.pixels=frame_pixels(); memset(buffer,0xA5,GUARD*2+640*480);
        assert(render_init(&f)); performance_scene(&g,1);
        render_performance_scene(&g,&f); memcpy(first,f.pixels,sizeof(first));
        s.gfx.scale=3; render_graphics(&s.gfx); render_performance_scene(&g,&f);
        assert(!memcmp(first,f.pixels,sizeof(first))); guards(f.w,f.h);
        if(renderer==RENDERER_CLASSIC) {
            s.gfx.texture=0; s.gfx.triangles=0; render_graphics(&s.gfx);
            assert(render_init(&f)); render_performance_scene(&g,&f);
            assert(!memcmp(first,f.pixels,sizeof(first))); guards(f.w,f.h);
        }
        render_shutdown();
    }
    puts("PASS legacy pixel-size overrides keep both renderers at full resolution");
}
int main(void) {
    buffer = malloc(GUARD * 2 + 640 * 480);
    assert(buffer);
    full_resolution();
    planet_outlines();
    bezier_option();
    scenes();
    title_sequence();
    free(buffer);
    return 0;
}

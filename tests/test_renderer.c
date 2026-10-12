/* SPDX-License-Identifier: Unlicense
   Isolated raster regressions exercise intersecting surfaces directly. */
#include <assert.h>
#include "../src/render.c"
#include "../src/native_video.h"

static void compact_display(void) {
    static Game g;
    Frame small = {320, 200, 1, 0, {0}}, full = {320, 256, 1, 0, {0}};
    uint8_t *planar;
    int page, depth, x, y, b;
    NativeVideo pal = native_video(1, 0), ntsc = native_video(0, 0), low = native_video(1, 1);
    assert(pal.start == 44 && pal.stop == 300 && pal.height == 256 && pal.rate == 50);
    assert(ntsc.start == 44 && ntsc.stop == 244 && ntsc.height == 200 && ntsc.rate == 60);
    assert(low.start == 72 && low.stop == 272 && low.height == 200 && low.rate == 50);
    game_init(&g, 0x53504143);
    small.pixels = malloc(320 * 200);
    full.pixels = malloc(320 * 256);
    planar = malloc(40 * 200 * 8 + 16);
    assert(small.pixels && full.pixels && planar);
    /* Compare every menu's text area with the established 256-line layout,
       including the last contract/commodity row and the bottom instructions. */
    for (page = MAP; page <= HELP; page++) {
        g.page = page;
        assert(render_init(&full));
        render_game(&g, &full);
        assert(render_init(&small));
        render_game(&g, &small);
        for (y = 12; y < 181; y++)
            assert(!memcmp(small.pixels + y * 320 + 12, full.pixels + y * 320 + 12, 296));
    }
    g.page = FLIGHT;
    render_game(&g, &small);
    for (depth = 5; depth <= 8; depth += 3) {
        int size = 40 * 200 * depth;
        for (x = 0; x < 320 * 200; x++)
            small.pixels[x] = (uint8_t)((x * 13 + x / 320) & ((1 << depth) - 1));
        memset(planar, 0xa5, 40 * 200 * 8 + 16);
        c2p(small.pixels, planar + 8, 320, 200, depth);
        for (x = 0; x < 8; x++)
            assert(planar[x] == 0xa5 && planar[8 + size + x] == 0xa5);
        for (y = 0; y < 200; y++)
            for (x = 0; x < 320; x++) {
                int pixel = 0;
                for (b = 0; b < depth; b++)
                    pixel |= ((planar[8 + b * 8000 + y * 40 + x / 8] >> (7 - (x & 7))) & 1) << b;
                assert(pixel == small.pixels[y * 320 + x]);
            }
    }
    free(planar);
    free(small.pixels);
    free(full.pixels);
    render_shutdown();
}

int main(void) {
    Frame f = {64, 120, 1, 0, {0}};
    Tri ground = {{0, 64, 0}, {0, 0, 64}, {1000, 1000, 1000}, 1000, 8};
    Tri wall = {{0, 64, 0}, {0, 0, 64}, {400, 2000, 400}, 1200, 16};
    uint8_t reference[64 * 64];
    int order;
    assert(render_init(&f));
    f.pixels = calloc((size_t)f.w * f.h, 1);
    assert(f.pixels);
    frame = &f;
    view_h = 64;
    for (order = 0; order < 2; order++) {
        memset(f.pixels, 0, (size_t)f.w * f.h);
        memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
        raster(order ? &wall : &ground);
        raster(order ? &ground : &wall);
        /* A large ground triangle passes in front of one end of the wall
           and behind the other. Average-distance sorting cannot do this. */
        assert(f.pixels[8 * 64 + 8] == 8);
        assert(f.pixels[8 * 64 + 40] == 16);
        if (!order)
            memcpy(reference, f.pixels, sizeof(reference));
        else {
            int x, y;
            for (y = 0; y < 64; y++)
                for (x = 0; x < 64; x++)
                    /* At x=24 the two surfaces have exactly equal depth. */
                    if (x != 24)
                        assert(reference[y * 64 + x] == f.pixels[y * 64 + x]);
        }
    }
    /* Clipped spans must evaluate depth at the clipped pixel position. */
    wall.x[0] = wall.x[2] = -32;
    wall.x[1] = 32;
    memset(depth_buffer, 0, depth_capacity * sizeof(*depth_buffer));
    raster(&wall);
    assert(depth_buffer[8 * 64 + 8] == 1400);
    /* ECS polar ice stays visibly darker than the atmospheric horizon. */
    assert(f.palette[30] != f.palette[7]);
    assert(((f.palette[30] >> 8) & 255) + 20 < ((f.palette[7] >> 8) & 255));
    free(f.pixels);
    render_shutdown();
    assert(!render_init(&(Frame){64, 56, 0, 0, {0}}));
    compact_display();
    puts("PASS depth, polar contrast, PAL/NTSC layout, compact menus and 200-line C2P");
    return 0;
}

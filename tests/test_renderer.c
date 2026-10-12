/* SPDX-License-Identifier: Unlicense
   Isolated raster regressions exercise intersecting surfaces directly. */
#include <assert.h>
#include "../src/render.c"

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
    puts("PASS crossing surfaces, draw order, clipped depth and polar contrast");
    return 0;
}

/* SPDX-License-Identifier: Unlicense; timings belong to this machine/config. */
#include "astralbound.h"
#include "performance.h"
#include "settings.h"
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <stdio.h>
#include <stdlib.h>
struct Device *TimerBase;
unsigned long __stack = 131072;
static Game g;
static uint64_t timer(void) {
    struct EClockVal e;
    ReadEClock(&e);
    return ((uint64_t)e.ev_hi << 32) | e.ev_lo;
}
int main(void) {
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *request = port ? (struct timerequest *)CreateIORequest(port, sizeof(*request)) : NULL;
    Frame f = {320, 256, 1, NULL, {0}};
    UBYTE *planar = NULL;
    struct EClockVal ec;
    ULONG rate;
    int i, depth, height;
    uint64_t begin, duration;
    uint32_t hash;
    int result = 1;
    if (!request || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)request, 0)) goto done;
    TimerBase = request->tr_node.io_Device;
    rate = ReadEClock(&ec);
    f.pixels = malloc(320 * 256);
    planar = AllocMem(40 * 256 * 8, MEMF_CHIP);
    if (!f.pixels || !planar || !render_init(&f)) goto done;
    game_init(&g, 0x53504143);
    for (height = 200; height <= 256; height += 56) for (depth = 5; depth <= 8; depth += 3) {
        for (i = 0; i < 320 * height; i++) f.pixels[i] = (UBYTE)hash32((unsigned)i);
        begin = timer();
        for (i = 0; i < 16; i++) c2p(f.pixels, planar, 320, height, depth);
        duration = timer() - begin;
        hash = 2166136261U;
        for (i = 0; i < 40 * height * depth; i++) hash = (hash ^ planar[i]) * 16777619U;
        printf("C2P 320x%d depth %d to Chip: %lu us/frame hash %08lx\n", height, depth,
               (unsigned long)(duration * 1000000 / rate / 16), (unsigned long)hash);
    }
    /* Both renderers at the Low preset, on six game scenes that span
       orbit, terrain, cities, stations, weather and combat. */
    {
        static const int scenes[] = {0, 1, 4, 5, 6, 9};
        Settings settings;
        int renderer, k;
        settings_defaults(&settings, 1);
        settings_preset(&settings, QUALITY_LOW);
        render_graphics(&settings.gfx);
        for (renderer = 0; renderer < 2; renderer++) {
            uint64_t total = 0;
            render_renderer(renderer);
            for (k = 0; k < 6; k++) {
                performance_scene(&g, scenes[k]);
                render_performance_scene(&g, &f);
                begin = timer();
                for (i = 0; i < 2; i++) render_performance_scene(&g, &f);
                duration = (timer() - begin) / 2;
                total += duration;
                hash = 2166136261U;
                for (i = 0; i < f.w * f.h; i++) hash = (hash ^ f.pixels[i]) * 16777619U;
                printf("%s %-18s %7lu us/frame (%lu.%02lu fps) hash %08lx\n", renderer_names[renderer],
                       performance_names[scenes[k]], (unsigned long)(duration * 1000000 / rate),
                       (unsigned long)(rate * 100 / (duration ? duration : 1) / 100),
                       (unsigned long)(rate * 100 / (duration ? duration : 1) % 100), (unsigned long)hash);
                fflush(stdout);
            }
            printf("%s total over six scenes: %lu us\n", renderer_names[renderer], (unsigned long)(total * 1000000 / rate));
        }
    }
    result = 0;
done:
    if (planar) FreeMem(planar, 40 * 256 * 8);
    free(f.pixels); render_shutdown();
    if (TimerBase) CloseDevice((struct IORequest *)request);
    if (request) DeleteIORequest((struct IORequest *)request);
    if (port) DeleteMsgPort(port);
    return result;
}

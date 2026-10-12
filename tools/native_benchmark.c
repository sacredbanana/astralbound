/* SPDX-License-Identifier: Unlicense; timings belong to this machine/config. */
#include "space.h"
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
    int i, depth, height, scene;
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
    g.docked = -1;
    for (scene = 0; scene < 2; scene++) {
        g.pos = add(scale(g.system.planets[0].center, FP, 1),
                    vec(0, (g.system.planets[0].radius + (scene ? 220LL : g.system.planets[0].atmosphere + 35000LL)) * FP,
                        (scene ? -900LL : -60000LL) * FP));
        g.pitch = scene ? -35 : -70;
        render_game(&g, &f);
        begin = timer();
        for (i = 0; i < 16; i++) render_game(&g, &f);
        duration = timer() - begin;
        hash = 2166136261U;
        for (i = 0; i < f.w * f.h; i++) hash = (hash ^ f.pixels[i]) * 16777619U;
        printf("%s render: %lu us/frame hash %08lx\n", scene ? "surface" : "orbit",
               (unsigned long)(duration * 1000000 / rate / 16), (unsigned long)hash);
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

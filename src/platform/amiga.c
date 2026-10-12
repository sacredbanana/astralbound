/* SPDX-License-Identifier: Unlicense
   Native PAL/NTSC ECS/AGA takeover; RTG stays under the graphics driver. */
#include "space.h"
#include "native_video.h"
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/timer.h>
#include <graphics/gfxbase.h>
#include <graphics/displayinfo.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <inline/cybergraphics.h> /* Installed proto header has malformed includes. */
#include <cybergraphx/cybergraphics.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Library *CyberGfxBase;
struct Device *TimerBase;
static struct MsgPort *timer_port;
static struct timerequest *timer_request;
static ULONG clock_rate = 50;
unsigned long __stack = 131072;
static volatile struct Custom *hw = (volatile struct Custom *)0xdff000;
static volatile UBYTE *cia_icr = (volatile UBYTE *)0xbfed01;
static volatile UBYTE *cia_sdr = (volatile UBYTE *)0xbfec01;
static volatile UBYTE *cia_cra = (volatile UBYTE *)0xbfee01;
static UBYTE held[128], pressed[128], ecs_map[256];
static Game g;
static Frame f;
static UBYTE *planes[2], *mapped;
static UWORD *copper[2];
static struct View *old_view;
static UWORD old_dma, old_int, old_adk;
static int taken, depth, aga, rtg, has_aga;
static NativeVideo video;
static struct Screen *screen;
static struct Window *window;
static struct ScreenBuffer *buffers[2];
static struct MsgPort *safe_port;
static int displayed;
static uint64_t clock_now(void) {
    if (rtg) {
        struct EClockVal ec;
        ReadEClock(&ec);
        return ((uint64_t)ec.ev_hi << 32) | ec.ev_lo;
    }
    /* Reading high latches TOD, reading low releases it. Do not reset the OS clock. */
    {
        ULONG hi = *(volatile UBYTE *)0xbfea01, mid = *(volatile UBYTE *)0xbfe901,
              lo = *(volatile UBYTE *)0xbfe801;
        return (hi << 16) | (mid << 8) | lo;
    }
}
static int beam(void) {
    return (int)((*(volatile ULONG *)0xdff004 >> 8) & 511);
}
static void vblank(void) {
    while (beam() == 20) {
    }
    while (beam() != 20) {
    }
}
static void cmove(UWORD **p, UWORD reg, UWORD val) {
    *(*p)++ = reg;
    *(*p)++ = val;
}
static void build_copper(int b) {
    UWORD *p = copper[b];
    int i, bank;
    ULONG address;
    cmove(&p, 0x100, aga ? 0x0211 : 0x5200);
    cmove(&p, 0x102, 0);
    cmove(&p, 0x104, 0x24);
    cmove(&p, 0x108, 0);
    cmove(&p, 0x10a, 0);
    if (has_aga) {
        cmove(&p, 0x1fc, aga ? 3 : 0); /* AGA 64-bit bitplane fetches. */
        cmove(&p, 0x10c, 0x11);
        cmove(&p, 0x106, 0);
    }
    cmove(&p, 0x1e4, 0);
    /* With DIWHIGH clear, legacy DIWSTOP infers vertical bit 8 from
       inverted bit 7: PAL stops at 300/272, NTSC at 244. */
    cmove(&p, 0x08e, (UWORD)((video.start << 8) | 0x81));
    cmove(&p, 0x090, (UWORD)(((video.stop & 255) << 8) | 0xc1));
    cmove(&p, 0x092, 0x38);
    cmove(&p, 0x094, aga ? 0xb8 : 0xd0);
    for (i = 0; i < depth; i++) {
        address = (ULONG)(planes[b] + i * 40 * f.h);
        cmove(&p, (UWORD)(0xe0 + i * 4), (UWORD)(address >> 16));
        cmove(&p, (UWORD)(0xe2 + i * 4), (UWORD)address);
    }
    for (bank = 0; bank < (aga ? 8 : 1); bank++) {
        if (aga)
            cmove(&p, 0x106, (UWORD)(bank << 13));
        for (i = 0; i < 32; i++) {
            ULONG c = f.palette[bank * 32 + i];
            cmove(&p, (UWORD)(0x180 + i * 2),
                  (UWORD)(((c >> 12) & 0xf00) | ((c >> 8) & 0xf0) | ((c >> 4) & 15)));
        }
        if (aga) {
            cmove(&p, 0x106, (UWORD)((bank << 13) | 0x200));
            for (i = 0; i < 32; i++) {
                ULONG c = f.palette[bank * 32 + i];
                cmove(&p, (UWORD)(0x180 + i * 2),
                      (UWORD)(((c >> 8) & 0xf00) | ((c >> 4) & 0xf0) | (c & 15)));
            }
        }
    }
    if (aga)
        cmove(&p, 0x106, 0);
    *p++ = 0xffff;
    *p = 0xfffe;
}
static void takeover(void) {
    old_view = GfxBase->ActiView;
    LoadView(0);
    WaitTOF();
    WaitTOF();
    OwnBlitter();
    WaitBlit();
    Forbid();
    old_dma = hw->dmaconr;
    old_int = hw->intenar;
    old_adk = hw->adkconr;
    Disable();
    hw->intena = 0x7fff;
    hw->intreq = 0x7fff;
    hw->dmacon = 0x7fff;
    vblank();
    hw->cop1lc = (ULONG)copper[0];
    hw->copjmp1 = 0;
    hw->dmacon = 0x8380;
    taken = 1;
}
static void restore(void) {
    if (!taken)
        return;
    vblank();
    hw->dmacon = 0x7fff;
    hw->intena = 0x7fff;
    hw->intreq = 0x7fff;
    if (has_aga) {
        hw->fmode = 0;
        hw->bplcon3 = 0;
    }
    hw->adkcon = 0x7fff;
    hw->adkcon = old_adk | 0x8000;
    hw->cop1lc = (ULONG)GfxBase->copinit;
    hw->copjmp1 = 0;
    hw->dmacon = old_dma | 0x8000;
    hw->intena = old_int | 0x8000;
    Enable();
    Permit();
    DisownBlitter();
    LoadView(old_view);
    WaitTOF();
    WaitTOF();
    taken = 0;
    memset(held, 0, sizeof(held));
    memset(pressed, 0, sizeof(pressed));
}
static void keyboard(void) {
    if (*cia_icr & 8) {
        UBYTE raw = *cia_sdr, key;
        int line;
        raw = (UBYTE)~raw;
        key = (UBYTE)((raw >> 1) | (raw << 7));
        if (!(key & 128) && !held[key & 127])
            pressed[key & 127] = 1;
        held[key & 127] = (key & 128) ? 0 : 1;
        *cia_cra |= 0x40;
        line = beam();
        while (beam() == line) {
        }
        line = beam();
        while (beam() == line) {
        }
        *cia_cra &= (UBYTE)~0x40;
    }
}
static int raw_action(int k) {
    switch (k) {
    case 0x50:
        return 0;
    case 0x4c:
        return 1001;
    case 0x4d:
        return 1002;
    case 0x4f:
        return 1003;
    case 0x4e:
        return 1004;
    case 0x44:
        return '\r';
    case 0x40:
        return ' ';
    case 0x0c:
        return '+';
    case 0x0b:
        return '-';
    case 0x1a:
        return '[';
    case 0x1b:
        return ']';
    default:
        break;
    }
    if (k >= 1 && k <= 8)
        return '0' + k;
    {
        static const char row1[] = "qwertyuiop", row2[] = "asdfghjkl", row3[] = "zxcvbnm";
        if (k >= 0x10 && k <= 0x19)
            return row1[k - 0x10];
        if (k >= 0x20 && k <= 0x28)
            return row2[k - 0x20];
        if (k >= 0x31 && k <= 0x37)
            return row3[k - 0x31];
    }
    return 0;
}
static void wait_safe(void) {
    WaitPort(safe_port);
    while (GetMsg(safe_port)) {
    };
}
static int open_rtg(void) {
    ULONG id, palette[770];
    int i;
    CyberGfxBase = OpenLibrary((CONST_STRPTR) "cybergraphics.library", 41);
    if (!CyberGfxBase) {
        puts("RTG requires CyberGraphX V41 or Picasso96 CGX compatibility.");
        return 0;
    }
    id = BestCModeIDTags(CYBRBIDTG_NominalWidth, 640, CYBRBIDTG_NominalHeight, 480, CYBRBIDTG_Depth,
                         8, TAG_DONE);
    if (id == (ULONG)INVALID_ID || !IsCyberModeID(id)) {
        puts("No compatible RTG mode.");
        return 0;
    }
    screen = OpenScreenTags(0, SA_DisplayID, id, SA_Width, 640, SA_Height, 480, SA_Depth, 8,
                            SA_Type, CUSTOMSCREEN, SA_Quiet, TRUE, SA_ShowTitle, FALSE, TAG_DONE);
    if (!screen)
        return 0;
    window = OpenWindowTags(0, WA_CustomScreen, (ULONG)screen, WA_Borderless, TRUE, WA_Backdrop,
                            TRUE, WA_Activate, TRUE, WA_IDCMP, IDCMP_RAWKEY, TAG_DONE);
    if (!window)
        return 0;
    palette[0] = 256UL << 16;
    for (i = 0; i < 256; i++) {
        ULONG c = f.palette[i];
        palette[1 + i * 3] = ((c >> 16) & 255) * 0x01010101UL;
        palette[2 + i * 3] = ((c >> 8) & 255) * 0x01010101UL;
        palette[3 + i * 3] = (c & 255) * 0x01010101UL;
    }
    palette[769] = 0;
    LoadRGB32(&screen->ViewPort, palette);
    safe_port = CreateMsgPort();
    if (!safe_port)
        return 0;
    buffers[0] = AllocScreenBuffer(screen, 0, SB_SCREEN_BITMAP);
    buffers[1] = AllocScreenBuffer(screen, 0, 0);
    if (!buffers[0] || !buffers[1])
        return 0;
    for (i = 0; i < 2; i++)
        buffers[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safe_port;
    return 1;
}
static void close_all(void) {
    int i;
    restore();
    if (rtg) {
        if (displayed && buffers[0]) {
            while (!ChangeScreenBuffer(screen, buffers[0]))
                WaitTOF();
            wait_safe();
        }
        for (i = 1; i >= 0; i--)
            if (buffers[i])
                FreeScreenBuffer(screen, buffers[i]);
        if (safe_port)
            DeleteMsgPort(safe_port);
        if (window)
            CloseWindow(window);
        if (screen)
            CloseScreen(screen);
    }
    for (i = 0; i < 2; i++) {
        if (planes[i])
            FreeMem(planes[i], 40 * f.h * depth);
        if (copper[i])
            FreeMem(copper[i], 4096);
    }
    render_set_pump(0);
    if (TimerBase)
        CloseDevice((struct IORequest *)timer_request);
    if (timer_request)
        DeleteIORequest((struct IORequest *)timer_request);
    if (timer_port)
        DeleteMsgPort(timer_port);
    free(mapped);
    free(f.pixels);
    render_shutdown();
    if (CyberGfxBase)
        CloseLibrary(CyberGfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
}
int main(int argc, char **argv) {
    int i, j, back = 1, quit = 0, lowres = 0;
    uint64_t accumulator = 0, last_time;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--aga"))
            aga = 1;
        else if (!strcmp(argv[i], "--rtg"))
            rtg = 1;
        else if (!strcmp(argv[i], "--lowres"))
            lowres = 1;
        else if (strcmp(argv[i], "--ecs")) {
            puts("Space --ecs | --aga | --rtg [--lowres (native only)]");
            return 1;
        }
    }
    if (rtg && lowres) {
        puts("--lowres is for native ECS/AGA output.");
        return 1;
    }
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR) "graphics.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    if (!GfxBase || !IntuitionBase) {
        puts("Space currently requires Kickstart 3.0+.");
        close_all();
        return 1;
    }
    if (!rtg && aga && !(GfxBase->ChipRevBits0 & GFXF_AA_LISA)) {
        puts("--aga requires AGA hardware.");
        close_all();
        return 1;
    }
    has_aga = (GfxBase->ChipRevBits0 & GFXF_AA_LISA) != 0;
    depth = aga ? 8 : 5;
    video = native_video((GfxBase->DisplayFlags & PAL) != 0, lowres);
    if (!rtg)
        clock_rate = video.rate;
    f.w = rtg ? 640 : 320;
    f.h = rtg ? 480 : video.height;
    f.enhanced = aga || rtg;
    f.pixels = calloc((size_t)f.w * f.h, 1);
    if (!rtg && !aga)
        mapped = malloc((size_t)f.w * f.h);
    if (!f.pixels || (!rtg && !aga && !mapped)) {
        close_all();
        return 1;
    }
    if (!render_init(&f)) {
        close_all();
        return 1;
    }
    game_init(&g, 0x53504143);
    if (rtg) {
        timer_port = CreateMsgPort();
        if (timer_port)
            timer_request =
                (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer_request));
        if (!timer_request || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK,
                                         (struct IORequest *)timer_request, 0)) {
            puts("Unable to open timer.device.");
            close_all();
            return 1;
        }
        TimerBase = timer_request->tr_node.io_Device;
        {
            struct EClockVal ec;
            clock_rate = ReadEClock(&ec);
        }
        if (!open_rtg()) {
            puts("Unable to open RTG screen.");
            close_all();
            return 1;
        }
    } else {
        for (i = 0; i < 2; i++) {
            planes[i] = AllocMem(40 * f.h * depth, MEMF_CHIP | MEMF_CLEAR);
            copper[i] = AllocMem(4096, MEMF_CHIP | MEMF_CLEAR);
            if (!planes[i] || !copper[i]) {
                close_all();
                return 1;
            }
            build_copper(i);
        }
        for (i = 0; i < 256; i++) {
            int best = 0, error = 1000000;
            for (j = 0; j < 32; j++) {
                int dr = (int)((f.palette[i] >> 16) & 255) - (int)((f.palette[j] >> 16) & 255),
                    dg = (int)((f.palette[i] >> 8) & 255) - (int)((f.palette[j] >> 8) & 255),
                    db = (int)(f.palette[i] & 255) - (int)(f.palette[j] & 255),
                    e = dr * dr + dg * dg + db * db;
                if (e < error) {
                    best = j;
                    error = e;
                }
            }
            ecs_map[i] = (UBYTE)best;
        }
        takeover();
        render_set_pump(keyboard);
    }
    last_time = clock_now();
    while (!quit) {
        Controls c = {0};
        int request = 0;
        if (rtg) {
            struct IntuiMessage *m;
            while ((m = (struct IntuiMessage *)GetMsg(window->UserPort))) {
                UWORD code = m->Code;
                if (!(code & 128) && !held[code & 127])
                    pressed[code & 127] = 1;
                held[code & 127] = (code & 128) ? 0 : 1;
                ReplyMsg((struct Message *)m);
            }
        } else
            keyboard();
        for (i = 0; i < 128; i++) {
            if (pressed[i]) {
                if (i == 0x45)
                    quit = 1;
                else if (i == 0x54)
                    request = 1;
                else if (i == 0x58)
                    request = 2;
                else
                    game_action(&g, raw_action(i));
            }
            pressed[i] = 0;
        }
        if (request) {
            restore();
            if (request == 1)
                snprintf(g.message, sizeof(g.message), "%s",
                         save_game(&g, "PROGDIR:space.sav") ? "SAVED"
                                                            : "SAVE FAILED - OLD SLOT PRESERVED");
            else if (!load_game(&g, "PROGDIR:space.sav"))
                snprintf(g.message, sizeof(g.message), "LOAD FAILED");
            if (!rtg)
                takeover();
            last_time = clock_now();
            accumulator = 0;
        }
        if (g.page == FLIGHT) {
            c.pitch = held[0x4c] - held[0x4d];
            c.yaw = held[0x4e] - held[0x4f];
            c.thrust = held[0x11] - held[0x21];
            c.fire = held[0x63];
        }
        {
            uint64_t now = clock_now(),
                     elapsed = rtg ? now - last_time : (now - last_time) & 0xffffffU;
            last_time = now;
            /* Never replace missed frames with a single large integration step. */
            if (elapsed > clock_rate / 4)
                elapsed = clock_rate / 4;
            accumulator += elapsed * HZ;
            while (accumulator >= clock_rate) {
                game_frame(&g, c, 1);
                accumulator -= clock_rate;
            }
        }
        render_game(&g, &f);
        if (rtg) {
            struct RastPort rp = screen->RastPort;
            rp.BitMap = buffers[back]->sb_BitMap;
            rp.Layer = 0;
            WritePixelArray(f.pixels, 0, 0, f.w, &rp, 0, 0, f.w, f.h, RECTFMT_LUT8);
            while (!ChangeScreenBuffer(screen, buffers[back]))
                WaitTOF();
            wait_safe();
            displayed = back;
            back = 1 - back;
        } else {
            const UBYTE *src = f.pixels;
            if (!aga) {
                for (i = 0; i < f.w * f.h; i++)
                    mapped[i] = ecs_map[f.pixels[i]];
                src = mapped;
            }
            c2p(src, planes[back], f.w, f.h, depth);
            vblank();
            hw->cop1lc = (ULONG)copper[back];
            hw->copjmp1 = 0;
            back = 1 - back;
        }
    }
    close_all();
    return 0;
}

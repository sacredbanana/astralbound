/* SPDX-License-Identifier: Unlicense
   Native PAL/NTSC ECS/AGA takeover; RTG stays under the graphics driver. */
#include "space.h"
#include "flight_input.h"
#include "ui.h"
#include "native_video.h"
#include "controller.h"
#include <proto/lowlevel.h>
#include <libraries/lowlevel.h>
#ifndef JP_TYPE_ANALOGUE
#define JP_TYPE_ANALOGUE (14UL << 28) /* Poseidon lowlevel extension */
#endif
#include <proto/potgo.h>
#include <resources/potgo.h>
#define System AmigaDOSSystem
#include <proto/dos.h>
#undef System
#include <proto/icon.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
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
struct Library *LowLevelBase, *IconBase;
struct Library *PotgoBase;
static UWORD pot_bits;
static Controller pad;
static int analog, analog_x = 128, analog_y = 128, highcolour, rtg_depth = 8;
static ULONG pad_state;
static uint64_t analog_last;
static int profiling;
static BPTR wb_old_dir;
static int wb_changed;
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
static UWORD *blank_pointer;
static MouseStick mouse;
static UWORD mouse_last;
static int mouse_right, mouse_left, input_active = 1;
static void native_mouse(void) {
    UWORD now = hw->joy0dat;
    int dx = ((int)(now & 255) - (int)(mouse_last & 255) + 128) & 255;
    int dy = ((int)(now >> 8) - (int)(mouse_last >> 8) + 128) & 255;
    mouse_last = now;
    mouse_right = !(hw->potinp & 0x0400);
    mouse_left = !(*(volatile UBYTE *)0xbfe001 & 0x40);
    if (!mouse_right) ui_move(&g, &f, g.ui_x + dx - 128, g.ui_y + dy - 128);
    mouse_stick(&mouse, dx - 128, dy - 128,
                mouse_right && g.page == FLIGHT && !g.paused);
}

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
#include "amiga_audio.h"
static void controller_poll(void) {
    if (analog) {
        uint64_t now = clock_now();
        UWORD data;
        if (now - analog_last < clock_rate / 50) return;
        analog_last = now;
        data = hw->pot1dat;
        analog_x = data & 255; analog_y = data >> 8;
        WritePotgo(1, 0xf001);
    } else if (LowLevelBase) pad_state = ReadJoyPort(1);
}
static unsigned controller_buttons(ULONG state) {
    unsigned b = 0;
    if (state & JPF_BUTTON_RED) b |= PAD_FIRE;
    if (state & JPF_BUTTON_BLUE) b |= PAD_BRAKE;
    if (state & JPF_BUTTON_YELLOW) b |= PAD_TARGET;
    if (state & JPF_BUTTON_GREEN) b |= PAD_AUTO;
    if (state & JPF_BUTTON_FORWARD) b |= PAD_THRUST;
    if (state & JPF_BUTTON_REVERSE) b |= PAD_REVERSE;
    if (state & JPF_BUTTON_PLAY) b |= PAD_PAUSE;
    return b;
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
static void native_clear_planes(void) {
    int b, p;
    /* OwnBlitter already held, interrupts disabled: poll hardware directly. */
    hw->dmacon = 0x8240;
    for (b = 0; b < 2; b++) for (p = 0; p < depth; p++) {
        while (hw->dmaconr & 0x4000) {}
        hw->bltcon0 = 0x0100; hw->bltcon1 = 0;
        hw->bltdmod = 0;
        hw->bltdpt = planes[b] + p * 40 * f.h;
        hw->bltsize = (UWORD)((f.h << 6) | 20);
        /* The first status read may precede the busy flag on old Agnus. */
        (void)hw->dmaconr;
        while (hw->dmaconr & 0x4000) {}
    }
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
    native_clear_planes();
    vblank();
    hw->cop1lc = (ULONG)copper[0];
    hw->copjmp1 = 0;
    hw->dmacon = 0x83c0; /* display + copper + blitter */
    audio_native_start();
    taken = 1;
    mouse_last = hw->joy0dat;
    mouse_stick(&mouse, 0, 0, 0);
}
static void restore(void) {
    if (!taken)
        return;
    audio_native_stop();
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
    sound_pump();
    native_mouse();
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
    case 0x29: return ';';
    case 0x09: return '9';
    case 0x0a: return '0';
    case 0x0d: return '\\';
    case 0x38: return ',';
    case 0x39: return '.';
    case 0x3a: return '/';
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
    rtg_depth = highcolour ? 16 : 8;
    id = BestCModeIDTags(CYBRBIDTG_NominalWidth, 640, CYBRBIDTG_NominalHeight, 480, CYBRBIDTG_Depth,
                         rtg_depth, TAG_DONE);
    if (id == (ULONG)INVALID_ID || !IsCyberModeID(id)) {
        puts("No compatible RTG mode.");
        return 0;
    }
    screen = OpenScreenTags(0, SA_DisplayID, id, SA_Width, 640, SA_Height, 480, SA_Depth, rtg_depth,
                            SA_Type, CUSTOMSCREEN, SA_Quiet, TRUE, SA_ShowTitle, FALSE, TAG_DONE);
    if (!screen)
        return 0;
    window = OpenWindowTags(0, WA_CustomScreen, (ULONG)screen, WA_Borderless, TRUE, WA_Backdrop,
                            TRUE, WA_Activate, TRUE, WA_ReportMouse, TRUE, WA_MouseQueue, 32,
                            WA_Flags, WFLG_BORDERLESS | WFLG_BACKDROP | WFLG_ACTIVATE |
                                      WFLG_REPORTMOUSE | WFLG_RMBTRAP,
                            WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS |
                                      IDCMP_DELTAMOVE | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
                            TAG_DONE);
    if (!window)
        return 0;
    blank_pointer = AllocMem(12, MEMF_CHIP | MEMF_CLEAR);
    if (blank_pointer) SetPointer(window, blank_pointer, 1, 16, 0, 0);
    palette[0] = 256UL << 16;
    for (i = 0; i < 256; i++) {
        ULONG c = f.palette[i];
        palette[1 + i * 3] = ((c >> 16) & 255) * 0x01010101UL;
        palette[2 + i * 3] = ((c >> 8) & 255) * 0x01010101UL;
        palette[3 + i * 3] = (c & 255) * 0x01010101UL;
    }
    palette[769] = 0;
    if (!highcolour) LoadRGB32(&screen->ViewPort, palette);
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
    sound_close();
    music_close();
    if (pot_bits) FreePotBits(pot_bits);
    if (LowLevelBase) {
        SetJoyPortAttrs(1, SJA_Reinitialize, TRUE, TAG_DONE);
        CloseLibrary(LowLevelBase);
    }
    if (wb_changed) CurrentDir(wb_old_dir);
    if (IconBase) CloseLibrary(IconBase);
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
        if (window) {
            if (blank_pointer) ClearPointer(window);
            CloseWindow(window);
        }
        if (blank_pointer) FreeMem(blank_pointer, 12);
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
    uint64_t render_ticks = 0, output_ticks = 0, tick_start = 0;
    unsigned frames = 0;
    if (argc == 0) {
        struct WBStartup *wb = (struct WBStartup *)argv;
        struct DiskObject *icon;
        wb_old_dir = CurrentDir(wb->sm_ArgList[0].wa_Lock); wb_changed = 1;
        IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 39);
        icon = IconBase ? GetDiskObject(wb->sm_ArgList[0].wa_Name) : NULL;
        if (icon) {
            STRPTR mode = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"MODE");
            aga = mode && !strcmp((char *)mode, "AGA");
            rtg = mode && (!strcmp((char *)mode, "RTG") || !strcmp((char *)mode, "RTG16"));
            highcolour = mode && !strcmp((char *)mode, "RTG16");
            lowres = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"LOWRES") != NULL;
            audio_muted = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"NOSOUND") != NULL;
            analog = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"ANALOG") != NULL;
            FreeDiskObject(icon);
        }
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--nosound")) audio_muted = 1;
        else if (!strcmp(argv[i], "--analog")) analog = 1;
        else if (!strcmp(argv[i], "--profile")) profiling = 1;
        else if (!strcmp(argv[i], "--rtg16")) rtg = highcolour = 1;
        else if (!strcmp(argv[i], "--aga"))
            aga = 1;
        else if (!strcmp(argv[i], "--rtg"))
            rtg = 1;
        else if (!strcmp(argv[i], "--lowres"))
            lowres = 1;
        else if (strcmp(argv[i], "--ecs")) {
            puts("Space --ecs | --aga | --rtg | --rtg16 [--lowres] [--analog] [--nosound] [--profile]");
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
    if (analog) {
        PotgoBase = OpenResource((CONST_STRPTR)POTGONAME);
        if (PotgoBase) pot_bits = AllocPotBits(0x5001);
        if ((pot_bits & 0x5001) != 0x5001) {
            puts("Analogue port unavailable: cannot own POT counters."); close_all(); return 1;
        }
        WritePotgo(1, 0xf001);
        WaitTOF(); WaitTOF();
        analog_x = hw->pot1dat & 255; analog_y = hw->pot1dat >> 8;
        controller_calibrate(&pad, analog_x, analog_y);
    } else {
        LowLevelBase = OpenLibrary((CONST_STRPTR)"lowlevel.library", 40);
        if (LowLevelBase) {
            pad_state = ReadJoyPort(1); /* acquire resources in task context */
            if ((pad_state & JP_TYPE_MASK) == JP_TYPE_NOTAVAIL) {
                CloseLibrary(LowLevelBase); LowLevelBase = NULL;
            }
        }
    }
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
            if (rtg) clock_rate = ReadEClock(&ec);
        }
    }
    if (rtg) {
        if (!open_rtg()) {
            puts("Unable to open RTG screen.");
            close_all();
            return 1;
        }
    } else {
        for (i = 0; i < 2; i++) {
            planes[i] = AllocMem(40 * f.h * depth, MEMF_CHIP);
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
    }
    /* Reserve display/audio buffers first, so optional tracks cannot consume
       memory needed to start the game. DOS is still available here. */
    if (!sound_open()) {
        sound_close(); puts("Audio unavailable; continuing silently.");
    } else if (!audio_muted) {
        music_load("PROGDIR:music", (uint32_t)clock_now());
        audio_observe(&sound, &g);
        if (!rtg) {
            music_mix((int8_t *)stream_pcm[0], STREAM_SAMPLES);
            music_mix((int8_t *)stream_pcm[1], STREAM_SAMPLES);
        }
    }
    if (!rtg) {
        takeover();
        render_set_pump(keyboard);
    }
    ui_move(&g, &f, f.w / 2, f.h / 2);
    last_time = clock_now();
    while (!quit) {
        Controls c = {0};
        int request = 0;
        if (rtg) {
            struct IntuiMessage *m;
            while ((m = (struct IntuiMessage *)GetMsg(window->UserPort))) {
                UWORD code = m->Code;
                if (m->Class == IDCMP_RAWKEY) {
                    if (!(code & 128) && !held[code & 127])
                        pressed[code & 127] = 1;
                    held[code & 127] = (code & 128) ? 0 : 1;
                } else if (m->Class == IDCMP_MOUSEBUTTONS) {
                    if (code == MENUDOWN) mouse_right = 1;
                    if (code == MENUUP) mouse_right = 0;
                    if (code == SELECTDOWN) mouse_left = 1;
                    if (code == SELECTUP) mouse_left = 0;
                    if (!mouse_right) mouse_stick(&mouse, 0, 0, 0);
                    { int u = ui_pointer(&g, &f, g.ui_x, g.ui_y, mouse_left);
                      if (u == UI_SAVE) request = 1;
                      if (u == UI_LOAD) request = 2;
                      if (u == UI_EXIT) quit = 1; }
                } else if (m->Class == IDCMP_MOUSEMOVE) {
                    if (!mouse_right) {
                        ui_move(&g, &f, g.ui_x + m->MouseX, g.ui_y + m->MouseY);
                        ui_pointer(&g, &f, g.ui_x, g.ui_y, mouse_left);
                    }
                    mouse_stick(&mouse, m->MouseX, m->MouseY,
                                mouse_right && g.page == FLIGHT && !g.paused);
                } else if (m->Class == IDCMP_INACTIVEWINDOW) {
                    ui_cancel(&g);
                    input_active = mouse_right = mouse_left = 0;
                    memset(held, 0, sizeof(held));
                    memset(pressed, 0, sizeof(pressed));
                    mouse_stick(&mouse, 0, 0, 0);
                } else if (m->Class == IDCMP_ACTIVEWINDOW) input_active = 1;
                ReplyMsg((struct Message *)m);
            }
        } else
            keyboard();
        if (input_active) {
            int ui_request = ui_pointer(&g, &f, g.ui_x, g.ui_y, mouse_left);
            if (ui_request == UI_SAVE) request = 1;
            if (ui_request == UI_LOAD) request = 2;
            if (ui_request == UI_EXIT) quit = 1;
        }
        controller_poll();
        for (i = 0; i < 128; i++) {
            if (pressed[i]) {
                if (i == 0x45)
                    quit = 1;
                else if (i == 0x54)
                    request = 1;
                else if (i == 0x58)
                    request = 2;
                else if (i == 0x33 && analog) {
                    controller_calibrate(&pad, analog_x, analog_y);
                    snprintf(g.message, sizeof(g.message), "ANALOGUE CENTRE SET");
                } else game_action(&g, raw_action(i));
            }
            pressed[i] = 0;
        }
        if (request) {
            restore();
            if (!rtg) sound_close();
            if (request == 1) {
                int saved = save_game(&g, "PROGDIR:space.sav");
                int flushed = 0;
                LONG storage_error = 0;
                if (saved) {
                    BPTR lock = GetProgramDir();
                    int borrowed = lock != 0;
                    if (!lock) lock = Lock((CONST_STRPTR)"", SHARED_LOCK);
                    if (lock) {
                        struct FileLock *directory = (struct FileLock *)BADDR(lock);
                        LONG result;
                        SetIoErr(0);
                        result = DoPkt0(directory->fl_Task, ACTION_FLUSH);
                        storage_error = IoErr();
                        /* Legacy handlers may return zero with no packet error. */
                        flushed = result != 0 || storage_error == 0;
                        if (!borrowed) UnLock(lock);
                    }

                }
                snprintf(g.message, sizeof(g.message), "%s", !saved ? "SAVE FAILED - OLD SLOT PRESERVED" :
                         flushed ? "SAVED" : "SAVED - FILESYSTEM FLUSH FAILED");
                if (saved && !flushed && !rtg) {
                    printf("Filesystem flush failed (%ld); leaving the OS active.\n", (long)storage_error);
                    quit = 1; break;
                }
            }
            else if (!load_game(&g, "PROGDIR:space.sav"))
                snprintf(g.message, sizeof(g.message), "LOAD FAILED");
            if (!rtg) {
                if (!sound_open()) sound_close();
                takeover();
            }
            last_time = clock_now();
            accumulator = 0;
            audio_init(&sound, &g);
        }
        if (g.page != FLIGHT || g.paused)
            mouse_stick(&mouse, 0, 0, 0);
        if (g.page == FLIGHT && input_active) {
            UWORD joy = hw->joy1dat;
            c.pitch = held[0x4c] - held[0x4d];
            c.roll = held[0x4e] - held[0x4f];
            c.thrust = held[0x11] - held[0x21];
            c.fire = held[0x63];
            flight_input(&c, &mouse, analog ? 0 : joystick_roll(joy), analog ? 0 : joystick_pitch(joy),
                         ui_fire(&g, &f) || !(*(volatile UBYTE *)0xbfe001 & 0x80));
        }
        if (input_active) {
            ULONG type = pad_state & JP_TYPE_MASK;
            int usb_analog = type == JP_TYPE_ANALOGUE;
            unsigned buttons = !analog && (type == JP_TYPE_GAMECTLR || type == JP_TYPE_JOYSTK || usb_analog)
                               ? controller_buttons(pad_state) : 0;
            if (usb_analog && !pad.calibrated) controller_calibrate(&pad, pad_state & 255, (pad_state >> 8) & 255);
            if (type == JP_TYPE_GAMECTLR || type == JP_TYPE_JOYSTK) {
                c.roll = flight_axis(c.roll + !!(pad_state & JPF_JOY_RIGHT) - !!(pad_state & JPF_JOY_LEFT), 0);
                c.pitch = flight_axis(c.pitch + !!(pad_state & JPF_JOY_UP) - !!(pad_state & JPF_JOY_DOWN), 0);
            }
            controller_apply(&pad, &g, &c, buttons,
                             analog ? analog_x : (int)(pad_state & 255),
                             analog ? analog_y : (int)((pad_state >> 8) & 255), analog || usb_analog);
        } else pad.held = 0;
        {
            uint64_t now = clock_now(),
                     elapsed = rtg ? now - last_time : (now - last_time) & 0xffffffU;
            last_time = now;
            /* Gather elapsed ticks so simulation work is bounded per display frame. */
            if (elapsed > clock_rate / 4)
                elapsed = clock_rate / 4;
            accumulator += elapsed * HZ;
            {
                int due = (int)(accumulator / clock_rate);
                accumulator -= (uint64_t)due * clock_rate;
                game_frame(&g, c, due);
            }
        }
        sound_update();
        if (profiling) tick_start = clock_now();
        g.ui_visible = input_active && !mouse_right;
        render_game(&g, &f);
        if (profiling) { uint64_t now = clock_now(); render_ticks += rtg ? now - tick_start : (now - tick_start) & 0xffffffU; tick_start = now; }
        if (rtg) {
            struct RastPort rp = screen->RastPort;
            rp.BitMap = buffers[back]->sb_BitMap;
            rp.Layer = 0;
            if (highcolour) WriteLUTPixelArray(f.pixels, 0, 0, f.w, &rp, f.palette,
                                               0, 0, f.w, f.h, CTABFMT_XRGB8);
            else WritePixelArray(f.pixels, 0, 0, f.w, &rp, 0, 0, f.w, f.h, RECTFMT_LUT8);
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
        if (profiling) {
            uint64_t now = clock_now();
            output_ticks += rtg ? now - tick_start : (now - tick_start) & 0xffffffU;
            if (++frames >= 120) quit = 1;
        }
    }
    if (profiling && frames) {
        struct EClockVal ec; ULONG rate;
        restore(); rate = rtg ? ReadEClock(&ec) : clock_rate;
        printf("%lu frames: render %lu us/frame, output+sync %lu us/frame, %dx%d %s\n",
               (unsigned long)frames, (unsigned long)(render_ticks * 1000000 / rate / frames),
               (unsigned long)(output_ticks * 1000000 / rate / frames), f.w, f.h,
               rtg ? (highcolour ? "RTG16" : "RTG8") : (aga ? "AGA" : "ECS"));
    }
    close_all();
    return 0;
}

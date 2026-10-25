/* SPDX-License-Identifier: Unlicense
   Native PAL/NTSC ECS/AGA takeover; RTG stays under the graphics driver. */
#include "astralbound.h"
#include "flight_input.h"
#include "ui.h"
#include "native_video.h"
#include "controller.h"
#include "intro.h"
#include "launcher.h"
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
static FootStick walker = {1, 1};
static int analog, analog_x = 128, analog_y = 128, highcolour, rtg_depth = 8;
static ULONG pad_state;
static uint64_t analog_last;
static int profiling, show_intro = 1;
static Intro intro;
static Settings preferences;
static Launcher launch_menu;
static Performance performance;
static int testing, testing_armed, no_menu;
static BPTR wb_old_dir;
static int wb_changed;
static struct MsgPort *timer_port;
static struct timerequest *timer_request;
static ULONG clock_rate = 50;
/* Save/load keep a commander copy and the stream on the stack. */
unsigned long __stack = 262144;
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
    mouse_right = !g.ui_quit && !(hw->potinp & 0x0400);
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
    case 0x41: return 8;
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
    id = BestCModeIDTags(CYBRBIDTG_NominalWidth, f.w, CYBRBIDTG_NominalHeight, f.h, CYBRBIDTG_Depth,
                         rtg_depth, TAG_DONE);
    if (id == (ULONG)INVALID_ID || !IsCyberModeID(id)) {
        puts("No compatible RTG mode.");
        return 0;
    }
    screen = OpenScreenTags(0, SA_DisplayID, id, SA_Width, f.w, SA_Height, f.h, SA_Depth, rtg_depth,
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
    GfxBase=NULL; IntuitionBase=NULL; CyberGfxBase=NULL; TimerBase=NULL;
    LowLevelBase=NULL; IconBase=NULL; PotgoBase=NULL; pot_bits=0; wb_changed=0;
    timer_port=NULL; timer_request=NULL; screen=NULL; window=NULL; safe_port=NULL;
    buffers[0]=buffers[1]=NULL; planes[0]=planes[1]=NULL; copper[0]=copper[1]=NULL;
    blank_pointer=NULL; mapped=NULL; f.pixels=NULL; displayed=0; input_active=1;
    memset(held,0,sizeof(held)); memset(pressed,0,sizeof(pressed));
    mouse_right=mouse_left=0; pad.held=0;

}
#include "amiga_launcher.inc"

int main(int argc, char **argv) {
    int i, j, back = 1, quit = 0, lowres = 0;
    uint64_t accumulator = 0, last_time;
    uint64_t render_ticks = 0, output_ticks = 0, tick_start = 0;
    unsigned frames = 0;
    const char *settings_path="PROGDIR:astralbound.cfg";
    settings_defaults(&preferences,1);
    settings_load(&preferences,settings_path);
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
            if(mode) preferences.mode=rtg ? (highcolour ? VIDEO_RTG16 : VIDEO_RTG) : aga ? VIDEO_AGA : VIDEO_ECS;
            lowres = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"LOWRES") != NULL;
            audio_muted = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"NOSOUND") != NULL;
            analog = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"ANALOG") != NULL;
            show_intro = FindToolType((CONST_STRPTR *)icon->do_ToolTypes, (CONST_STRPTR)"NOINTRO") == NULL;
            if(lowres) preferences.compact=1;
            if(audio_muted) preferences.sound=0;
            if(!show_intro) preferences.intro=0;
            no_menu=FindToolType((CONST_STRPTR *)icon->do_ToolTypes,(CONST_STRPTR)"NOMENU")!=NULL;
            FreeDiskObject(icon);
        }
    }
    aga=preferences.mode==VIDEO_AGA; rtg=preferences.mode>=VIDEO_RTG; highcolour=preferences.mode==VIDEO_RTG16;
    lowres=preferences.compact; audio_muted=!preferences.sound; show_intro=preferences.intro;
    for (i = 1; i < argc; i++) {
        if(!strcmp(argv[i],"--nomenu")) no_menu=1;
        else if(!strcmp(argv[i],"--benchmark")) { testing=1; no_menu=1; }
        else if(!strcmp(argv[i],"--preset") && i+1<argc) {
            static const char *names[]={"low","medium","high","ultra","custom"}; int k; i++;
            for(k=0;k<5;k++) if(!strcmp(argv[i],names[k])) settings_preset(&preferences,k);
        }
        else if (!strcmp(argv[i], "--nosound")) audio_muted = 1;
        else if (!strcmp(argv[i], "--analog")) analog = 1;
        else if (!strcmp(argv[i], "--profile")) profiling = 1;
        else if (!strcmp(argv[i], "--nointro")) show_intro = 0;
        else if (!strcmp(argv[i], "--rtg16")) { rtg=highcolour=1; aga=0; }
        else if (!strcmp(argv[i], "--aga")) { aga=1; rtg=highcolour=0; }
        else if (!strcmp(argv[i], "--rtg")) { rtg=1; highcolour=aga=0; }
        else if (!strcmp(argv[i], "--lowres"))
            lowres = 1;
        else if(!strcmp(argv[i],"--ecs")) aga=rtg=highcolour=0;
        else {
            puts("Astralbound --ecs | --aga | --rtg | --rtg16 [--lowres] [--analog] [--nosound] [--nointro] [--profile] [--nomenu] [--preset low|medium|high|ultra|custom] [--benchmark]");
            return 1;
        }
    }
    preferences.mode=rtg ? (highcolour ? VIDEO_RTG16 : VIDEO_RTG) : aga ? VIDEO_AGA : VIDEO_ECS;
    preferences.compact=lowres; preferences.sound=!audio_muted; preferences.intro=show_intro;
    launcher_init(&launch_menu,&preferences,1,1);
launch_again:
    accumulator=0; render_ticks=output_ticks=tick_start=0; frames=0; back=1; quit=0;
    memset(&intro,0,sizeof(intro)); testing_armed=0;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR) "graphics.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR) "intuition.library", 39);
    if (!GfxBase || !IntuitionBase) {
        puts("Astralbound currently requires Kickstart 3.0+.");
        close_all();
        return 1;
    }
    if(!no_menu && !profiling) {
        struct Library *cgx=OpenLibrary((CONST_STRPTR)"cybergraphics.library",41);
        int action;
        launch_menu.available_aga=(GfxBase->ChipRevBits0&GFXF_AA_LISA)!=0;
        launch_menu.available_rtg=cgx!=NULL;
        if(cgx) CloseLibrary(cgx);
        if(launch_menu.settings.mode==VIDEO_AGA && !launch_menu.available_aga) launch_menu.settings.mode=VIDEO_ECS;
        if(launch_menu.settings.mode>=VIDEO_RTG && !launch_menu.available_rtg) launch_menu.settings.mode=VIDEO_ECS;
        action=amiga_launcher(&launch_menu,settings_path);
        if(!settings_save(&launch_menu.settings,settings_path)) puts("Graphics settings could not be saved.");
        if(action==LAUNCH_QUIT) { close_all(); return 0; }
        testing=action==LAUNCH_TEST;
        preferences=launch_menu.settings;
    }
    aga=preferences.mode==VIDEO_AGA; rtg=preferences.mode>=VIDEO_RTG;
    highcolour=preferences.mode==VIDEO_RTG16; lowres=preferences.compact;
    audio_muted=!preferences.sound; show_intro=preferences.intro;
    render_graphics(&preferences.gfx);
    render_renderer(preferences.renderer);
    render_bezier(preferences.bezier);
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
    settings_dimensions(&preferences,(GfxBase->DisplayFlags&PAL)!=0,&f.w,&f.h);
    f.enhanced = aga || rtg;
    f.pixels = calloc((size_t)f.w * f.h, 1);
    if (!rtg && !aga)
        mapped = malloc((size_t)f.w * f.h);
    if (!f.pixels || (!rtg && !aga && !mapped)) {
        goto session_failed;
    }
    if (!render_init(&f)) {
        goto session_failed;
    }
    game_init(&g, 0x53504143);
    /* The title sequence chooses its light with DOS still running, so the
       search cannot stall a native display. Profiling measures flight. */
    if(testing) { performance_begin(&performance,&g,&preferences,launch_menu.test_scene); performance.report.width=f.w; performance.report.height=f.h; }
    else if (show_intro && !profiling) intro_start(&intro, &g, 0x53504143);
    if (analog) {
        PotgoBase = OpenResource((CONST_STRPTR)POTGONAME);
        if (PotgoBase) pot_bits = AllocPotBits(0x5001);
        if ((pot_bits & 0x5001) != 0x5001) {
            puts("Analogue port unavailable: cannot own POT counters."); goto session_failed;
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
            goto session_failed;
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
            goto session_failed;
        }
    } else {
        for (i = 0; i < 2; i++) {
            planes[i] = AllocMem(40 * f.h * depth, MEMF_CHIP);
            copper[i] = AllocMem(4096, MEMF_CHIP | MEMF_CLEAR);
            if (!planes[i] || !copper[i]) {
                goto session_failed;
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
        uint64_t test_start=clock_now(),render_start=0,output_start=0;
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
                    if (code == MENUDOWN && !g.ui_quit) mouse_right = 1;
                    if (code == MENUUP) mouse_right = 0;
                    if (code == SELECTDOWN) mouse_left = 1;
                    if (code == SELECTUP) mouse_left = 0;
                    if (!mouse_right) mouse_stick(&mouse, 0, 0, 0);
                    if (!testing && !intro.running) { int u = ui_pointer(&g, &f, g.ui_x, g.ui_y, mouse_left);
                      if (u == UI_SAVE) request = 1;
                      if (u == UI_LOAD) request = 2;
                      if (u == UI_EXIT) quit = 1;
                      if (u == UI_SAVE_EXIT) request = 3; }
                } else if (m->Class == IDCMP_MOUSEMOVE) {
                    if (!mouse_right) {
                        ui_move(&g, &f, g.ui_x + m->MouseX, g.ui_y + m->MouseY);
                        if (!testing && !intro.running) ui_pointer(&g, &f, g.ui_x, g.ui_y, mouse_left);
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
        if (!testing && input_active && !intro.running) {
            int ui_request = ui_pointer(&g, &f, g.ui_x, g.ui_y, mouse_left);
            if (ui_request == UI_SAVE) request = 1;
            if (ui_request == UI_LOAD) request = 2;
            if (ui_request == UI_EXIT) quit = 1;
            if (ui_request == UI_SAVE_EXIT) request = 3;
        }
        controller_poll();
        if(testing) {
            unsigned buttons=mouse_left || !(*(volatile UBYTE *)0xbfe001&0x80);
            if(LowLevelBase) buttons|=controller_buttons(pad_state);
            if(!buttons) testing_armed=1;
            if(pressed[0x45] || (testing_armed && buttons)) quit=1;
            memset(pressed,0,sizeof(pressed));
        }
        if (intro.running) {
            /* Any key, click, fire button or pad button skips the sequence. */
            int skip = mouse_left || !(*(volatile UBYTE *)0xbfe001 & 0x80);
            ULONG type = pad_state & JP_TYPE_MASK;
            if (LowLevelBase && (type == JP_TYPE_GAMECTLR || type == JP_TYPE_JOYSTK) &&
                controller_buttons(pad_state)) skip = 1;
            for (i = 0; i < 128; i++) { skip |= pressed[i]; pressed[i] = 0; }
            if (skip && intro_skippable(&intro)) {
                intro_finish(&intro, &g);
                audio_init(&sound, &g);
            }
        }
        for (i = 0; i < 128; i++) {
            if (pressed[i]) {
                if (i == 0x45 || g.ui_quit) {
                    int u = ui_quit_key(&g, i == 0x45 ? 27 : raw_action(i));
                    if (u == UI_EXIT) quit = 1;
                    if (u == UI_SAVE_EXIT) request = 3;
                }
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
            if (request == 1 || request == 3) {
                int saved = save_game(&g, "PROGDIR:astralbound.sav");
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
                if (request == 3) {
                    if (saved && flushed) { quit = 1; break; }
                    g.ui_quit = 2;
                }
                if (saved && !flushed && !rtg && !g.ui_quit) {
                    printf("Filesystem flush failed (%ld); leaving the OS active.\n", (long)storage_error);
                    quit = 1; break;
                }
            }
            else if (!load_game(&g, "PROGDIR:astralbound.sav"))
                snprintf(g.message, sizeof(g.message), "LOAD FAILED");
            if (!rtg) {
                if (!sound_open()) sound_close();
                takeover();
            }
            last_time = clock_now();
            accumulator = 0;
            audio_init(&sound, &g);
        }
        if (g.page != FLIGHT || g.paused || g.ui_quit) {
            if (g.ui_quit) mouse_right = 0;
            mouse_stick(&mouse, 0, 0, 0);
        }
        if (!testing && g.page == FLIGHT && input_active && !g.ui_quit) {
            UWORD joy = hw->joy1dat;
            int fire = !(*(volatile UBYTE *)0xbfe001 & 0x80);
            c.pitch = held[0x4d] - held[0x4c];
            c.roll = held[0x4e] - held[0x4f];
            c.thrust = held[0x11] - held[0x21];
            c.fire = held[0x63];
            if (g.on_foot) {
                /* Port 2's stick, a lowlevel pad's d-pad and red button and
                   the fire line are one walker's stick, so fire taps once. */
                ULONG type = analog ? 0 : pad_state & JP_TYPE_MASK;
                int x = analog ? 0 : joystick_roll(joy), y = analog ? 0 : joystick_pitch(joy);
                c.strafe = held[0x22] - held[0x20];
                if (type == JP_TYPE_GAMECTLR || type == JP_TYPE_JOYSTK) {
                    x = flight_axis(x + !!(pad_state & JPF_JOY_RIGHT) - !!(pad_state & JPF_JOY_LEFT), 0);
                    y = flight_axis(y + !!(pad_state & JPF_JOY_DOWN) - !!(pad_state & JPF_JOY_UP), 0);
                }
                if (type == JP_TYPE_GAMECTLR || type == JP_TYPE_JOYSTK || type == JP_TYPE_ANALOGUE)
                    fire |= !!(pad_state & JPF_BUTTON_RED);
                flight_input(&c, &mouse, 0, 0, 0);
                if (foot_stick(&walker, &c, x, y, fire)) game_action(&g, 'e');
            } else
                flight_input(&c, &mouse, analog ? 0 : joystick_roll(joy), analog ? 0 : joystick_pitch(joy),
                             ui_fire(&g, &f) || fire);
        }
        /* Fire already down when the commander steps out is not a tap. */
        if (!g.on_foot) walker = (FootStick){1, 1};
        if (!testing && input_active && !g.ui_quit && !intro.running) {
            ULONG type = pad_state & JP_TYPE_MASK;
            int usb_analog = type == JP_TYPE_ANALOGUE;
            unsigned buttons = !analog && (type == JP_TYPE_GAMECTLR || type == JP_TYPE_JOYSTK || usb_analog)
                               ? controller_buttons(pad_state) : 0;
            if (usb_analog && !pad.calibrated) controller_calibrate(&pad, pad_state & 255, (pad_state >> 8) & 255);
            if (!g.on_foot && (type == JP_TYPE_GAMECTLR || type == JP_TYPE_JOYSTK)) {
                c.roll = flight_axis(c.roll + !!(pad_state & JPF_JOY_RIGHT) - !!(pad_state & JPF_JOY_LEFT), 0);
                c.pitch = flight_axis(c.pitch + !!(pad_state & JPF_JOY_DOWN) - !!(pad_state & JPF_JOY_UP), 0);
            }
            controller_apply(&pad, &g, &c, buttons,
                             analog ? analog_x : (int)(pad_state & 255),
                             analog ? analog_y : (int)((pad_state >> 8) & 255), analog || usb_analog, 128, 128);
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
                if(testing) performance_animate(&performance,&g);
                else if (intro.running) {
                    if (!intro_frame(&intro, &g, due)) audio_init(&sound, &g);
                } else game_frame(&g, c, due);
            }
        }
        sound_update();
        if (profiling) tick_start = clock_now();
        if(testing) render_start=clock_now();
        g.ui_visible = input_active && !mouse_right && !intro.running;
        if (intro.running) render_intro(&intro, &g, &f);
        else if(testing) render_performance_scene(&g,&f);
        else render_game(&g, &f);
        if(testing) { render_performance(&performance,&f); output_start=clock_now(); }
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
        if(testing && !quit) {
            uint64_t end=clock_now();
            uint64_t render_delta=rtg ? output_start-render_start : (output_start-render_start)&0xffffffU;
            uint64_t output_delta=rtg ? end-output_start : (end-output_start)&0xffffffU;
            uint64_t total_delta=rtg ? end-test_start : (end-test_start)&0xffffffU;
            if(performance_record(&performance,&g,render_delta*1000000/clock_rate,
                 output_delta*1000000/clock_rate,total_delta*1000000/clock_rate)==1) quit=1;
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
    if(testing) performance_print(stdout,&performance.report);
    if(testing && !no_menu) { launcher_results(&launch_menu,&performance.report); goto launch_again; }
    return 0;
session_failed:
    close_all();
    if(no_menu || profiling) return 1;
    snprintf(launch_menu.message,sizeof(launch_menu.message),"VIDEO/MEMORY FAILED - LOWER OPTIONS");
    launch_menu.page=0; launch_menu.selected=1;
    goto launch_again;
}

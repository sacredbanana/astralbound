/* SPDX-License-Identifier: Unlicense; original two-plane ship icons. */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/dos.h>
#include <workbench/workbench.h>
#include <intuition/intuition.h>
#include <stdio.h>
#include <string.h>
struct Library *IconBase;
unsigned long __stack = 32768;
static UWORD bits[96];
static struct Image image;
static void dot(int x, int y, int color) {
    int p;
    for (p = 0; p < 2; p++) if (color & (1 << p)) bits[p * 48 + y * 2 + x / 16] |= 1U << (15 - (x & 15));
}
static int write_icon(const char *name, int type, const char *tool, STRPTR *types) {
    struct DiskObject icon;
    struct DrawerData drawer;
    struct DiskObject *readback;
    char path[160];
    memset(&icon, 0, sizeof(icon)); memset(&drawer, 0, sizeof(drawer));
    icon.do_Magic = WB_DISKMAGIC; icon.do_Version = WB_DISKVERSION;
    icon.do_Gadget.Width = 32; icon.do_Gadget.Height = 24;
    icon.do_Gadget.Flags = GFLG_GADGIMAGE | GFLG_GADGHCOMP;
    icon.do_Gadget.Activation = GACT_RELVERIFY;
    icon.do_Gadget.GadgetRender = &image;
    icon.do_Type = type; icon.do_DefaultTool = (STRPTR)tool; icon.do_ToolTypes = types;
    icon.do_CurrentX = icon.do_CurrentY = NO_ICON_POSITION;
    icon.do_StackSize = 262144;
    if (type == WBDRAWER || type == WBDISK) {
        drawer.dd_NewWindow.LeftEdge = 20; drawer.dd_NewWindow.TopEdge = 20;
        drawer.dd_NewWindow.Width = 500; drawer.dd_NewWindow.Height = 220;
        drawer.dd_Flags = DDFLAGS_SHOWICONS; drawer.dd_ViewModes = DDVM_BYICON;
        icon.do_DrawerData = &drawer;
    }
    snprintf(path, sizeof(path), "PROGDIR:%s", name);
    if (!PutDiskObject((CONST_STRPTR)path, &icon)) return 0;
    readback = GetDiskObject((CONST_STRPTR)path);
    if (!readback) return 0;
    if (readback->do_Type != type || readback->do_StackSize != 262144) {
        FreeDiskObject(readback); return 0;
    }
    FreeDiskObject(readback);
    return 1;
}
int main(void) {
    static STRPTR ecs[] = {(STRPTR)"MODE=ECS", NULL};
    static STRPTR install[] = {(STRPTR)"MINUSER=AVERAGE", (STRPTR)"APPNAME=Space", NULL};
    static const char *tools[] = {"Space.000", "Space.020", "Space.030", "Space.020fpu", "Space.040", "Space.060"};
    static const char *scripts[] = {"Play-AGA", "Play-RTG", "Play-RTG16", "Play-ECS-Lowres"};
    int x, y, i, ok = 1;
    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 39);
    if (!IconBase) return 1;
    image.Width = 32; image.Height = 24; image.Depth = 2;
    image.ImageData = bits; image.PlanePick = 3;
    for (y = 2; y < 21; y++) for (x = 2; x < 30; x++) {
        if ((x == 16 && y < 7) || (y >= 7 && x >= 16 - (y - 6) && x <= 16 + (y - 6))) {
            int color = x == 16 || x == 16 - (y - 6) || x == 16 + (y - 6) ? 1 : 2;
            if (y > 16 && x > 12 && x < 20) color = 3;
            dot(x, y, color);
        }
    }
    dot(3, 3, 1); dot(27, 4, 1); dot(5, 10, 1); dot(26, 12, 1);
    for (i = 0; i < 6; i++) ok &= write_icon(tools[i], WBTOOL, NULL, ecs);
    for (i = 0; i < 4; i++) ok &= write_icon(scripts[i], WBPROJECT, "C:IconX", NULL);
    ok &= write_icon("Install", WBPROJECT, "SYS:Utilities/Installer", install);
    ok &= write_icon("Space.guide", WBPROJECT, "SYS:Utilities/MultiView", NULL);
    ok &= write_icon("Space", WBDRAWER, NULL, NULL);
    ok &= write_icon("Disk", WBDISK, NULL, NULL);
    CloseLibrary(IconBase);
    puts(ok ? "Original Workbench icons written and reloaded." : "Icon generation failed.");
    return ok ? 0 : 1;
}

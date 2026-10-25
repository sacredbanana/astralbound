/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_LAUNCHER_H
#define ASTRALBOUND_LAUNCHER_H
#include "performance.h"
enum { LAUNCH_UP=1001, LAUNCH_DOWN, LAUNCH_LEFT, LAUNCH_RIGHT };
enum { LAUNCH_NONE, LAUNCH_PLAY, LAUNCH_TEST, LAUNCH_QUIT };
/* Row geometry of the 320x200 launcher page, shared by drawing and pointer. */
#define LAUNCHER_ROW_TOP 44
#define LAUNCHER_ROW_PITCH 9
/* Renderer, Bezier, then the 17 detail options (legacy scale is hidden). */
#define GRAPHICS_MENU_OPTIONS (GRAPHICS_OPTIONS + 1)
#define GRAPHICS_MENU_PAGES ((GRAPHICS_MENU_OPTIONS + 5) / 6)
typedef struct {
    Settings settings; PerformanceReport latest, previous;
    int page, selected, group, test_scene, down, available_aga, available_rtg;
    char message[64];
} Launcher;
void launcher_init(Launcher *m, const Settings *s, int aga, int rtg);
int launcher_rows(const Launcher *m);
int launcher_graphics_rows(const Launcher *m);
const char *launcher_graphics_hint(const Launcher *m);
int launcher_graphics_available(const Launcher *m, int row);
void launcher_row(const Launcher *m, int row, char *name, size_t size, char *value, size_t value_size);
int launcher_key(Launcher *m, int key);
int launcher_pointer(Launcher *m, int x, int y, int down);
void launcher_results(Launcher *m, const PerformanceReport *report);
void render_launcher(const Launcher *m, Frame *f);
#endif

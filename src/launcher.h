/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_LAUNCHER_H
#define ASTRALBOUND_LAUNCHER_H
#include "performance.h"
enum { LAUNCH_UP=1001, LAUNCH_DOWN, LAUNCH_LEFT, LAUNCH_RIGHT };
enum { LAUNCH_NONE, LAUNCH_PLAY, LAUNCH_TEST, LAUNCH_QUIT };
typedef struct {
    Settings settings; PerformanceReport latest, previous;
    int page, selected, group, test_scene, down, available_aga, available_rtg;
    char message[64];
} Launcher;
void launcher_init(Launcher *m, const Settings *s, int aga, int rtg);
int launcher_rows(const Launcher *m);
void launcher_row(const Launcher *m, int row, char *name, size_t size, char *value, size_t value_size);
int launcher_key(Launcher *m, int key);
int launcher_pointer(Launcher *m, int x, int y, int down);
void launcher_results(Launcher *m, const PerformanceReport *report);
void render_launcher(const Launcher *m, Frame *f);
#endif

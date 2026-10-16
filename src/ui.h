/* SPDX-License-Identifier: Unlicense */
#ifndef SPACE_UI_H
#define SPACE_UI_H
#include "space.h"
#define UI_MAX_WIDGETS 128
#define UI_SAVE 2001
#define UI_LOAD 2002
#define UI_EXIT 2003
#define UI_RESTART 2004
#define UI_SAVE_EXIT 2005
#define UI_CANCEL_EXIT 2006
#define UI_ROW 3000
#define UI_NAV 4000
#define UI_WARP 5000
#define UI_THROTTLE 6000
#define UI_SCAN 7000
#define UI_TAB 8000
#define UI_SECTOR 9000
#define UI_BODY 10000
#define UI_PORT 11000
void ui_orbit_geometry(const Game *g, Vec *origin, int64_t *extent, int *parent);
void ui_map_point(Vec p, Vec origin, int64_t extent, int *x, int *y);
typedef struct {
    int x, y, w, h, action, enabled, active;
    char label[48];
} UiWidget;
int ui_scale(const Frame *f);
int ui_view_height(const Game *g, const Frame *f);
int ui_widgets(const Game *g, const Frame *f, UiWidget *out);
void ui_move(Game *g, const Frame *f, int x, int y);
/* Returns a platform request (save/load/exit), otherwise zero. */
int ui_pointer(Game *g, const Frame *f, int x, int y, int down);
/* Escape opens/cancels the modal; S/Q/C choose save/quit/cancel. */
int ui_quit_key(Game *g, int key);
int ui_fire(const Game *g, const Frame *f);
void ui_cancel(Game *g);
#endif

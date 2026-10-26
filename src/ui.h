/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_UI_H
#define ASTRALBOUND_UI_H
#include "astralbound.h"
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
#define UI_BODY 10000
#define UI_PORT 11000
/* System map camera controls; the map area spans SYSMAP_LEFT..RIGHT and
   SYSMAP_TOP to 80 logical pixels above the screen's foot, its camera row
   just below and the page's message line clear of both. */
#define SYS_DRAG 12300
#define SYS_LEFT 12301
#define SYS_RIGHT 12302
#define SYS_UP 12303
#define SYS_DOWN 12304
#define SYS_IN 12305
#define SYS_OUT 12306
#define SYS_PAN 12307
#define SYS_FOCUS 12308
#define SYS_SHIP 12309
#define SYS_WHOLE 12310
#define SYS_LOCAL 12311
#define SYSMAP_LEFT 10
#define SYSMAP_RIGHT 310
#define SYSMAP_TOP 31
#define SYSMAP_BOTTOM(h) ((h) - 80)
void ui_orbit_geometry(const Game *g, Vec *origin, int64_t *extent, int *parent);
void ui_sysmap_view(const Game *g, Vec *centre, int64_t *extent, int *yaw, int *pitch);
/* Pixels per half-width at the centre's depth: one scale for both axes,
   so the whole system fits the map's shorter side and circles stay round. */
int ui_sysmap_scale(int h);
/* -1 behind the camera; else 1 inside the map area, 0 outside it. */
int ui_sysmap_project(const Game *g, int h, Vec p, int *x, int *y, int *depth);
/* The body (or, *port set, the port) nearest a map point, or -1. */
int ui_sysmap_pick(const Game *g, int h, int x, int y, int *port);
int ui_sysmap_key(Game *g, int key);
void ui_sysmap_drag(Game *g, int h, int dx, int dy);
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

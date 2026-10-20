/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_SETTINGS_H
#define ASTRALBOUND_SETTINGS_H
#include <stddef.h>
/* Presentation only. These values never enter a commander or physics tick. */
enum { VIDEO_ECS, VIDEO_AGA, VIDEO_RTG, VIDEO_RTG16 };
enum { QUALITY_LOW, QUALITY_MEDIUM, QUALITY_HIGH, QUALITY_ULTRA, QUALITY_CUSTOM };
/* Enhanced: depth-buffered, per-pixel shaded, for accelerated Amigas.
   Classic: Frontier-style painter's spans for a 68030 without an FPU. */
enum { RENDERER_ENHANCED, RENDERER_CLASSIC };
typedef struct {
    int scale, terrain, texture, planets, clouds, cloud_range, trees, ports;
    int port_range, particles, stars, rings, sky, weather, lights, labels;
    int triangles, ships;
} Graphics;
typedef struct { int mode, compact, rtg_size, preset, intro, sound, test_scene, renderer; Graphics gfx; } Settings;
typedef struct { const char *name, *help; size_t offset; int maximum; const char *values[6]; } GraphicsOption;
#define GRAPHICS_OPTIONS 18
extern const GraphicsOption graphics_options[GRAPHICS_OPTIONS];
extern const char *quality_names[5], *video_names[4], *renderer_names[2];
void settings_defaults(Settings *s, int amiga);
void settings_preset(Settings *s, int preset);
void settings_validate(Settings *s);
int settings_load(Settings *s, const char *path);
int settings_save(const Settings *s, const char *path);
int graphics_value(const Graphics *g, int option);
void graphics_change(Settings *s, int option, int direction);
void settings_dimensions(const Settings *s, int pal, int *w, int *h);
void render_graphics(const Graphics *g);
void render_renderer(int renderer);
#endif

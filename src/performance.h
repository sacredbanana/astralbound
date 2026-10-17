/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_PERFORMANCE_H
#define ASTRALBOUND_PERFORMANCE_H
#include "astralbound.h"
#include "settings.h"
#include <stdio.h>
#define PERFORMANCE_SCENES 14
extern const char *performance_names[PERFORMANCE_SCENES];
typedef struct { uint64_t render_us, output_us, total_us; unsigned frames; uint32_t slowest_us; } PerformanceResult;
typedef struct { Settings settings; PerformanceResult result[PERFORMANCE_SCENES]; int complete, width, height; } PerformanceReport;
typedef struct { PerformanceReport report; int scene, selected, warmup, step; } Performance;
void performance_begin(Performance *p, Game *g, const Settings *s, int selected);
void performance_scene(Game *g, int scene);
void performance_animate(Performance *p, Game *g);
/* Call after presenting. Returns 1 when finished, -1 on scene change. */
int performance_record(Performance *p, Game *g, uint64_t render_us, uint64_t output_us, uint64_t total_us);
void render_performance(const Performance *p, Frame *f);
void performance_print(FILE *out, const PerformanceReport *report);
#endif

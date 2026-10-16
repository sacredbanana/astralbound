/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_SOLAR_H
#define ASTRALBOUND_SOLAR_H
#include "astralbound.h"
int solar_body_count(void);
int solar_legacy_body(int body);
int solar_legacy_count(void);
void solar_system(System *s);
int solar_body_index(const System *s, const char *name);
#endif

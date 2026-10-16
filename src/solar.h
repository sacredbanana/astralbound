/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_SOLAR_H
#define ASTRALBOUND_SOLAR_H
#include "astralbound.h"
int solar_body_count(void);
int solar_legacy_body(int body);
int solar_legacy_count(void);
void solar_system(System *s);
int solar_body_index(const System *s, const char *name);
/* Physical data sheet for one body: measured values for the Solar System,
   values consistent with the generator's climate for every other system. */
typedef struct {
    int mass, mass_exponent; /* kg = mass / 100 * 10^mass_exponent; mass 100..999 */
    int32_t gravity;         /* surface gravity, mm/s^2 */
    int density;             /* mean density, g/cm^3 * 100 */
    int32_t escape;          /* escape velocity, m/s */
    int temperature;         /* mean surface temperature (1 bar for giants), K */
    int32_t pressure;        /* surface pressure in Pa; -1 without a solid surface */
    char air[32];            /* atmospheric composition */
} BodyFacts;
void body_facts(const System *s, int body, BodyFacts *out);
#endif

/* SPDX-License-Identifier: Unlicense */
#ifndef ASTRALBOUND_SOLAR_H
#define ASTRALBOUND_SOLAR_H
#include "astralbound.h"
/* Rendering chemistry, separate from density/weather climate categories. */
typedef enum {
    AIR_NONE, AIR_NITROGEN_OXYGEN, AIR_CARBON_DIOXIDE, AIR_NITROGEN_METHANE,
    AIR_HYDROGEN_HELIUM, AIR_HYDROGEN_METHANE, AIR_TYPES
} AtmosphereGas;
typedef enum {
    HAZE_NONE, HAZE_DUST, HAZE_SULPHUR, HAZE_ORGANIC, HAZE_AMMONIA,
    HAZE_METHANE, HAZE_BLUE, HAZE_TYPES
} AtmosphereHaze;
AtmosphereGas atmosphere_gases(const Planet *p);
AtmosphereHaze atmosphere_haze(const Planet *p);
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

/* SPDX-License-Identifier: Unlicense */
#include "solar.h"
#include "body_map.h"
#include <string.h>
#include <stdio.h>
typedef struct {
    const char *name;
    int parent;
    int64_t orbit;
    uint64_t period;
    int radius,gravity,type,climate,atmosphere,inclination,phase,eccentricity,node,periapsis;
    uint32_t spin;
    int known;
    int32_t basis[6];
    int legacy_index;
    AtmosphereGas gases;
    AtmosphereHaze haze;
    int32_t tilt[9];    /* spin frame, see Planet */
    uint32_t spin_phase;
} SolarBody;
#include "solar_catalog.inc"
/* NASA planetary/satellite fact sheets: mass (3 figures, exponent), gravity
   mm/s^2, density g/cm^3*100, mean temperature K, surface pressure Pa. */
typedef struct {
    const char *name;
    int mass, mass_exponent;
    int32_t gravity;
    int density, temperature;
    int32_t pressure;
    const char *air;
} SolarFacts;
static const SolarFacts solar_facts[] = {
    {"Mercury",330,23,3700,543,440,0,"TRACE O2 NA H2 HE K"},
    {"Venus",487,24,8870,524,737,9200000,"96.5% CO2 3.5% N2"},
    {"Earth",597,24,9807,551,288,101325,"78% N2 21% O2 1% AR"},
    {"Mars",642,23,3710,393,210,636,"95% CO2 2.8% N2 2% AR"},
    {"Jupiter",190,27,24790,133,165,-1,"90% H2 10% HE"},
    {"Saturn",568,26,10440,69,134,-1,"96% H2 3% HE"},
    {"Uranus",868,25,8870,127,76,-1,"83% H2 15% HE 2% CH4"},
    {"Neptune",102,26,11150,164,72,-1,"80% H2 19% HE 1% CH4"},
    {"Ceres",938,20,280,216,168,0,"NONE"},
    {"Pluto",130,22,620,185,44,1,"99% N2 0.5% CH4 0.05% CO"},
    {"Eris",166,22,820,252,43,0,"NONE"},
    {"Haumea",401,21,440,202,50,0,"NONE"},
    {"Makemake",310,21,400,203,40,0,"NONE"},
    {"Moon",735,22,1620,334,250,0,"TRACE HE AR NE"},
    {"Phobos",107,16,6,186,233,0,"NONE"},
    {"Deimos",148,15,3,147,233,0,"NONE"},
    {"Io",893,22,1796,353,110,0,"TRACE SO2"},
    {"Europa",480,22,1314,301,102,0,"TRACE O2"},
    {"Ganymede",148,23,1428,194,110,0,"TRACE O2"},
    {"Callisto",108,23,1235,183,134,0,"TRACE CO2 O2"},
    {"Mimas",375,19,64,115,64,0,"NONE"},
    {"Enceladus",108,20,113,161,75,0,"TRACE H2O PLUMES"},
    {"Tethys",617,20,146,98,86,0,"NONE"},
    {"Dione",110,21,232,148,87,0,"TRACE O2"},
    {"Rhea",231,21,264,124,76,0,"TRACE O2 CO2"},
    {"Titan",135,23,1352,188,94,146700,"95% N2 5% CH4"},
    {"Iapetus",181,21,223,109,110,0,"NONE"},
    {"Ariel",125,21,269,159,60,0,"NONE"},
    {"Umbriel",128,21,200,139,75,0,"NONE"},
    {"Titania",340,21,367,171,70,0,"NONE"},
    {"Oberon",308,21,346,163,75,0,"NONE"},
    {"Miranda",629,19,79,120,60,0,"NONE"},
    {"Triton",214,22,779,206,38,1,"99.9% N2 TRACE CH4 CO"},
    {"Charon",159,21,288,170,53,0,"NONE"},
};
/* The physical catalogue can distinguish gases that a density-oriented
   climate cannot (Pluto is THIN but nitrogen-rich; Neptune contains methane).
   Generated gas families match the composition printed by body_facts(). */
AtmosphereGas atmosphere_gases(const Planet *p) {
    static const AtmosphereGas generated[] = {
        AIR_NONE, AIR_NITROGEN_OXYGEN, AIR_CARBON_DIOXIDE,
        AIR_CARBON_DIOXIDE, AIR_NITROGEN_METHANE, AIR_HYDROGEN_HELIUM
    };
    int i;
    if (p->catalog) for (i=0;i<(int)(sizeof(solar_bodies)/sizeof(solar_bodies[0]));i++)
        if (!strcmp(p->name,solar_bodies[i].name)) return solar_bodies[i].gases;
    return (unsigned)p->climate < sizeof(generated)/sizeof(generated[0]) ? generated[p->climate] : AIR_NONE;
}
AtmosphereHaze atmosphere_haze(const Planet *p) {
    static const AtmosphereHaze generated[] = {
        HAZE_NONE, HAZE_NONE, HAZE_DUST, HAZE_SULPHUR, HAZE_ORGANIC, HAZE_AMMONIA
    };
    int i;
    if (p->catalog) for (i=0;i<(int)(sizeof(solar_bodies)/sizeof(solar_bodies[0]));i++)
        if (!strcmp(p->name,solar_bodies[i].name)) return solar_bodies[i].haze;
    return (unsigned)p->climate < sizeof(generated)/sizeof(generated[0]) ? generated[p->climate] : HAZE_NONE;
}
int solar_body_count(void) { return sizeof(solar_bodies)/sizeof(solar_bodies[0]); }
int solar_legacy_body(int body) {
    return body>=0 && body<(int)(sizeof(solar_v8_to_current)/sizeof(solar_v8_to_current[0]))?solar_v8_to_current[body]:-1;
}
int solar_legacy_count(void) { return sizeof(solar_v8_to_current)/sizeof(solar_v8_to_current[0]); }
int solar_body_index(const System *s,const char *name) {
    int i; for(i=0;i<s->count;i++) if(!strcmp(s->planets[i].name,name)) return i;
    return -1;
}
void solar_system(System *s) {
    int i;
    memset(s->planets,0,sizeof(s->planets));
    s->count=solar_body_count();
    for(i=0;i<s->count;i++) {
        const SolarBody *d=&solar_bodies[i]; Planet *p=&s->planets[i];
        p->seed=hash32(0x534f4c00U+(unsigned)d->legacy_index); p->parent=d->parent;
        p->radius=d->radius; p->gravity=d->gravity; p->type=(BodyType)d->type;
        p->climate=(AtmosphereType)d->climate; p->atmosphere=d->atmosphere;
        p->orbit=d->orbit; p->period=d->period; p->phase=d->phase;
        p->inclination=d->inclination; p->eccentricity=d->eccentricity;
        p->ascending_node=d->node; p->periapsis=d->periapsis;
        p->spin_period=d->spin; p->catalog=d->known;
        memcpy(p->orbit_basis,d->basis,sizeof(p->orbit_basis));
        p->influence=p->parent>=0?p->orbit/5:p->orbit/100;
        /* Jupiter's, Uranus's and Neptune's rings are faint dust and dark ice,
           all but invisible in sunlight; only Saturn's are bright. */
        p->rings=!strcmp(d->name,"Saturn");
        /* Fictional ports only; the celestial catalog remains real. */
        p->settled=i==2 || i==3 || !strcmp(d->name,"Moon");
        snprintf(p->name,sizeof(p->name),"%s",d->name);
        p->map=body_map_find(d->name);
        p->tilted=1; p->spin_phase=d->spin_phase;
        memcpy(p->tilt,d->tilt,sizeof(p->tilt));
    }
}
static void mass_digits(uint64_t x, int exponent, BodyFacts *f) {
    while (x >= 1000) { x = (x + 5) / 10; exponent++; }
    while (x && x < 100) { x *= 10; exponent--; }
    f->mass = (int)x;
    f->mass_exponent = exponent + 2;
}
/* Generated worlds are placed by gameplay rules rather than stellar
   luminosity: the first primary is always the temperate ocean world. Scale
   equilibrium temperature from that orbit so the sheet agrees with climate. */
static int generated_temperature(const System *s, const Planet *p) {
    const Planet *host = p->parent >= 0 ? &s->planets[p->parent] : p;
    int64_t home = s->planets[0].parent < 0 && s->planets[0].orbit ? s->planets[0].orbit : AU_METRES / 3;
    int64_t d = host->orbit > 0 ? host->orbit : home;
    int t = (int)(255 * isqrt64((uint64_t)(home << 20) / (uint64_t)d) / 1024);
    t = p->climate == TEMPERATE ? t + 33 : p->climate == DENSE ? t * 12 / 5 :
        p->climate == THIN ? t + 5 : p->climate == FROZEN ? t + 10 :
        p->climate == HYDROGEN ? t * 3 / 2 : t;
    t += (int)(hash32(p->seed ^ 0x54454d50U) % 17) - 8;
    return t < 20 ? 20 : t;
}
void body_facts(const System *s, int body, BodyFacts *f) {
    const Planet *p = &s->planets[body];
    uint32_t h = hash32(p->seed ^ 0x41495200U);
    int64_t km = p->radius / 1000;
    int i;
    memset(f, 0, sizeof(*f));
    if (p->catalog)
        for (i = 0; i < (int)(sizeof(solar_facts) / sizeof(solar_facts[0])); i++)
            if (!strcmp(solar_facts[i].name, p->name)) {
                const SolarFacts *d = &solar_facts[i];
                f->mass = d->mass; f->mass_exponent = d->mass_exponent;
                f->gravity = d->gravity; f->density = d->density;
                f->temperature = d->temperature; f->pressure = d->pressure;
                /* Escape speed from GM (6.674e-11 * mass), not the spin-reduced
                   surface gravity quoted for the giants. */
                {
                    uint64_t gm = (uint64_t)d->mass * 6674U;
                    int e;
                    for (e = d->mass_exponent - 16; e > 0; e--) gm *= 10;
                    for (; e < 0; e++) gm /= 10;
                    f->escape = (int32_t)isqrt64(gm * 2 / (uint64_t)p->radius);
                }
                snprintf(f->air, sizeof(f->air), "%s", d->air);
                return;
            }
    /* M = g R^2 / G, with 1/G = 1.4984e10 and R in kilometres (x 1e6). */
    f->gravity = p->gravity * 1000;
    mass_digits((uint64_t)p->gravity * (uint64_t)(km * km) * 14984U, 12, f);
    /* rho = 3 g / (4 pi G R): 3.5773e5 * g(mm/s^2) / R(m) in g/cm^3 * 100. */
    f->density = p->radius ? (int)((int64_t)f->gravity * 357730 / p->radius) : 0;
    f->escape = (int32_t)isqrt64((uint64_t)f->gravity * (uint64_t)p->radius / 500);
    f->temperature = generated_temperature(s, p);
    switch (p->climate) {
    case TEMPERATE:
        f->pressure = 70000 + (int32_t)(h % 70000);
        snprintf(f->air, sizeof(f->air), "%d%% N2 %d%% O2 1%% AR", 83 - (int)(h % 10), 16 + (int)(h % 10));
        break;
    case THIN:
        f->pressure = 300 + (int32_t)(h % 1500);
        snprintf(f->air, sizeof(f->air), "%d%% CO2 %d%% N2 %d%% AR", 85 + (int)(h % 12),
                 (15 - (int)(h % 12)) * 2 / 3, 15 - (int)(h % 12) - (15 - (int)(h % 12)) * 2 / 3);
        break;
    case DENSE:
        f->pressure = 4000000 + (int32_t)(h % 6000000);
        snprintf(f->air, sizeof(f->air), "%d%% CO2 %d%% N2 TRACE SO2", 90 + (int)(h % 8), 10 - (int)(h % 8));
        break;
    case FROZEN:
        f->pressure = 50000 + (int32_t)(h % 150000);
        snprintf(f->air, sizeof(f->air), "%d%% N2 %d%% CH4 TRACE CO", 90 + (int)(h % 9), 10 - (int)(h % 9));
        break;
    case HYDROGEN:
        f->pressure = -1;
        snprintf(f->air, sizeof(f->air), "%d%% H2 %d%% HE 1%% CH4", 80 + (int)(h % 12), 19 - (int)(h % 12));
        break;
    default:
        snprintf(f->air, sizeof(f->air), "NONE");
        break;
    }
}

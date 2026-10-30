/* SPDX-License-Identifier: Unlicense
   The Solar System tour: a single camera flight from deep space to the Sun,
   out through every planet and its major moons to Pluto and Charon, and back
   into deep space. World time is frozen at J2000, so every stop is a fixed
   point and the flight between two stops is a pure function of time. A
   planet with a landmark to show (Earth's Africa, Jupiter's Red Spot,
   Pluto's heart) is turned on its axis to bring it into afternoon light.

   A flight never cuts. It leaves one stop on a sphere round that body whose
   radius grows exponentially, so the body recedes at a steady rate whatever
   the distance, swings onto the line to the next body, and arrives the same
   way in reverse. Speed peaks between the two at a junction where both
   halves meet with equal velocity, so a hop from Earth to the Moon and a
   crossing from Uranus to Neptune take a few seconds each.

   Moons are placed on their real orbits where they show lit with their
   planet behind them; tidally locked moons are turned with their orbits,
   so they still show their planets their real near sides.
   Everything is integer arithmetic: playback is identical on every CPU. */
#include "tour.h"
#include "solar.h"
#include "galaxy.h"
#include <stdio.h>
#include <string.h>

#define S(seconds) ((int)((seconds) * HZ))
#define ONE (1LL << 30)  /* Q30 unit vectors */
#define TURN (1L << 20)  /* fine angles per turn */
#define DEGREES(d) ((int32_t)((d) * TURN / 360))
#define Q16 65536
/* The opening and closing viewpoints, and the Sun's place in the table. */
enum { FAR_START = 0, SUN_STOP = 1, FAR_END = TOUR_STOPS - 1 };
/* Holds last while the text types, then long enough to read it. */
#define READING S(3.2)

const TourStop tour_stops[TOUR_STOPS] = {
    {NULL, "THE SOLAR SYSTEM", "A GUIDED TOUR",
     {"ONE STAR, EIGHT PLANETS AND HUNDREDS OF MOONS", "FORMED 4.6 BILLION YEARS AGO FROM A GAS CLOUD",
      "ANY KEY RETURNS TO THE MENU", NULL}, 30, 35, 0, 32, 0, 0, 0},
    {"Sun", "THE SUN", "A YELLOW DWARF STAR",
     {"HOLDS 99.86% OF ALL THE MASS IN THE SOLAR SYSTEM", "1.3 MILLION EARTHS WOULD FIT INSIDE IT",
      "ITS CORE BURNS AT ABOUT 15 MILLION C", "SUNLIGHT TAKES 8 MINUTES 20 SECONDS TO REACH EARTH"}, 45, -60, 16, 10, 0, 0, 0},
    {"Mercury", "MERCURY", "THE SMALLEST PLANET",
     {"A YEAR LASTS 88 DAYS, ONE SUNRISE TO THE NEXT 176", "430 C BY DAY, -180 C AT NIGHT: NO AIR HOLDS HEAT",
      "ITS IRON CORE FILLS 85% OF THE PLANET'S RADIUS", "WATER ICE HIDES IN SHADOWED POLAR CRATERS"}, 32, 42, 14, 12, 0, 0, 0},
    {"Venus", "VENUS", "THE HOTTEST PLANET",
     {"465 C AT THE SURFACE: HOT ENOUGH TO MELT LEAD", "AIR 92 TIMES DENSER THAN EARTH'S, ACID CLOUDS",
      "SPINS BACKWARDS: ITS DAY OUTLASTS ITS YEAR", "THE BRIGHTEST PLANET IN EARTH'S NIGHT SKY"}, 32, 42, 14, 12, 0, 0, 0},
    {"Earth", "EARTH", "OUR HOME",
     {"THE ONLY WORLD KNOWN TO HARBOUR LIFE", "OCEANS COVER 71% OF ITS SURFACE",
      "THE DENSEST PLANET IN THE SOLAR SYSTEM", "ITS MAGNETIC FIELD DEFLECTS THE SOLAR WIND"}, 32, 0, 10, 0,
     1, 25, 15}, /* Africa, Europe and Arabia */
    {"Moon", "MOON", "EARTH'S NATURAL SATELLITE",
     {"TIDALLY LOCKED: IT ALWAYS SHOWS EARTH ONE FACE", "DRIFTING AWAY FROM EARTH BY 3.8 CM A YEAR",
      "12 PEOPLE WALKED ON IT BETWEEN 1969 AND 1972", "PROBABLY BORN IN A GIANT IMPACT ON EARTH"}, 36, 24, 10, 0, 0, 0, 0},
    {"Mars", "MARS", "THE RED PLANET",
     {"OLYMPUS MONS: THE TALLEST VOLCANO KNOWN, 22 KM", "VALLES MARINERIS RUNS 4000 KM ACROSS IT",
      "RUSTY IRON OXIDE DUST GIVES IT ITS COLOUR", "A DAY LASTS 24 HOURS 40 MINUTES"}, 32, 42, 14, 12, 0, 0, 0},
    {"Phobos", "PHOBOS", "INNER MOON OF MARS",
     {"ORBITS 6000 KM UP, THREE TIMES A MARTIAN DAY", "STICKNEY CRATER IS 9 KM WIDE: A THIRD OF PHOBOS",
      "SPIRALLING INWARD: GONE IN 30 TO 50 MILLION YEARS", NULL}, 40, 24, 10, 0, 0, 0, 0},
    {"Deimos", "DEIMOS", "OUTER MOON OF MARS",
     {"ONLY ABOUT 12 KM ACROSS", "ESCAPE SPEED 5.6 M/S: A HARD JUMP COULD LEAVE IT",
      "NAMED FOR THE GREEK SPIRIT OF DREAD", NULL}, 40, 24, 10, 0, 0, 0, 0},
    {"Jupiter", "JUPITER", "KING OF THE PLANETS",
     {"MORE THAN TWICE THE MASS OF ALL OTHER PLANETS", "THE GREAT RED SPOT IS A STORM WIDER THAN EARTH",
      "THE SHORTEST DAY: ONE TURN IN UNDER 10 HOURS", "THE STRONGEST MAGNETIC FIELD OF ANY PLANET"}, 32, 0, 10, 0,
     1, -22, 132}, /* the Great Red Spot, where Cassini mapped it */
    {"Io", "IO", "THE VOLCANIC MOON",
     {"THE MOST VOLCANIC WORLD IN THE SOLAR SYSTEM", "JUPITER'S TIDES FLEX AND MELT ITS INTERIOR",
      "SULPHUR PLUMES RISE UP TO 400 KM HIGH", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Europa", "EUROPA", "AN OCEAN UNDER ICE",
     {"A SALTWATER OCEAN LIES BENEATH ITS ICE SHELL", "IT MAY HOLD TWICE THE WATER OF EARTH'S OCEANS",
      "A PRIME PLACE TO SEARCH FOR LIFE BEYOND EARTH", NULL}, 26, 24, 8, 0,
     1, 5, 155}, /* its densest lineae, beside the anti-Jovian point */
    {"Ganymede", "GANYMEDE", "THE LARGEST MOON",
     {"BIGGER THAN THE PLANET MERCURY", "THE ONLY MOON WITH ITS OWN MAGNETIC FIELD",
      "A HIDDEN OCEAN LIES BETWEEN LAYERS OF ICE", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Callisto", "CALLISTO", "THE BATTERED MOON",
     {"THE MOST HEAVILY CRATERED SURFACE KNOWN", "ITS SURFACE IS ABOUT 4 BILLION YEARS OLD",
      "VALHALLA: AN IMPACT BASIN 3800 KM ACROSS", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Saturn", "SATURN", "THE RINGED PLANET",
     {"LESS DENSE THAN WATER: IT WOULD FLOAT IN A BATH", "ITS RINGS SPAN 280000 KM YET ARE OFTEN 10 M THICK",
      "A HEXAGONAL JET STREAM CIRCLES ITS NORTH POLE", "WINDS REACH 1800 KM/H"}, 54, 42, 14, 24, 0, 0, 0},
    {"Mimas", "MIMAS", "THE CRATER MOON",
     {"HERSCHEL CRATER SPANS A THIRD OF ITS WIDTH", "THE IMPACT NEARLY SHATTERED IT",
      "ITS PULL CLEARS THE CASSINI DIVISION IN THE RINGS", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Enceladus", "ENCELADUS", "THE ICE FOUNTAIN",
     {"GEYSERS AT ITS SOUTH POLE SPRAY WATER INTO SPACE", "THE SPRAY FEEDS SATURN'S FAINT E RING",
      "IT REFLECTS ALMOST ALL THE SUNLIGHT THAT HITS IT", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Tethys", "TETHYS", "A MOON OF WATER ICE",
     {"ALMOST PURE WATER ICE, AS LIGHT AS WATER", "ITHACA CHASMA: A CANYON 2000 KM LONG",
      "ODYSSEUS CRATER IS 445 KM WIDE", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Dione", "DIONE", "THE CLIFFED MOON",
     {"BRIGHT ICE CLIFFS HUNDREDS OF METRES HIGH", "A WHISPER OF OXYGEN AROUND IT",
      "SHARES ITS ORBIT WITH HELENE AND POLYDEUCES", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Rhea", "RHEA", "SATURN'S SECOND MOON BY SIZE",
     {"A FROZEN BALL OF ICE AND ROCK", "AROUND -200 C ON ITS SHADED SIDE",
      "A THIN HAZE OF OXYGEN AND CARBON DIOXIDE", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Titan", "TITAN", "THE HAZY MOON",
     {"ITS AIR IS 1.5 TIMES THE PRESSURE OF EARTH'S", "METHANE RAIN FILLS RIVERS, LAKES AND SEAS",
      "LARGER THAN THE PLANET MERCURY", "THE HUYGENS PROBE LANDED HERE IN 2005"}, 36, 24, 10, 0, 0, 0, 0},
    {"Iapetus", "IAPETUS", "THE TWO-TONED MOON",
     {"ONE HEMISPHERE COAL DARK, THE OTHER SNOW BRIGHT", "A RIDGE UP TO 20 KM HIGH GIRDLES ITS EQUATOR",
      "DISCOVERED BY GIOVANNI CASSINI IN 1671", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Uranus", "URANUS", "THE TILTED GIANT",
     {"IT ROLLS ROUND THE SUN TIPPED 98 DEGREES", "EACH POLE GETS 42 YEARS OF SUN, THEN 42 OF NIGHT",
      "THE COLDEST AIR OF ANY PLANET: -224 C", "THE FIRST PLANET FOUND BY TELESCOPE, IN 1781"}, 34, 42, 14, 12, 0, 0, 0},
    {"Miranda", "MIRANDA", "THE PATCHWORK MOON",
     {"A JUMBLE OF TERRAINS, AS IF SMASHED AND REBUILT", "VERONA RUPES MAY BE THE TALLEST CLIFF KNOWN",
      "ONLY 470 KM ACROSS", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Ariel", "ARIEL", "THE BRIGHT MOON",
     {"THE BRIGHTEST OF URANUS'S MOONS", "LONG FAULT VALLEYS CRISSCROSS ITS FACE",
      "ITS SURFACE MAY BE THE YOUNGEST OF THE FIVE", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Umbriel", "UMBRIEL", "THE DARK MOON",
     {"THE DARKEST OF URANUS'S LARGE MOONS", "AN ANCIENT, HEAVILY CRATERED SURFACE",
      "A BRIGHT RING SITS IN WUNDA CRATER", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Titania", "TITANIA", "URANUS'S LARGEST MOON",
     {"1580 KM ACROSS", "MESSINA CHASMA: A CANYON 1500 KM LONG",
      "NAMED FOR SHAKESPEARE'S QUEEN OF THE FAIRIES", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Oberon", "OBERON", "THE OUTERMOST GIANT MOON",
     {"THE FARTHEST OF URANUS'S MAJOR MOONS", "AN OLD, ICY SURFACE SCARRED BY CRATERS",
      "FOUND BY WILLIAM HERSCHEL IN 1787", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Neptune", "NEPTUNE", "THE WINDY GIANT",
     {"THE FASTEST WINDS KNOWN: OVER 2000 KM/H", "FOUND BY MATHEMATICS BEFORE IT WAS SEEN, 1846",
      "ONE ORBIT OF THE SUN TAKES 165 YEARS", "SUNLIGHT HERE IS 900 TIMES DIMMER THAN AT EARTH"}, 34, 42, 14, 12, 0, 0, 0},
    {"Triton", "TRITON", "THE BACKWARDS MOON",
     {"IT ORBITS AGAINST NEPTUNE'S SPIN: A CAPTURED WORLD", "NITROGEN GEYSERS ERUPT FROM ITS FROZEN SURFACE",
      "ONE OF THE COLDEST SURFACES KNOWN: -235 C", NULL}, 36, 24, 10, 0, 0, 0, 0},
    {"Pluto", "PLUTO", "THE DWARF PLANET",
     {"A HEART-SHAPED GLACIER OF NITROGEN ICE", "WATER-ICE MOUNTAINS RISE UP TO 6 KM HIGH",
      "A YEAR ON PLUTO LASTS 248 EARTH YEARS", "NEW HORIZONS FLEW PAST IN 2015"}, 30, 0, 10, 0,
     1, 20, -170}, /* Tombaugh Regio, the heart, with Sputnik Planitia in its west */
    {"Charon", "CHARON", "PLUTO'S PARTNER",
     {"HALF PLUTO'S SIZE: THEY ORBIT A POINT BETWEEN THEM", "EACH ALWAYS SHOWS THE OTHER THE SAME FACE",
      "A DARK RED CAP COLOURS ITS NORTH POLE", NULL}, 40, 24, 10, 0, 0, 0, 0},
    {NULL, "FAREWELL", "THE SUN FROM 60 AU",
     {"FROM HERE THE SUN IS JUST THE BRIGHTEST STAR", "ITS LIGHT TAKES 8 HOURS TO REACH THIS FAR",
      "VOYAGER 1 HAS FLOWN MORE THAN TWICE AS FAR", "THANK YOU FOR FLYING ASTRALBOUND"}, 600, 25, 0, 28, 0, 0, 0},
};

/* ---- Fine angles and Q30 directions ------------------------------------ */
/* The 1024-entry sine table, interpolated: angles in 2^20 per turn, Q24. */
static int64_t fsin(int32_t a) {
    uint32_t u = (uint32_t)a & (TURN - 1);
    int i = (int)(u >> 10);
    int64_t s0 = sini(i), s1 = sini(i + 1);
    return s0 * 1024 + (s1 - s0) * (int64_t)(u & 1023);
}
static int64_t fcos(int32_t a) { return fsin(a + TURN / 4); }
static Vec mul(Vec v, int64_t k, int64_t one) { return vec(v.x * k / one, v.y * k / one, v.z * k / one); }
static int64_t dot30(Vec a, Vec b) { return (a.x * b.x + a.y * b.y + a.z * b.z) / ONE; }
static Vec cross30(Vec a, Vec b) {
    return vec((a.y * b.z - a.z * b.y) / ONE, (a.z * b.x - a.x * b.z) / ONE, (a.x * b.y - a.y * b.x) / ONE);
}
static Vec unit30(Vec v) { return unit(v, ONE); }
/* v less its part along the unit n; v must be Q30 too. */
static Vec flat(Vec v, Vec n) { return sub(v, mul(n, dot30(n, v), ONE)); }
/* The angle in [0, half a turn] whose sine and cosine are proportional to s
   (non-negative) and c: bisection on the sign of sin(a - target). */
static int32_t angle_of(int64_t s, int64_t c) {
    int32_t lo = 0, hi = TURN / 2;
    while (s > ONE || c > ONE || c < -ONE) { s /= 2; c /= 2; }
    while (hi - lo > 1) {
        int32_t mid = lo + (hi - lo) / 2;
        if (fsin(mid) * c - fcos(mid) * s < 0) lo = mid; else hi = mid;
    }
    return lo;
}
/* Rodrigues: v turned by a about the unit axis n, right-handed. */
static Vec turn(Vec v, Vec n, int32_t a) {
    int64_t c = fcos(a), s = fsin(a), along = dot30(n, v);
    return add(add(mul(v, c, 1 << 24), mul(cross30(n, v), s, 1 << 24)),
               mul(n, along * ((1 << 24) - c) / (1 << 24), ONE));
}
/* v lifted by a towards the pole n, keeping its bearing round n. */
static Vec lift(Vec v, Vec n, int32_t a) {
    Vec level = flat(v, n);
    int64_t up = dot30(n, v), across = length(level);
    int32_t now = up >= 0 ? angle_of(up, across) : -angle_of(-up, across);
    level = unit30(level);
    return add(mul(level, fcos(now + a), 1 << 24), mul(n, fsin(now + a), 1 << 24));
}
/* A great-circle turn from a to b: the start, the perpendicular it turns
   towards and the whole angle. Opposite directions turn about the pole. */
typedef struct { Vec a, c; int32_t angle; } Arc;
static Arc arc(Vec a, Vec b, Vec pole) {
    Arc r;
    int64_t c = dot30(a, b);
    Vec perp = sub(b, mul(a, c, ONE));
    int64_t s = length(perp);
    r.a = a;
    if (s > ONE / 4096) {
        r.c = unit30(perp);
        r.angle = angle_of(s, c);
    } else {
        r.c = unit30(cross30(pole, a));
        if (length(r.c) < ONE / 2) r.c = unit30(cross30(vec(ONE, 0, 0), a));
        r.angle = c > 0 ? 0 : TURN / 2;
    }
    return r;
}
static Vec along(const Arc *r, int k) { /* k: Q16 progress */
    int32_t a = (int32_t)((int64_t)r->angle * k / Q16);
    return add(mul(r->a, fcos(a), 1 << 24), mul(r->c, fsin(a), 1 << 24));
}
/* Angles of a direction in a frame: yaw round the pole from ref towards
   pole x ref, and pitch towards the pole. */
typedef struct { Vec pole, ref, side; } Frame3;
static void heading(const Frame3 *f, Vec d, int32_t *yaw, int32_t *pitch) {
    int64_t x = dot30(d, f->ref), y = dot30(d, f->side), z = dot30(d, f->pole);
    int64_t level = length(vec(x, y, 0));
    *yaw = y >= 0 ? angle_of(y, x) : -angle_of(-y, x);
    *pitch = z >= 0 ? angle_of(z, level) : -angle_of(-z, level);
}
static Vec direction(const Frame3 *f, int32_t yaw, int32_t pitch) {
    int64_t level = fcos(pitch);
    Vec across = add(mul(f->ref, fcos(yaw), 1 << 24), mul(f->side, fsin(yaw), 1 << 24));
    return add(mul(across, level, 1 << 24), mul(f->pole, fsin(pitch), 1 << 24));
}
/* ---- Exponential distances --------------------------------------------- */
/* log2 in Q16 by repeated squaring of a Q30 mantissa. */
static int32_t log2q(int64_t x) {
    int n = 62, i;
    uint64_t m;
    int32_t r;
    if (x < 1) x = 1;
    while (!((uint64_t)x >> n)) n--;
    m = n >= 30 ? (uint64_t)x >> (n - 30) : (uint64_t)x << (30 - n);
    r = n * Q16;
    for (i = 15; i >= 0; i--) {
        m = m * m >> 30;
        if (m >= 2ULL << 30) { m >>= 1; r += 1 << i; }
    }
    return r;
}
/* 2^(2^-k) for k = 1..16, Q30: square roots of square roots of two. */
static uint32_t roots[16];
static void prepare_roots(void) {
    uint64_t v = 2ULL << 30;
    int k;
    for (k = 0; k < 16; k++) { v = isqrt64(v << 30); roots[k] = (uint32_t)v; }
}
/* r * 2^(y / 65536) for positive r. */
static int64_t grow(int64_t r, int32_t y) {
    int32_t n = y >= 0 ? y / Q16 : -((-y + Q16 - 1) / Q16);
    uint32_t f = (uint32_t)(y - n * Q16);
    uint64_t m = (uint64_t)ONE;
    int k;
    for (k = 0; k < 16; k++) if (f & (0x8000U >> k)) m = m * roots[k] >> 30;
    r = muldiv(r, (int64_t)m, ONE);
    while (n > 0 && r < INT64_MAX / 2) { r *= 2; n--; }
    while (n < 0) { r /= 2; n++; }
    return r;
}
/* Q16 easings: smoothstep, and a rise that ends at full slope without
   acceleration, so two exponential halves join smoothly. */
static int smooth(int x) { int64_t q = x; return (int)(q * q / Q16 * (3 * Q16 - 2 * q) / Q16); }
static int rise(int x) { int64_t q = x, q2 = q * q / Q16; return (int)((3 * q2 - q2 * q / Q16) / 2); }
static int progress(int t, int from, int to) {
    return t <= from ? 0 : t >= to ? Q16 : (int)((int64_t)(t - from) * Q16 / (to - from));
}
/* ---- The plan: places and flights -------------------------------------- */
typedef struct {
    Vec centre;         /* Q8 system position */
    int64_t reach;      /* Q8 camera distance from the centre */
    int64_t clear;      /* Q8: flights keep at least this far from the centre */
    int64_t radius;     /* metres: the surface, for the distance readout */
    Vec pole, base;     /* Q30: azimuth turns round pole, starting from base */
    int32_t azimuth, drift, elevation;
    int body, parent;   /* planet indices; -1 the star or none */
    int hold, start;    /* ticks */
} Place;
typedef struct {
    int junction;       /* 1: depart A, junction, approach B; 0: approach B only */
    int t1, length;     /* ticks: departure half, whole flight */
    int64_t ra, rb;     /* Q8 radii at either end */
    int32_t la, lb;     /* log2 growth of each half, Q16 */
    Arc away, in;       /* directions of the eye from A, and from B */
    Frame3 pan;         /* the view turns round this pole, away from its ends */
    int settle;         /* ticks for the view to turn from one body to the next */
} Flight;
static struct {
    Place place[TOUR_STOPS];
    Flight flight[TOUR_STOPS - 1];
    char stats[TOUR_STOPS][64];
} plan;
static const Vec north = {0, ONE, 0};
static Vec sun_q8(const Game *g) { return scale(g->system.star[0].center, FP, 1); }
/* Sunward direction from a point, flattened onto the ecliptic. */
static Vec sunward(const Game *g, Vec q8) { return unit30(flat(unit30(sub(sun_q8(g), q8)), north)); }
static Vec centre_of(const Game *g, int body) {
    return body < 0 ? sun_q8(g) : scale(g->system.planets[body].center, FP, 1);
}
static Vec hold_eye(int stop, int k) {
    const Place *p = &plan.place[stop];
    Vec d = turn(p->base, p->pole, p->azimuth + (int32_t)((int64_t)p->drift * smooth(k) / Q16));
    if (p->elevation) d = lift(d, p->pole, p->elevation);
    return add(p->centre, scale(d, p->reach, ONE));
}
/* Where the camera looks from an eye at a stop: at the body, at the Sun
   from deep space, and between a moon and its planet. */
static Vec look_from(const Game *g, int stop, Vec eye) {
    const Place *p = &plan.place[stop];
    if (!tour_stops[stop].body) return unit30(sub(sun_q8(g), eye));
    if (p->parent >= 0)
        return unit30(add(mul(unit30(sub(p->centre, eye)), 6, 10),
                          mul(unit30(sub(centre_of(g, p->parent), eye)), 4, 10)));
    return unit30(sub(p->centre, eye));
}
/* Seconds of flight: a base plus a little for every doubling of scale. */
static int flight_ticks(int32_t doublings) { return S(1.3) + (int)((int64_t)doublings * S(0.12) / Q16); }
/* A swing half way round a body takes a second and a half more. */
static int swing(const Flight *f) {
    int32_t most = f->away.angle > f->in.angle ? f->away.angle : f->in.angle;
    return (int)((int64_t)most * S(1.5) / (TURN / 2));
}
/* The view turns over most of a flight, unless the camera swings far round
   the departure body: then it settles on the destination before the swing
   gathers pace, and the swing carries the departure body out of view. */
static int settle(const Flight *f) {
    int slow = f->length * 2 / 3, quick = (f->junction ? f->t1 : f->length) * 2 / 5;
    int w = f->away.angle >= TURN / 4 ? Q16 : (int)((int64_t)f->away.angle * Q16 / (TURN / 4));
    if (quick < S(0.8)) quick = S(0.8);
    if (quick > slow) quick = slow;
    return slow + (int)((int64_t)(quick - slow) * w / Q16);
}
static void plan_flight(const Game *g, int i) {
    const Place *a = &plan.place[i], *b = &plan.place[i + 1];
    Flight *f = &plan.flight[i];
    Vec ea = hold_eye(i, Q16), eb = hold_eye(i + 1, 0), e = unit30(sub(b->centre, a->centre));
    int64_t d = length(sub(b->centre, a->centre)), near_a, near_b;
    memset(f, 0, sizeof(*f));
    f->ra = length(sub(ea, a->centre));
    f->rb = length(sub(eb, b->centre));
    {
        /* The view pans round its own up at the start, so a level view
           turns through the horizon and a steep one never goes overhead. */
        Vec la = look_from(g, i, ea), n = flat(north, la);
        if (length(n) < ONE / 4) n = cross30(vec(ONE, 0, 0), la);
        f->pan.pole = unit30(n);
        f->pan.ref = unit30(flat(la, f->pan.pole));
        f->pan.side = cross30(f->pan.pole, f->pan.ref);
    }
    near_a = f->ra * 3 / 2 > a->clear ? f->ra * 3 / 2 : a->clear;
    near_b = f->rb * 3 / 2 > b->clear ? f->rb * 3 / 2 : b->clear;
    if (near_a + near_b <= d) {
        /* The junction sits midway unless either end needs more room. */
        int64_t ja = d / 2, jb, wa, wb;
        if (ja < near_a) ja = near_a;
        if (d - ja < near_b) ja = d - near_b;
        jb = d - ja;
        f->junction = 1;
        f->la = log2q(ja) - log2q(f->ra);
        f->lb = log2q(jb) - log2q(f->rb);
        f->away = arc(unit30(sub(ea, a->centre)), e, north);
        f->in = arc(scale(e, -1, 1), unit30(sub(eb, b->centre)), north);
        f->length = flight_ticks(f->la + f->lb) + swing(f);
        /* Equal speeds at the junction: each half's share of the time goes
           with its distance times its growth. */
        wa = muldiv(ja, f->la, Q16); wb = muldiv(jb, f->lb, Q16);
        f->t1 = (int)muldiv(f->length, wa, wa + wb);
        if (f->t1 < 1) f->t1 = 1;
        if (f->t1 > f->length - 1) f->t1 = f->length - 1;
    } else {
        /* The next body lies inside this view (a planet's inner moon):
           close on it directly, turning round it on the way in. */
        Vec from = sub(ea, b->centre);
        f->ra = length(from);
        f->lb = log2q(f->ra) - log2q(f->rb);
        f->in = arc(unit30(from), unit30(sub(eb, b->centre)), north);
        f->length = flight_ticks(f->lb < 0 ? -f->lb : f->lb) + swing(f);
    }
    f->settle = settle(f);
}
static Vec flight_eye(int i, int t) {
    const Flight *f = &plan.flight[i];
    const Place *a = &plan.place[i], *b = &plan.place[i + 1];
    if (!f->junction) {
        int k = progress(t, 0, f->length);
        int64_t r = grow(f->ra, -(int32_t)((int64_t)f->lb * smooth(k) / Q16));
        return add(b->centre, scale(along(&f->in, smooth(k)), r, ONE));
    }
    if (t < f->t1) {
        int x = progress(t, 0, f->t1);
        int64_t r = grow(f->ra, (int32_t)((int64_t)f->la * rise(x) / Q16));
        return add(a->centre, scale(along(&f->away, smooth(x)), r, ONE));
    } else {
        int y = Q16 - progress(t, f->t1, f->length);
        int64_t r = grow(f->rb, (int32_t)((int64_t)f->lb * rise(y) / Q16));
        return add(b->centre, scale(along(&f->in, smooth(Q16 - y)), r, ONE));
    }
}
/* ---- Staging ------------------------------------------------------------ */
/* A stop's landmark as a system-frame Q30 direction from its body's centre. */
static Vec landmark(const Planet *p, const TourStop *s) {
    int32_t la = DEGREES(s->lat), lo = DEGREES(s->lon);
    /* Body north is -z; east turns from +x towards +y (body_map.h). */
    Vec fixed = vec(fcos(la) * fcos(lo) / (1 << 18), fcos(la) * fsin(lo) / (1 << 18), -fsin(la) * 64);
    return unit30(body_rotate(p, fixed, 0));
}
/* Turn a planet on its axis (a planet's spin is free in a frozen world)
   until its landmark stands 35 degrees from the Sun: lit, with a
   terminator in view. */
static void face_landmark(Game *g, const TourStop *s) {
    Planet *p = &g->system.planets[solar_body_index(&g->system, s->body)];
    Vec sun = unit30(sub(g->system.star[0].center, p->center));
    uint32_t base = p->spin_phase, best = base;
    int64_t error = INT64_MAX, aim = fcos(DEGREES(35)) << 6;
    int k;
    for (k = 0; k < 256; k++) {
        int64_t off;
        p->spin_phase = base + ((uint32_t)k << 24);
        set_rotation(p, g->tick);
        off = dot30(landmark(p, s), sun) - aim;
        if (off < 0) off = -off;
        if (off < error) { error = off; best = p->spin_phase; }
    }
    p->spin_phase = best;
    set_rotation(p, g->tick);
}
/* Unit direction of a catalog orbit at a table angle (eccentricity aside). */
static Vec orbit_direction(const Planet *p, int a) {
    const int32_t *b = p->orbit_basis;
    return unit30(vec((int64_t)sini(a) * b[0] + (int64_t)cosi(a) * b[3],
                      (int64_t)sini(a) * b[1] + (int64_t)cosi(a) * b[4],
                      (int64_t)sini(a) * b[2] + (int64_t)cosi(a) * b[5]));
}
/* Signed angle of b from a about an axis, both flattened across it. */
static int32_t bearing(Vec a, Vec b, Vec axis) {
    Vec fa = unit30(flat(a, axis)), fb = unit30(flat(b, axis));
    int64_t s = dot30(cross30(fa, fb), axis);
    return s >= 0 ? angle_of(s, dot30(fa, fb)) : -angle_of(-s, dot30(fa, fb));
}
/* Put a moon where it shows half lit with its planet behind: on the orbit
   point 75 degrees from the Sun, on the side a bearing 75 degrees round from
   it. A steep orbit (Uranus's moons) trades light for a level line to the
   planet, which keeps flights between its moons off the vertical; a later
   moon keeps to the side of the one before it. A moon with a landmark goes
   where its landmark, carried round by the lock, stands 35 degrees from the
   Sun. A tidally locked moon turns with its orbit, so it still shows its
   real near side. Returns the moon's direction from its planet. */
static Vec place_moon(Game *g, int body, const Vec *after_moon, const TourStop *face) {
    Planet *m = &g->system.planets[body];
    const Planet *host = &g->system.planets[m->parent];
    Vec h = scale(host->center, FP, 1), sun = unit30(sub(sun_q8(g), h));
    Vec target = turn(sunward(g, h), north, DEGREES(75)), before, after, spin;
    uint64_t cycle = m->period * HZ;
    int a, best = 0, now;
    int64_t score = INT64_MIN, quarter = fcos(DEGREES(75)) << 6, lit = fcos(DEGREES(35)) << 6;
    Vec mark = face ? landmark(m, face) : north, axis = unit30(body_tilt(m, vec(0, 0, ONE), 0));
    before = unit30(sub(host->center, m->center));
    for (a = 0; a < 1024; a++) {
        Vec d = orbit_direction(m, a);
        int64_t off = dot30(d, sun) - quarter, s = (after_moon ? 4 * dot30(d, *after_moon) : dot30(d, target) / 8) -
                      4 * (off < 0 ? -off : off) - 3 * (d.y < 0 ? -d.y : d.y);
        if (face) {
            /* The landmark where the lock carries it with the moon here. */
            int64_t light = dot30(turn(mark, axis, bearing(before, scale(d, -1, 1), axis)), sun) - lit;
            s = -8 * (light < 0 ? -light : light) - (d.y < 0 ? -d.y : d.y);
        }
        if (s > score) { score = s; best = a; }
    }
    /* Mean anomaly is the phase plus the time part; the table angle stands
       in for it, within the eccentricity. */
    now = (int)(((uint64_t)g->tick % cycle) * 1024 / cycle);
    m->phase = (best - now) & 1023;
    m->center = body_position(&g->system, body, g->tick);
    after = unit30(sub(host->center, m->center));
    if (m->tilted && m->spin_period == (uint32_t)m->period) {
        spin = unit30(body_tilt(m, vec(0, 0, ONE), 0));
        m->spin_phase += (uint32_t)bearing(before, after, spin) << 12;
    }
    return scale(after, -1, 1);
}
static void stage_place(Game *g, int i) {
    const TourStop *s = &tour_stops[i];
    Place *p = &plan.place[i];
    int k, chars = 0;
    memset(p, 0, sizeof(*p));
    p->pole = north;
    p->azimuth = DEGREES(s->azimuth);
    p->drift = DEGREES(s->drift);
    p->elevation = DEGREES(s->elevation);
    p->parent = -1;
    if (!s->body) {
        /* Deep space: a virtual body a million kilometres short of the eye,
           so flights to and from it grow and shrink like any other. */
        Vec base = sunward(g, centre_of(g, i ? solar_body_index(&g->system, "Pluto") : 2));
        Vec d = lift(turn(scale(base, -1, 1), north, p->azimuth), north, p->elevation);
        p->reach = 1000000000LL * FP;
        p->centre = add(sun_q8(g), scale(d, (int64_t)s->reach * (AU_METRES / 10) * FP - p->reach, ONE));
        p->base = d;
        p->azimuth = p->drift = p->elevation = 0;
        p->body = -1;
        p->radius = 0;
    } else if (!strcmp(s->body, "Sun")) {
        const Star *star = &g->system.star[0];
        p->body = -1;
        p->centre = sun_q8(g);
        p->radius = star->radius;
        p->clear = star->radius * 3 * FP;
        p->base = unit30(flat(unit30(sub(centre_of(g, 0), p->centre)), north));
        snprintf(plan.stats[i], sizeof(plan.stats[i]), "RADIUS %d KM  GRAVITY 27.94 G", (int)(star->radius / 1000));
    } else {
        const Planet *b;
        BodyFacts facts;
        p->body = solar_body_index(&g->system, s->body);
        b = &g->system.planets[p->body];
        p->parent = b->parent;
        p->centre = centre_of(g, p->body);
        p->radius = b->radius;
        p->clear = (int64_t)b->radius * (b->rings ? 26 : 13) / 10 * FP;
        if (p->parent >= 0) {
            /* From the planet's far side, turned towards the Sun; the drift
               circles the line to the planet, which stays behind the moon. */
            Arc lit = arc(unit30(sub(p->centre, centre_of(g, p->parent))), unit30(sub(sun_q8(g), p->centre)), north);
            p->pole = lit.a;
            if (lit.angle > p->azimuth) lit.angle = p->azimuth;
            p->base = along(&lit, Q16);
            p->azimuth = 0;
        } else p->base = sunward(g, p->centre);
        /* Facing a landmark: the azimuth then turns from it. */
        if (s->face) p->base = landmark(b, s);
        /* Ringed worlds are seen from above their sunlit face. */
        if (b->rings) {
            p->pole = unit30(body_tilt(b, vec(0, 0, ONE), 0));
            if (dot30(p->pole, unit30(sub(sun_q8(g), p->centre))) < 0) p->pole = scale(p->pole, -1, 1);
            p->base = unit30(flat(unit30(sub(sun_q8(g), p->centre)), p->pole));
        }
        body_facts(&g->system, p->body, &facts);
        {
            int hundredths = (int)((int64_t)facts.gravity * 100 / 9807);
            if (hundredths) snprintf(plan.stats[i], sizeof(plan.stats[i]), "RADIUS %d KM  GRAVITY %d.%02d G",
                                     (int)(b->radius / 1000), hundredths / 100, hundredths % 100);
            else snprintf(plan.stats[i], sizeof(plan.stats[i]), "RADIUS %d KM  GRAVITY %d MM/S2",
                          (int)(b->radius / 1000), (int)facts.gravity);
        }
    }
    if (s->body) p->reach = (int64_t)p->radius * s->reach / 10 * FP;
    for (k = 0; k < TOUR_LINES && s->fact[k]; k++) chars += (int)strlen(s->fact[k]);
    p->hold = (int)strlen(s->title) * TOUR_TITLE_TICKS + chars + READING;
    if (p->hold < S(6)) p->hold = S(6);
}
/* ---- Presentation -------------------------------------------------------- */
/* A levelling gimbal: the up of the last tick, drawn towards ecliptic north
   and squared to the view, so steep views keep a steady roll. */
static void frame_camera(Tour *t, Vec eye, Vec front) {
    Intro *v = &t->view;
    Vec right, up = flat(add(t->up, mul(north, 1, 6)), front);
    if (length(up) < ONE / 64) up = flat(north, front);
    if (length(up) < ONE / 64) up = flat(vec(0, 0, ONE), front);
    up = unit30(up);
    right = cross30(up, front);
    t->up = up;
    v->eye = eye;
    v->exact = 1;
    v->axes[0] = scale(right, 1, 1 << 16);
    v->axes[1] = scale(up, 1, 1 << 16);
    v->axes[2] = scale(front, 1, 1 << 16);
    /* Table angles for the few effects that read them (cloud fog). */
    aim_angles(v->axes[2], &v->yaw, &v->pitch);
    {
        Vec right0 = vec(cosi(v->yaw), 0, -sini(v->yaw)), up0, d;
        int unused = 0;
        up0 = vec(-(int64_t)sini(v->pitch) * sini(v->yaw) / 16384, cosi(v->pitch),
                  -(int64_t)sini(v->pitch) * cosi(v->yaw) / 16384);
        d = vec((v->axes[1].x * right0.x + v->axes[1].y * right0.y + v->axes[1].z * right0.z) / 16384, 0,
                (v->axes[1].x * up0.x + v->axes[1].y * up0.y + v->axes[1].z * up0.z) / 16384);
        aim_angles(d, &v->roll, &unused);
    }
}
/* The star as a soft disc: a white-hot corona past the limb close by, a
   bright point far away, the same in both renderers. */
static void star_glow(Tour *t, const Game *g, Vec eye) {
    const Star *s = &g->system.star[0];
    int64_t r = s->radius / 10 * 16, reach = length(sub(s->center, scale(eye, 1, FP))) / 50;
    t->view.glow[0] = (IntroGlow){sun_q8(g), r > reach ? r : reach, 31, 12};
    t->view.glows = 1;
}
static void step(Tour *t, Game *g) {
    int i = t->stop, local;
    const Place *p;
    Vec eye, front, previous = t->view.eye;
    while (i + 1 < TOUR_STOPS && t->tick >= plan.place[i + 1].start) i++;
    p = &plan.place[i];
    local = t->tick - p->start;
    t->stop = i;
    if (local < p->hold || i == TOUR_STOPS - 1) {
        eye = hold_eye(i, progress(local, 0, p->hold));
        front = look_from(g, i, eye);
        t->shown = local; t->transit = 0;
        t->distance = t->speed = 0;
        memcpy(t->stats, plan.stats[i], sizeof(t->stats));
    } else {
        const Flight *f = &plan.flight[i];
        int fly = local - p->hold;
        int32_t ya, pa, yb, pb, k;
        eye = flight_eye(i, fly);
        heading(&f->pan, look_from(g, i, eye), &ya, &pa);
        heading(&f->pan, look_from(g, i + 1, eye), &yb, &pb);
        /* The pan between the two bodies keeps its sense for the whole
           flight, even as the camera passes between them. */
        yb -= ya;
        if (!fly) t->pan = 0;
        yb -= (yb - t->pan + TURN / 2 + 4 * TURN) / TURN * TURN - 4 * TURN;
        t->pan = yb;
        k = smooth(progress(fly, 0, f->settle));
        front = direction(&f->pan, ya + (int32_t)((int64_t)yb * k / Q16), pa + (int32_t)((int64_t)(pb - pa) * k / Q16));
        t->shown = 0; t->transit = fly + 1; t->flight = f->length;
        t->distance = length(sub(eye, plan.place[i + 1].centre)) / FP - plan.place[i + 1].radius;
        if (t->distance < 0) t->distance = 0;
        t->speed = fly ? length(sub(eye, previous)) / FP * HZ : 0;
    }
    frame_camera(t, eye, front);
    star_glow(t, g, eye);
    t->fade = t->tick < S(1) ? 16 - t->tick * 16 / S(1) :
              t->tick > t->length - S(1.5) ? (t->tick - (t->length - S(1.5))) * 16 / S(1.5) : 0;
    if (t->fade > 16) t->fade = 16;
    g->pos = eye;
    g->body = nearest_body(g, eye);
    g->ui_clock++;
}
void tour_start(Tour *t, Game *g, uint32_t seed) {
    int i, start = 0;
    memset(t, 0, sizeof(*t));
    memset(&plan, 0, sizeof(plan));
    prepare_roots();
    t->seed = seed;
    t->running = 1;
    game_init(g, seed);
    setup_system(g, catalog_sector(0));
    g->tick = 0; /* J2000 */
    g->system.world_ready = 0;
    update_world(g);
    {
        /* Each moon lines up with the one before it round the same planet,
           so flights between them run straight out from the planet. */
        int host = -1;
        Vec last = north;
        for (i = 0; i < TOUR_STOPS; i++)
            if (tour_stops[i].body && strcmp(tour_stops[i].body, "Sun")) {
                int body = solar_body_index(&g->system, tour_stops[i].body), parent = g->system.planets[body].parent;
                if (parent < 0) continue;
                last = place_moon(g, body, parent == host ? &last : NULL, tour_stops[i].face ? &tour_stops[i] : NULL);
                host = parent;
            }
    }
    for (i = 0; i < TOUR_STOPS; i++)
        if (tour_stops[i].face && g->system.planets[solar_body_index(&g->system, tour_stops[i].body)].parent < 0)
            face_landmark(g, &tour_stops[i]);
    g->system.world_ready = 0;
    update_world(g);
    /* Fictional ports and traffic stay out of the real Solar System. */
    g->nports = 0;
    memset(g->npc, 0, sizeof(g->npc));
    g->docked = -1; g->target = -1; g->target_body = -1;
    g->cinematic = 1;
    g->page = FLIGHT;
    g->camera_mode = 0;
    g->throttle = 0;
    for (i = 0; i < TOUR_STOPS; i++) stage_place(g, i);
    for (i = 0; i + 1 < TOUR_STOPS; i++) plan_flight(g, i);
    for (i = 0; i < TOUR_STOPS; i++) {
        plan.place[i].start = start;
        start += plan.place[i].hold + (i + 1 < TOUR_STOPS ? plan.flight[i].length : 0);
    }
    t->length = start;
    step(t, g);
}
int tour_frame(Tour *t, Game *g, int ticks) {
    while (t->running && ticks-- > 0) {
        if (++t->tick >= t->length) { tour_finish(t, g); break; }
        step(t, g);
    }
    return t->running;
}
void tour_finish(Tour *t, Game *g) {
    t->running = 0;
    game_init(g, t->seed);
}
int tour_skippable(const Tour *t) { return t->running && t->tick >= HZ / 2; }
int tour_stop_tick(int stop) { return plan.place[stop < 0 ? 0 : stop >= TOUR_STOPS ? TOUR_STOPS - 1 : stop].start; }

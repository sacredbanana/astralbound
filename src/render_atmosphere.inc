/* SPDX-License-Identifier: Unlicense
   Visible-light palette approximations of gas emission and scattering.
   Chemistry comes from the physical catalogue or generated gas family;
   climate still controls density, dust/clouds, weather and flight physics.
   References and indexed-colour limits: docs/planet-rendering.md. */
static const struct { uint8_t day, glow, steps, warm; uint16_t band; } sky_tint[] = {
    {32, 32, 32, 0, 0},       /* vacuum */
    {32, 128, 32, 1, 4800},   /* clear N2/O2: Rayleigh blue, warm sunset */
    {192, 160, 32, 0, 3600},  /* thin CO2 with dust: ochre sky, blue near sun */
    {128, 96, 16, 1, 11000},  /* dense CO2 with sulphur cloud: warm diffuse sky */
    {128, 160, 32, 0, 3000},  /* N2/CH4 organic haze: orange, blue forward scatter */
    {192, 80, 16, 0, 8000}};  /* H2/He beneath cloud/haze */
/* Solid sky bands: the 32-colour palette uses its darkest muted warm tones
   at low light. Never screen the whole sky against black for a darker hue. */
static const uint8_t ecs_sky[6][8] = {
    {0,0,0,0,0,0,0,0}, {0,1,2,3,4,5,6,7},
    {0,0,21,22,14,12,24,15}, {0,21,22,14,12,24,15,31},
    {0,0,21,22,14,12,24,24}, {0,16,19,28,29,20,18,15}
};
static const uint8_t ecs_glow[2][4] = {{25,26,27,20}, {21,22,14,12}};
static int atmosphere_sky_tint(const Planet *p, int64_t altitude) {
    static const uint8_t clear[AIR_TYPES] = {VACUUM,TEMPERATE,THIN,TEMPERATE,TEMPERATE,TEMPERATE};
    static const uint8_t hazy[HAZE_TYPES] = {TEMPERATE,THIN,DENSE,FROZEN,HYDROGEN,TEMPERATE,TEMPERATE};
    AtmosphereGas gas = atmosphere_gases(p);
    AtmosphereHaze haze = atmosphere_haze(p);
    /* High cloud remains diffuse on Venus-like worlds. Other haze layers
       give way to short-wavelength scattering higher in the atmosphere. */
    if (gas == AIR_NONE) return VACUUM;
    if (haze == HAZE_SULPHUR || altitude < p->atmosphere / 2) return hazy[haze];
    return clear[gas];
}
typedef struct { uint8_t colour[2][3], transmission[3]; } AtmosphereEmission;
/* Lower edge / main curtain / upper fringe. UV/IR emission is not assigned
   a false visible colour. Hydrogen's visible component is deliberately dim. */
static const AtmosphereEmission atmosphere_emission[AIR_TYPES] = {
    {{{0,0,0},{0,0,0}}, {0,0,0}},
    {{{23,11,14},{23,11,14}}, {16,16,12}}, /* nitrogen pink, oxygen green/red */
    {{{27,11,22},{187,11,22}}, {6,10,6}}, /* CO2 dissociation: faint O green/red */
    {{{23,27,26},{23,187,185}}, {14,12,10}}, /* nitrogen bands; no oxygen green */
    {{{14,23,26},{14,23,187}}, {8,8,8}}, /* hydrogen red below, violet above */
    {{{14,23,26},{14,23,187}}, {8,8,8}}
};
/* Both backends share the exact atmospheric shell and gas colour. Geometry
   only disc setup avoids touching the terrain/continent caches. The planet
   masks the inner interval; ordered transparency fades on the night side. */
static void atmosphere_halo(const Planet *p, int painter) {
    Planet outer = *p;
    Disc shell, globe;
    int64_t distance, thickness;
    int y, tint, colour;
    if (!p->atmosphere || p->climate == VACUUM || atmosphere_gases(p) == AIR_NONE) return;
    distance = length(sub(p->center, scale(camera_position, 1, FP)));
    thickness = p->atmosphere;
    if (thickness > p->radius / 8) thickness = p->radius / 8;
    if (distance <= (int64_t)p->radius + thickness * 2 || thickness <= 0 ||
        (int64_t)p->radius * frame->w < distance * 6) return;
    outer.radius += (int32_t)thickness;
    if (!disc_setup(&shell, &outer, -1) || !disc_setup(&globe, p, -1)) return;
    tint = atmosphere_sky_tint(p, p->atmosphere);
    colour = frame->enhanced ? sky_tint[tint].day + 18 : ecs_sky[tint][5];
    for (y = shell.view.top; y < shell.view.bottom; y++) {
        int x, first, last, inner0 = 1, inner1 = 0, has_inner, flat_until = -1;
        DiscSample flat = {0};
        if (!planet_row_pixels(&shell.view, y, &first, &last)) continue;
        has_inner = planet_row_pixels(&globe.view, y, &inner0, &inner1);
        for (x = first; x <= last; x++) {
            DiscSample sample;
            int density;
            size_t at = (size_t)y * frame->w + x;
            if (has_inner && x >= inner0 && x <= inner1) { x = inner1; continue; }
            if (painter) {
                /* Flat painter spans need one light sample per side/row,
                   rather than a depth solve at every glow pixel. */
                if (x > flat_until) {
                    flat_until = has_inner && x < inner0 ? inner0 - 1 : last;
                    disc_sample(&shell, (x + flat_until) / 2, y, &flat);
                }
                sample = flat;
            } else disc_sample(&shell, x, y, &sample);
            density = sample.light * (sample.limb ? 4 : 10) / 16384;
            if (bayer4[(y & 3) * 4 + (x & 3)] >= density) continue;
            if (!painter && sample.depth < depth_buffer[at]) continue;
            frame->pixels[at] = (uint8_t)colour;
        }
    }
}

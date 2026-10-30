/* SPDX-License-Identifier: Unlicense
   Analytic planets for the enhanced renderer. planet_view supplies each
   scanline's exact span of the sphere; this backend shades it per pixel from
   the planet field and writes depth, so ships, ports and terrain occlude and
   are occluded correctly. Replaces the latitude/longitude facet globe.

   For a pixel ray d = (u, v, f) and the unit centre direction c, with
   b = c.d, s = |d|^2 and k = cos^2 of the angular radius, the near hit is
   D w d with w = k / (b + sqrt(b^2 - k s)); this form has no cancellation at
   the limb. The unit normal is (D/R)(w d - c), and the hit's depth along
   the view axis is D w f, so its reciprocal needs no further division. */
#define PLANET_FIELDS 4
#define DISC_STARS 6
#define DISC_SHADOWS 4
#define BACKDROP_DROP 8 /* metres */
static struct {
    PlanetField field;
    uint32_t key;
    unsigned used;
} planet_fields[PLANET_FIELDS];
static unsigned planet_field_clock;
/* Fields are small (about 14 KB) and cheap to rebuild; keep the last few. */
static PlanetField *planet_field_for(const Planet *p) {
    uint32_t key = p->seed ^ (uint32_t)p->type * 0x9e3779b9U ^ (uint32_t)p->radius;
    int i, oldest = 0;
    for (i = 0; i < PLANET_FIELDS; i++)
        if (planet_fields[i].used && planet_fields[i].key == key) {
            planet_fields[i].used = ++planet_field_clock;
            return &planet_fields[i].field;
        }
    for (i = 1; i < PLANET_FIELDS; i++)
        if (planet_fields[i].used < planet_fields[oldest].used) oldest = i;
    planet_field_init(&planet_fields[oldest].field, p);
    planet_fields[oldest].key = key;
    planet_fields[oldest].used = ++planet_field_clock;
    return &planet_fields[oldest].field;
}
typedef struct {
    PlanetView view;
    const Planet *p;
    PlanetField *field;
    int64_t ch[3];        /* centre direction, Q28 camera space */
    int64_t kappa;        /* cos^2 of the angular radius, Q28 */
    int64_t dr;           /* D / R, Q16 */
    int32_t m[3][3];      /* camera to body-fixed rotation, Q30 */
    int32_t light[DISC_STARS][3]; /* camera-space Q14 directions to the stars */
    int lights;
    /* Eclipses, in camera-space metres shifted down by shadow_shift. */
    int64_t centre[3], radius, occluder[DISC_SHADOWS][3], occluder_r[DISC_SHADOWS];
    int occluder_star[DISC_SHADOWS], shadows, shadow_shift; /* occluder_star: light index */
    uint32_t depth_m;     /* inverse depth = den * depth_m >> depth_s */
    int depth_s, backdrop, block;
    int64_t kappa_depth;  /* backdrop: cos^2 of the lowered depth sphere, Q28 */
} Disc;
typedef struct {
    int32_t n[3];      /* body-fixed normal, Q16 */
    int32_t light;     /* 0..16384 */
    int32_t height;    /* planet-scale base height (ocean worlds), metres */
    int32_t albedo;    /* regional reflectance of land, Q8 */
    int32_t ice;       /* signed geographic ice coverage */
    uint32_t depth;
    int limb, field; /* field: height and albedo evaluated */
} DiscSample;
/* floor(sqrt(x)) to about 2^-19 relative: the top eight bits of x, scaled
   to [2^30, 2^32) by an even shift, seed a 16-bit root; one Newton step with
   a 32-bit division finishes it. The bit-serial isqrt64 was a fifth of the
   time spent drawing a full-screen planet. */
static uint16_t disc_root_seed[256];
static uint32_t disc_sqrt(uint64_t x) {
    int shift = 0, step;
    uint32_t m, r;
    if (!x) return 0;
    if (!disc_root_seed[255]) {
        int i;
        for (i = 64; i < 256; i++)
            disc_root_seed[i] = (uint16_t)isqrt64(((uint64_t)i << 24) + (1U << 23));
    }
    for (step = 32; step >= 2; step /= 2)
        if (x >> (30 + step)) { x >>= step; shift += step; }
    while (x >> 32) { x >>= 2; shift += 2; }
    while (x < (1U << 30)) { x <<= 2; shift -= 2; }
    m = (uint32_t)x;
    r = disc_root_seed[m >> 24];
    r = (r + m / r) >> 1;
    return shift >= 0 ? r << (shift / 2) : r >> (-shift / 2);
}
static int disc_setup(Disc *s, const Planet *p, int backdrop) {
    Vec off = sub(scale(p->center, FP, 1), camera_position), c;
    int64_t unit_m, d, r, alt;
    Projection proj;
    int i, k;
    XFloat depth;
    memset(s, 0, sizeof(*s));
    /* Q8 metres keep low-flight horizons exact; far bodies use metres. */
    if (llabs(off.x) < (1LL << 43) && llabs(off.y) < (1LL << 43) && llabs(off.z) < (1LL << 43)) {
        unit_m = FP; r = (int64_t)p->radius * FP;
    } else {
        off = sub(p->center, scale(camera_position, 1, FP));
        unit_m = 1; r = p->radius;
    }
    d = length(off);
    c = view_rotate(off);
    /* The Q14 view matrix is orthonormal only to about 1e-4; restore the
       exact distance so the altitude and the cone agree. */
    if (length(c)) c = scale(c, d, length(c));
    alt = d - r;
    proj.cx = frame->w / 2 + proj_dx; proj.cy = view_h / 2 + proj_dy; proj.focal = frame->w / 2;
    proj.left = view_left; proj.top = view_top; proj.right = frame->w - view_right_inset; proj.bottom = view_h;
    if (planet_view(&s->view, &proj, c, r, alt) == PLANET_HIDDEN) return 0;
    s->p = p;
    s->field = backdrop < 0 ? NULL : planet_field_for(p);
    s->backdrop = backdrop > 0;
    s->block = graphics.planets >= 2 ? 4 : graphics.planets == 1 ? 8 : 16;
    if (alt < 1) alt = 1;
    s->ch[0] = muldiv(c.x, 1LL << 28, d); s->ch[1] = muldiv(c.y, 1LL << 28, d); s->ch[2] = muldiv(c.z, 1LL << 28, d);
    s->kappa = muldiv(muldiv(alt, 2 * r + alt, d), 1LL << 28, d);
    if (s->kappa >= 1LL << 28) s->kappa = (1LL << 28) - 1;
    if (s->kappa < 1) s->kappa = 1;
    s->dr = muldiv(d, 65536, r);
    /* Camera axis i in the world is row i of the view matrix; then undo
       the body's rotation, as ray_prepare does. */
    camera_to_body(p, s->m);
    for (i = 0; i < game->system.stars && s->lights < DISC_STARS; i++) {
        const Star *star = &game->system.star[i];
        Vec l;
        if (star->type == ST_BLACK_HOLE) continue;
        l = view_rotate(unit(sub(star->center, p->center), 16384));
        s->light[s->lights][0] = (int32_t)l.x; s->light[s->lights][1] = (int32_t)l.y;
        s->light[s->lights][2] = (int32_t)l.z; s->lights++;
    }
    /* As a backdrop the sphere's depth comes from one BACKDROP_DROP below
       the true surface. Terrain vertices are whole metres and their chords
       sag below the sphere; near the ground a relative bias alone let the
       sphere's ground win the depth test over real grass a metre above it.
       The drop grows towards grazing views, where that sag matters most. */
    s->kappa_depth = s->kappa;
    if (s->backdrop) {
        int64_t drop = BACKDROP_DROP * unit_m, ra = r - drop, aa = alt + drop;
        s->kappa_depth = muldiv(muldiv(aa, ra + d, d), 1LL << 28, d);
        if (s->kappa_depth >= 1LL << 28) s->kappa_depth = (1LL << 28) - 1;
    }
    /* inverse depth = 2^30 unit_m den / (D kappa f), den in Q14 pixels. */
    depth = xf_div(xf_fixed(unit_m, 44), xf_mul(xf_mul(xf_int(d), xf_int(s->kappa_depth)), xf_int(proj.focal)));
    s->depth_m = (uint32_t)depth.m; s->depth_s = -depth.e;
    if (backdrop < 0) return 1; /* atmosphere: geometry and light only */
    /* Eclipses: bodies that can cross the line from this one to a star. */
    {
        int64_t reach = p->radius;
        int light = -1;
        for (k = 0; k < game->system.stars; k++) {
            const Star *star = &game->system.star[k];
            if (star->type == ST_BLACK_HOLE) continue;
            if (++light >= DISC_STARS) break;
            for (i = 0; i < game->system.count && s->shadows < DISC_SHADOWS; i++) {
                const Planet *o = &game->system.planets[i];
                if (o == p || !swept_sphere(p->center, star->center, o->center, (int64_t)o->radius + p->radius))
                    continue;
                {
                    Vec q = view_rotate(sub(o->center, scale(camera_position, 1, FP)));
                    int64_t gap = length(sub(o->center, p->center)) + o->radius;
                    s->occluder[s->shadows][0] = q.x; s->occluder[s->shadows][1] = q.y;
                    s->occluder[s->shadows][2] = q.z;
                    s->occluder_r[s->shadows] = o->radius;
                    s->occluder_star[s->shadows] = light;
                    if (gap > reach) reach = gap;
                    s->shadows++;
                }
            }
        }
        if (s->shadows) {
            Vec q = view_rotate(sub(p->center, scale(camera_position, 1, FP)));
            int64_t far = length(q) + reach;
            while ((far >> s->shadow_shift) >= (1LL << 30)) s->shadow_shift++;
            s->centre[0] = q.x >> s->shadow_shift; s->centre[1] = q.y >> s->shadow_shift;
            s->centre[2] = q.z >> s->shadow_shift; s->radius = p->radius >> s->shadow_shift;
            for (i = 0; i < s->shadows; i++) {
                for (k = 0; k < 3; k++) s->occluder[i][k] >>= s->shadow_shift;
                s->occluder_r[i] >>= s->shadow_shift;
            }
        }
    }
    return 1;
}
/* Is the surface point under camera-space normal n (Q14) shadowed from
   light by an occluding body? */
static int disc_shadowed(const Disc *s, const int32_t *n, int light) {
    int i, k;
    for (i = 0; i < s->shadows; i++) {
        int64_t q[3], t = 0, q2 = 0;
        if (s->occluder_star[i] != light) continue;
        for (k = 0; k < 3; k++) {
            q[k] = s->occluder[i][k] - (s->centre[k] + s->radius * n[k] / 16384);
            t += q[k] * s->light[light][k];
            q2 += q[k] * q[k];
        }
        t /= 16384;
        if (t > 0 && q2 - t * t < s->occluder_r[i] * s->occluder_r[i]) return 1;
    }
    return 0;
}
static void disc_sample(const Disc *s, int x, int y, DiscSample *out) {
    int64_t u = x - s->view.proj.cx, v = s->view.proj.cy - y, f = s->view.proj.focal, d[3];
    int64_t b, sq, disc, r, den, wq;
    int32_t n[3];
    int i, k, light = 0;
    d[0] = u; d[1] = v; d[2] = f;
    b = (s->ch[0] * u + s->ch[1] * v + s->ch[2] * f) >> 14;    /* Q14 pixels */
    sq = u * u + v * v + f * f;
    disc = b * b - s->kappa * sq;                               /* Q28 pixels^2 */
    if (disc < 0) disc = 0;
    r = disc_sqrt((uint64_t)disc);
    den = b + r;
    if (den < 1) den = 1;
    {
        /* wq = kappa 2^28 / den, from a division-free reciprocal. */
        int shift;
        uint32_t y = reciprocal(den, &shift);
        uint64_t p = (uint64_t)s->kappa * y;                    /* 1/den = y 2^(shift-63) */
        wq = (int64_t)(shift <= 35 ? p >> (35 - shift) : p << (shift - 35)); /* Q42 / pixel */
    }
    for (i = 0; i < 3; i++) {
        int64_t diff = ((wq * d[i]) >> 14) - s->ch[i];          /* Q28 */
        n[i] = (int32_t)((diff * s->dr) >> 30);                 /* Q14 */
    }
    for (k = 0; k < s->lights; k++) {
        int dot = (int)(((int64_t)n[0] * s->light[k][0] + (int64_t)n[1] * s->light[k][1] +
                         (int64_t)n[2] * s->light[k][2]) >> 14);
        if (dot > 0 && (!s->shadows || !disc_shadowed(s, n, k))) light += dot;
    }
    out->light = light > 16384 ? 16384 : light;
    for (i = 0; i < 3; i++) {
        int64_t q = ((int64_t)s->m[i][0] * n[0] + (int64_t)s->m[i][1] * n[1] +
                     (int64_t)s->m[i][2] * n[2]) >> 28;
        out->n[i] = (int32_t)(q < -65536 ? -65536 : q > 65536 ? 65536 : q);
    }
    out->field = 0;
    {
        uint64_t inv;
        if (s->backdrop) {
            /* Depth on the lowered sphere; rays that miss it near the limb
               take its point of closest approach. */
            int64_t low = b * b - s->kappa_depth * sq;
            den = b + (low > 0 ? disc_sqrt((uint64_t)low) : 0);
        }
        inv = (uint64_t)den * s->depth_m;
        inv = s->depth_s >= 0 ? (s->depth_s < 64 ? inv >> s->depth_s : 0)
                              : (-s->depth_s < 8 && !(inv >> (63 + s->depth_s)) ? inv << -s->depth_s : 0xFFFFFFFFU);
        if (inv > 0xFFFFFFFFU) inv = 0xFFFFFFFFU;
        /* Behind nearby terrain, whose facets sag below the true sphere. */
        if (s->backdrop) inv -= inv >> 7;
        out->depth = (uint32_t)inv;
    }
    /* Within an eighth of the radius of the limb the normal turns too fast
       to interpolate across a block. */
    out->limb = r * 8 < b;
}
/* The planet-field part of a sample: evaluated only where a pixel shows. */
static void disc_field(const Disc *s, DiscSample *m) {
    Vec nb = vec(m->n[0], m->n[1], m->n[2]);
    if (m->field) return;
    m->height = s->field->type == OCEAN ? planet_field_base_height(s->field, nb) : 0;
    m->albedo = s->field->veil && !s->field->map ? 0 : planet_field_regional_albedo(s->field, nb, 1) * 256;
    m->ice = s->field->veil || s->field->type == ICE ? -9000 : planet_field_ice(s->field, nb);
    m->field = 1;
}
/* Linear steps across a block in Q16: one division per channel per block,
   then additions per pixel (a 68k divide costs about a hundred cycles). */
typedef struct { int64_t v[8], dv[8]; } DiscStep;
static void disc_step_begin(DiscStep *t, const DiscSample *a, const DiscSample *b, int len) {
    int64_t from[8], to[8];
    int i;
    for (i = 0; i < 3; i++) { from[i] = a->n[i]; to[i] = b->n[i]; }
    from[3] = a->light; to[3] = b->light;
    from[4] = a->height; to[4] = b->height;
    from[5] = a->albedo; to[5] = b->albedo;
    from[6] = a->depth; to[6] = b->depth;
    from[7] = a->ice; to[7] = b->ice;
    for (i = 0; i < 8; i++) {
        t->v[i] = from[i] * 65536 + 32768;
        t->dv[i] = (to[i] - from[i]) * 65536 / len;
    }
}
static void disc_step(DiscStep *t, DiscSample *out) {
    int i;
    for (i = 0; i < 8; i++) t->v[i] += t->dv[i];
    for (i = 0; i < 3; i++) out->n[i] = (int32_t)(t->v[i] >> 16);
    out->light = (int32_t)(t->v[3] >> 16);
    out->height = (int32_t)(t->v[4] >> 16);
    out->albedo = (int32_t)(t->v[5] >> 16);
    out->depth = (uint32_t)(t->v[6] >> 16);
    out->ice = (int32_t)(t->v[7] >> 16);
}
static int disc_colour(const Disc *s, const DiscSample *m, int x, int y) {
    int mix[3], material, shade, dark = 0, t = bayer4[(y & 3) * 4 + (x & 3)];
    const Ramp *ramp;
    material = planet_field_classify(s->field, m->height, m->ice, m->albedo, mix);
    /* Lambert into the 15-step material ramps, in sixteenths for dithering. */
    shade = m->light * 224 / 16384;
    if (material == 5 && s->field->map) {
        /* Measured belts and spots; a veil without a map is uniform. The
           belt ramp is ordered-dithered in over two albedo steps, so belts
           shade into zones instead of breaking into blotches. */
        dark = m->albedo + (t - 8) * 32 < 0;
        shade += m->albedo * shade / (14 * 256);
    } else if (material == 5) {
        /* Gas bands follow latitude; broad longitude variation shows spin
           (material_color's banding, evaluated per pixel). */
        int fx = m->n[0] / 4, fy = m->n[1] / 4, fz = m->n[2] / 4;
        if (fz < -16384) fz = -16384;
        if (fz > 16384) fz = 16384;
        /* x/15 as x * 4369 >> 16, exact for these small products. */
        shade = shade * (8 + ((fz + 16384) * 2330 >> 22) % 8) * 4369 >> 16;
        shade = shade * (12 + fx * 2 / 16384 + fy / 16384) * 4369 >> 16;
    } else if (material >= 1 && material <= 4) {
        /* Sub-pixel lowland/forest/rock, mixed in their expected shares. */
        if (s->field->type == OCEAN && material <= 3)
            material = (bayer4[((y + 1) & 3) * 4 + ((x + 2) & 3)] * 16 + 8) < mix[2] ? 3 : 1;
        shade += m->albedo * shade / (14 * 256);
        /* Measured dark ground takes its own ramp, dithered in like belts. */
        if (s->field->map && s->field->map->albedo && s->field->map->dark_ramp)
            dark = m->albedo + (t - 8) * 32 < 0;
    }
    if (shade < 0) shade = 0;
    if (shade > 224) shade = 224;
    ramp = body_ramp(s->p, material, dark);
    if (frame->enhanced) {
        shade = (shade + t) >> 4;
        return ramp->aga + (shade > 14 ? 14 : shade) * ramp->step;
    }
    shade = (shade / 6 + t) >> 4;
    return ramp->ecs + (shade > 2 ? 2 : shade);
}
/* Terrain triangles leave occasional one-pixel cracks where neighbouring
   tiles and LOD levels meet after rounding. Behind the ground the sphere
   would show through them as a line of its own colour. A sphere pixel
   between two clearly nearer neighbours, left and right or above and below,
   is such a crack: take the neighbour's colour and depth instead. Pixels
   beyond the terrain, or at its silhouette, never have terrain both sides. */
static int disc_crack(const Disc *s, uint8_t *pixels, uint32_t *depths, int x, int y, uint32_t depth) {
    uint32_t near = depth + (depth >> 8);
    int w = frame->w, up = y > s->view.proj.top, down = y + 1 < s->view.proj.bottom;
    (void)s;
    if (x > s->view.proj.left && x + 1 < s->view.proj.right && depths[x - 1] > near && depths[x + 1] > near) {
        pixels[x] = pixels[x - 1];
        depths[x] = depths[x - 1];
        return 1;
    }
    /* Missing rows at the view's edge count as terrain: a crack along the
       bottom row has nothing below it. */
    if ((!up || depths[x - w] > near) && (!down || depths[x + w] > near) && (up || down)) {
        pixels[x] = up ? pixels[x - w] : pixels[x + w];
        depths[x] = up ? depths[x - w] : depths[x + w];
        return 1;
    }
    return 0;
}
static void disc_draw(const Disc *s) {
    int y;
    if (s->view.kind == PLANET_POINT) {
        int x = s->view.centre_x >> 8;
        y = s->view.centre_y >> 8;
        if (x >= s->view.proj.left && x < s->view.proj.right && y >= s->view.proj.top && y < s->view.proj.bottom) {
            DiscSample m;
            uint32_t *depth = depth_buffer + (size_t)y * frame->w + x;
            disc_sample(s, x, y, &m);
            if (m.depth >= *depth) {
                disc_field(s, &m);
                *depth = m.depth;
                frame->pixels[(size_t)y * frame->w + x] = (uint8_t)disc_colour(s, &m, x, y);
            }
        }
        return;
    }
    for (y = s->view.top; y < s->view.bottom; y++) {
        int x0, x1, x;
        uint8_t *pixels;
        uint32_t *depths;
        DiscSample a, b;
        DiscStep step;
        if (!planet_row_pixels(&s->view, y, &x0, &x1)) continue;
        if (platform_pump && !(y & 15)) platform_pump();
        pixels = frame->pixels + (size_t)y * frame->w;
        depths = depth_buffer + (size_t)y * frame->w;
        disc_sample(s, x0, y, &a);
        for (x = x0; x <= x1;) {
            int end = x + s->block > x1 ? x1 : x + s->block, last = end == x1 ? end : end - 1, i;
            if (end > x) disc_sample(s, end, y, &b); else b = a;
            /* Depth first: blocks hidden behind ships, ports or terrain
               skip the planet field entirely. */
            {
                int visible = a.limb || b.limb;
                int64_t z = (int64_t)a.depth * 65536 + 32768;
                int64_t dz = end > x ? ((int64_t)b.depth - a.depth) * 65536 / (end - x) : 0;
                for (i = x; i <= last && !visible; i++, z += dz)
                    visible = (uint32_t)(z >> 16) >= depths[i];
                if (!visible) { a = b; x = last + 1; continue; }
            }
            disc_field(s, &a);
            disc_field(s, &b);
            if (end > x && !a.limb && !b.limb) disc_step_begin(&step, &a, &b, end - x);
            for (i = x; i <= last; i++) {
                DiscSample m;
                if (i == x) m = a;
                else if (a.limb || b.limb) {
                    if (i == end) m = b;
                    else { disc_sample(s, i, y, &m); if (m.depth >= depths[i]) disc_field(s, &m); }
                } else { disc_step(&step, &m); if (i == end) m = b; }
                if (m.depth < depths[i]) continue;
                if (s->backdrop && disc_crack(s, pixels, depths, i, y, m.depth)) continue;
                depths[i] = m.depth;
                pixels[i] = (uint8_t)disc_colour(s, &m, i, y);
            }
            a = b;
            x = last + 1;
        }
    }
}
/* Draw one body's sphere. backdrop: nearby terrain geometry owns the
   ground, and the sphere only fills in beyond it. */
static void planet_disc(int body, int backdrop) {
    const Planet *p = &game->system.planets[body];
    Disc s;
    if (p->catalog) {
        /* As before: hundreds of distant catalog moons are unresolved. */
        Vec c = camera(p->center);
        if (c.z > 0 && (int64_t)p->radius * frame->w < c.z * 2) return;
    }
    if (disc_setup(&s, p, backdrop)) disc_draw(&s);
}

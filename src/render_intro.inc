/* SPDX-License-Identifier: Unlicense
   Title sequence presentation. The 3D view renders into a letterboxed
   sub-frame, so projection, sky and clipping stay centred and fewer pixels
   are rasterised. Beams, glows, fades and the logo use only the 32 base
   colours and ordered dither: ECS, AGA and RTG show the same picture. */
static const uint8_t intro_bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
/* Camera-space point at the near plane between a and b (a behind it). */
static Vec intro_near(Vec a, Vec b) {
    int64_t span = b.z - a.z;
    return vec(a.x + muldiv(b.x - a.x, 8 - a.z, span), a.y + muldiv(b.y - a.y, 8 - a.z, span), 8);
}
/* Depth-tested laser, clipped to the view (Liang-Barsky, Q16 parameter):
   reciprocal depth is linear along the projected line. */
static void intro_beam(const IntroBeam *b) {
    Vec a = camera(scale(b->a, 1, FP)), c = camera(scale(b->b, 1, FP));
    int64_t x0, y0, dx, dy, t0 = 0, t1 = 65536, edge[4][2];
    int n, i, k, thick;
    uint32_t da, dc;
    if (a.z < 8 && c.z < 8) return;
    if (a.z < 8) a = intro_near(a, c);
    else if (c.z < 8) c = intro_near(c, a);
    x0 = frame->w / 2 + a.x * (frame->w / 2) / a.z;
    y0 = view_h / 2 - a.y * (frame->w / 2) / a.z;
    dx = frame->w / 2 + c.x * (frame->w / 2) / c.z - x0;
    dy = view_h / 2 - c.y * (frame->w / 2) / c.z - y0;
    edge[0][0] = -dx; edge[0][1] = x0;
    edge[1][0] = dx; edge[1][1] = frame->w - 1 - x0;
    edge[2][0] = -dy; edge[2][1] = y0;
    edge[3][0] = dy; edge[3][1] = view_h - 2 - y0;
    for (k = 0; k < 4; k++) {
        int64_t p = edge[k][0], q = edge[k][1], r;
        if (!p) { if (q < 0) return; continue; }
        r = q * 65536 / p;
        if (p < 0) { if (r > t1) return; if (r > t0) t0 = r; }
        else { if (r < t0) return; if (r < t1) t1 = r; }
    }
    da = (uint32_t)(1073741824LL / a.z); dc = (uint32_t)(1073741824LL / c.z);
    {
        int64_t sx = x0 + dx * t0 / 65536, sy = y0 + dy * t0 / 65536;
        int64_t ex = x0 + dx * t1 / 65536, ey = y0 + dy * t1 / 65536;
        int64_t sd = da + ((int64_t)dc - da) * t0 / 65536, ed = da + ((int64_t)dc - da) * t1 / 65536;
        n = (int)(llabs(ex - sx) > llabs(ey - sy) ? llabs(ex - sx) : llabs(ey - sy));
        if (n < 1) n = 1;
        /* Close beams gain a white second row so they read as bolts. */
        thick = (a.z < 900 || c.z < 900) ? 1 : 0;
        for (i = 0; i <= n; i++) {
            int x = (int)(sx + (ex - sx) * i / n), y = (int)(sy + (ey - sy) * i / n);
            uint32_t depth = (uint32_t)(sd + (ed - sd) * i / n);
            for (k = 0; k <= thick; k++) {
                int yy = y + k;
                if (x < 0 || x >= frame->w || yy < 0 || yy >= view_h) continue;
                if (depth < depth_buffer[yy * frame->w + x]) continue;
                frame->pixels[yy * frame->w + x] = (uint8_t)(k ? 31 : b->colour);
            }
        }
    }
}
/* A soft emissive disc: white-hot core, coloured halo, dithered rim. The
   depth test uses the near surface, so exhausts show in front of their hull. */
static void intro_glow(const IntroGlow *g) {
    Vec v = camera(scale(g->p, 1, FP));
    int cx, cy, r, x, y;
    uint32_t depth;
    int64_t near = v.z - g->radius, size;
    if (v.z < 8) return;
    cx = screen_coord(frame->w / 2 + v.x * (frame->w / 2) / v.z);
    cy = screen_coord(view_h / 2 - v.y * (frame->w / 2) / v.z);
    /* Distant glows (a far exhaust) still light one pixel. */
    size = g->radius / 4 * (frame->w / 2) / v.z * 4;
    r = size < 1 ? 1 : size > frame->w ? frame->w : (int)size;
    if (cx + r < 0 || cx - r >= frame->w || cy + r < 0 || cy - r >= view_h) return;
    depth = (uint32_t)(1073741824LL / (near < 8 ? 8 : near));
    for (y = cy - r; y <= cy + r; y++) {
        if (y < 0 || y >= view_h) continue;
        for (x = cx - r; x <= cx + r; x++) {
            int dx = x - cx, dy = y - cy, d, c;
            if (x < 0 || x >= frame->w) continue;
            d = (dx * dx + dy * dy) * 256 / (r * r);
            if (d > 256 || depth < depth_buffer[y * frame->w + x]) continue;
            if (d < 50) c = g->core;
            else if (d < 150) c = g->halo;
            else if (intro_bayer[(y & 3) * 4 + (x & 3)] * 16 < (256 - d) * 2) c = g->halo;
            else continue;
            frame->pixels[y * frame->w + x] = (uint8_t)c;
        }
    }
}
/* Ordered dither of a whole region towards one colour, level 0..16. */
static void intro_dither(int level, int colour, int top, int bottom) {
    int x, y;
    if (level <= 0) return;
    for (y = top; y < bottom; y++) {
        uint8_t *row = frame->pixels + y * frame->w;
        const uint8_t *b = intro_bayer + (y & 3) * 4;
        if (level >= 16) { memset(row, colour, (size_t)frame->w); continue; }
        for (x = 0; x < frame->w; x++)
            if (b[x & 3] < level) row[x] = (uint8_t)colour;
    }
}
/* Glyphs at any integer scale, one colour per glyph row for metallic ramps. */
static const uint8_t *intro_glyph(char ch) {
    if (ch >= 'A' && ch <= 'Z') return glyphs[ch - 'A'];
    if (ch >= '0' && ch <= '9') return glyphs[26 + ch - '0'];
    return NULL;
}
static void intro_big_text(int x, int y, int s, const char *text, const uint8_t ramp[7], int limit) {
    int i, j, n = 0;
    for (; *text && n < limit; text++, n++, x += 6 * s) {
        const uint8_t *p = intro_glyph(*text);
        if (!p) continue;
        for (j = 0; j < 7; j++)
            for (i = 0; i < 5; i++)
                if (p[j] & (16 >> i)) {
                    int yy, top = y + j * s;
                    for (yy = top; yy < top + s; yy++)
                        if (yy >= 0 && yy < frame->h && x + i * s >= 0 && x + i * s + s <= frame->w)
                            memset(frame->pixels + yy * frame->w + x + i * s, ramp[j], (size_t)s);
                }
    }
}
static void intro_caption(const Intro *in, int bar, int s) {
    char shown[40];
    int n = in->caption_chars, width, x, y;
    if (!in->caption || n <= 0) return;
    if (n > (int)sizeof(shown) - 1) n = sizeof(shown) - 1;
    snprintf(shown, sizeof(shown), "%.*s", n, in->caption);
    width = (int)strlen(in->caption) * 6 * s;
    x = (frame->w - width) / 2;
    /* Inside the lower bar when it has room, else just above the frame edge. */
    y = bar >= 11 * s ? frame->h - bar + (bar - 7 * s) / 2 : frame->h - 14 * s;
    draw_scale = s;
    text(x / s, y / s, 13, shown);
    if (n < (int)strlen(in->caption) && (in->tick & 4)) rect(x / s + n * 6, y / s, 5, 7, 13);
    draw_scale = 1;
}
/* The closing card: a star streak opens across the frame, the name resolves
   from its centre outwards and a highlight sweeps the letters. */
static void intro_logo(const Intro *in) {
    static const char name[] = "ASTRALBOUND";
    static const uint8_t ramp[7] = {31, 15, 24, 23, 14, 22, 21};
    static const uint8_t shadow[7] = {25, 25, 25, 25, 25, 25, 25};
    int t = in->title, w = frame->w, h = frame->h, i;
    int s = w / 72, len = (int)strlen(name), width = (len * 6 - 1) * s;
    int x = (w - width) / 2, y = h / 2 - 7 * s / 2 - h / 16, cy = y + 7 * s / 2;
    memset(frame->pixels, 0, (size_t)w * h);
    for (i = 0; i < 160; i++) {
        uint32_t r = hash32((unsigned)i * 2654435761U + 99);
        int sx = (int)(r % (unsigned)w), sy = (int)((r >> 12) % (unsigned)h);
        int twinkle = (int)((r >> 24) + (unsigned)t / 3) % 23;
        if (twinkle) frame->pixels[sy * w + sx] = (uint8_t)(i % 6 ? 7 : 15);
    }
    {   /* Streak: grows from the centre, then thins to a rule under the name. */
        int reach = t < 24 ? w * t * t / (2 * 24 * 24) : w / 2;
        int fade = t < 48 ? 0 : t < 96 ? (t - 48) / 6 : 8;
        int k, row = cy + (t < 48 ? 0 : (t < 72 ? (t - 48) * (7 * s / 2 + s * 2) / 24 : 7 * s / 2 + s * 2));
        for (k = -reach; k <= reach; k++) {
            int px = w / 2 + k, a = abs(k) * 8 / (reach + 1);
            if (px < 0 || px >= w || a < fade) continue;
            frame->pixels[row * w + px] = (uint8_t)(a < 2 ? 31 : a < 5 ? 15 : 13);
        }
        if (t < 40) for (k = -(40 - t) / 4; k <= (40 - t) / 4; k++)
            if (row + k >= 0 && row + k < h) frame->pixels[(row + k) * w + w / 2] = 31;
    }
    if (t >= 16) {
        /* Letters appear in pairs from the middle, each flashing white first. */
        for (i = 0; i < len; i++) {
            int order = abs(2 * i - (len - 1)) / 2, born = 16 + order * 6, age = t - born;
            uint8_t flash[7];
            if (age < 0) continue;
            intro_big_text(x + i * 6 * s + s / 2 + 1, y + s / 2 + 1, s, name + i, shadow, 1);
            if (age < 4) { memset(flash, 31, sizeof(flash)); intro_big_text(x + i * 6 * s, y, s, name + i, flash, 1); }
            else {
                int sweep = (t - 96) % 160, j;
                memcpy(flash, ramp, sizeof(flash));
                /* A diagonal glint crosses every five seconds. */
                if (t >= 96 && abs(sweep * len / 48 - i) < 1)
                    for (j = 0; j < 7; j++) flash[j] = j < 3 ? 31 : 15;
                intro_big_text(x + i * 6 * s, y, s, name + i, flash, 1);
            }
        }
    }
    draw_scale = ui_scale(frame);
    if (t > 120 && (t / 20) % 3 != 2) {
        const char *press = "PRESS FIRE";
        text((w / draw_scale - (int)strlen(press) * 6) / 2, (h * 3 / 4) / draw_scale, 15, press);
    }
    draw_scale = 1;
}
static void render_intro_full(const Intro *in, const Game *g, Frame *f) {
    Frame view = *f;
    int bar = 0, i, s = ui_scale(f);
    frame = f; game = g; view_h = f->h;
    if (in->title) {
        intro_logo(in);
        intro_dither(in->fade, 0, 0, f->h);
        return;
    }
    if (in->letterbox) {
        bar = (f->h - f->w * 100 / 185) / 2;
        if (bar < 0) bar = 0;
    }
    view.pixels = f->pixels + (size_t)bar * f->w;
    view.h = f->h - 2 * bar;
    cinema = in;
    render_game(g, &view);
    /* render_game leaves the projection set up for the sub-frame. */
    for (i = 0; i < in->beams; i++) intro_beam(&in->beam[i]);
    for (i = 0; i < in->glows; i++) intro_glow(&in->glow[i]);
    if (in->cockpit) {
        int cx = view.w / 2, cy = view_h / 2;
        line(cx - 9, cy, cx - 3, cy, 15); line(cx + 3, cy, cx + 9, cy, 15);
        line(cx, cy - 9, cx, cy - 3, 15); line(cx, cy + 3, cx, cy + 9, 15);
        if (in->guns) {
            line(20, view_h - 20, cx, cy, 12); line(view.w - 20, view_h - 20, cx, cy, 12);
            line(21, view_h - 20, cx, cy, 31); line(view.w - 21, view_h - 20, cx, cy, 31);
        }
    }
    cinema = NULL;
    frame = f; view_h = f->h;
    if (bar) {
        memset(f->pixels, 0, (size_t)bar * f->w);
        memset(f->pixels + (size_t)(f->h - bar) * f->w, 0, (size_t)bar * f->w);
    }
    intro_dither(in->flash, 31, 0, f->h);
    intro_dither(in->fade, 0, 0, f->h);
    intro_caption(in, bar, s);
}

void render_intro(const Intro *in, const Game *g, Frame *f) {
    int factor=graphics.scale+1;
    if(factor>1 && scaled_capacity>=(size_t)f->w*f->h) {
        Frame low=*f;
        low.w=(f->w+factor-1)/factor; low.h=(f->h+factor-1)/factor;
        low.pixels=scaled_pixels;
        render_intro_full(in,g,&low);
        enlarge(&low,f,f->h);
        frame=f; draw_scale=1;
    } else render_intro_full(in,g,f);
}

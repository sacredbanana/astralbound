/* SPDX-License-Identifier: Unlicense; shared by combat and cinematic beams. */
/* Camera-space point at the near plane between a and b (a behind it). */
static Vec weapon_near(Vec a, Vec b) {
    int64_t span = b.z - a.z;
    return vec(a.x + muldiv(b.x - a.x, 8 - a.z, span), a.y + muldiv(b.y - a.y, 8 - a.z, span), 8);
}
/* Depth-tested laser, clipped to the view (Liang-Barsky, Q16 parameter):
   reciprocal depth is linear along the projected line. */
static void weapon_beam(Vec start, Vec end, int colour, int rich) {
    Vec a = camera(scale(start, 1, FP)), c = camera(scale(end, 1, FP));
    int64_t x0, y0, dx, dy, t0 = 0, t1 = 65536, edge[4][2];
    int n, i, k, thick;
    uint32_t da, dc;
    if (a.z < 8 && c.z < 8) return;
    /* Conservative frustum rejection also bounds projection arithmetic when
       System Drive leaves a short-lived projectile far behind the camera. */
    if ((a.x < -a.z && c.x < -c.z) || (a.x > a.z && c.x > c.z) ||
        (a.y < -a.z && c.y < -c.z) || (a.y > a.z && c.y > c.z)) return;
    if (a.z < 8) a = weapon_near(a, c);
    else if (c.z < 8) c = weapon_near(c, a);
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
        thick = rich && (a.z < 900 || c.z < 900) ? 1 : 0;
        for (i = 0; i <= n; i++) {
            int x = (int)(sx + (ex - sx) * i / n), y = (int)(sy + (ey - sy) * i / n);
            uint32_t depth = (uint32_t)(sd + (ed - sd) * i / n);
            for (k = 0; k <= thick; k++) {
                int yy = y + k;
                if (x < 0 || x >= frame->w || yy < 0 || yy >= view_h) continue;
                if (depth < depth_buffer[yy * frame->w + x]) continue;
                frame->pixels[yy * frame->w + x] = (uint8_t)(k ? 31 : colour);
            }
        }
    }
}
static void weapon_effects(void) {
    int i;
    if(game->bolt_count) for(i=0;i<MAX_WEAPON_BOLTS;i++) {
        const WeaponBolt *b=&game->bolts[i];
        Vec direction,a,c;
        int head;
        if(!b->ticks) continue;
        direction=sub(b->end,b->start);
        head=b->age+2; if(head>b->ticks) head=b->ticks;
        a=add(b->start,scale(direction,b->age,b->ticks));
        c=add(b->start,scale(direction,head,b->ticks));
        weapon_beam(a,c,b->colour,graphics.particles>0);
    }
    for(i=0;i<MAX_MISSILES;i++) {
        const Missile *m=&game->projectiles[i];
        Vec direction,tail,tip;
        if(!m->active) continue;
        direction=m->vel;
        if(m->target>=0 && m->target<MAX_NPC) direction=sub(direction,game->npc[m->target].vel);
        direction=unit(direction,16384);
        tail=sub(m->pos,scale(direction,(40+graphics.particles*20LL+(game->tick&3)*4)*FP,16384));
        tip=add(m->pos,scale(direction,8*FP,16384));
        weapon_beam(tail,m->pos,12,graphics.particles>0);
        weapon_beam(m->pos,tip,31,0);
    }
}

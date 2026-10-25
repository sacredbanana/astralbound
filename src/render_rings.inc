/* SPDX-License-Identifier: Unlicense
   Enhanced ring backend. Classic shares the cubic controls and crossing
   generator, but fills flat painter's spans. Here each covered pixel gets
   the ring plane's reciprocal depth, so terrain, planets and hulls occlude
   correctly. Inverse depth is affine on a plane: the pixel loop only adds.
   No triangle budget is consumed by the normal orbital ring path. */
static void ring_depth_spans(int colour, int64_t dx, int64_t dy, int64_t origin) {
    int y;
    for (y = cl_spans.top; y < cl_spans.bottom; y++) {
        ClRow *row = &cl_spans.rows[y];
        int i;
        if (platform_pump && !(y & 31)) platform_pump();
        if (y >= view_top && y < view_h) for (i = 0; i + 1 < row->n; i += 2) {
            int x, first = row->x[i], last = row->x[i + 1];
            int64_t z;
            if (first < view_left) first = view_left;
            if (last > frame->w - view_right_inset) last = frame->w - view_right_inset;
            z = origin + dy * y + dx * first;
            for (x = first; x < last; x++, z += dx) {
                int64_t depth = xf_down(z, 24);
                size_t at = (size_t)y * frame->w + x;
                if (depth > 0 && depth <= 1073741824 / 8 && (uint32_t)depth >= depth_buffer[at]) {
                    frame->pixels[at] = (uint8_t)colour;
                    depth_buffer[at] = (uint32_t)depth;
                }
            }
        }
        row->n = 0;
    }
    cl_spans.top = cl_spans.height; cl_spans.bottom = 0;
}
/* Returns zero only when a boundary crosses the near plane or exceeds the
   safe curve guard band. Those close-up views use the existing clipped
   mesh instead of dropping the ring or dividing by a depth near zero. */
static int ring_curves(const Planet *p) {
    int32_t points[3][2][8][4][2];
    int band, r, arc, j, focal = frame->w / 2;
    int cx = frame->w / 2 + proj_dx, cy = view_h / 2 + proj_dy;
    int64_t height = p->center.z - camera_position.z / FP;
    int64_t dx, dy, origin;
    Vec centre;
    XFloat factor;
    if (!graphics.rings || !height) return 1; /* in the ring plane: no area */
    if (view_h > 1024) return 0;
    centre = camera(p->center);
    if (centre.z < -(int64_t)p->radius * 3) return 1;
    for (band = 0; band < 3; band++) for (r = 0; r < 2; r++) {
        int32_t radius = 16384 * ((r ? 130 : 150) + band * 25) / 100;
        for (arc = 0; arc < 8; arc++) for (j = 0; j < 4; j++) {
            int angle = (arc + (j >= 2)) * 128;
            int handle = j == 1 ? 1 : j == 2 ? -1 : 0;
            int32_t wx, wy;
            int64_t x, y;
            Vec v;
            planet_ring_control(radius, angle, handle, 1, &wx, &wy);
            v = add(centre, view_rotate(vec((int64_t)wx * p->radius / 16384,
                                           (int64_t)wy * p->radius / 16384, 0)));
            if (v.z < 8) return 0;
            x = cx * 16 + v.x * focal * 16 / v.z;
            y = cy * 16 - v.y * focal * 16 / v.z;
            if (llabs(x) > (1 << 20) || llabs(y) > (1 << 20)) return 0;
            points[band][r][arc][j][0] = (int32_t)x;
            points[band][r][arc][j][1] = (int32_t)y;
        }
    }
    /* n is the ring normal in camera space (Q14). For ray (u,v,f),
       1/Z = n.ray / (height*f*16384). Keep 24 fractional depth bits:
       distant rings often have less than one depth unit per pixel. */
    factor = xf_div(xf_int(1073741824), xf_mul(xf_int(height), xf_int(focal * 16384)));
    dx = xf_to_fixed(xf_mul(factor, xf_int(view_matrix.m[0][2])), 24, 1LL << 46);
    dy = xf_to_fixed(xf_mul(factor, xf_int(-view_matrix.m[1][2])), 24, 1LL << 46);
    origin = xf_to_fixed(xf_mul(factor, xf_int(view_matrix.m[2][2] * focal)), 24, 1LL << 46)
             - dx * cx - dy * cy;
    if (cl_spans.height != view_h) cl_spans_init(&cl_spans, cl_rows, view_h);
    for (band = 0; band < 3; band++) {
        for (r = 0; r < 2; r++) for (arc = 0; arc < 8; arc++)
            cl_spans_bezier(&cl_spans, (const int32_t (*)[2])points[band][r][arc]);
        ring_depth_spans(17 + band, dx, dy, origin);
    }
    return 1;
}

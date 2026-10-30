/* SPDX-License-Identifier: Unlicense
   Solar System tour presentation: the cinematic view between a title bar
   and a fact bar, laid out on the interface's 320-wide grid. Text and the
   route strip use only the 32 base colours, so ECS, AGA and RTG agree. */
#define TOUR_TOP 28
#define TOUR_BOTTOM 40
static const uint8_t tour_ramp[7] = {31, 31, 15, 20, 13, 6, 27};
/* The first chars of s, with a block cursor while typing. */
static void tour_typed(int x, int y, int c, const char *s, int chars, int clock) {
    char shown[TOUR_TEXT + 1];
    int n = (int)strlen(s);
    if (chars <= 0) return;
    if (chars > n) chars = n;
    snprintf(shown, sizeof(shown), "%.*s", chars, s);
    text(x, y, c, shown);
    if (chars < n && (clock & 4)) rect(x + chars * 6, y, 5, 7, 13);
}
static void tour_distance(char *out, size_t size, int64_t metres) {
    if (metres >= AU_METRES / 10) {
        long hundredths = (long)(metres * 100 / AU_METRES);
        snprintf(out, size, "%ld.%02ld AU", hundredths / 100, hundredths % 100);
    } else snprintf(out, size, "%ld KM", (long)(metres / 1000));
}
static void tour_speed(char *out, size_t size, int64_t speed) {
    if (speed >= LIGHT_SPEED) {
        long tenths = (long)(speed * 10 / LIGHT_SPEED);
        snprintf(out, size, "%ld.%ld C", tenths / 10, tenths % 10);
    } else if (speed >= 10000) snprintf(out, size, "%ld KM/S", (long)(speed / 1000));
    else snprintf(out, size, "%ld M/S", (long)speed);
}
/* Holding: the name, a subtitle and measured data, then the facts. */
static void tour_card(const Tour *t, int w, int s) {
    const TourStop *stop = &tour_stops[t->stop];
    int name = (int)strlen(stop->title), typed = t->shown - name * TOUR_TITLE_TICKS, i;
    intro_big_text(8 * s, 7 * s, 2 * s, stop->title, tour_ramp, t->shown / TOUR_TITLE_TICKS + 1);
    if (typed < 0) return;
    text(w - 8 - (int)strlen(stop->subtitle) * 6, 6, 12, stop->subtitle);
    if (stop->body) text(w - 8 - (int)strlen(t->stats) * 6, 15, 7, t->stats);
    for (i = 0; i < TOUR_LINES && stop->fact[i]; i++) {
        tour_typed(8, frame->h / s - TOUR_BOTTOM + 3 + i * 9, 15, stop->fact[i], typed, t->tick);
        typed -= (int)strlen(stop->fact[i]);
    }
}
/* Flying: the destination, range and speed, and progress along the route. */
static void tour_route(const Tour *t, int w, int s) {
    char buf[32];
    int h = frame->h / s, i, y = h - TOUR_BOTTOM / 2, left = 16, span = w - 32;
    int at = left + (int)(((int64_t)t->stop * t->flight + t->transit) * span / ((TOUR_STOPS - 1) * (int64_t)t->flight));
    text(8, 6, 7, "EN ROUTE TO");
    text(8, 15, 15, tour_stops[t->stop + 1].title);
    tour_distance(buf, sizeof(buf), t->distance);
    text(w - 8 - (int)strlen(buf) * 6, 6, 13, buf);
    tour_speed(buf, sizeof(buf), t->speed);
    text(w - 8 - (int)strlen(buf) * 6, 15, 13, buf);
    rect(left, y, span + 1, 1, 3);
    rect(left, y, at - left + 1, 1, 13);
    for (i = 0; i < TOUR_STOPS; i++) {
        int x = left + i * span / (TOUR_STOPS - 1), seen = i <= t->stop;
        rect(x, y - (seen ? 2 : 1), 1, seen ? 5 : 3, seen ? 13 : 6);
    }
    rect(at - 1, y - 1, 3, 3, 31);
}
void render_tour(const Tour *t, const Game *g, Frame *f) {
    Frame view = *f;
    int s = ui_scale(f), w = f->w / s;
    view.pixels = f->pixels + (size_t)TOUR_TOP * s * f->w;
    view.h = f->h - (TOUR_TOP + TOUR_BOTTOM) * s;
    render_intro(&t->view, g, &view);
    frame = f; view_h = f->h; draw_scale = s;
    memset(f->pixels, 0, (size_t)TOUR_TOP * s * f->w);
    memset(f->pixels + (size_t)(f->h - TOUR_BOTTOM * s) * f->w, 0, (size_t)TOUR_BOTTOM * s * f->w);
    rect(0, TOUR_TOP - 1, w, 1, 2);
    rect(0, f->h / s - TOUR_BOTTOM, w, 1, 2);
    if (t->transit) tour_route(t, w, s);
    else tour_card(t, w, s);
    draw_scale = 1;
    intro_dither(t->fade, 0, 0, f->h);
}

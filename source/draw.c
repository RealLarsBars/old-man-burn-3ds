// The game picture, ported from the drawing half of web/game.js (default outfit only: no cosmetics on the 3DS).
#include "draw.h"
#include "sim.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define H(x) RGB(0x##x)
#define F(x, y, w, h) cv_rect((x), (y), (w), (h))
static void R(double x, double y, double w, double h, col_t c) { cv_fill(c); cv_rect(x, y, w, h); }

/* ---------- scenery: three landscapes, each with its own sky chosen by the weekly seed ---------- */
typedef struct { int kind; col_t ground, spA, spB, lit, shade, haze, sea, seaHi; } Zone;   // kind 0 desert, 1 coast, 2 forest
static const Zone BASE_ZONES[3] = {
  { 0, H(c8d489), H(8aa764), H(e6ebad), H(dcd77c), H(3e7f7a), H(d9dea0), 0, 0 },
  { 1, H(ecd0a8), H(c59a78), H(fbe8c8), H(9fd0d6), H(2f7288), H(f6dcb8), 0, 0 },
  { 2, H(86b57c), H(4e8260), H(b4d896), H(9a88cc), H(4a3e84), H(adcf98), 0, 0 }
};
typedef struct { col_t top, bot; bool sun, stars, moon; } Sky;
static const Sky SKIES[4] = { { H(ff3a4a), H(ff9a62), true, false, false }, { H(15405c), H(f2b08e), false, false, false },
                              { H(0b0830), H(7a3590), false, true, false }, { H(5a6fd4), H(ffc9a6), true, false, false } };
static const Zone SEASON_ZONE[3] = { { 0 }, { 0, H(4a3b30), H(33281f), H(6e4f33), H(4b3a6e), H(21163a), H(8a3a5a), H(231a44), H(6a5a9a) },
                                     { 0, H(e6edf8), H(c6d2ea), H(ffffff), H(f4f8ff), H(8aa0c8), H(dfe8ff), H(3a6a9a), H(cfe4ff) } };
static const Sky SEASON_SKY[3] = { { 0 }, { H(0e0620), H(d8582a), false, true, true }, { H(0b1638), H(7d97d0), false, true, false } };
static Zone ZONES[3];
static struct { col_t wall, A, B; } GAR;
static double stars[36][3];
static bool garageDirty = true;                                  // the garage's cached picture needs redrawing

static const Sky *skyOf(int zi) { return SEASON ? &SEASON_SKY[SEASON] : &SKIES[skyPerm[zi]]; }

void look_apply(void) {                                         // rebuild the sky, ground and garage for the current map
  for (int i = 0; i < 3; i++) {
    ZONES[i] = BASE_ZONES[i];
    if (SEASON) { int k = ZONES[i].kind; ZONES[i] = SEASON_ZONE[SEASON]; ZONES[i].kind = k; }
    pat_make(PAT_G0 + i, ZONES[i].ground, ZONES[i].spA, ZONES[i].spB, 520, 3);
  }
  static const col_t walls[3] = { H(1c1a2e), H(2b1824), H(142533) }, As[3] = { H(ff3ea5), H(ffd21f), H(ff5a3a) }, Bs[3] = { H(22e6d8), H(9bb8ff), H(7dff9c) };
  GAR.wall = walls[(int)floor(hash(SEED, 40) * 3)]; GAR.A = As[(int)floor(hash(SEED, 41) * 3)]; GAR.B = Bs[(int)floor(hash(SEED, 42) * 3)];
  if (SEASON == 1) { GAR.wall = H(1a1024); GAR.A = H(ff8a1f); GAR.B = H(b98cff); }
  if (SEASON == 2) { GAR.wall = H(142533); GAR.A = H(ff3a3a); GAR.B = H(7dff9c); }
  garageDirty = true;
}
void draw_init(void) {
  pat_make(PAT_ROAD, H(34747f), H(2a606b), H(427f8b), 260, 2);
  for (int i = 0; i < 36; i++) { stars[i][0] = hash(5, i) * LW; stars[i][1] = hash(6, i) * (HOR - 14); stars[i][2] = hash(7, i); }
}
void dirt_colors(uint32_t out[4]) {
  const Zone *z = &ZONES[zoneAt()];
  out[0] = shadec(z->ground, 0.78); out[1] = shadec(z->ground, 0.66); out[2] = shadec(z->spA, 0.85); out[3] = shadec(z->ground, 0.9);
}

static void drawMoon(void) {                                    // a big harvest moon with a few craters, top left
  double mx = 96, my = 34;
  cv_fill(rgba(255, 190, 110, .18)); cv_circle(mx, my, 30);
  cv_fill(H(ffd9a0)); cv_circle(mx, my, 20);
  cv_fill(H(f0bc7a)); const double cr[4][3] = { { -7, -6, 4 }, { 6, 6, 3 }, { 8, -7, 2 }, { -5, 8, 2 } };
  for (int i = 0; i < 4; i++) cv_circle(mx + cr[i][0], my + cr[i][1], cr[i][2]);
}
static double batBeat(int i, double speed) { double q = 0.5 + 0.5 * sin(T * 0.7 + i * 1.7); return sin(T * speed + i * 2.1) * (0.3 + 0.7 * q * q); }
static void batShape(double x, double y, double w) {
  F(x - 1, y, 3, 2);
  double a = rnd(w * 0.7), b = rnd(w * 1.6), c = rnd(w * 2.6);
  F(x - 3, y + a, 2, 1); F(x + 2, y + a, 2, 1);
  F(x - 5, y + b, 2, 1); F(x + 4, y + b, 2, 1);
  F(x - 6, y + c, 1, 1); F(x + 6, y + c, 1, 1);
}
static double jmod(double a, double n) { return fmod(a, n); }   // JS % keeps the sign of the dividend, as fmod does
static void drawBats(double camX) {
  cv_fill(H(120a1e));
  for (int i = 0; i < 7; i++) {
    double x = rnd(jmod(jmod(hash(i, 300) * 900 - camX * 0.06 - T * (14 + hash(i, 301) * 16), 560) + 560, 560) - 40);
    double w = batBeat(i, 8 + hash(i, 303) * 3), y = rnd(14 + hash(i, 302) * 56 + sin(T * 2 + i) * 4 - w * 1.2);
    batShape(x, y, w);
  }
}
static void drawSnow(void) {
  cv_fill(H(ffffff)); double a0 = cv_get_alpha();
  for (int i = 0; i < 70; i++) {
    double sp = 18 + hash(i, 310) * 26, x = rnd(jmod(jmod(hash(i, 311) * LW + sin(T * 0.8 + i) * 10 - T * 22, LW) + LW, LW)), y = rnd(jmod(hash(i, 312) * LH + T * sp, LH));
    cv_alpha(0.5 + 0.5 * hash(i, 313)); F(x, y, i % 5 ? 1 : 2, i % 5 ? 1 : 2);
  }
  cv_alpha(a0);
}
static void drawSeasonDecor(double sx, double oy, double sz, double ty) {
  if (SEASON == 1) {
    if (ty < 0.45) {                                            // a jack-o'-lantern with a flickering candle
      int w = 9 + (int)(sz * 4), h = 7 + (int)(sz * 2); double fl = 0.75 + 0.25 * sin(T * 13 + sx);
      R(sx - (w >> 1), oy - h, w, h, H(e0701a)); R(sx - (w >> 1) + 1, oy - h - 1, w - 2, 1, H(e0701a)); R(sx - (w >> 1) + 1, oy - h + 1, 1, h - 2, H(b8520e)); R(sx + (w >> 1) - 2, oy - h + 1, 1, h - 2, H(b8520e));
      R(sx - 1, oy - h - 3, 2, 3, H(4a7a2a));
      double a0 = cv_get_alpha(); cv_alpha(a0 * fl);
      R(sx - 3, oy - h + 2, 2, 2, H(ffe14a)); R(sx + 1, oy - h + 2, 2, 2, H(ffe14a)); R(sx - 3, oy - 3, 6, 1, H(ffe14a)); R(sx - 2, oy - 2, 1, 1, H(ffe14a)); R(sx + 1, oy - 2, 1, 1, H(ffe14a));
      cv_alpha(a0);
    } else if (ty < 0.75) {                                     // a crooked tombstone
      R(sx - 4, oy - 10, 9, 10, H(7a7690)); R(sx - 3, oy - 11, 7, 1, H(7a7690)); R(sx - 4, oy - 10, 1, 10, H(9a96b0)); R(sx - 1, oy - 8, 3, 1, H(4a4660)); R(sx, oy - 9, 1, 4, H(4a4660)); R(sx - 6, oy - 1, 13, 1, H(2a2238));
    } else {                                                    // a bare, twisted tree
      int h = 18 + (int)(sz * 10); col_t c = H(2a1c24);
      R(sx, oy - h, 2, h, c); R(sx - 5, oy - h + 4, 6, 1, c); R(sx - 6, oy - h + 1, 1, 4, c); R(sx + 2, oy - h + 7, 6, 1, c); R(sx + 7, oy - h + 4, 1, 4, c); R(sx + 1, oy - h - 3, 1, 3, c);
    }
  } else {
    if (ty < 0.55) {                                            // a snowy pine strung with blinking lights
      int h = 14 + (int)(sz * 12);
      for (int q = 0; q < 3; q++) { double w = 3 + q * 3, yy = oy - h + q * (h / 3.0 - 1); R(sx - w, yy + 2, w * 2 + 1, h / 3.0 + 1, H(1f5a3e)); R(sx - w + 1, yy + 2, w * 2 - 1, 1, H(f4f8ff)); }
      R(sx - 1, oy - 2, 3, 3, H(4a2e1a));
      static const col_t cs[4] = { H(ff3a3a), H(ffd21f), H(3ab0ff), H(7dff9c) };
      for (int q = 0; q < 5; q++) { cv_fill(cs[(q + (int)floor(T * 3)) % 4]); F(rnd(sx - 4 + hash(q, (int)sx) * 8), rnd(oy - h + 4 + q * (h - 6) / 5.0), 1, 1); }
      R(sx, oy - h - 1, 1, 1, H(ffe14a));
    } else if (ty < 0.8) {                                      // a snowman
      cv_fill(H(ffffff)); cv_circle(sx, oy - 4, 4.5); cv_circle(sx, oy - 11, 3.2);
      R(sx - 1, oy - 12, 1, 1, H(14101f)); R(sx + 1, oy - 12, 1, 1, H(14101f)); R(sx + 1, oy - 11, 2, 1, H(ff8a1f)); R(sx - 3, oy - 15, 6, 1, H(14101f)); R(sx - 2, oy - 18, 4, 3, H(14101f)); R(sx - 3, oy - 8, 6, 1, H(d8241e));
    } else {                                                    // a stack of presents
      R(sx - 6, oy - 6, 7, 6, H(d8241e)); R(sx - 3, oy - 6, 1, 6, H(ffd21f)); R(sx + 1, oy - 5, 5, 5, H(2fa05a)); R(sx + 3, oy - 5, 1, 5, H(f4f8ff)); R(sx - 4, oy - 10, 5, 4, H(3a7ae0)); R(sx - 2, oy - 10, 1, 4, H(ffd21f));
    }
  }
}

static void tri(double x0, double y0, double x1, double y1, double x2, double y2) { double p[6] = { x0, y0, x1, y1, x2, y2 }; cv_poly(p, 3); }
static void peaks(double par, double hmin, double hmax, double wmin, double wmax, double step, double alpha, double camX, int so, col_t lit, col_t shade) {
  double off = camX * par; int i0 = (int)floor(off / step) - 1, i1 = (int)floor((off + LW) / step) + 2;
  double a0 = cv_get_alpha(); cv_alpha(a0 * alpha);
  for (int i = i0; i <= i1; i++) {
    double px = rnd(i * step + hash(i, so + 1) * step * 0.5 - off), h = rnd(hmin + hash(i, so + 2) * (hmax - hmin)), hw = rnd(wmin + hash(i, so + 3) * (wmax - wmin));
    cv_fill(shade); tri(px - hw, HOR + 8, px, HOR - h, px + rnd(hw * 0.18), HOR + 8);
    cv_fill(lit); tri(px, HOR - h, px + rnd(hw * 1.15), HOR + 8, px + rnd(hw * 0.18), HOR + 8);
  }
  cv_alpha(a0);
}
static void drawBackdrop(int zi, double camX) {
  const Zone *Z = &ZONES[zi]; const Sky *sky = skyOf(zi);
  { double st0[2] = { 0, 1 }; col_t sc[2] = { sky->top, sky->bot }; cv_grad(0, 0, LW, HOR + 2, true, 0, HOR, 2, st0, sc); }
  if (sky->stars) { cv_fill(H(f1ecf7)); for (int i = 0; i < 36; i++) { cv_alpha(0.4 + 0.6 * fabs(sin(T * 1.5 + stars[i][2] * 9))); F(rnd(stars[i][0]), rnd(stars[i][1]), 1, 1); } cv_alpha(1); }
  if (sky->moon) drawMoon();
  if (SEASON == 1) drawBats(camX);
  if (sky->sun) { cv_fill(rgba(255, 225, 140, .85)); cv_circle(330, HOR - 8, 22); cv_fill(rgba(255, 225, 140, .25)); cv_circle(330, HOR - 8, 34); }
  if (Z->kind == 0) { peaks(0.1, 22, 42, 40, 70, 90, 0.55, camX, 50, Z->lit, Z->shade); peaks(0.22, 34, 74, 48, 82, 100, 1, camX, 70, Z->lit, Z->shade); }
  else if (Z->kind == 1) {
    R(0, HOR - 18, LW, 22, Z->sea ? Z->sea : H(2f8fa6)); R(0, HOR - 18, LW, 1, Z->seaHi ? Z->seaHi : H(7bd0d8));
    cv_fill(rgba(255, 240, 200, .7)); for (int i = 0; i < 26; i++) F(rnd(jmod(hash(i, 80) * LW - camX * 0.05 + LW * 4, LW)), HOR - 16 + (int)(hash(i, 81) * 16), 2 + (int)(hash(i, 82) * 4), 1);
    peaks(0.08, 8, 20, 30, 56, 130, 1, camX, 90, Z->lit, Z->shade);
  } else {
    peaks(0.08, 20, 40, 50, 90, 110, 0.6, camX, 110, Z->lit, Z->shade);
    double off = camX * 0.1, step = 620; int i0 = (int)floor(off / step) - 1, i1 = (int)floor((off + LW) / step) + 1;
    for (int i = i0; i <= i1; i++) {
      double px = rnd(i * step + 200 + hash(i, 120) * 160 - off);
      cv_fill(Z->shade); { double p[8] = { px - 150, HOR + 8, px - 14, HOR - 86, px + 14, HOR - 86, px + 150, HOR + 8 }; cv_poly(p, 4); }
      cv_fill(Z->lit); tri(px + 14, HOR - 86, px + 150, HOR + 8, px + 40, HOR + 8);
      cv_fill(H(f1ecf7)); { double p[16] = { px - 38, HOR - 66, px - 14, HOR - 86, px + 14, HOR - 86, px + 38, HOR - 66, px + 22, HOR - 70, px + 8, HOR - 62, px - 6, HOR - 69, px - 20, HOR - 63 }; cv_poly(p, 8); }
    }
  }
  cv_pattern(PAT_G0 + zi, 0, HOR, LW, LH - HOR);
  { double st0[3] = { 0, 0.3, 1 }; col_t hc[3] = { 0xffffff00, Z->haze, 0xffffff00 }; cv_grad(0, HOR - 6, LW, 28, true, HOR - 6, HOR + 22, 3, st0, hc); }
}
static void drawDecor(int zi, double camX, bool front) {
  const double CH = 90; int c0 = (int)floor(camX / CH) - 1, c1 = (int)floor((camX + LW) / CH) + 1, kind = ZONES[zi].kind;
  for (int c = c0; c <= c1; c++) for (int k = 0; k < 2; k++) {
    int sd = k * 9 + zi * 100;
    double ox = c * CH + hash(c, sd + 1) * CH, f = hash(c, sd + 2), sz = hash(c, sd + 3), ty = hash(c, sd + 4);
    if (ox < 780) continue;                                    // the garage area stays clear
    double oy = rnd(HOR + 10 + f * (LH - HOR - 16));
    if ((oy > g.y) != front) continue;
    roadAt(ox); if (oy > RC - RW / 2 - 8 && oy < RC + RW / 2 + 8) continue;
    double sx = rnd(ox - camX);
    if (SEASON) drawSeasonDecor(sx, oy, sz, ty);
    else if (kind == 0) {
      if (ty < 0.7) { int h = 10 + (int)(sz * 14); cv_fill(H(4d8f4a)); F(sx, oy - h, 3, h); F(sx - 4, oy - h * 0.7, 4, 2); F(sx - 4, oy - h * 0.7 - 5, 2, 6); F(sx + 3, oy - h * 0.5, 4, 2); F(sx + 5, oy - h * 0.5 - 4, 2, 5); cv_fill(H(7fb867)); F(sx + 1, oy - h, 1, h); }
      else { R(sx - 5, oy - 5, 11, 5, H(8f8a6a)); R(sx - 3, oy - 7, 6, 3, H(b9b48c)); }
    } else if (kind == 1) {
      if (ty < 0.65) { R(sx, oy - 16, 2, 16, H(8a5a3a)); cv_fill(H(2ea57f)); for (int q = 0; q < 6; q++) { double an = q / 6.0 * 6.283 + sz * 3; for (int r = 2; r < 11; r += 2) F(rnd(sx + 1 + cos(an) * r), rnd(oy - 16 + sin(an) * r * 0.55), 2, 2); } }
      else { R(sx, oy - 12, 1, 12, H(f1ecf7)); cv_fill(sz < 0.5 ? H(ff4a4a) : H(ffd21f)); F(sx - 7, oy - 14, 15, 3); F(sx - 5, oy - 16, 11, 2); }
    } else {
      if (ty < 0.8) { int h = 12 + (int)(sz * 12); cv_fill(H(2c6b4a)); for (int q = 0; q < 3; q++) { double w = 4 + q * 3, yy = oy - h + q * (h / 3.0 - 1); F(sx - w, yy + 2, w * 2 + 1, h / 3.0 + 1); } R(sx - 1, oy - 2, 3, 3, H(1d4d38)); R(sx - 1, oy - h + 1, 3, 2, H(dfe8ff)); }
      else { R(sx - 6, oy - 5, 13, 5, H(5c5a76)); R(sx - 4, oy - 7, 8, 3, H(8d8aa8)); }
    }
  }
}
static void drawRoad(double camX) {
  enum { N = 122 };                                            // Math.ceil(LW / 4) + 2
  static double top[N], bot[N];
  for (int i = 0; i < N; i++) { roadAt(camX + i * 4 - 4); top[i] = rnd(RC - RW / 2); bot[i] = rnd(RC + RW / 2); }
  for (int i = 0; i + 1 < N; i++) {                             // the road band, one strip per 4 px segment
    double y0 = rnd((top[i] + top[i + 1]) / 2), y1 = rnd((bot[i] + bot[i + 1]) / 2);
    cv_pattern(PAT_ROAD, i * 4 - 4, y0, 4, y1 - y0);
  }
  cv_begin(); for (int i = 0; i < N; i++) { double x = i * 4 - 4; i ? cv_line(x, top[i] + 0.5) : cv_move(x, top[i] + 0.5); } cv_stroke(H(4f94a0), 1);
  cv_begin(); for (int i = 0; i < N; i++) { double x = i * 4 - 4; i ? cv_line(x, bot[i] - 0.5) : cv_move(x, bot[i] - 0.5); } cv_stroke(H(255b66), 1);
  cv_fill(H(e0963a));
  for (double x = floor(camX / 24) * 24; x < camX + LW + 24; x += 24) { roadAt(x + 3); F(rnd(x - camX), rnd(RC - RW * 0.2), 8, 1); F(rnd(x - camX + 12), rnd(RC + RW * 0.2), 8, 1); }
  if (camX < 420) { roadAt(304); double y0 = rnd(RC - RW / 2); int n = (int)floor(RW / 4); for (int r = 0; r < n; r++) for (int c = 0; c < 3; c++) R(rnd(304 - camX) + c * 4, y0 + r * 4, 4, 4, (r + c) & 1 ? H(f1ecf7) : H(0a0612)); }
  // gaps: paint the ground back over the road, with a ragged edge
  genTo(camX + LW + 2200);
  for (int i = lbSpans(&gaps, camX - 4200); i < gaps.n && gaps.a[i].x < camX + LW + 20; i++) {
    double gx = gaps.a[i].x, sx = rnd(gx - camX), ex = sx + gaps.a[i].len; if (ex < -20) continue;
    int gi = (int)gx;
    for (int y = 118; y <= 262; y += 3) {
      double l = sx + rnd(hash(gi, y) * 12 - 6), r = ex + rnd(hash(gi + 7, y) * 12 - 6);
      cv_pattern(PAT_G0 + zoneAt(), l, y, r - l, y + 3 <= 262 ? 3 : 1);
    }
  }
}
typedef double (*YFn)(double x, int arg);
static double yEdgeTop(double x, int a) { (void)a; roadAt(x); return EDGE_TOP(); }
static double yEdgeBot(double x, int a) { (void)a; roadAt(x); return EDGE_BOT(); }
static double yRail(double x, int i) { roadAt(x); return railY(railSide(i)); }
static void railLine(double x0, double x1, double camX, YFn yFn, int arg, bool heavy, double postLen, bool cap) {
  double a = fmax(x0, camX - 8), b = fmin(x1, camX + LW + 8); if (b <= a) return;
  double s0 = floor(a / 4) * 4;
  if (SEASON != 1) {                                           // the usual rails, merged into runs at the same height
    col_t beam = heavy ? H(6b2a3a) : H(7d5a6a), hi = heavy ? H(b04656) : H(a88494), post = heavy ? H(4e1d2a) : H(5e4552);
    double runX = 0, runY = 0; bool run = false; int bh = heavy ? 3 : 2;
    for (double x = s0; x <= b + 4; x += 4) {
      bool end = x > b; double y = end ? 0 : rnd(yFn(x, arg)), sx = rnd(x - camX);
      if (run && (end || y != runY)) { R(runX, runY, sx - runX, bh, beam); R(runX, runY, sx - runX, 1, hi); run = false; }
      if (end) break;
      if (!run) { run = true; runX = sx; runY = y; }
      if (fmod(x, 32) == 0) R(sx, y + bh, 2, postLen, post);
    }
  } else {
    // Halloween's dark dirt hides the usual rails, so there they get red and white stripes with a dark outline.
    // The outline is merged into runs at one height; the stripes go 8 px at a time.
    col_t post = heavy ? H(2a0e16) : H(5e4552); int bh = heavy ? 3 : 2;
    double runX = 0, runY = 0; bool run = false;
    for (double x = s0; x <= b + 4; x += 4) {
      bool end = x > b; double y = end ? 0 : rnd(yFn(x, arg)), sx = rnd(x - camX);
      if (run && (end || y != runY)) { R(runX, runY - 1, sx - runX, bh + 2, H(14080e)); run = false; }
      if (end) break;
      if (!run) { run = true; runX = sx; runY = y; }
    }
    for (double x = s0; x <= b; x += 4) {
      double y = rnd(yFn(x, arg)), sx = rnd(x - camX);
      if (fmod(x, 32) == 0) { R(sx - 1, y + bh, 4, postLen + 1, post); R(sx, y + bh, 2, postLen, heavy ? H(7a2c38) : H(7d5a6a)); }
      bool white = heavy && (((long long)x >> 3) % 2 == 0);
      double w = 4;                                            // the next piece joins this one when it is the same stripe at the same height
      if (x + 4 <= b && (((long long)(x + 4) >> 3) % 2 == 0) == white && rnd(yFn(x + 4, arg)) == y) { w = 8; }
      R(sx, y, w, bh, white ? H(f1ecf7) : heavy ? H(e8343f) : H(a88494));
      R(sx, y, w, 1, white ? H(ffffff) : heavy ? H(ff7a6a) : H(c8a8b8));
      if (w == 8) { x += 4; if (fmod(x, 32) == 0) { R(sx + 3, y + bh, 4, postLen + 1, post); R(sx + 4, y + bh, 2, postLen, heavy ? H(7a2c38) : H(7d5a6a)); } }
    }
  }
  if (cap) { double ends[2] = { x0, x1 }; for (int k = 0; k < 2; k++) { double ex = ends[k]; if (ex < camX - 4 || ex > camX + LW + 4) continue; double y = yFn(ex, arg); R(rnd(ex - camX) - 1, rnd(y) - 3, 3, 14, H(4e1d2a)); R(rnd(ex - camX) - 1, rnd(y) - 3, 3, 2, H(ffd21f)); } }
}
static void drawRails(double camX, bool front) {
  genTo(camX + LW + 2200);
  roadAt(g.cam + g.bx); double bikeBottomLine = RC + RW / 2 + 1;
  for (int i = lbSpans(&edges, camX - 4200); i < edges.n && edges.a[i].x < camX + LW + 20; i++) {
    double ex = edges.a[i].x, el = edges.a[i].len;
    roadAt(g.cam + g.bx); if ((g.y < EDGE_TOP() + 8) == front) railLine(ex, ex + el, camX, yEdgeTop, 0, true, 5, false);
    bool bottomFront = g.y < bikeBottomLine;
    if (front == bottomFront) railLine(ex, ex + el, camX, yEdgeBot, 0, true, 6, false);
  }
  for (int i = lbRails(camX - 4200); i < nrails && rails[i].x < camX + LW + 20; i++) {
    // a barricade is drawn in front of the bike when its foot is nearer the camera (lower) than the bike's wheels
    roadAt(g.cam + g.bx); int side = railSide(i); if ((g.y < railY(side) + 8) != front) continue;
    railLine(rails[i].x, rails[i].x + rails[i].len, camX, yRail, i, true, 8, true);
  }
}

/* ---------- the Old Man and his bike (the default look) ---------- */
static void line2(col_t c, double w, double x1, double y1, double x2, double y2) { cv_begin(); cv_move(x1, y1); cv_line(x2, y2); cv_stroke(c, w); }
static void drawWheel(double cx) {
  cv_save(); cv_translate(cx, 0);
  cv_fill(H(14101f)); cv_circle(0, 0, 8);
  cv_fill(H(6c6a86)); cv_circle(0, 0, 5);
  cv_fill(H(14101f)); cv_circle(0, 0, 3.5);
  R(-1, -1, 2, 2, H(c8c8d8)); cv_restore();
}
static void drawTank(void) {
  col_t c1 = H(ffd21f), c2 = H(ff5a1f);
  cv_save(); cv_begin(); cv_ellipse(20, -14, 9, 5, 0, 0, 7); cv_fill(c1); cv_fillpath(); cv_clip();
  R(10, -11.4, 20, 3, shadec(c1, 0.72));
  cv_fill(c2); { double p[8] = { 13, -13, 20, -17, 24, -13, 18, -12 }; cv_poly(p, 4); }
  cv_fill(rgba(255, 255, 255, .75)); F(24, -17.6, 2, 1); F(26.4, -16.8, 1, 1);
  cv_restore();
}
// the brown cardigan, knitted: dark ribs every fourth column and light stitches between (KNIT in the web game)
static void knit(int x0, int y0, int x1, int y1) {
  R(x0, y0, x1 - x0, y1 - y0, H(8a5a2e));
  for (int x = x0; x < x1; x++) {
    int m = ((x % 4) + 4) % 4; double w = x + 1 < x1 ? 1.5 : 1;
    if (m == 0) R(x, y0, w, y1 - y0, H(714822));
    else if (m == 2) for (int y = y0; y < y1; y++) if ((((y % 2) + 2) % 2) == 0) R(x, y, w, y + 1 < y1 ? 1.5 : 1, H(9c6a3a));
  }
}
static void drawCoat(void) {
  col_t trim = H(5e3a1a), btn = H(e9dcb8);
  cv_save(); cv_begin(); cv_move(6, -15); cv_line(-8, -14); cv_line(-6, -24); cv_line(7, -28);
  cv_move(11 + 6 * cos(0.25), -24 + 6 * sin(0.25)); cv_ellipse(11, -24, 6, 9, 0.25, 0, 7); cv_clip();
  knit(-9, -34, 19, -12);
  cv_fill(rgba(20, 10, 30, .28)); F(-9, -25, 6, 12); F(12, -25, 7, 1);
  R(-9, -16, 28, 4, trim); cv_fill(rgba(0, 0, 0, .25)); for (int x = -9; x < 19; x += 2) F(x, -16, 1, 4);
  R(15, -26, 1, 10, trim); R(16, -23, 1, 1, btn); R(16, -19, 1, 1, btn);
  cv_restore();
  R(9, -32, 5, 2, trim);
}
static void drawSleeve(void) {
  cv_save(); cv_begin(); cv_move(11, -30); cv_line(24.5, -28); cv_line(24.5, -24.5); cv_line(11, -26); cv_clip();
  knit(11, -31, 25, -24);
  cv_fill(rgba(20, 10, 30, .25)); F(11, -26, 14, 2);
  R(22, -29, 2, 5, H(5e3a1a));
  cv_restore();
}
// Everything that tilts with the wheelie, in the bike's own coordinates. It never changes (the default look has plain
// rims), so it is drawn once into a sprite at twice the size and stamped every frame.
static void bikeBody(void) {
  drawWheel(0); drawWheel(34);
  col_t fc = H(ffd21f);
  cv_begin(); cv_arc(34, 0, 10, -M_PI * 0.97, -M_PI * 0.42); cv_stroke(shadec(fc, 0.55), 2.6);
  cv_begin(); cv_arc(34, 0, 10.3, -M_PI * 0.95, -M_PI * 0.44); cv_stroke(fc, 1.6);
  cv_begin(); cv_move(30, -22); cv_line(34, 0); cv_move(24, -27); cv_line(29, -23); cv_stroke(H(cfd2e8), 2);
  line2(H(3a3556), 2, 0, 0, 22, -6);
  R(10, -9, 14, 9, H(8e8aa0)); R(10, -3, 14, 3, H(5e5a76));
  line2(H(d9dcf0), 2, 16, -1, -10, 3);
  R(-15, 1, 6, 4, H(ff2d2d));
  drawTank();
  R(2, -15, 14, 3, H(2a1a3a));
  // the Old Man: flat cap, round specs, white mustache, brown cardigan, cane strapped on the back
  R(32, -22, 3, 3, H(ffee99));
  drawCoat();
  line2(H(8a5a2e), 2, -9, -22, -11, -37);
  cv_begin(); cv_move(-11, -37); cv_line(-7, -38); cv_line(-6, -35); cv_stroke(H(e8d9b0), 2);
  R(-14, -28, 11, 15, H(c0392b)); R(-14, -17, 11, 4, H(8e2418)); cv_fill(H(ffd21f)); F(-12, -22, 7, 2); F(-9, -15, 3, 2);
  cv_fill(H(6a1a10)); F(-14, -28, 11, 1); F(-3, -27, 1, 12);
  drawSleeve();
  R(24, -28, 4, 4, H(f2c9a0));
  line2(H(3a4a6a), 3, 8, -16, 17, -8);
  cv_fill(H(f2c9a0)); cv_circle(15, -36, 5);
  cv_fill(H(f1ecf7)); F(10, -38, 2, 5); F(18, -33, 5, 2); F(19, -31, 3, 1);
  cv_fill(H(4a5a3a)); { double p[8] = { 9, -39, 13, -43, 22, -42, 23, -39 }; cv_poly(p, 4); }
  R(18, -40, 7, 2, H(2f3a24));
  cv_stroke_rect(18.5, -37.5, 3, 3, H(14101f), 1); R(19, -37, 3, 3, rgba(190, 230, 255, .6));
}
void drawBike(double x, double y, double wheelie, double spin, bool noShadow) {
  x = rnd(x); y = rnd(y);
  cv_save(); cv_translate(x, y); cv_scale(BIKE_SCALE, BIKE_SCALE); cv_translate(-x, -y);
  if (!noShadow) { cv_fill(rgba(0, 0, 0, .28)); F(x - 24, y, 48, 2); F(x - 20, y + 2, 40, 1); }
  if (wheelie > 0.4 && st != ST_CRASH) R(x - 64, y - 1, 46, 1, rgba(30, 20, 50, .25));
  if (spin) { cv_translate(x, y - 14); cv_rotate(spin); cv_translate(-x, -(y - 14)); }
  cv_translate(x - 17, y - 8); cv_rotate(-0.5 * wheelie);
  cv_sprite(SPR_BIKE);
  cv_restore();
}
static void halftone(void) {
  double cx = LW / 2, cy = LH / 2 + 14;
  cv_fill(rgba(10, 6, 18, 1));
  for (int y = 4; y < LH; y += 8) for (int x = ((y / 8) & 1) * 4; x < LW; x += 8) { double s = clampd(hypot(x - cx, y - cy) / 280 * 4.5, 0, 4.5); F(x, y, s, s); }
}
void draw_sprites(void) {
  spr_begin(SPR_BIKE, 128, 128, 2, 17, 50); bikeBody();
  spr_begin(SPR_HALFTONE, 480, 270, 1, 0, 0); halftone();
}

/* ---------- the start area: a big Japanese garage the run begins in front of ---------- */
static void neon(const char *txt, double cx, double cy, double size, col_t col, double alpha) {
  double a0 = cv_get_alpha();
  cv_alpha(alpha * 0.35);
  const int d[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
  for (int i = 0; i < 4; i++) cv_text(txt, cx + d[i][0], cy + d[i][1], size, col, AL_CENTER, BL_MIDDLE);
  cv_alpha(alpha); cv_text(txt, cx, cy, size, col, AL_CENTER, BL_MIDDLE); cv_alpha(a0);
}
static void ellipseFill(double cx, double cy, double rx, double ry) { cv_begin(); cv_ellipse(cx, cy, rx, ry, 0, 0, 7); cv_fillpath(); }
// The garage at the start of every map. Everything that doesn't move is drawn once into a texture when the map's look is
// set (it is a few hundred shapes, too many to draw every frame on an original 3DS); the swaying and flickering bits are
// drawn live on top.
#define GAR_X0 -140                                               // the cached picture covers world x -140..820, y 40..270
#define GAR_Y0 40
#define GAR_W 960
static void garageStatic(void) {
  const double camX = 0;
  #define X(w) rnd((w) - camX)
  const double x0 = 6, x1 = 470, wallTop = 80, base = 160;
  // concrete forecourt that fades into the landscape
  R(X(-140), base, 880, LH - base, H(8a86a4));
  cv_fill(H(767290)); for (int x = -140; x < 740; x += 40) F(X(x), base, 1, LH - base);
  for (double y = base + 30; y < LH; y += 34) F(X(-140), y, 880, 1);
  cv_fill(rgba(20, 16, 32, .18)); { const double sh[4][3] = { { 110, 232, 26 }, { 250, 246, 20 }, { 380, 226, 30 }, { 520, 244, 22 } }; for (int i = 0; i < 4; i++) ellipseFill(X(sh[i][0]), sh[i][1], sh[i][2], 4); }
  cv_fill(rgba(255, 210, 31, .5)); for (int x = 30; x < 470; x += 56) F(X(x), 228, 2, 30);
  for (int y = (int)base; y < LH; y += 4) cv_pattern(PAT_G0 + zoneAt(), X(690 + (int)(hash(y, 77) * 20)), y, 80, 4);
  { double st0[2] = { 0, 1 }; col_t gc[2] = { GAR.A, GAR.A & 0xffffff00 }; cv_alpha(0.16); cv_grad(X(60), base, 260, 70, true, base, base + 70, 2, st0, gc); cv_alpha(1); }
  // roof
  double roofP[8] = { X(x0 - 16), wallTop - 2, X(x0 + 34), 50, X(x1 - 34), 50, X(x1 + 16), wallTop - 2 };
  cv_fill(H(2b2542)); cv_poly(roofP, 4);
  cv_save(); cv_begin(); for (int i = 0; i < 4; i++) i ? cv_line(roofP[i * 2], roofP[i * 2 + 1]) : cv_move(roofP[0], roofP[1]); cv_clip();
  cv_fill(rgba(0, 0, 0, .28)); for (int y = 54; y < wallTop; y += 4) F(X(x0 - 20), y, x1 - x0 + 40, 1);
  cv_fill(rgba(255, 255, 255, .05)); for (double x = x0 - 16; x < x1 + 16; x += 8) F(X(x), 52, 1, 28); cv_restore();
  R(X(x0 + 34), 50, x1 - x0 - 68, 2, H(4a4275));
  cv_fill(H(ffc83d)); { double rxs[2] = { x0 + 30, x1 - 38 }; for (int i = 0; i < 2; i++) tri(X(rxs[i]), 52, X(rxs[i] + 4), 41, X(rxs[i] + 8), 52); }
  R(X(x0 - 18), wallTop - 2, x1 - x0 + 36, 4, H(1a1530)); R(X(x0 - 18), wallTop - 2, x1 - x0 + 36, 1, H(4a4275));
  // wall
  R(X(x0), wallTop + 2, x1 - x0, base - wallTop - 2, GAR.wall);
  cv_fill(rgba(255, 255, 255, .045)); for (double x = x0; x < x1; x += 6) F(X(x), wallTop + 2, 1, base - wallTop - 2);
  R(X(x0), base - 4, x1 - x0, 4, H(4a4666));
  { double sx = X(x0 + 96), sy = 88; R(sx, sy, 244, 30, H(0a0612)); }   // the neon sign's board
  // bays with warm light and parked silhouettes
  for (int i = 0; i < 3; i++) {
    double bx = X(x0 + 24 + i * 112), by = 122, bw = 100, bh = base - by - 4, o = (i % 2) * 10;
    { double st0[2] = { 0, 1 }; col_t bc[2] = { H(ffd77a), H(b85a1a) }; cv_grad(bx, by, bw, bh, true, by, by + bh, 2, st0, bc); }
    R(bx + 4, by + 8, bw - 8, 10, H(7a4a1f)); cv_fill(H(d9d4e8));
    for (int k = 0; k < 9; k++) F(bx + 8 + k * 10, by + 10 + ((k * 7) % 5), 3, 5);
    cv_fill(H(1a0f12)); cv_circle(bx + 24 + o, by + bh - 6, 6); cv_circle(bx + 52 + o, by + bh - 6, 6);
    cv_begin(); cv_move(bx + 24 + o, by + bh - 6); cv_line(bx + 40 + o, by + bh - 16); cv_line(bx + 52 + o, by + bh - 6); cv_move(bx + 26 + o, by + bh - 8); cv_line(bx + 22 + o, by + bh - 26); cv_stroke(H(1a0f12), 2);
    cv_fill(rgba(255, 240, 170, .35)); cv_circle(bx + 50, by + 24, 14); R(bx + 48, by + 22, 4, 4, H(fff6c0));
    R(bx, by, bw, 8, H(9a98b0)); cv_fill(H(6f6d88)); for (int k = 0; k < 4; k++) F(bx, by + 1 + k * 2, bw, 1);
    cv_stroke_rect(bx, by, bw, bh, H(0a0612), 2);
  }
  // office door, window, noren curtain, OPEN sign
  double ox0 = X(x0 + 372);
  R(ox0, 122, 34, base - 126, H(26385a)); R(ox0 + 3, 126, 6, base - 134, H(4a6a9a)); R(ox0 + 16, 122, 2, base - 126, H(0a0612));
  R(ox0 + 42, 128, 40, 20, H(ffd77a)); cv_fill(H(0a0612)); F(ox0 + 61, 128, 2, 20); F(ox0 + 42, 137, 40, 2);

  #undef X
}
static void garageCache(void) {                                  // at the start of a frame, before anything else is drawn
  if (!garageDirty) return;
  pat_offset(0); spr_begin(SPR_GARAGE, GAR_W, LH - GAR_Y0, 1, -GAR_X0, -GAR_Y0); garageStatic(); spr_end(); garageDirty = false;
}
static void drawGarage(double camX, bool front) {
  if (camX > 800) return;
  #define X(w) rnd((w) - camX)
  if (front) {
    drawBike(X(70), 247, 0, 0, false); drawBike(X(168), 247, 0, 0, false);
    for (int k = 0; k < 4; k++) { double y = 256 - k * 6; cv_fill(H(18121f)); ellipseFill(X(16), y, 12, 4.5); R(X(16) - 9, y - 1, 18, 1, H(d9d4e8)); R(X(16) - 11, y - 3, 22, 1, H(3a3350)); }
    for (int k = 0; k < 2; k++) { double dx = k ? 504 : 486; R(X(dx), 238, 14, 18, H(2a6a9a)); cv_fill(H(1c4a70)); F(X(dx), 242, 14, 2); F(X(dx), 250, 14, 2); R(X(dx) + 2, 238, 2, 18, H(7fb5dc)); }
    double vx = X(548); R(vx, 226, 18, 30, H(d8243a)); R(vx, 252, 18, 4, H(8c1626));
    R(vx + 2, 229, 14, 14, H(f1ecf7)); static const col_t vc[6] = { H(ffd21f), H(22e6d8), H(ff3ea5), H(7dff9c), H(ff8a2f), H(9bb8ff) };
    for (int i = 0; i < 3; i++) for (int j = 0; j < 2; j++) R(vx + 3 + j * 6, 231 + i * 4, 4, 3, vc[i * 2 + j]);
    R(vx + 4, 246, 10, 3, H(0a0612));
    return;
  }
  const double x0 = 6, x1 = 470, wallTop = 80;
  cv_save(); cv_translate(rnd(-camX), 0); cv_sprite(SPR_GARAGE); cv_restore();
  // lanterns along the eave
  for (double x = x0 + 12; x < x1 - 6; x += 36) {
    double sw = sin(T * 1.6 + x) * 1; cv_fill(rgba(255, 106, 74, .16)); cv_circle(X(x) + sw, 90, 13);
    R(X(x) + sw, wallTop + 2, 1, 4, H(0a0612)); cv_fill(H(e8333f)); ellipseFill(X(x) + sw + 0.5, 90, 6, 8);
    cv_fill(H(5a0d17)); F(X(x) + sw - 5, 86, 11, 1); F(X(x) + sw - 5, 93, 11, 1); R(X(x) + sw - 1, 89, 3, 2, H(ffd9a0));
  }
  // neon sign
  double fl = (sin(T * 2.3) > 0.985 ? 0.35 : 1) * (0.88 + 0.12 * sin(T * 9));
  double sx = X(x0 + 96), sy = 88;
  cv_alpha(fl); cv_stroke_rect(sx + 1, sy + 1, 242, 28, GAR.A, 2); cv_alpha(1);
  neon("じいさんガレージ", sx + 122, sy + 12, 15, GAR.A, fl);
  neon("OLD MAN'S GARAGE · EST. 1952", sx + 122, sy + 24, 6, GAR.B, fl);
  double ox0 = X(x0 + 372);
  for (int k = 0; k < 4; k++) { double wv = sin(T * 2 + k) * 1.5; R(ox0 + k * 8 + wv, 118, 7, 14, H(c5243a)); cv_fill(H(f1ecf7)); F(ox0 + k * 8 + wv, 118, 7, 1); F(ox0 + k * 8 + 3 + wv, 124, 1, 5); }
  neon("BACK IN 20", ox0 + 62, 118, 6, GAR.B, fl * (sin(T * 5) > -0.6 ? 1 : 0.4));
  // nobori banners along the front
  const double bfx[5] = { x0 + 60, x0 + 150, x0 + 232, x0 + 330, x0 + 440 }; const col_t bcol[5] = { H(d8243a), H(ffd21f), H(ff3ea5), H(d8243a), H(22e6d8) };
  for (int i = 0; i < 5; i++) {
    double px = X(bfx[i]), sw = sin(T * 3 + i * 1.7) * 2;
    R(px, 124, 1, 38, H(9a98b0));
    cv_fill(bcol[i]); { double p[8] = { px + 1, 126, px + 11 + sw, 127, px + 11 + sw * 1.4, 156, px + 1, 155 }; cv_poly(p, 4); }
    cv_fill(rgba(255, 255, 255, .85)); for (int k = 0; k < 3; k++) F(px + 4 + sw * 0.5, 132 + k * 8, 3, 5);
  }
  #undef X
}

/* ---------- crash cinematic ---------- */
static void vtext(const char **chars, int n, double x, double y0, double size, col_t fill, col_t shadow, double sc) {
  for (int i = 0; i < n; i++) {
    cv_save(); cv_translate(x, y0 + i * size * 1.04); cv_scale(sc, sc);
    bool bar = !strcmp(chars[i], "ー");                         // the long-vowel mark stands upright in vertical text
    const int d[6][2] = { { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 }, { 2, 2 }, { -2, -2 } };
    for (int k = 0; k < 7; k++) {
      double dx = k < 6 ? d[k][0] : 3, dy = k < 6 ? d[k][1] : 3; col_t c = k < 6 ? H(0a0612) : shadow;
      if (bar) R(dx - size * 0.09, dy - size * 0.38, size * 0.18, size * 0.76, c); else cv_text(chars[i], dx, dy, size, c, AL_CENTER, BL_MIDDLE);
    }
    if (bar) R(-size * 0.09, -size * 0.38, size * 0.18, size * 0.76, fill); else cv_text(chars[i], 0, 0, size, fill, AL_CENTER, BL_MIDDLE);
    cv_restore();
  }
}
static const col_t PAIRS[4][2] = { { H(ff2d2d), H(ffd21f) }, { H(ff3ea5), H(2a1a8a) }, { H(22e6d8), H(ff2d2d) }, { H(ffd21f), H(2a1a8a) } };
static void drawCrashFX(void) {
  double t = g.crashT, cx = LW / 2, cy = LH / 2 + 14;
  cv_save(); cv_alpha(clampd(t / 0.14, 0, 1));
  const col_t *pair = PAIRS[(int)floor(t * 6) % 4]; double rot = t * 0.8; const int N = 28;
  for (int i = 0; i < N; i++) {
    double a0 = rot + i / (double)N * 6.2832, a1 = rot + (i + 1) / (double)N * 6.2832;
    cv_fill(pair[i & 1]); tri(cx, cy, cx + cos(a0) * 420, cy + sin(a0) * 420, cx + cos(a1) * 420, cy + sin(a1) * 420);
  }
  { double a0 = cv_get_alpha(); cv_alpha(a0 * 0.22); cv_sprite(SPR_HALFTONE); cv_alpha(a0); }
  for (int i = 0; i < 3; i++) { double r = fmod(t * 260 + i * 100, 300); cv_begin(); cv_arc(cx, cy, r, 0, 7); cv_stroke(rgba(255, 255, 255, (1 - r / 300) * 0.8), 2); }
  if (t < 0.24) {
    double Rr = 30 + 190 * (t / 0.24); double p[56];
    for (int i = 0; i < 28; i++) { double a = i / 28.0 * 6.2832, rr = i & 1 ? Rr * 0.45 : Rr; p[i * 2] = cx + cos(a) * rr; p[i * 2 + 1] = cy + sin(a) * rr; }
    cv_fill(rgba(255, 255, 255, 1 - t / 0.24)); cv_poly(p, 28);
  }
  cv_fill(H(0a0612)); cv_circle(cx, cy, 74);
  cv_begin(); cv_arc(cx, cy, 74, 0, 7); cv_stroke(H(ffd21f), 3);
  cv_begin(); cv_arc(cx, cy, 79, 0, 7); cv_stroke(H(ff2d2d), 2);
  cv_restore();
  // the rider, tumbling at twice size
  double k = clampd(t / 0.28, 0, 1), e = 1 - pow(1 - k, 3);
  double bxp = g.cx0 + (cx - g.cx0) * e, byp = g.cy0 + (cy + 34 - g.cy0) * e - fabs(sin(t * 3.4)) * 12 * k;
  cv_save(); cv_translate(bxp, byp - 18); cv_scale(1.8, 1.8); cv_translate(-bxp, -(byp - 18));
  drawBike(bxp, byp, 0, t * 3.6, true); cv_restore();
  for (int i = 0; i < g.nfx; i++) { Part *p = &g.fx[i]; cv_alpha(clampd(p->life / p->max, 0, 1)); cv_fill(p->c); F(rnd(p->x), rnd(p->y), p->s, p->s); }
  cv_alpha(1);
  double s1 = clampd((t - 0.1) / 0.12, 0, 1), s2 = clampd((t - 0.26) / 0.12, 0, 1);
  if (s1 > 0) { static const char *a[2] = { "激", "突" }; cv_alpha(s1); vtext(a, 2, 54, 82, 46, H(ffd21f), H(ff2d2d), 1 + (1 - s1) * 1.5); }
  if (s2 > 0) { static const char *b[4] = { "ド", "カ", "ー", "ン" }; cv_alpha(s2); vtext(b, 4, LW - 50, 112, 30, H(f1ecf7), H(ff3ea5), 1 + (1 - s2) * 1.5); }
  cv_alpha(1);
}
static void drawParts(double camX) {
  for (int i = 0; i < g.nparts; i++) {
    Part *p = &g.parts[i];
    double k = clampd(p->life / p->max, 0, 1), x = rnd(p->x - camX), y = rnd(p->y); cv_fill(p->c);
    if (p->round) { cv_alpha(k * 0.85); int s = (int)rnd(p->s); F(x - (s >> 1), y - (s >> 1), s, s); cv_alpha(k * 0.35); F(x - (s >> 1) - 1, y - (s >> 1) - 1, s + 2, s + 2); }
    else { cv_alpha(k); F(x, y, p->w ? p->w : p->s, p->s); }
  }
  cv_alpha(1);
}

void render_game(const char *hint) {
  double camX = g.cam;
  if (camX <= 800) garageCache();
  pat_offset(fmod(fmod(g.cam, TILE) + TILE, TILE));
  cv_save();
  if (g.shake > 0.3) cv_translate(rnd((frand() - 0.5) * g.shake), rnd((frand() - 0.5) * g.shake));
  int z0 = zoneAt();
  drawBackdrop(z0, camX);
  drawGarage(camX, false);
  drawRoad(camX);
  drawDecor(z0, camX, false);
  drawRails(camX, false);
  drawParts(camX);
  cv_alpha(1);
  if (st == ST_PLAY && g.v > g.P * 1.25) {
    cv_fill(rgba(255, 255, 255, .35));
    for (int i = 0; i < 6 + 10 * (g.v / g.P - 1.25); i++) F(rnd(frand() * LW), rnd(HOR + 10 + frand() * (LH - HOR - 14)), 14 + (int)(frand() * 30), 1);
  }
  if (st == ST_TITLE || st == ST_PLAY) drawBike(g.bx, g.y, g.wheelie, g.spin, false);
  drawDecor(z0, camX, true);
  drawGarage(camX, true);
  drawRails(camX, true);
  if (st == ST_CRASH || st == ST_OVER) drawCrashFX();
  if (SEASON == 2) drawSnow();
  cv_restore();
  if (st == ST_PLAY) {
    double pulse = 0.6 + 0.4 * sin(T * 14);
    if (g.bx < 130) { double a = clampd((130 - g.bx) / 130, 0, 1) * 0.6 * pulse, s2[2] = { 0, 1 }; col_t c[2] = { rgba(255, 45, 45, a), rgba(255, 45, 45, 0) }; cv_grad(0, 0, 70, LH, false, 0, 70, 2, s2, c); }
    if (g.bx > LW - 130) { double a = clampd((g.bx - (LW - 130)) / 130, 0, 1) * 0.6 * pulse, s2[2] = { 0, 1 }; col_t c[2] = { rgba(255, 45, 45, a), rgba(255, 45, 45, 0) }; cv_grad(LW - 70, 0, 70, LH, false, LW, LW - 70, 2, s2, c); }
  }
  if (g.flash > 0) R(0, 0, LW, LH, rgba(255, 255, 255, fmin(0.85, g.flash)));
  if (st == ST_PLAY && g.t < 5 && !paused && hint) {
    cv_alpha(0.7 + 0.3 * sin(T * 8));
    cv_text(hint, LW / 2 + 1, LH - 11, 11, H(7a1020), AL_CENTER, BL_ALPHA); cv_text(hint, LW / 2, LH - 12, 11, H(fff3d6), AL_CENTER, BL_ALPHA); cv_alpha(1);
  }
  if (paused && st == ST_PLAY) {
    R(0, 0, LW, LH, rgba(10, 6, 18, .7));
    cv_text("PAUSED", LW / 2, LH / 2, 26, H(ffd21f), AL_CENTER, BL_ALPHA);
    cv_text("Press START to ride on", LW / 2, LH / 2 + 16, 9, H(f1ecf7), AL_CENTER, BL_ALPHA);
  }
}

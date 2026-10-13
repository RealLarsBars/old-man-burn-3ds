// The game picture, ported from the drawing half of web/game.js (the Old Man's cosmetics are in look.c).
#include "draw.h"
#include "sim.h"
#include "gfx.h"
#include "look.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define H(x) RGB(0x##x)
#define F(x, y, w, h) cv_rect((x), (y), (w), (h))
static void R(double x, double y, double w, double h, col_t c) { cv_fill(c); cv_rect(x, y, w, h); }

/* ---------- scenery: one landscape per map, each with its own sky chosen by the seed ---------- */
typedef struct { int kind; col_t ground, spA, spB, lit, shade, haze, sea, seaHi; } Zone;   // kind: the LAND index (sim.h)
#define NZONES N_LANDS
static const Zone BASE_ZONES[NZONES] = {
  { 0, H(c8d489), H(8aa764), H(e6ebad), H(dcd77c), H(3e7f7a), H(d9dea0), 0, 0 },
  { 1, H(ecd0a8), H(c59a78), H(fbe8c8), H(9fd0d6), H(2f7288), H(f6dcb8), 0, 0 },
  { 2, H(86b57c), H(4e8260), H(b4d896), H(9a88cc), H(4a3e84), H(adcf98), 0, 0 },
  // national parks (maps from PARKS_FROM on): granite domes over a pine meadow, and red canyon buttes
  { 3, H(8fb36a), H(6e9a52), H(c4d890), H(d4cfc4), H(7c8094), H(d4e4c8), 0, 0 },
  { 4, H(c8784a), H(a85a32), H(e8a870), H(ec9a5c), H(983a2c), H(f2c8a0), 0, 0 },
  // themed landscapes (THEMES below): they bring their own road, guardrails, barricades and floor
  { 5, H(6a4222), H(5c381c), H(704826), H(8a6a3a), H(3a2010), H(3a2414), 0, 0 },
  { 6, H(1c1a2a), H(262438), H(3a2a4e), H(4a3a6a), H(0e0a1e), H(6a2a8a), 0, 0 },
  { 7, H(ecd0a8), H(c59a78), H(fbe8c8), H(9fd0d6), H(2f7288), H(f6dcb8), 0, 0 }
};
typedef struct { col_t top, bot; bool sun, stars, moon; } Sky;
static const Sky SKIES[4] = { { H(ff3a4a), H(ff9a62), true, false, false }, { H(15405c), H(f2b08e), false, false, false },
                              { H(0b0830), H(7a3590), false, true, false }, { H(5a6fd4), H(ffc9a6), true, false, false } };
// the skies of the newer landscapes (Yosemite on), one picked by the seed
static const Sky LAND_SKIES[N_LANDS - 3][2] = { { { H(2f6fd0), H(c4e2ff), true, false, false }, { H(4a5aa8), H(ffb48e), true, false, false } },
                                                 { { H(ff5a3a), H(ffd08a), true, false, false }, { H(3a8ae0), H(ffe0b0), true, false, false } },
                                                 { { H(140b07), H(3a2414), false, false, false }, { 0 } },
                                                 { { H(05031a), H(5a1a6a), false, true, false }, { H(0a0420), H(a02a5a), false, true, false } },
                                                 { { H(3a8ad8), H(ffd9b0), true, false, false }, { H(ff7a5a), H(ffd8a0), true, false, false } } };
static const int LAND_NSKY[N_LANDS - 3] = { 2, 2, 1, 2, 2 };
// index by SEASON: 1 Halloween, 2 Christmas, 3 Thanksgiving (Thanksgiving only recolours the ground and haze)
static const Zone SEASON_ZONE[4] = { { 0 }, { 0, H(4a3b30), H(33281f), H(6e4f33), H(4b3a6e), H(21163a), H(8a3a5a), H(231a44), H(6a5a9a) },
                                     { 0, H(e6edf8), H(c6d2ea), H(ffffff), H(f4f8ff), H(8aa0c8), H(dfe8ff), H(3a6a9a), H(cfe4ff) },
                                     { 0, H(7f9a4a), H(d8782a), H(b8402a), 0, 0, H(f0c890), 0, 0 } };
static const Sky SEASON_SKY[4] = { { 0 }, { H(0e0620), H(d8582a), false, true, true }, { H(0b1638), H(7d97d0), false, true, false },
                                   { H(4a74c8), H(ffc890), true, false, false } };
static Zone ZONES[NZONES];
static const col_t FALL[4] = { H(d8602a), H(e8a030), H(b83a24), H(c8782a) };   // autumn leaf colours
static struct { col_t wall, A, B; } GAR;
static double stars[36][3];
static bool garageDirty = true;                                  // the garage's cached picture needs redrawing
#define GAR_X0 -140                                               // the cached picture covers world x -140..820, y 40..270
#define GAR_Y0 40
#define GAR_W 960
enum { TH_LIBRARY = 5, TH_NEON = 6, TH_PIER = 7 };
typedef struct { col_t base, p0, p1, hi, lo, dash; } Theme;
static const Theme THEMES[3] = {
  { H(7a1622), H(6a121c), H(8e2230), H(d9a441), H(b08030), H(e8c46a) },   // a red carpet runner with gold edging
  { H(24242e), H(1c1c26), H(30303c), H(ff3ea5), H(22e6d8), H(f4f0ff) },   // wet asphalt with neon edge lines
  { H(a8743a), H(94622e), H(b8844a), H(c8945a), H(6a4420), 0 }            // boardwalk planks
};
static int themeKind(void) { int k = zoneAt(); return k >= FIRST_THEME && !SEASON ? k : 0; }
static int stripKind = -1; static bool stripFail;   // which theme's backdrop strip SPR_GARAGE holds (see stripCache)

static const Sky *skyOf(int zi) {
  if (SEASON) return &SEASON_SKY[SEASON];
  if (zi >= 3) return &LAND_SKIES[zi - 3][(int)floor(hash(SEED, 25) * LAND_NSKY[zi - 3])];
  return &SKIES[skyPerm[zi]];
}

void look_apply(void) {                                         // rebuild the sky, ground and garage for the current map
  for (int i = 0; i < NZONES; i++) {
    ZONES[i] = BASE_ZONES[i];
    const Zone *L = &SEASON_ZONE[SEASON];
    if (SEASON == 3) { ZONES[i].ground = L->ground; ZONES[i].spA = L->spA; ZONES[i].spB = L->spB; ZONES[i].haze = L->haze; }
    else if (SEASON) { int k = ZONES[i].kind; ZONES[i] = *L; ZONES[i].kind = k; }
  }
  { const Zone *z = &ZONES[zoneAt()]; int th = themeKind();       // only this map's ground is ever drawn; the indoor and city themes get a calmer floor
    pat_make(PAT_G0, z->ground, z->spA, z->spB, th == TH_LIBRARY || th == TH_NEON ? 260 : 520, 3);
    if (th) { const Theme *t = &THEMES[th - FIRST_THEME]; pat_make(PAT_ROAD, t->base, t->p0, t->p1, 260, 2); }
    else pat_make(PAT_ROAD, H(34747f), H(2a606b), H(427f8b), 260, 2); }
  stripKind = -1;
  static const col_t walls[3] = { H(1c1a2e), H(2b1824), H(142533) }, As[3] = { H(ff3ea5), H(ffd21f), H(ff5a3a) }, Bs[3] = { H(22e6d8), H(9bb8ff), H(7dff9c) };
  GAR.wall = walls[(int)floor(hash(SEED, 40) * 3)]; GAR.A = As[(int)floor(hash(SEED, 41) * 3)]; GAR.B = Bs[(int)floor(hash(SEED, 42) * 3)];
  if (SEASON == 1) { GAR.wall = H(1a1024); GAR.A = H(ff8a1f); GAR.B = H(b98cff); }
  if (SEASON == 2) { GAR.wall = H(142533); GAR.A = H(ff3a3a); GAR.B = H(7dff9c); }
  if (SEASON == 3) { GAR.wall = H(2a1c14); GAR.A = H(ff8a2a); GAR.B = H(ffd27a); }
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
  } else if (SEASON == 3) {
    if (ty < 0.3) {                                             // a pile of raked leaves
      for (int q = 0; q < 9; q++) R(rnd(sx - 7 + hash(q, (int)sx) * 13), rnd(oy - 2 - hash(q + 9, (int)sx) * 5), 3, 2, FALL[q & 3]);
    } else if (ty < 0.5) {                                      // a turkey strutting along the lawn
      int bob = sin(T * 6 + sx) > 0 ? 1 : 0; static const col_t tail[5] = { H(b83a24), H(e8a030), H(6a3a1e), H(e8a030), H(b83a24) };
      for (int q = 0; q < 5; q++) R(sx - 7 + q * 2, oy - 12 + abs(q - 2), 2, 6, tail[q]);
      R(sx - 4, oy - 7, 7, 5, H(5a3418)); R(sx + 3, oy - 10 + bob, 2, 4, H(5a3418)); R(sx + 4, oy - 10 + bob, 2, 1, H(e8a030)); R(sx + 4, oy - 8 + bob, 1, 2, H(d8242e));
      R(sx - 2, oy - 2, 1, 2, H(e8a030)); R(sx + 1, oy - 2, 1, 2, H(e8a030));
    } else if (ty < 0.7) {                                      // a hay bale with a pumpkin on top
      R(sx - 7, oy - 6, 14, 6, H(d8b858)); R(sx - 7, oy - 4, 14, 1, H(b89838)); R(sx - 7, oy - 2, 14, 1, H(b89838));
      R(sx - 3, oy - 10, 6, 4, H(e0701a)); R(sx, oy - 12, 1, 2, H(4a7a2a));
    } else if (ty < 0.85) {                                     // a mailbox
      static const col_t box[3] = { H(3a6a9a), H(2a2a32), H(c8242e) };
      R(sx, oy - 10, 2, 10, H(5a3a2a)); R(sx - 4, oy - 14, 9, 5, box[(int)floor(sz * 3)]); R(sx + 4, oy - 16, 1, 3, H(d8242e));
    } else {                                                    // a street sign pointing the way
      R(sx, oy - 16, 1, 16, H(8a8a96)); R(sx - 6, oy - 18, 13, 4, H(1f7a4a)); R(sx + 7, oy - 17, 1, 2, H(1f7a4a)); R(sx - 5, oy - 16, 9, 1, H(f4f0e8));
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
/* ---------- the national parks and the Thanksgiving suburb (as in web/game.js) ---------- */
static void ellipseFill(double cx, double cy, double rx, double ry);
typedef void (*AtFn)(double x, int i, const Zone *Z);
static void eachAt(double camX, double par, double step, AtFn fn, const Zone *Z) {   // one thing every `step` px of a layer scrolling at `par`
  double off = camX * par; int i0 = (int)floor(off / step) - 1, i1 = (int)floor((off + LW) / step) + 1;
  for (int i = i0; i <= i1; i++) fn(rnd(i * step - off), i, Z);
}
static void pine(double x, double base, int h, col_t dark, col_t lit) {   // a stepped pixel pine
  for (int q = 0, rows = h >> 1; q < rows; q++) { int w = 1 + q * 5 / rows + (q & 1); R(x - w, base - h + q * 2, w * 2 + 1, 2, dark); R(x - w, base - h + q * 2, 1, 2, lit); }
  R(x, base, 1, 2, H(3a2a1e));
}
#define EACH(camX, par, step, i, x) for (int i##0 = (int)floor((camX) * (par) / (step)) - 1, i##1 = (int)floor(((camX) * (par) + LW) / (step)) + 1, i = i##0; i <= i##1; i++) \
  for (double x = rnd(i * (step) - (camX) * (par)), x##once = 1; x##once; x##once = 0)
static void fall(double x, double top, double bot, bool tall, int i) {   // a waterfall: tall ones drop in two tiers like Yosemite Falls, the rest sway like Bridalveil
  double mid = top + (bot - top) * 0.45, a0 = cv_get_alpha();
  for (double y = top; y < bot; y += 3) {                       // 3 px steps on the 3DS (the web uses 2)
    bool tier2 = tall && y > mid + 6, cascade = tall && y >= mid && y <= mid + 6;
    double k = (y - top) / (bot - top), sway = tall ? 0 : sin(T * 1.4 + y * 0.09 + i) * 2.2 * k;
    double fx = rnd(x + sway + (tier2 ? 3 : 0) + (cascade ? ((int)y & 2) : 0)), fw = cascade ? 6 : !tall ? 2 + (k > 0.6 ? 1 : 0) : 4;
    cv_alpha(a0 * 0.8); R(fx - 1, y, fw + 2, 3, H(bcdcf0));
    cv_alpha(a0 * ((tall ? 0.65 : 0.5) + 0.35 * fabs(sin(T * 9 - y * 0.35 + i)))); R(fx, y, fw, 3, H(ffffff));
  }
  cv_alpha(a0 * (0.35 + 0.1 * sin(T * 3 + i))); cv_fill(H(f4f8ff));   // spray at the foot
  for (int q = 0; q < 3; q++) cv_circle(x + (tall ? 5 : 1) + (q - 1) * 5, bot - 1, 4 + q % 2 * 2 + sin(T * 2 + q));
  cv_alpha(a0);
}
static void halfDome(const Zone *Z, double x) {                // a round granite dome sliced off on one side
  cv_fill(Z->lit); cv_begin(); cv_move(x - 115, HOR + 8); cv_quad(x - 76, HOR - 86, x, HOR - 92);
  cv_line(x + 11, HOR - 90); cv_line(x + 16, HOR - 34); cv_quad(x + 50, HOR - 20, x + 104, HOR + 8); cv_close(); cv_fillpath();
  cv_fill(Z->shade); { double p[8] = { x + 2, HOR - 92, x + 11, HOR - 90, x + 16, HOR - 34, x + 5, HOR - 44 }; cv_poly(p, 4); }
  cv_begin(); cv_move(x + 16, HOR - 34); cv_quad(x + 50, HOR - 20, x + 104, HOR + 8); cv_line(x + 22, HOR + 8); cv_close(); cv_fillpath();
  for (int k = 0; k < 4; k++) R(x + 6 + k * 2, HOR - 86 + k * 3, 1, 30 + k * 2, shadec(Z->shade, 0.85));
  for (int k = 0; k < 7; k++) R(x - 72 + k * 10, HOR - 44 - (k < 4 ? k * 12 : (6 - k) * 10), 1, 34, shadec(Z->lit, 0.9));   // granite streaks
}
static void elCapitan(const Zone *Z, double x, int i) {        // a huge flat-topped block with a sheer prow, and a climber inching up the Nose
  cv_fill(Z->lit); cv_begin(); cv_move(x - 92, HOR + 8); cv_line(x - 76, HOR - 52); cv_quad(x - 58, HOR - 96, x - 24, HOR - 100);
  cv_line(x + 34, HOR - 98); cv_line(x + 42, HOR + 8); cv_close(); cv_fillpath();
  cv_fill(Z->shade); { double p[8] = { x + 16, HOR - 99, x + 34, HOR - 98, x + 42, HOR + 8, x + 20, HOR + 8 }; cv_poly(p, 4); }
  for (int k = 0; k < 6; k++) R(x - 36 + k * 9, HOR - 90 + (int)(hash(i, k) * 20), 1, 46, shadec(Z->lit, 0.88));
  R(x + 14, HOR - 30 - rnd(fmod(T * 0.8, 56)), 1, 1, H(ff3a3a));
}
static void yosemiteFalls(const Zone *Z, double x, int i) {    // a tall dark-streaked cliff with the big two-tier fall
  cv_fill(shadec(Z->lit, 0.9)); cv_begin(); cv_move(x - 100, HOR + 8); cv_line(x - 84, HOR - 60); cv_quad(x - 70, HOR - 84, x - 30, HOR - 86);
  cv_line(x + 40, HOR - 84); cv_quad(x + 76, HOR - 80, x + 88, HOR - 50); cv_line(x + 100, HOR + 8); cv_close(); cv_fillpath();
  cv_fill(Z->shade); cv_begin(); cv_move(x + 40, HOR - 84); cv_quad(x + 76, HOR - 80, x + 88, HOR - 50); cv_line(x + 100, HOR + 8); cv_line(x + 60, HOR + 8); cv_close(); cv_fillpath();
  for (int k = 0; k < 7; k++) R(x - 70 + k * 18 + rnd(hash(i, 260 + k) * 8), HOR - 78 + rnd(hash(i, 270 + k) * 16), 1, 40 + rnd(hash(i, 280 + k) * 30), shadec(Z->lit, 0.72));
  R(x - 4, HOR - 86, 14, 94, shadec(Z->shade, 0.75));         // the wet, dark rock behind the water
  fall(x, HOR - 85, HOR + 4, true, i);
}
static void granite(const Zone *Z, double x, int i, double *w, double *top) {   // a stretch of valley wall: cliffs with rounded shoulders and dark streaks
  *w = 150 + rnd(hash(i, 201) * 50); *top = HOR - 30 - rnd(hash(i, 202) * 26); double t = *top, W = *w, dip = t + 6 + rnd(hash(i, 203) * 10);
  cv_fill(shadec(Z->lit, 0.82)); cv_begin(); cv_move(x - 12, HOR + 8); cv_line(x, t + 16); cv_quad(x + 6, t, x + 34, t + 1);
  cv_line(x + W * 0.55, dip); cv_quad(x + W - 12, t + 2, x + W, t + 18); cv_line(x + W + 12, HOR + 8); cv_close(); cv_fillpath();
  cv_fill(Z->shade); tri(x + W, t + 18, x + W + 12, HOR + 8, x + W - 10, HOR + 8);
  for (int k = 0; k < 8; k++) { double sx = x + 10 + rnd(hash(i, 210 + k) * (W - 20)); R(sx, t + 8 + rnd(hash(i, 220 + k) * 14), 1, 14 + rnd(hash(i, 230 + k) * 24), shadec(Z->lit, 0.7)); }
}
// The Old 3DS can't afford the web's stepped treelines (over a thousand rectangles), so those pines are plain triangles.
static void triPine(double x, double base, int h, col_t dark, col_t lit) {
  int w = 2 + h / 5; cv_fill(dark); tri(x - w, base, x, base - h, x + w + 1, base); R(x - 1, base - h + 3, 1, h - 4, lit);
}
static void drawYosemite(const Zone *Z, double camX) {
  peaks(0.03, 14, 32, 60, 110, 130, 0.38, camX, 130, H(eef2f8), H(a4aec4));   // the high Sierra, snowy and hazy
  double falls[8][3]; int nf = 0;
  EACH(camX, 0.06, 150, i, px) {                                // the valley walls, with a waterfall on most stretches
    double x = px + rnd(hash(i, 200) * 30), w, top; granite(Z, x, i, &w, &top);
    if (hash(i, 205) < 0.6 && nf < 8) { falls[nf][0] = x + 30 + rnd(hash(i, 206) * (w - 70)); falls[nf][1] = top + 3; falls[nf][2] = i; nf++; }
  }
  for (int k = 0; k < nf; k++) { R(falls[k][0] - 3, falls[k][1], 9, HOR + 4 - falls[k][1], shadec(Z->lit, 0.62)); fall(falls[k][0], falls[k][1], HOR + 4, false, (int)falls[k][2]); }
  EACH(camX, 0.09, 520, i, px) {                                // the famous three, taking turns
    double x = px + 140 + rnd(hash(i, 140) * 100); int k = ((i % 3) + 3) % 3;
    if (k == 0) elCapitan(Z, x, i); else if (k == 1) halfDome(Z, x); else yosemiteFalls(Z, x, i);
  }
  for (int i = 0; i < 3; i++) {                                 // hawks circling on the thermals
    double span = LW * 1.4, cx = jmod(jmod(hash(i, 240) * span - camX * 0.02, span) + span, span) - LW * 0.2, a = T * (0.4 + hash(i, 241) * 0.25) + i * 2.1, r = 14 + hash(i, 242) * 12;
    double bx = rnd(cx + cos(a) * r), by = rnd(18 + hash(i, 243) * 34 + sin(a) * r * 0.35); bool flap = sin(T * 7 + i * 3) > 0.6;
    cv_fill(H(3a2c28)); F(bx - 5, by, 11, 1); F(bx - 1, by - 1, 3, 2); F(bx - 6, by + (flap ? -1 : 1), 1, 1); F(bx + 6, by + (flap ? -1 : 1), 1, 1);
  }
  double fp = fmod(T, 22) / 7;                                   // now and then a flock of swifts cuts across the valley
  if (fp < 1) { cv_fill(H(2a2430)); for (int k = 0; k < 7; k++) {
    double x = rnd(LW + 30 - fp * (LW + 90) + k * 7), y = rnd(26 + abs(k - 3) * 3 + sin(T * 2 + k) * 1.5); int up = sin(T * 14 + k) > 0;
    F(x - 1, y - up, 1, 1); F(x, y, 1, 1); F(x + 1, y - up, 1, 1); } }
  EACH(camX, 0.18, 7, i, x) triPine(x, HOR + 6, 12 + (int)(hash(i, 150) * 16), H(2c5a40), H(3e7452));
  EACH(camX, 0.26, 23, i, x) if (hash(i, 151) < 0.6) triPine(x, HOR + 7, 22 + (int)(hash(i, 152) * 12), H(1e4a32), H(2e6244));   // a nearer row of tall pines
}
static void drawMerced(double camX) {                           // the Merced River winding through the meadow (8 px pieces on the 3DS)
  for (double x = -8; x < LW + 8; x += 8) { double y = rnd(HOR + 9 + sin((x + camX * 0.3) / 50) * 1.5); R(x, y, 8, 3, H(6aa8d0)); R(x, y, 8, 1, H(b8e0f0)); }
  for (int i = 0; i < 12; i++) { double x = rnd(jmod(jmod(hash(i, 250) * LW - camX * 0.3, LW) + LW, LW)); R(x, rnd(HOR + 10 + sin((x + camX * 0.3) / 50) * 1.5), 2, 1, sin(T * 4 + i) > 0 ? H(ffffff) : H(b8e0f0)); }
}
static void bear(double x, double y, int dir, double big, col_t col) {   // a bear ambling along, legs stepping
  int s = sin(T * 5 + x * 0.1) > 0 ? 1 : 0;
  #define BR(a, b, w, h, c) R(rnd(dir > 0 ? x + (a) * big : x - ((a) + (w)) * big), rnd(y + (b) * big), fmax(1, rnd((w) * big)), fmax(1, rnd((h) * big)), c)
  BR(-6, -7, 12, 5, col); BR(-2, -8, 5, 1, col); BR(5, -8, 4, 4, col); BR(8, -6, 2, 2, H(c8a080)); BR(5, -9, 1, 1, col); BR(9, -6, 1, 1, H(14100c));
  static const int legs[4][2] = { { -5, 0 }, { -2, 1 }, { 2, 0 }, { 4, 1 } };
  for (int k = 0; k < 4; k++) { int up = legs[k][1] == s; BR(legs[k][0], -2 - up, 2, up ? 1 : 2, col); }
  #undef BR
}
static void drawYosemiteDecor(double sx, double oy, double sz, double ty) {
  if (ty < 0.34) pine(sx, oy - 2, 18 + (int)(sz * 16), H(24543a), H(3a7050));                                  // a tall pine
  else if (ty < 0.44) {                                         // a giant sequoia
    R(sx - 3, oy - 32, 6, 32, H(8a3a22)); R(sx - 2, oy - 32, 2, 32, H(a8502e)); R(sx - 5, oy - 3, 10, 3, H(7a3018));
    static const double cr[5][3] = { { 0, -36, 7 }, { -6, -30, 5 }, { 6, -31, 5 }, { -1, -44, 6 }, { 0, -51, 4 } }, hl[3][3] = { { 2, -38, 3 }, { 1, -47, 2 }, { -5, -32, 2 } };
    cv_fill(H(24543a)); for (int q = 0; q < 5; q++) cv_circle(sx + cr[q][0], oy + cr[q][1], cr[q][2]);
    cv_fill(H(3a7050)); for (int q = 0; q < 3; q++) cv_circle(sx + hl[q][0], oy + hl[q][1], hl[q][2]);
  }
  else if (ty < 0.56) { R(sx - 6, oy - 6, 13, 6, H(9a9aa8)); R(sx - 4, oy - 8, 9, 2, H(b8b8c4)); R(sx - 6, oy - 1, 13, 1, H(6a6a7a)); }   // a granite boulder
  else if (ty < 0.74) {                                         // a black bear (some are cinnamon), sometimes with a cub
    int dir = sz < 0.5 ? -1 : 1; double x = sx + rnd(sin(T * 0.35 + sz * 9) * 8); col_t col = sz > 0.82 ? H(6a4228) : H(2a1e18);
    bear(x, oy, dir, 1.3, col); if (sz < 0.3 || sz > 0.9) bear(x - dir * 17, oy, dir, 0.7, col);
  }
  else if (ty < 0.83) {                                         // a mule deer, grazing now and then
    bool down = sin(T * 0.7 + sz * 11) > 0.3;
    R(sx - 4, oy - 8, 8, 3, H(a8784a)); R(sx - 5, oy - 8, 1, 2, H(f4f0e8));
    static const int lg[4] = { -4, -2, 2, 3 }; for (int q = 0; q < 4; q++) R(sx + lg[q], oy - 5, 1, 5, H(7a5432));
    if (down) { R(sx + 3, oy - 8, 2, 3, H(a8784a)); R(sx + 4, oy - 5, 3, 2, H(a8784a)); }
    else { R(sx + 3, oy - 12, 2, 4, H(a8784a)); R(sx + 3, oy - 13, 3, 2, H(a8784a)); R(sx + 3, oy - 15, 1, 2, H(8a6440)); R(sx + 5, oy - 15, 1, 2, H(8a6440)); }
  }
  else if (ty < 0.9) {                                          // a Steller's jay hopping on a stump
    int hop = fmod(T * 2 + sz * 9, 3) < 0.2 ? 2 : 0;
    R(sx - 3, oy - 5, 6, 5, H(6a4424)); R(sx - 3, oy - 5, 6, 1, H(a8784a));
    R(sx - 1, oy - 9 - hop, 4, 3, H(2a5ac8)); R(sx - 2, oy - 8 - hop, 1, 2, H(1a3a8a)); R(sx + 1, oy - 11 - hop, 2, 2, H(1a2a4a)); R(sx + 2, oy - 12 - hop, 1, 1, H(1a2a4a)); R(sx + 3, oy - 10 - hop, 1, 1, H(14100c));
  }
  else if (ty < 0.95) { R(sx - 6, oy - 7, 12, 7, H(6a5a3a)); R(sx - 6, oy - 7, 12, 1, H(8a7a5a)); R(sx - 1, oy - 5, 3, 1, H(2a2420)); R(sx - 6, oy - 1, 12, 1, H(4a3e28)); }   // a bear-proof food locker
  else { R(sx - 1, oy - 10, 1, 10, H(5a3a22)); R(sx + 6, oy - 10, 1, 10, H(5a3a22)); R(sx - 3, oy - 15, 12, 6, H(6a4424)); R(sx - 2, oy - 13, 10, 1, H(e8d8a0)); R(sx - 2, oy - 11, 7, 1, H(e8d8a0)); }   // a park sign
}
/* now and then (about 8% of the ride) the Yosemite road runs through a campground: look only (campAt in web/game.js) */
#define CAMP_W 1800
static const char *const CAMP_NAMES[8] = { "UPPER PINES", "LOWER PINES", "NORTH PINES", "CAMP 4", "WAWONA", "CRANE FLAT", "TUOLUMNE", "BRIDALVEIL" };
static bool campAt(double wx) { int c = (int)floor(wx / CAMP_W); return c >= 2 && hash(c + (int)(hash(SEED, 403) * 1e6), 401) < 0.08; }   // ~8% of 300 m stretches
static const col_t TENTS[5][2] = { { H(e8743a), H(b8542a) }, { H(3a8a5a), H(2a6a44) }, { H(3a6ac8), H(2a4a9a) }, { H(e8c43a), H(b8942a) }, { H(c83a3a), H(9a2a2a) } };
static void campSign(double sx, double oy, int c) {             // the wooden campground sign by the road
  R(sx - 20, oy - 8, 2, 8, H(5a3a22)); R(sx + 19, oy - 8, 2, 8, H(5a3a22));
  R(sx - 24, oy - 22, 49, 15, H(6a4424)); R(sx - 24, oy - 22, 49, 1, H(8a6440)); R(sx - 24, oy - 8, 49, 1, H(4a2e18));
  cv_text(CAMP_NAMES[(int)(hash(c, 402) * 8)], sx + 0.5, oy - 18, 6, H(e8d8a0), AL_CENTER, BL_MIDDLE);
  cv_text("CAMPGROUND", sx + 0.5, oy - 11.5, 6, H(e8d8a0), AL_CENTER, BL_MIDDLE);
}
static void drawCampDecor(double sx, double oy, double sz, double ty) {
  double a0 = cv_get_alpha();
  cv_alpha(a0 * 0.5); cv_fill(H(b8a47a)); ellipseFill(sx, oy - 1, 15, 3); cv_alpha(a0);   // the campsite's dirt pad
  if (ty < 0.3) {                                               // a tent: A-frame or dome
    col_t c = TENTS[(int)(sz * 5)][0], d = TENTS[(int)(sz * 5)][1];
    if (sz < 0.6) {
      for (int q = 0; q < 9; q++) { R(sx - q, oy - 9 + q, q * 2 + 1, 1, c); R(sx + 1, oy - 9 + q, q, 1, d); }
      for (int q = 0; q < 5; q++) R(sx - (q >> 1), oy - 5 + q, (q >> 1) * 2 + 1, 1, H(2a1e18));
      R(sx, oy - 10, 1, 1, H(5a3a22)); R(sx - 11, oy - 1, 1, 1, H(d8d0c0)); R(sx + 11, oy - 1, 1, 1, H(d8d0c0));
    } else {
      cv_fill(c); cv_begin(); cv_ellipse(sx, oy, 10, 9, 0, M_PI, 2 * M_PI); cv_fillpath();
      cv_fill(d); cv_begin(); cv_ellipse(sx, oy, 10, 9, 0, -M_PI / 2, 0); cv_line(sx, oy); cv_fillpath();
      R(sx - 9, oy - 4, 18, 1, shadec(d, 0.8)); R(sx - 3, oy - 5, 5, 5, H(2a1e18));
    }
  } else if (ty < 0.44) {                                       // a campfire with someone roasting a marshmallow
    double i = sz * 20;
    cv_alpha(a0 * 0.18); cv_fill(H(ffb040)); cv_circle(sx, oy - 3, 11 + sin(T * 9 + i)); cv_alpha(a0);
    for (int q = 0; q < 5; q++) R(sx - 5 + q * 2 + (q > 2 ? 1 : 0), oy - 2, 2, 2, H(7a7a84));
    R(sx - 3, oy - 3, 7, 1, H(5a3a22));
    for (int q = 0; q < 4; q++) { double h = 2 + rnd(fabs(sin(T * 11 + q * 2.3 + i)) * 4); R(sx - 2 + q, oy - 3 - h, 1, h, q & 1 ? H(ffd23a) : H(ff7a2a)); }
    for (int k = 0; k < 4; k++) {
      double p = fmod(T * 0.45 + k / 4.0 + i, 1); cv_alpha(a0 * 0.4 * (1 - p)); cv_fill(H(d8d8e0));
      cv_circle(sx + sin(p * 6 + i) * 2 + p * 6, oy - 9 - p * 28, 1.5 + p * 3); cv_alpha(a0);
    }
    R(sx + 7, oy - 2, 7, 2, H(6a4424)); R(sx + 9, oy - 8, 3, 6, sz < 0.5 ? H(3a5a9a) : H(9a3a4a)); R(sx + 9, oy - 10, 3, 2, H(e8b890)); R(sx + 9, oy - 11, 3, 1, H(5a3a22));
    for (int q = 0; q < 5; q++) R(sx + 8 - q, oy - 6 - (q >> 1), 1, 1, H(8a6440));
    R(sx + 2, oy - 9, 2, 2, H(f4f0e8));
  } else if (ty < 0.56) {                                       // a travel trailer, sometimes with its awning out
    col_t st = sz < 0.5 ? H(3a8ab8) : H(c8643a);
    R(sx - 12, oy - 13, 24, 10, H(e8e4dc)); R(sx - 11, oy - 14, 22, 1, H(e8e4dc)); R(sx - 12, oy - 4, 24, 1, H(a8a49c));
    R(sx - 12, oy - 8, 24, 1, st); R(sx - 9, oy - 12, 5, 3, H(5a7a9a)); R(sx + 3, oy - 12, 6, 3, H(5a7a9a)); R(sx - 2, oy - 11, 3, 7, H(b8b4ac));
    R(sx - 5, oy - 3, 4, 3, H(1a1a1e)); R(sx + 12, oy - 4, 4, 1, H(4a4a52)); R(sx + 15, oy - 3, 1, 3, H(4a4a52));
    if (sz > 0.3 && sz < 0.8) { R(sx - 14, oy - 15, 13, 2, st); R(sx - 14, oy - 13, 1, 10, H(8a8a92)); }
  } else if (ty < 0.66) {                                       // a picnic table with a cooler on it
    R(sx - 7, oy - 7, 15, 2, H(8a5a32)); R(sx - 7, oy - 7, 15, 1, H(a8784a)); R(sx - 9, oy - 4, 19, 1, H(7a4a2a));
    R(sx - 5, oy - 5, 1, 5, H(6a4024)); R(sx + 5, oy - 5, 1, 5, H(6a4024));
    R(sx - 3, oy - 10, 6, 3, sz < 0.5 ? H(c83a3a) : H(3a8ac8)); R(sx - 3, oy - 10, 6, 1, H(f4f0e8));
  } else if (ty < 0.75) {                                       // a wagon with a canoe on the roof
    static const col_t cars[4] = { H(4a6a8a), H(8a3a2a), H(5a7a4a), H(c8b48a) }; col_t col = cars[(int)(sz * 4)];
    R(sx - 11, oy - 8, 22, 5, col); R(sx - 7, oy - 11, 15, 3, col); R(sx - 6, oy - 10, 6, 2, H(9ac0d8)); R(sx + 1, oy - 10, 6, 2, H(9ac0d8));
    R(sx - 8, oy - 3, 4, 3, H(1a1a1e)); R(sx + 4, oy - 3, 4, 3, H(1a1a1e)); R(sx + 10, oy - 7, 1, 2, H(ffe8a0));
    R(sx - 11, oy - 13, 22, 2, H(e8743a)); R(sx - 13, oy - 13, 2, 1, H(e8743a)); R(sx + 11, oy - 13, 2, 1, H(e8743a)); R(sx - 11, oy - 12, 22, 1, H(b8542a));
  } else if (ty < 0.83) {                                       // a bear helping itself to an open cooler
    int dir = sz < 0.5 ? 1 : -1;
    R(sx + dir * 12 - 3, oy - 4, 7, 4, H(3a8ac8)); R(sx + dir * 12 - 3, oy - 4, 7, 1, H(f4f0e8)); R(sx + dir * 12 + (dir > 0 ? 4 : -4), oy - 9, 1, 5, H(f4f0e8));
    R(sx + dir * 16, oy - 1, 2, 1, H(e8c43a)); R(sx + dir * 19, oy - 1, 2, 1, H(c8643a));
    bear(sx + rnd(sin(T * 1.6 + sz * 9)), oy, dir, 1.3, sz > 0.8 ? H(6a4228) : H(2a1e18));
  } else if (ty < 0.9) { R(sx - 6, oy - 7, 12, 7, H(6a5a3a)); R(sx - 6, oy - 7, 12, 1, H(8a7a5a)); R(sx - 1, oy - 5, 3, 1, H(2a2420)); R(sx - 6, oy - 1, 12, 1, H(4a3e28)); }   // a food locker
  else if (ty < 0.95) {                                         // a hammock slung between two pines
    pine(sx - 9, oy - 2, 22, H(24543a), H(3a7050)); pine(sx + 9, oy - 2, 24, H(24543a), H(3a7050));
    double sw = sin(T * 1.3 + sz * 7);
    for (int q = -7; q <= 7; q++) R(sx + q, rnd(oy - 12 + (49 - q * q) / 12.0 + sw * (1 - q * q / 49.0)), 1, 1, H(c8643a));
    R(sx - 3, rnd(oy - 9 + sw), 6, 1, H(3a5a9a)); R(sx + 3, rnd(oy - 10 + sw), 2, 1, H(e8b890));
  } else {                                                      // a lantern post
    R(sx, oy - 15, 1, 15, H(4a3a2a)); R(sx, oy - 15, 4, 1, H(4a3a2a)); R(sx + 3, oy - 13, 2, 3, H(ffd27a));
    cv_alpha(a0 * 0.25); cv_fill(H(ffd27a)); cv_circle(sx + 4, oy - 12, 5); cv_alpha(a0);
  }
}
/* ---------- themed landscapes: the library, neon city and boardwalk pier (as THEMES in web/game.js) ----------
   They reskin every piece (floor, road, gaps, guardrails, barricades, backdrop, props) but ride exactly like any map.
   A holiday never lands on one (landOf), and a holiday look always wins over one. */
static const col_t BOOKS[6] = { H(7a2a26), H(2a4a34), H(2a3456), H(8a6a2a), H(4a2a4a), H(6a4a2a) };
static int imod(int a, int n) { return ((a % n) + n) % n; }
/* The library shelves and the neon skyline are a few hundred shapes each, too many to draw every frame on an original 3DS.
   Once the ride has left the garage behind, its cached picture (SPR_GARAGE) is reused for a 960 px strip of the backdrop
   that repeats; the shelves and windows only repeat every 960 px of their layer, which is about 8,000 m of road. Near the
   garage (and on the Wii, which can't make a texture that wide) they are drawn live. */
#define STRIP_W 960
static double stripPar(int th) { return th == TH_LIBRARY ? 0.12 : 0.1; }
static void stripLayer(int th, double off, double x0, double x1) {   // the strip's shapes between layer x0 and x1, shifted left by off
  if (th == TH_LIBRARY) {
    for (int i = (int)floor(x0 / 6) - 1; i <= (int)floor(x1 / 6) + 1; i++) {   // floor-to-ceiling shelves of books
      double x = rnd(i * 6 - off); int im = imod(i, STRIP_W / 6);
      for (int s = 0; s < 5; s++) { int y = 10 + s * 18, h = 11 + (int)(hash(im, s) * 5); if (hash(im, s + 9) < 0.07) continue; R(x, y + 16 - h, 5, h, BOOKS[(int)floor(hash(im, s + 20) * 6)]); R(x + 1, y + 18 - h, 3, 1, rgba(217, 164, 65, .5)); }
    }
    for (int s = 0; s < 6; s++) R(x0 - off - 6, 24 + s * 18, x1 - x0 + 12, 2, H(3a2010));
    for (int i = (int)floor(x0 / 120) - 1; i <= (int)floor(x1 / 120) + 1; i++) R(rnd(i * 120 - off), 0, 6, HOR, H(3a2010));
  } else {
    for (int i = (int)floor(x0 / 48) - 1; i <= (int)floor(x1 / 48) + 1; i++) {   // the far skyline
      double x = rnd(i * 48 - off); int im = imod(i, STRIP_W / 48), h = 30 + (int)(hash(im, 1) * 40), w = 30 + (int)(hash(im, 2) * 14);
      R(x, HOR - h, w, h + 4, H(1a1030)); cv_fill(H(4a3a6a));
      for (int y = HOR - h + 4; y < HOR; y += 5) for (int k = 3; k < w - 3; k += 5) if (hash(im * 31 + k, y) < 0.35) F(x + k, y, 2, 2);
    }
    for (int i = (int)floor(x0 / 80) - 1; i <= (int)floor(x1 / 80) + 1; i++) {   // the near skyline with lit windows
      double x = rnd(i * 80 - off); int im = imod(i, STRIP_W / 80), h = 40 + (int)(hash(im, 5) * 50), w = 36 + (int)(hash(im, 6) * 20);
      R(x, HOR - h, w, h + 4, H(0e0a1e));
      for (int y = HOR - h + 5; y < HOR; y += 6) for (int k = 4; k < w - 4; k += 6) if (hash(im * 17 + k, y) < 0.4) R(x + k, y, 2, 3, hash(im, y) < 0.5 ? H(ffd27a) : H(9ab8ff));
    }
  }
}
static void stripCache(int th) {                                 // at the start of a frame, away from the garage
  if (stripKind == th || stripFail) return;
  if (!spr_begin(SPR_GARAGE, GAR_W, LH - GAR_Y0, 1, 0, 0)) { stripFail = true; return; }
  stripLayer(th, 0, 0, STRIP_W); spr_end(); stripKind = th; garageDirty = true;
}
static void drawStrip(int th, double camX) {
  double off = camX * stripPar(th);
  if (stripKind == th && camX > 800) { double o = rnd(fmod(off, STRIP_W)); for (int k = 0; k < 2; k++) { cv_save(); cv_translate(-o + k * STRIP_W, 0); cv_sprite(SPR_GARAGE); cv_restore(); } }
  else stripLayer(th, off, off, off + LW);
}
static void drawLibrary(double camX) {
  drawStrip(TH_LIBRARY, camX);
  EACH(camX, 0.12, 420, i, x) {                                 // a rolling ladder
    double lx = x + 60; cv_begin(); cv_move(lx, 4); cv_line(lx - 14, HOR); cv_stroke(H(8a6a3a), 1); cv_begin(); cv_move(lx + 12, 4); cv_line(lx - 2, HOR); cv_stroke(H(8a6a3a), 1);
    for (int r = 8; r < HOR; r += 9) R(lx - 14.0 * r / HOR, r, 12, 1, H(8a6a3a));
  }
  EACH(camX, 0.2, 300, i, x) {                                  // hanging lamps with light shafts
    cv_fill(rgba(255, 200, 110, .10)); { double p[8] = { x - 4, 14, x + 4, 14, x + 40, HOR + 30, x - 40, HOR + 30 }; cv_poly(p, 4); }
    R(x - 1, 0, 2, 10, H(2a1a10)); R(x - 6, 10, 12, 4, H(c89a3a)); R(x - 5, 14, 10, 1, H(ffe8a8));
  }
  cv_fill(rgba(255, 230, 170, .7));                              // dust drifting in the light
  for (int i = 0; i < 18; i++) F(rnd(jmod(jmod(hash(i, 400) * LW - camX * 0.02 + T * 3, LW) + LW, LW)), rnd(20 + hash(i, 401) * 80 + sin(T + i) * 3), 1, 1);
}
static void drawNeonCity(double camX) {
  drawStrip(TH_NEON, camX);
  double off = camX * 0.1, a0 = cv_get_alpha();
  for (int i = (int)floor(off / 80) - 1; i <= (int)floor((off + LW) / 80) + 1; i++) {   // flickering neon signs on the near towers
    int im = imod(i, STRIP_W / 80); if (hash(im, 7) >= 0.5) continue;
    double x = rnd(i * 80 - off); int h = 40 + (int)(hash(im, 5) * 50), w = 36 + (int)(hash(im, 6) * 20); bool on = sin(T * 6 + i * 3) > -0.6;
    cv_alpha(a0 * (on ? 1 : 0.3)); R(x + w - 6, HOR - h + 8, 4, 22, hash(im, 8) < 0.5 ? H(ff3ea5) : H(22e6d8)); cv_alpha(a0);
  }
}
static void drawPier(double camX) {
  R(0, HOR - 30, LW, 34, H(2f8fa6)); R(0, HOR - 30, LW, 1, H(7bd0d8));
  cv_fill(rgba(255, 255, 255, .7)); for (int i = 0; i < 30; i++) F(rnd(jmod(hash(i, 80) * LW - camX * 0.05 + LW * 4, LW)), HOR - 28 + (int)(hash(i, 81) * 28), 3, 1);
  EACH(camX, 0.08, 700, i, x) {                                  // a Ferris wheel and a roller coaster out on the pier
    R(x, HOR - 34, 140, 4, H(6a4420)); cv_fill(H(6a4420)); for (int k = 0; k < 140; k += 10) F(x + k, HOR - 30, 2, 10);
    double cx = x + 40, cy = HOR - 70, r = 30; cv_begin(); cv_arc(cx, cy, r, 0, 6.2832); cv_stroke(H(f4f0e8), 2);
    static const col_t cars[3] = { H(ff3a4a), H(ffd21f), H(3ab0ff) };
    for (int k = 0; k < 8; k++) { double an = k / 8.0 * 6.283 + T * 0.3; cv_begin(); cv_move(cx, cy); cv_line(cx + cos(an) * r, cy + sin(an) * r); cv_stroke(H(f4f0e8), 1); R(rnd(cx + cos(an) * r) - 2, rnd(cy + sin(an) * r), 5, 4, cars[k % 3]); }
    R(cx - 1, cy, 2, 36, H(f4f0e8)); cv_begin(); cv_move(x + 80, HOR - 34); for (int k = 0; k <= 50; k += 2) cv_line(x + 80 + k, HOR - 34 - 28 * fabs(sin(k / 50.0 * 3.14 * 1.5))); cv_stroke(H(ff5a3a), 1);
  }
  cv_fill(H(ffffff));                                            // gulls
  for (int i = 0; i < 4; i++) { double x = rnd(jmod(jmod(hash(i, 90) * LW - T * 20, LW) + LW, LW)), y = rnd(20 + hash(i, 91) * 30); int up = sin(T * 6 + i) > 0 ? 0 : 1; F(x - 3, y + up, 3, 1); F(x, y, 1, 1); F(x + 1, y + up, 3, 1); }
}
static const struct { char ch; uint16_t b; } GLYPHS[] = { { 'S', 0x79cf }, { 'P', 0x7be4 }, { 'E', 0x79e7 }, { 'D', 0x6b6e }, { 'L', 0x4927 }, { 'I', 0x7497 }, { 'M', 0x5fed },
  { 'T', 0x7492 }, { '0', 0x7b6f }, { '1', 0x2c97 }, { '2', 0x73e7 }, { '3', 0x73cf }, { '4', 0x5bc9 }, { '5', 0x79cf }, { '6', 0x79ef } };
static void pixText(const char *str, double x, double y, col_t col, int k) {   // 3x5 pixel letters, scaled by k, so small signs stay sharp
  cv_fill(col);
  for (; *str; str++, x += 4 * k) {
    unsigned b = 0; for (unsigned q = 0; q < sizeof GLYPHS / sizeof GLYPHS[0]; q++) if (GLYPHS[q].ch == *str) b = GLYPHS[q].b;
    for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) if (b >> (14 - r * 3 - c) & 1) F(x + c * k, y + r * k, k, k);
  }
}
static void speedSign(double wx, double camX, int i) {          // a white speed limit sign on a post at the top edge of the road
  roadAt(wx); double x = rnd(wx - camX), y = rnd(RC - RW / 2 - 3);
  if (x < -20 || x > LW + 20) return;
  R(x, y - 12, 2, 12, H(8a8a96)); R(x - 11, y - 39, 25, 28, H(14101f)); R(x - 10, y - 38, 23, 26, H(f4f0e8));
  pixText("SPEED", x - 8, y - 36, H(14101f), 1); pixText("LIMIT", x - 8, y - 30, H(14101f), 1);
  static const char *const lim[5] = { "25", "35", "45", "55", "65" }; pixText(lim[(int)(hash(i, 404) * 5)], x - 6, y - 23, H(14101f), 2);
}
static void sandcastle(double sx, double oy, double sz) {       // a sandcastle: a keep with towers, a waving flag, a moat and a bucket and spade
  bool big = sz > 0.55; col_t S = H(e0c890), L = H(f0dcac), D = H(b89c64);
  #define TOWER(x, w, h) do { R(x, oy - (h), w, h, S); R(x, oy - (h), 1, h, L); R((x) + (w) - 1, oy - (h), 1, h, D); for (int q = 0; q < (w); q += 2) R((x) + q, oy - (h) - 2, 1, 2, S); } while (0)
  R(sx - 13, oy - 1, 27, 2, H(5ab0d0)); R(sx - 13, oy - 1, 27, 1, H(9ad8f0));   // the moat
  if (big) { TOWER(sx - 11, 6, 12); TOWER(sx + 6, 6, 12); TOWER(sx - 5, 11, 9); TOWER(sx - 2, 5, 17); R(sx - 1, oy - 5, 3, 4, H(8a6a3a)); R(sx - 1, oy - 6, 3, 1, H(7a5a2a)); }
  else { TOWER(sx - 7, 5, 8); TOWER(sx + 3, 5, 8); TOWER(sx - 3, 7, 6); TOWER(sx - 1, 3, 11); }
  #undef TOWER
  double top = oy - (big ? 19 : 13); int wv = sin(T * 6 + sz * 20) > 0 ? 1 : 0; col_t fl = sz < 0.3 || (sz > 0.55 && sz < 0.8) ? H(ff4a4a) : H(3ab0ff);
  R(sx, top - 7, 1, 7, H(6a4a2a)); R(sx + 1, top - 7 + wv, 4, 2, fl); R(sx + 4, top - 6 - wv, 1, 1, fl);
  for (int q = 0; q < 3; q++) R(sx - 9 + q * 7, oy - 3 - (q & 1), 1, 1, H(f4f0e8));   // shells pressed into the walls
  if (sz > 0.4) { R(sx + 15, oy - 5, 5, 5, H(ff8a3a)); R(sx + 15, oy - 5, 5, 1, H(ffb070)); R(sx + 16, oy - 7, 3, 1, H(ff8a3a)); R(sx + 21, oy - 9, 1, 8, H(3ab0ff)); R(sx + 20, oy - 2, 3, 2, H(3ab0ff)); }   // a bucket and spade
}
static void drawThemeRoad(double camX, int th) {
  const Theme *C = &THEMES[th - FIRST_THEME];
  enum { N = 122 };
  static double top[N], bot[N];
  for (int i = 0; i < N; i++) { roadAt(camX + i * 4 - 4); top[i] = rnd(RC - RW / 2); bot[i] = rnd(RC + RW / 2); }
  for (int i = 0; i + 1 < N; i++) { double y0 = rnd((top[i] + top[i + 1]) / 2), y1 = rnd((bot[i] + bot[i + 1]) / 2); cv_pattern(PAT_ROAD, i * 4 - 4, y0, 4, y1 - y0); }
  if (th == TH_PIER) {                                           // planks running across the walk, with the odd nail
    for (double x = floor(camX / 6) * 6; x < camX + LW + 6; x += 6) { roadAt(x); R(rnd(x - camX), rnd(RC - RW / 2), 1, rnd(RW), H(6a4420)); if (hash((int)x, 9) < 0.3) R(rnd(x - camX) + 3, rnd(RC - RW / 2 + hash((int)x, 8) * RW), 1, 1, H(4a2e14)); }
  }
  cv_begin(); for (int i = 0; i < N; i++) { double x = i * 4 - 4; i ? cv_line(x, top[i] + 1) : cv_move(x, top[i] + 1); } cv_stroke(C->hi, 2);
  cv_begin(); for (int i = 0; i < N; i++) { double x = i * 4 - 4; i ? cv_line(x, bot[i] - 1) : cv_move(x, bot[i] - 1); } cv_stroke(C->lo, 2);
  if (C->dash) { cv_fill(C->dash); for (double x = floor(camX / 24) * 24; x < camX + LW + 24; x += 24) { roadAt(x + 3); F(rnd(x - camX), rnd(RC - RW * 0.2), 8, 1); F(rnd(x - camX + 12), rnd(RC + RW * 0.2), 8, 1); } }
  if (camX < 420) { roadAt(304); double y0 = rnd(RC - RW / 2); int n = (int)floor(RW / 4); for (int r = 0; r < n; r++) for (int c = 0; c < 3; c++) R(rnd(304 - camX) + c * 4, y0 + r * 4, 4, 4, (r + c) & 1 ? H(f1ecf7) : H(0a0612)); }
  genTo(camX + LW + 2200);
  for (int i = lbSpans(&gaps, camX - 4200); i < gaps.n && gaps.a[i].x < camX + LW + 20; i++) {
    double gx = gaps.a[i].x, gl = gaps.a[i].len, sx = rnd(gx - camX), ex = sx + gl; if (ex < -20) continue;
    int gi = (int)gx;
    for (int y = 118; y <= 262; y += 3) {
      double l = sx + rnd(hash(gi, y) * 12 - 6), r = ex + rnd(hash(gi + 7, y) * 12 - 6), h = y + 3 <= 262 ? 3 : 1;
      cv_pattern(PAT_G0, l, y, r - l, h);
      if (th == TH_NEON) R(l, y, r - l, h, rgba(80, 140, 220, .35));   // a flooded stretch of road
    }
    if (th == TH_NEON) speedSign(gx - 34, camX, i);            // a speed limit sign before each flooded stretch
    if (th == TH_LIBRARY) for (int q = 0; q < 14; q++) { roadAt(gx + hash(i, q) * gl); R(rnd(sx + hash(i, q) * gl), rnd(RC - RW / 2 + hash(i, q + 50) * RW), 4, 3, q % 3 ? H(f2ead2) : H(d8ccb0)); }   // loose pages
  }
}
typedef double (*YFn)(double x, int arg);
static void themeRail(double x0, double x1, double camX, YFn yFn, int arg, bool cap, int th) {
  double a = fmax(x0, camX - 8), b = fmin(x1, camX + LW + 8); if (b <= a) return;
  double s0 = floor(a / 4) * 4;
  if (th == TH_NEON) {                                            // concrete barriers with reflectors (pink on the barricades), merged into runs
    double runX = 0, runY = 0; bool run = false;
    for (double x = s0; x <= b + 4; x += 4) {
      bool end = x > b; double y = end ? 0 : rnd(yFn(x, arg)), sx = rnd(x - camX);
      if (run && (end || y != runY)) { R(runX, runY - 3, sx - runX, 6, H(8a8a96)); R(runX, runY - 3, sx - runX, 1, H(c0c0cc)); R(runX, runY + 3, sx - runX, 1, H(4a4a56)); run = false; }
      if (end) break;
      if (!run) { run = true; runX = sx; runY = y; }
    }
    for (double x = ceil(s0 / 16) * 16; x <= b; x += 16) R(rnd(x - camX) + 1, rnd(yFn(x, arg)) - 1, 2, 2, cap ? H(ff3ea5) : H(ffd21f));
    return;
  }
  for (double x = s0; x <= b; x += 4) {
    double y = rnd(yFn(x, arg)), sx = rnd(x - camX); long long xi = (long long)x;
    if (th == TH_LIBRARY) {
      if (cap) { int h = 5 + (int)(hash((int)x, 4) * 5); R(sx, y + 6 - h, 4, h, BOOKS[(int)floor(hash((int)x, 3) * 5)]); R(sx, y + 6 - h, 4, 1, H(f2ead2)); R(sx + 3, y + 6 - h, 1, h, rgba(0, 0, 0, .25)); R(sx, y + 6, 4, 1, H(2a160c)); }   // a wall of stacked books
      else { if (xi % 32 == 0) { R(sx, y - 5, 2, 9, H(d9a441)); R(sx - 1, y - 6, 4, 2, H(f2d27a)); R(sx - 1, y + 4, 4, 1, H(8a6420)); } double sag = sin(((double)(xi % 32) / 32) * M_PI) * 3; R(sx, rnd(y - 3 + sag), 4, 2, H(b0202e)); R(sx, rnd(y - 3 + sag), 4, 1, H(e04a56)); }   // brass stanchions and velvet rope
    } else {                                                      // rope between posts; the barricades are red and white lifeguard boards
      if (cap) R(sx, y - 2, 4, 6, (xi >> 3) & 1 ? H(f4f0e8) : H(e03a3a));
      else { if (xi % 24 == 0) R(sx, y - 6, 3, 10, H(7a5228)); double sag = sin(((double)(xi % 24) / 24) * M_PI) * 2; R(sx, rnd(y - 4 + sag), 4, 2, H(8a6438)); R(sx, rnd(y - 4 + sag), 4, 1, H(c8a46a)); }
    }
  }
}
static void drawThemeDecor(int th, double sx, double oy, double sz, double ty) {
  if (th == TH_LIBRARY) {
    if (ty < 0.35) { R(sx - 9, oy - 7, 19, 2, H(5a3418)); R(sx - 8, oy - 5, 2, 5, H(3a2010)); R(sx + 7, oy - 5, 2, 5, H(3a2010)); R(sx - 4, oy - 13, 1, 6, H(c89a3a)); R(sx - 7, oy - 15, 7, 3, H(1f7a4a)); R(sx - 6, oy - 14, 5, 1, H(5ac08a)); cv_fill(rgba(255, 230, 140, .18)); cv_circle(sx - 3, oy - 9, 8); R(sx + 2, oy - 9, 6, 2, H(8a2a2a)); }   // a reading table with a banker's lamp
    else if (ty < 0.6) { for (int q = 0; q < 4 + (int)(sz * 4); q++) R(sx - 5 + (int)(hash(q, (int)sx) * 3), oy - 3 - q * 3, 10, 3, BOOKS[q % 4]); }   // a pile of books
    else if (ty < 0.8) { R(sx - 1, oy - 4, 3, 4, H(c89a3a)); R(sx - 4, oy - 1, 9, 1, H(c89a3a)); cv_fill(H(2a6a8a)); cv_circle(sx, oy - 9, 5); R(sx - 3, oy - 11, 3, 3, H(5aa04a)); R(sx + 1, oy - 8, 2, 2, H(5aa04a)); }   // a globe
    else { R(sx - 6, oy - 8, 13, 8, H(6a1a1a)); R(sx - 7, oy - 12, 3, 12, H(5a1414)); R(sx + 5, oy - 12, 3, 12, H(5a1414)); R(sx - 5, oy - 14, 11, 4, H(7a2222)); }   // a wingback chair
  } else if (th == TH_NEON) {
    if (ty < 0.4) { R(sx, oy - 24, 2, 24, H(4a4a5a)); R(sx, oy - 24, 6, 2, H(4a4a5a)); R(sx + 5, oy - 23, 3, 1, H(ffe8a8)); cv_fill(rgba(255, 230, 160, .12)); cv_circle(sx + 6, oy - 4, 10); }   // a streetlight
    else if (ty < 0.7) { static const col_t cans[3] = { H(ff3a3a), H(ffd21f), H(3ab0ff) }; R(sx - 4, oy - 14, 9, 14, H(d83a5a)); R(sx - 3, oy - 13, 7, 6, H(9ad8ff)); for (int q = 0; q < 3; q++) R(sx - 2 + q * 2, oy - 12, 1, 4, cans[q]); }   // a vending machine
    else { R(sx - 2, oy - 6, 5, 6, H(ff7a1a)); R(sx - 1, oy - 4, 3, 1, H(ffffff)); R(sx - 3, oy - 1, 7, 1, H(ff7a1a)); }   // a traffic cone
  } else {
    if (ty < 0.4) { col_t c = sz < 0.5 ? H(ff4a4a) : H(3ab0ff); R(sx, oy - 16, 1, 16, H(f4f0e8)); R(sx - 8, oy - 18, 17, 3, c); R(sx - 5, oy - 20, 11, 2, c); R(sx - 4, oy - 18, 3, 3, H(f4f0e8)); }   // a beach umbrella
    else if (ty < 0.55) { R(sx - 6, oy - 3, 12, 3, H(ff8a3a)); R(sx - 6, oy - 3, 12, 1, H(ffd21f)); }   // a towel
    else sandcastle(sx, oy, sz);
  }
}
static col_t bands[5];
static void butte(double px, int i, double sc, col_t fade, double fadeA) {   // a stepped butte striped with rock layers
  double x = px + rnd(hash(i, 161) * 60), w = rnd((50 + hash(i, 162) * 70) * sc), h = rnd((40 + hash(i, 163) * 44) * sc); int tiers = 2 + (int)(hash(i, 164) * 3);
  for (int k = 0; k < tiers; k++) {
    double tw = rnd(w * (1 - k * 0.24)), tx = x + rnd((w - tw) * (0.3 + hash(i, 165) * 0.4)), ty = HOR + 6 - rnd(h * (k + 1) / tiers), th = rnd(h / tiers) + 1;
    for (int y = 0; y < th; y += 3) R(tx, ty + y, tw, fmin(3, th - y), bands[(k * 2 + y / 3 + ((i % 5) + 5)) % 5]);
    R(tx + rnd(tw * 0.68), ty, tw - rnd(tw * 0.68), th, rgba(40, 10, 20, .22));
    if (fadeA > 0) R(tx, ty, tw, th, fade);
  }
}
static void farButte(double px, int i, const Zone *Z) { (void)Z; butte(px, i + 500, 0.7, rgba(190, 120, 150, .45), 1); }
static void nearButte(double px, int i, const Zone *Z) { (void)Z; butte(px, i, 1, 0, 0); }
static void farRim(double x, int i, const Zone *Z) {
  (void)Z; double top = HOR - 20 - (int)(hash(i, 160) * 8), a0 = cv_get_alpha(); cv_alpha(a0 * 0.55);
  R(x, top, 41, HOR + 8 - top, H(b2708a)); R(x, top + 5, 41, 1, H(d8a0a8)); R(x, top + 11, 41, 1, H(9a5a7a)); cv_alpha(a0);
}
static void drawCanyon(const Zone *Z, double camX) {
  bands[0] = Z->shade; bands[1] = Z->lit; bands[2] = shadec(Z->lit, 0.86); bands[3] = shadec(Z->shade, 0.8); bands[4] = shadec(Z->lit, 0.94);
  eachAt(camX, 0.03, 40, farRim, Z);                            // the far rim
  eachAt(camX, 0.055, 110, farButte, Z);                        // far buttes, hazy
  eachAt(camX, 0.1, 190, nearButte, Z);
  R(0, HOR - 1, LW, 3, shadec(Z->shade, 0.55));                 // the gorge, with the river glinting at the bottom
  for (int i = 0; i < 14; i++) R(rnd(jmod(jmod(hash(i, 170) * LW - camX * 0.1, LW) + LW, LW)), HOR, 3, 1, H(7ad0e0));
}
static const col_t HOUSE[6] = { H(e8dcc0), H(a8c4d8), H(c8d4a8), H(c86a4a), H(f0d890), H(d8b0a0) }, ROOF[3] = { H(5a3a3a), H(3a3a4a), H(6a4a2a) };
static void house(double x, double base, double w, double h, col_t body, col_t roof, bool chimney) {
  R(x, base - h, w, h, body); R(x, base - h, w, 1, shadec(body, 0.8));
  cv_fill(roof); tri(x - 4, base - h + 1, x + w / 2, base - h - 16, x + w + 4, base - h + 1);
  if (chimney) {
    R(x + w - 16, base - h - 14, 5, 9, H(8a3a2a)); double a0 = cv_get_alpha();
    for (int k = 0; k < 3; k++) { double t = fmod(T * 0.5 + k / 3.0, 1); cv_alpha(a0 * 0.5 * (1 - t)); cv_fill(H(e8e4ec)); cv_circle(x + w - 13 + t * 8, base - h - 16 - t * 18, 2 + t * 3); }
    cv_alpha(a0);
  }
  for (int k = 0, n = w > 70 ? 3 : 2; k < n; k++) { int wx = (int)(x + 6 + k * ((w - 18) / (n - 1))); R(wx - 1, base - h + 5, 8, 8, H(f4f0e8)); R(wx, base - h + 6, 6, 6, H(ffd88a)); R(wx + 3, base - h + 6, 1, 6, H(f4f0e8)); }
  R(x + (int)w / 2 - 3, base - 10, 7, 10, H(7a2a2a)); R(x + (int)w / 2 + 2, base - 5, 1, 1, H(ffd21f));
}
static void farTree(double x, int i, const Zone *Z) { (void)Z; cv_fill(FALL[i & 3]); cv_circle(x, HOR - 6 - hash(i, 180) * 10, 10 + hash(i, 181) * 6); }
static void streetHouse(double x, int i, const Zone *Z) {
  (void)Z; int w = 56 + (int)(hash(i, 182) * 30), h = 24 + (int)(hash(i, 183) * 10);
  house(x, HOR + 6, w, h, HOUSE[(int)floor(hash(i, 184) * 6)], ROOF[(int)floor(hash(i, 185) * 3)], hash(i, 186) < 0.6);
  double tx = x + w + 14; R(tx, HOR - 8, 3, 14, H(4a2e1e)); cv_fill(FALL[(i + 1) & 3]); cv_circle(tx + 1, HOR - 14, 9);
}
static void drawSuburb(const Zone *Z, double camX) {
  eachAt(camX, 0.05, 18, farTree, Z);                           // far treetops in autumn colours
  eachAt(camX, 0.14, 120, streetHouse, Z);
  R(0, HOR + 6, LW, 3, H(cfc6b4)); R(0, HOR + 9, LW, 1, H(9a917e));   // the sidewalk
}
// Thanksgiving: Grandson's house waits at your best distance on this map, so beating your best means you made it for dinner.
int draw_best_m = 0;
static void drawGrandson(double camX) {
  double wx = fmax(draw_best_m, 300) * M_PX, x = rnd(wx - camX); if (x < -120 || x > LW + 40) return;
  double top = 1e9; for (int k = -20; k <= 100; k += 20) { roadAt(wx + k); top = fmin(top, RC - RW / 2); }
  double base = rnd(top - 4), w = 76, h = 30;
  house(x, base, w, h, H(f0d890), H(6a2a2a), true);
  R(x + 10, base - h - 26, 1, 10, H(f4f0e8)); R(x + w - 10, base - h - 26, 1, 10, H(f4f0e8));   // balloons on the porch
  cv_fill(H(ff3a4a)); ellipseFill(x + 10, base - h - 29 + sin(T * 2 + x + 10) * 1.5, 3, 4);
  cv_fill(H(3ab0ff)); ellipseFill(x + w - 10, base - h - 29 + sin(T * 2 + x + w - 10) * 1.5, 3, 4);
  R(x + 8, base - h + 15, w - 16, 7, H(c8242e)); R(x + 8, base - h + 15, w - 16, 1, H(ff6a5a));
  cv_text("HI GRANDPA!", x + w / 2, base - h + 18.5, 6, H(ffffff), AL_CENTER, BL_MIDDLE);
  R(x + w + 6, base - 12, 2, 12, H(5a3a2a)); R(x + w + 2, base - 15, 10, 5, H(3a6a9a));   // the mailbox
}
static void drawLeaves(void) {                                  // autumn leaves tumbling across the screen
  for (int i = 0; i < 22; i++) {
    double x = rnd(jmod(jmod(hash(i, 190) * LW - T * (30 + hash(i, 191) * 30), LW) + LW, LW)), y = rnd(jmod(hash(i, 192) * LH + T * (14 + hash(i, 193) * 14) + sin(T * 2 + i) * 6, LH));
    R(x, y, sin(T * 5 + i) > 0 ? 2 : 1, 2, FALL[i & 3]);
  }
}
static void drawBackdrop(int zi, double camX) {
  const Zone *Z = &ZONES[zi]; const Sky *sky = skyOf(zi);
  { double st0[2] = { 0, 1 }; col_t sc[2] = { sky->top, sky->bot }; cv_grad(0, 0, LW, HOR + 2, true, 0, HOR, 2, st0, sc); }
  if (sky->stars) { cv_fill(H(f1ecf7)); for (int i = 0; i < 36; i++) { cv_alpha(0.4 + 0.6 * fabs(sin(T * 1.5 + stars[i][2] * 9))); F(rnd(stars[i][0]), rnd(stars[i][1]), 1, 1); } cv_alpha(1); }
  if (sky->moon) drawMoon();
  if (SEASON == 1) drawBats(camX);
  if (sky->sun) { cv_fill(rgba(255, 225, 140, .85)); cv_circle(330, HOR - 8, 22); cv_fill(rgba(255, 225, 140, .25)); cv_circle(330, HOR - 8, 34); }
  int th = themeKind();
  if (SEASON == 3) drawSuburb(Z, camX);
  else if (th == TH_LIBRARY) drawLibrary(camX);
  else if (th == TH_NEON) drawNeonCity(camX);
  else if (th == TH_PIER) drawPier(camX);
  else if (Z->kind == 3) drawYosemite(Z, camX);
  else if (Z->kind == 4) drawCanyon(Z, camX);
  else if (Z->kind == 0) { peaks(0.1, 22, 42, 40, 70, 90, 0.55, camX, 50, Z->lit, Z->shade); peaks(0.22, 34, 74, 48, 82, 100, 1, camX, 70, Z->lit, Z->shade); }
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
  cv_pattern(PAT_G0, 0, HOR, LW, LH - HOR);
  if (th == TH_LIBRARY) R(0, HOR, LW, 2, rgba(0, 0, 0, .38));   // the shadow under the shelves
  if (Z->kind == 3 && !SEASON) drawMerced(camX);
  { double a0 = cv_get_alpha(), st0[3] = { 0, 0.3, 1 }; col_t hc[3] = { 0xffffff00, Z->haze, 0xffffff00 };
    cv_alpha(th ? a0 * 0.5 : a0); cv_grad(0, HOR - 6, LW, 28, true, HOR - 6, HOR + 22, 3, st0, hc); cv_alpha(a0); }
}
static void drawDecor(int zi, double camX, bool front) {
  const double CH = 90; int c0 = (int)floor(camX / CH) - 1, c1 = (int)floor((camX + LW) / CH) + 1, kind = ZONES[zi].kind;
  bool camps = kind == 3 && !SEASON;
  if (camps) for (int c = (int)floor((camX - 60) / CAMP_W); c <= (int)floor((camX + LW + 30) / CAMP_W); c++) {
    double ox = c * CAMP_W + 40; if (!campAt(ox) || campAt(ox - CAMP_W)) continue;   // a sign where each campground starts
    roadAt(ox); double oy = fmax(HOR + 24, rnd(RC - RW / 2 - 6));
    if ((oy > g.y) == front) campSign(rnd(ox - camX), oy, c);
  }
  for (int c = c0; c <= c1; c++) for (int k = 0; k < 3; k++) {
    int sd = k * 9 + zi * 100;
    double ox = c * CH + hash(c, sd + 1) * CH, f = hash(c, sd + 2), sz = hash(c, sd + 3), ty = hash(c, sd + 4);
    if (ox < 780) continue;                                    // the garage area stays clear
    bool camp = camps && campAt(ox); if (k >= 2 && !camp) continue;   // campgrounds are busier
    if (kind == 3 && !camp && k == 1 && !SEASON) continue;     // the meadow is kept calm: half the props
    double oy = rnd(HOR + 10 + f * (LH - HOR - 16));
    if ((oy > g.y) != front) continue;
    roadAt(ox); if (oy > RC - RW / 2 - 8 && oy < RC + RW / 2 + 8) continue;
    double sx = rnd(ox - camX);
    if (SEASON) drawSeasonDecor(sx, oy, sz, ty);
    else if (kind >= FIRST_THEME) drawThemeDecor(kind, sx, oy, sz, ty);
    else if (kind == 0) {
      if (ty < 0.7) { int h = 10 + (int)(sz * 14); cv_fill(H(4d8f4a)); F(sx, oy - h, 3, h); F(sx - 4, oy - h * 0.7, 4, 2); F(sx - 4, oy - h * 0.7 - 5, 2, 6); F(sx + 3, oy - h * 0.5, 4, 2); F(sx + 5, oy - h * 0.5 - 4, 2, 5); cv_fill(H(7fb867)); F(sx + 1, oy - h, 1, h); }
      else { R(sx - 5, oy - 5, 11, 5, H(8f8a6a)); R(sx - 3, oy - 7, 6, 3, H(b9b48c)); }
    } else if (kind == 1) {
      if (ty < 0.65) { R(sx, oy - 16, 2, 16, H(8a5a3a)); cv_fill(H(2ea57f)); for (int q = 0; q < 6; q++) { double an = q / 6.0 * 6.283 + sz * 3; for (int r = 2; r < 11; r += 2) F(rnd(sx + 1 + cos(an) * r), rnd(oy - 16 + sin(an) * r * 0.55), 2, 2); } }
      else { R(sx, oy - 12, 1, 12, H(f1ecf7)); cv_fill(sz < 0.5 ? H(ff4a4a) : H(ffd21f)); F(sx - 7, oy - 14, 15, 3); F(sx - 5, oy - 16, 11, 2); }
    } else if (camp) drawCampDecor(sx, oy, sz, ty);
    else if (kind == 3) drawYosemiteDecor(sx, oy, sz, ty);
    else if (kind == 4) {
      if (ty < 0.45) { R(sx, oy - 4, 2, 4, H(5a3a22)); cv_fill(H(4a6a3a)); cv_circle(sx + 1, oy - 7, 4 + sz * 2); R(sx - 1, oy - 9, 2, 1, H(6a8a4a)); }   // a juniper
      else if (ty < 0.75) { R(sx - 5, oy - 5, 11, 5, H(a84a2a)); R(sx - 3, oy - 7, 6, 2, H(c8643a)); R(sx - 5, oy - 1, 11, 1, H(7a3020)); }   // a red rock
      else if (ty < 0.9) { for (int q = 0; q < 4; q++) R(sx - 3 + q, oy - 2 - q * 2, 7 - q * 2, 2, q & 1 ? H(c8a888) : H(a88868)); }   // a cairn
      else { for (int q = -2; q <= 2; q++) R(sx + q * 2, oy - 6 + abs(q), 1, 6 - abs(q), H(7a9a5a)); }   // an agave
    } else {
      if (ty < 0.8) { int h = 12 + (int)(sz * 12); cv_fill(H(2c6b4a)); for (int q = 0; q < 3; q++) { double w = 4 + q * 3, yy = oy - h + q * (h / 3.0 - 1); F(sx - w, yy + 2, w * 2 + 1, h / 3.0 + 1); } R(sx - 1, oy - 2, 3, 3, H(1d4d38)); R(sx - 1, oy - h + 1, 3, 2, H(dfe8ff)); }
      else { R(sx - 6, oy - 5, 13, 5, H(5c5a76)); R(sx - 4, oy - 7, 8, 3, H(8d8aa8)); }
    }
  }
}
static void drawRoad(double camX) {
  int th = themeKind(); if (th) { drawThemeRoad(camX, th); return; }
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
      cv_pattern(PAT_G0, l, y, r - l, y + 3 <= 262 ? 3 : 1);
    }
  }
}
static double yEdgeTop(double x, int a) { (void)a; roadAt(x); return EDGE_TOP(); }
static double yEdgeBot(double x, int a) { (void)a; roadAt(x); return EDGE_BOT(); }
static double yRail(double x, int i) { roadAt(x); return railY(railSide(i)); }
static void railLine(double x0, double x1, double camX, YFn yFn, int arg, bool heavy, double postLen, bool cap) {
  int th = themeKind(); if (th) { themeRail(x0, x1, camX, yFn, arg, cap, th); return; }
  double a = fmax(x0, camX - 8), b = fmin(x1, camX + LW + 8); if (b <= a) return;
  double s0 = floor(a / 4) * 4;
  if (SEASON == 3) {                                           // a white picket fence along the lawns; the rail is merged into runs
    double runX = 0, runY = 0; bool run = false; int rh = heavy ? 2 : 1, ph = heavy ? 5 : 4;
    for (double x = s0; x <= b + 4; x += 4) {
      bool end = x > b; double y = end ? 0 : rnd(yFn(x, arg)), sx = rnd(x - camX);
      if (run && (end || y != runY)) { R(runX, runY - 1, sx - runX, 1, H(5a4a3a)); R(runX, runY, sx - runX, rh, H(f4f0e8)); run = false; }
      if (end) break;
      if (!run) { run = true; runX = sx; runY = y; }
      R(sx + 1, y - ph, 2, ph + 3, H(f4f0e8)); R(sx + 2, y - ph + 1, 1, ph + 2, H(c8c0b4));
      if (fmod(x, 32) == 0) R(sx, y - ph - 1, 3, postLen + ph + 1, H(d8d0c4));
    }
  } else if (SEASON != 1) {                                    // the usual rails, merged into runs at the same height
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

/* ---------- the Old Man and his bike: the still parts are the SPR_BIKE sprite, the moving ones are drawn live (look.c) ---------- */
void drawBike(double x, double y, double wheelie, double spin, bool noShadow) {
  x = rnd(x); y = rnd(y);
  cv_save(); cv_translate(x, y); cv_scale(BIKE_SCALE, BIKE_SCALE); cv_translate(-x, -y);
  if (!noShadow) { cv_fill(rgba(0, 0, 0, .28)); F(x - 24, y, 48, 2); F(x - 20, y + 2, 40, 1); }
  if (wheelie > 0.4 && st != ST_CRASH) R(x - 64, y - 1, 46, 1, rgba(30, 20, 50, .25));
  if (spin) { cv_translate(x, y - 14); cv_rotate(spin); cv_translate(-x, -(y - 14)); }
  cv_translate(x - 17, y - 8); cv_rotate(-0.5 * wheelie);
  look_under(); cv_sprite(look_spr()); look_over();
  cv_restore();
}
static void halftone(void) {
  double cx = LW / 2, cy = LH / 2 + 14;
  cv_fill(rgba(10, 6, 18, 1));
  for (int y = 4; y < LH; y += 8) for (int x = ((y / 8) & 1) * 4; x < LW; x += 8) { double s = clampd(hypot(x - cx, y - cy) / 280 * 4.5, 0, 4.5); F(x, y, s, s); }
}
void draw_sprites(void) {
  look_sprite();
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
  for (int y = (int)base; y < LH; y += 4) cv_pattern(PAT_G0, X(690 + (int)(hash(y, 77) * 20)), y, 80, 4);
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
  pat_offset(0); spr_begin(SPR_GARAGE, GAR_W, LH - GAR_Y0, 1, -GAR_X0, -GAR_Y0); garageStatic(); spr_end(); garageDirty = false; stripKind = -1;
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
// particles; the trails' kinds get their own little sprites, everything else is a square
void draw_parts(const Part *ps, int n, double camX) {
  for (int i = 0; i < n; i++) {
    const Part *p = &ps[i];
    double k = clampd(p->life / p->max, 0, 1), x = rnd(p->x - camX), y = rnd(p->y); cv_fill(p->c);
    switch (p->kind) {
    case PK_RING: {                                             // a smoke ring: a thick, squashed loop that widens, tumbles and fades
      double rx = p->s / 2, ry = fmax(1.6, rx * (0.55 + 0.2 * sin(p->life * 5)));
      cv_alpha(fmin(1, k * 1.3)); cv_begin(); cv_ellipse(x, y, rx, ry, 0, 0, 6.2831); cv_stroke(p->c, 1.6);
      cv_alpha(k * 0.35); cv_stroke(p->c, 3); break; }
    case PK_BUBBLE: {                                           // a soap bubble: thin round rim and a white shine
      int s = (int)p->s; double l = x - (s >> 1), t = y - (s >> 1);
      cv_alpha(0.25 + k * 0.6); F(l + 1, t, s - 2, 1); F(l + 1, t + s - 1, s - 2, 1); F(l, t + 1, 1, s - 2); F(l + s - 1, t + 1, 1, s - 2);
      cv_alpha(k * 0.18); F(l + 1, t + 1, s - 2, s - 2);
      cv_alpha(k); R(l + 1, t + 1, 1, 1, H(ffffff)); break; }
    case PK_COIN: {                                             // a spinning coin: its width flips as it turns
      double sp = fabs(cos(T * 9 + p->ph)); int w = (int)fmax(1, rnd(4 * sp)), h = w >> 1, e = w > 2 ? 1 : 0;
      cv_alpha(fmin(1, k * 2.5)); cv_fill(H(a87400)); F(x - h, y - 2, w, 4); F(x - h - e, y - 1, w + e * 2, 2);
      R(x - h, y - 1, w, 2, sp > 0.4 ? H(ffd21f) : H(fff3a8)); if (w > 2) R(x - h, y - 1, 1, 1, H(fff3a8)); break; }
    case PK_BAT:                                                // a bat: body plus wings that flap up and down
      cv_alpha(fmin(1, k * 2)); batShape(x, y, sin(T * 13 + p->ph) * 0.8); R(x, y, 1, 1, H(ff5a3a)); break;
    case PK_FLAKE: {                                            // a snowflake that drifts side to side; big ones are little crosses
      double xx = x + rnd(sin(T * 3 + p->ph) * 1.5);
      cv_alpha(fmin(1, k * 1.6)); if (p->s > 1) { F(xx - 1, y, 3, 1); F(xx, y - 1, 1, 3); } else F(xx, y, 1, 1); break; }
    case PK_GLINT:                                              // glitter: a pixel that flares into a four-point twinkle
      cv_alpha(k); F(x, y, p->s, p->s);
      if (sin(T * 10 + p->ph) > 0.55) { cv_fill(H(ffffff)); F(x - 1, y, 3, 1); F(x, y - 1, 1, 3); cv_alpha(k * 0.6); F(x - 2, y, 1, 1); F(x + 2, y, 1, 1); F(x, y - 2, 1, 1); F(x, y + 2, 1, 1); }
      break;
    case PK_FIRE: {                                             // exhaust fire: white-yellow at the pipe, cooling to red and shrinking
      int s = (int)fmax(1, rnd(p->s * (0.4 + 0.6 * k)));
      cv_alpha(0.3 * k); R(x - (s >> 1) - 1, y - (s >> 1) - 1, s + 2, s + 2, H(ff6a1f));
      cv_alpha(1); R(x - (s >> 1), y - (s >> 1), s, s, k > 0.75 ? H(fff6c8) : k > 0.5 ? H(ffd21f) : k > 0.28 ? H(ff8a1f) : H(e0242e)); break; }
    case PK_SPARK:                                              // a spark: a short streak along its flight
      cv_alpha(fmin(1, k * 2)); F(x, y, p->s + 2, p->s); cv_alpha(fmin(1, k * 2) * 0.4); F(x + p->s + 2, y, 2, 1); break;
    default:
      if (p->round) { cv_alpha(k * 0.85); int s = (int)rnd(p->s); F(x - (s >> 1), y - (s >> 1), s, s); cv_alpha(k * 0.35); F(x - (s >> 1) - 1, y - (s >> 1) - 1, s + 2, s + 2); }
      else { cv_alpha(k); F(x, y, p->w ? p->w : p->s, p->s); }
    }
  }
  cv_alpha(1);
}

void render_game(const char *hint) {
  double camX = g.cam;
  if (camX <= 800) garageCache();
  else if (themeKind() == TH_LIBRARY || themeKind() == TH_NEON) stripCache(themeKind());
  pat_offset(fmod(fmod(g.cam, TILE) + TILE, TILE));
  cv_save();
  if (g.shake > 0.3) cv_translate(rnd((frand() - 0.5) * g.shake), rnd((frand() - 0.5) * g.shake));
  int z0 = zoneAt();
  drawBackdrop(z0, camX);
  drawGarage(camX, false);
  drawRoad(camX);
  if (SEASON == 3) drawGrandson(camX);
  drawDecor(z0, camX, false);
  drawRails(camX, false);
  draw_parts(g.parts, g.nparts, camX);
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
  if (SEASON == 3) drawLeaves();
  cv_restore();
  if (st == ST_PLAY) {
    double pulse = 0.6 + 0.4 * sin(T * 14);
    if (g.bx < 130) { double a = clampd((130 - g.bx) / 130, 0, 1) * 0.6 * pulse, s2[2] = { 0, 1 }; col_t c[2] = { rgba(255, 45, 45, a), rgba(255, 45, 45, 0) }; cv_grad(0, 0, 70, LH, false, 0, 70, 2, s2, c); }
    if (g.bx > LW - 130) { double a = clampd((g.bx - (LW - 130)) / 130, 0, 1) * 0.6 * pulse, s2[2] = { 0, 1 }; col_t c[2] = { rgba(255, 45, 45, a), rgba(255, 45, 45, 0) }; cv_grad(LW - 70, 0, 70, LH, false, LW, LW - 70, 2, s2, c); }
  }
  if (g.flash > 0) R(0, 0, LW, LH, rgba(255, 255, 255, fmin(0.85, g.flash)));
  if (st == ST_PLAY && g.t < 5 && hint) {
    cv_alpha(0.7 + 0.3 * sin(T * 8));
    cv_text(hint, LW / 2 + 1, LH - 11, 11, H(7a1020), AL_CENTER, BL_ALPHA); cv_text(hint, LW / 2, LH - 12, 11, H(fff3d6), AL_CENTER, BL_ALPHA); cv_alpha(1);
  }
}

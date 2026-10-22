// The Old Man Derby's pixel art, ported line by line from web/derby-art.js (see derby_art.h). The numbers, colours and
// shapes are the web game's, so a race looks the same here, in the Race tab and in the replay video.
#include "derby_art.h"
#pragma GCC diagnostic ignored "-Wformat-truncation"   // names are cut to fit on purpose
#include "look.h"
#include "sim.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define H(x) RGB(0x##x)
#define FAR_RAIL 160
#define TRACK_TOP 166
#define TRACK_BOT 236
#define NEAR_RAIL 238
#define K 1.5

static int md(int a, int n) { return ((a % n) + n) % n; }
static double mdf(double a, double n) { return fmod(fmod(a, n) + n, n); }
double da_hash(int a, int b) {
  uint32_t h = (uint32_t)a * 374761393u ^ (uint32_t)b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (h ^ (h >> 16)) / 4294967296.0;
}
#define hash da_hash
static void R(double x, double y, double w, double h, col_t c) { cv_fill(c); cv_rect(rnd(x), rnd(y), rnd(w), rnd(h)); }
static void disc(double x, double y, double r, col_t c) { cv_fill(c); cv_disc(x, y, r); }

/* ---------- the atlases: every horse's poses, and the sky, the stands, the tote board and the font ---------- */
#define NPH 12                         // strides of the gallop drawn per horse; one more cell is the horse standing
#define CW 112                         // a horse's cell, with its feet at (CX, CY)
#define CH 104
#define CX 56
#define CY 96
#define CCOLS 9
static int hAtl = -1, bAtl = -1;       // horses 1024x1024, background 1024x512
static bool building;                   // drawing into an atlas: text is painted, not stamped
static DaHorse cur[DA_NH]; static int ncur;
static DaBoardRow board[DA_NH]; static int nboard; static char boardTxt[DA_NH][2][32], boardOdds[DA_NH][8];
static int hb[DA_NH], toBuild;         // cells built for each horse; background parts done (see da_build)
static bool hFresh = true;              // the horse atlas has never been cleared
static bool bgDone, glyphsDone;
// where things are in the background atlas
#define SKY_X 0
#define SKY_Y 0
#define GLY_X 480
#define GLY_Y 0
#define TOTE_X 480
#define TOTE_Y 8
#define TOTE_W 136
#define TOTE_H 76
#define STAND_TOP 62                    // a stand's picture runs from its roof's pennants down to the lawn
#define STAND_H 88
static const double STAND_AT[2][5][2] = {   // stand 260 wide (272 with its roof) and 220 wide (232): calm, then 4 of the cheer
  { { 0, 140 }, { 272, 140 }, { 544, 140 }, { 0, 228 }, { 272, 228 } },
  { { 544, 228 }, { 776, 228 }, { 0, 316 }, { 232, 316 }, { 464, 316 } } };
static const int STAND_W[2] = { 260, 220 };

/* ---------- a 3x5 pixel font for the tote board, name plates and saddle cloths ---------- */
static const char GCH[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/.:!#-' ";
static const char *const GLYPHS[] = {
  "010101111101101", "110101110101110", "011100100100011", "110101101101110", "111100110100111",
  "111100110100100", "011100101101011", "101101111101101", "111010010010111", "001001001101010",
  "101101110101101", "100100100100111", "101111111101101", "110101101101101", "010101101101010",
  "110101110100100", "010101101110011", "110101110101101", "011100010001110", "111010010010010",
  "101101101101111", "101101101101010", "101101111111101", "101101010101101", "101101010010010",
  "111001010100111", "111101101101111", "010110010010111", "110001010100111", "110001010001110",
  "101101111001001", "111100110001110", "011100111101111", "111001010010010", "111101111101111",
  "111101111001110", "001001010100100", "000000000000010", "000010000010000", "010010010000010",
  "101111101111101", "000000111000000", "010010000000000", "000000000000000" };
static int glyphOf(char ch) {
  if (ch >= 'a' && ch <= 'z') ch -= 32;
  const char *p = strchr(GCH, ch);
  return p && ch ? (int)(p - GCH) : (int)(sizeof GCH - 2);   // anything else is a space
}
void da_text(const char *s, double x, double y, col_t c, int sc) {
  bool stamp = !building && glyphsDone;
  if (!stamp) cv_fill(c);
  for (; *s; s++, x += 4 * sc) {
    int g = glyphOf(*s);
    if (g == (int)(sizeof GCH - 2)) continue;
    if (stamp) { cv_atlas_tint(bAtl, GLY_X + g * 4, GLY_Y, 3, 5, x, y, 3 * sc, 5 * sc, c); continue; }
    for (int i = 0; i < 15; i++) if (GLYPHS[g][i] == '1') cv_rect(x + (i % 3) * sc, y + (i / 3) * sc, sc, sc);
  }
}
int da_textw(const char *s, int sc) { return (int)strlen(s) * 4 * sc - sc; }
#define text da_text
#define textW da_textw

/* ---------- the horses ---------- */
typedef struct { const char *id; col_t base, dark, light; bool points; } CoatC;
static const CoatC COATS[] = {      // base, shadow, highlight, and whether the lower legs are dark ("points")
  { "chestnut", H(b5562a), H(7e3518), H(d9783e), 0 }, { "bay", H(8a4a24), H(5a2c14), H(ad6a3e), 1 },
  { "grey", H(b4b4c2), H(858598), H(dcdce8), 0 }, { "black", H(2e2634), H(17121d), H(4c405a), 0 },
  { "palomino", H(deb058), H(b48632), H(f2d38a), 0 }, { "dun", H(c8a36a), H(987646), H(dfc290), 1 },
  { "roan", H(6e7182), H(4c4e5c), H(9a9cab), 1 }, { "white", H(e9e8f0), H(bdbbcc), H(ffffff), 0 },
  { "appaloosa", H(e4ddd0), H(b3aa98), H(fbf8f0), 0 }, { "dapple", H(9a9aae), H(6c6c80), H(d4d4e2), 0 },
  { "pinto", H(b5562a), H(7e3518), H(d9783e), 0 }, { "tobiano", H(2e2634), H(17121d), H(4c405a), 0 },
  { "buckskin", H(c7913e), H(966624), H(e0b062), 1 }, { "liver", H(5e2a1c), H(3e1a10), H(7e3e2a), 0 },
  { "strawberry", H(b77068), H(8a4c46), H(d9a29a), 0 }, { "grulla", H(8a7f74), H(5f564e), H(ab9f92), 1 } };
static const CoatC *coatC(const char *id) {
  for (size_t i = 0; i < sizeof COATS / sizeof COATS[0]; i++) if (!strcmp(COATS[i].id, id)) return &COATS[i];
  return &COATS[1];   // bay
}
// Paint horses: where the big white patches go, in body coordinates (x along the body, y up from the hooves)
static bool patchPinto(double x, double y) { return (x > -11 && x < 3 && y > -28) || (x < -15 && y > -23); }
static bool patchTobiano(double x, double y) { return (x > -17 && x < -6) || (x > 3 && x < 9 && y > -25); }
static col_t maneC(const char *id) {
  static const struct { const char *id; col_t c; } M[] = { { "flaxen", H(f0dca0) }, { "black", H(1a1418) }, { "white", H(f4f4f8) },
    { "cream", H(f6e8c0) }, { "grey", H(7c7c8e) }, { "chestnut", H(86361a) }, { "brown", H(5a3a24) } };
  for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) if (!strcmp(M[i].id, id)) return M[i].c;
  return H(1a1418);
}
#define HOOF H(2a2226)
#define POINTS H(1d171c)
#define SOCK H(f4f2ec)

// Silks patterns on the saddle cloth (12x7), as a function of the cell: true = second color
static bool silk(const char *p, int x, int y) {
  static const int L1[7] = { 8, 7, 6, 7, 6, 5, 4 }, L2[7] = { 9, 8, 7, 8, 7, 6, 5 };
  if (!strcmp(p, "sun")) return (x - 6) * (x - 6) + (y - 3) * (y - 3) < 7;
  if (!strcmp(p, "cross")) return x == 5 || x == 6 || y == 3;
  if (!strcmp(p, "hoops")) return y % 3 == 1;
  if (!strcmp(p, "diamonds")) return md(x + y, 4) == 0 || md(x - y, 4) == 0;
  if (!strcmp(p, "spots")) return md(x, 4) == 1 && md(y, 3) == 1;
  if (!strcmp(p, "chevrons")) return md(abs(x - 6) + y, 4) == 0;
  if (!strcmp(p, "stripes")) return md(x, 3) == 0;
  if (!strcmp(p, "sash")) return md(x - y * 2, 12) < 3;
  if (!strcmp(p, "checks")) return (x / 3 + y / 2) % 2 == 0;
  if (!strcmp(p, "stars")) return (md(x, 6) == 2 && md(y, 4) != 0) || (md(y, 4) == 2 && md(x, 6) >= 1 && md(x, 6) <= 3);
  if (!strcmp(p, "quarters")) return (x >= 6) != (y >= 4);
  if (!strcmp(p, "moons")) return (md(x, 6) == 1 && (y == 2 || y == 5)) || (md(x, 6) == 2 && (y == 1 || y == 6));
  if (!strcmp(p, "lightning")) return x == L1[y] || x == L2[y];
  return x >= 6;   // halves
}

/* The horse and jockey are designed on a small grid and drawn K times bigger, still with 1px detail: S() scales a
   rectangle about the runner's feet and rounds it to whole screen pixels. */
static void S(double x, double y, double w, double h, col_t c) {
  double x0 = rnd(x * K), y0 = rnd(y * K);
  cv_fill(c); cv_rect(x0, y0, fmax(1, rnd((x + w) * K) - x0), fmax(1, rnd((y + h) * K) - y0));
}
// what colour a stroke paints at each step: a plain colour, a lower leg (white socks, dark points), or the jockey's coat
enum { C_SOLID, C_LOW, C_COAT, C_SLEEVE };
typedef struct { int kind; col_t c; } Paint;
static const DaLook *lk; static bool lkPoints; static const DaRider *rd; static double jyNow;
static col_t paintAt(Paint p, double t, double x, double y) {
  if (p.kind == C_LOW) return !strcmp(lk->marks, "socks") && t > 0.5 ? SOCK : lkPoints ? POINTS : p.c;
  if (p.kind == C_COAT || p.kind == C_SLEEVE) return look_coat_px(rd->coat, (int)rnd(x * K), (int)rnd((y - jyNow) * K) + 28, p.kind == C_SLEEVE);
  return p.c;
}
static Paint P(col_t c) { return (Paint){ C_SOLID, c }; }
// A thick stroke from one point to another, stamped pixel row by pixel row; the paint may vary along it (t = 0..1).
static void stroke(double x0, double y0, double x1, double y1, double w, Paint col) {
  int n = (int)fmax(2, ceil(hypot(x1 - x0, y1 - y0) * K));
  for (int i = 0; i <= n; i++) {
    double t = (double)i / n, x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
    S(x - w / 2, y - w / 2, w, w, paintAt(col, t, x, y));
  }
}

/* The gallop, after Muybridge's plates of Sallie Gardner (1878): see derby-art.js. */
enum { HL, HR, FL, FR };
static const double GAIT[4] = { 0.0, 0.09, 0.31, 0.41 };   // when in the stride each hoof lands
#define STANCE 0.3
typedef struct { double jx, jy, l1, l2, fwd, back, lift, tuck, bend; } Leg;
static const Leg LEG_F = { 10, -17, 9.5, 11, 9, -5.5, 9.5, -4, 1 }, LEG_H = { -12, -18, 10, 11, 4, -12, 8, 3, -1 };
static double ease(double w) { return w * w * (3 - 2 * w); }
typedef struct { double x, y, u; bool planted; } Hoof;
static Hoof hoof(int name, double phase) {
  bool fore = name >= FL; const Leg *L = fore ? &LEG_F : &LEG_H; double u = mdf(phase - GAIT[name], 1);
  if (u < STANCE) return (Hoof){ L->jx + L->fwd + (L->back - L->fwd) * (u / STANCE), 0, u, true };
  double w = (u - STANCE) / (1 - STANCE);
  if (fore) {
    if (w < 0.45) {                                     // folding: hoof flicks up behind the knee and tucks in
      double k = w / 0.45;
      return (Hoof){ L->jx + L->back + (0 - L->back) * ease(k) + L->tuck * sin(M_PI * k), -L->lift * sin(M_PI / 2 * fmin(1, k * 1.6)), u, false };
    }
    if (w < 0.85) {                                     // reaching: the leg unfolds straight out in front, nearly level
      double k = ease((w - 0.45) / 0.4);
      return (Hoof){ L->jx + (L->fwd + 8) * k, -L->lift + (L->lift - 6) * k, u, false };
    }
    double k = (w - 0.85) / 0.15;                       // and comes down to strike the ground
    return (Hoof){ L->jx + L->fwd + 8 - 8 * k, -6 + 6 * k, u, false };
  }
  // a hind hoof stays stretched out behind as it leaves the ground, then gathers under the belly and swings forward
  double x = L->back + (L->fwd - L->back) * ease(fmax(0, (w - 0.3) / 0.7)) - 6 * sin(M_PI * fmin(1, w / 0.6)) + L->tuck * sin(M_PI * w);
  return (Hoof){ L->jx + x, -L->lift * sin(M_PI * w), u, false };
}
typedef struct { double kx, ky, hx, hy, lo; Hoof h; } LegPose;
static LegPose legPose(int name, double by, double phase, bool standing) {
  const Leg *L = name >= FL ? &LEG_F : &LEG_H; double jx = L->jx, jy = by + L->jy;
  Hoof h = standing ? (Hoof){ jx + (name == HL || name == FL ? 1.5 : -1), 0, 0, true } : hoof(name, phase);   // standing square
  double dx = h.x - jx, dy = h.y - jy, d = fmin(hypot(dx, dy), L->l1 + L->l2 - 0.05);
  double th = atan2(dx, dy), a = acos(fmax(-1, fmin(1, (L->l1 * L->l1 + d * d - L->l2 * L->l2) / (2 * L->l1 * d))));
  double up = th + L->bend * a, kx = jx + sin(up) * L->l1, ky = jy + cos(up) * L->l1;
  double lo = atan2(h.x - kx, h.y - ky);
  return (LegPose){ kx, ky, kx + sin(lo) * L->l2, ky + cos(lo) * L->l2, lo, h };
}
static void leg(int name, double by, double phase, bool standing, double w, col_t col, col_t lowCol) {
  const Leg *L = name >= FL ? &LEG_F : &LEG_H; double jx = L->jx, jy = by + L->jy;
  LegPose p = legPose(name, by, phase, standing);
  stroke(jx, jy, p.kx, p.ky, w, P(col));                                              // forearm / gaskin
  S(p.kx - w * 0.35, p.ky - w * 0.35, w * 0.7, w * 0.7, col);                          // the knee or hock
  stroke(p.kx, p.ky, p.hx, p.hy, w * 0.55, (Paint){ C_LOW, lowCol });                  // cannon and pastern
  S(p.hx - 1.3 + (p.h.planted ? 0 : sin(p.lo) * 0.7), p.hy - 1, 2.6, 1.6, HOOF);
}
static double bounce(double phase, bool standing) { return standing ? -1.6 : -1.2 - 0.9 * sin(2 * M_PI * (phase - 0.6)); }

static col_t shade(col_t c) { return shadec(c, 0.75); }
static void jockey(const DaRider *r, double by, double wave) {
  rd = r;
  col_t trim = look_coat_trim(r->coat);
  double jy = rnd(by * 0.4 * K) / K; jyNow = jy;
  col_t legs = H(3a3a4e);
  stroke(-2, jy - 33, 5, jy - 31, 3.4, P(legs));                                     // thigh
  stroke(5, jy - 31, 2.5, jy - 26.5, 2.6, P(legs));                                  // shin
  S(0.5, jy - 27.5, 5, 2, H(1a1420)); S(0, jy - 25.6, 6, 0.8, H(c9c9d6));            // boot in the iron
  stroke(-3.5, jy - 35, 7, jy - 40, 5.4, (Paint){ C_COAT, 0 });                     // back, low and flat
  stroke(-3, jy - 37.6, 6.5, jy - 42.4, 0.8, P(rgba(255, 255, 255, .14)));          // light along his back
  stroke(-4.5, jy - 32.6, 4.5, jy - 37, 1, P(trim));                                 // the hem along his side
  stroke(7, jy - 40, 10.5, jy - 36.5, 2.4, (Paint){ C_SLEEVE, 0 });                 // upper arm
  stroke(10.5, jy - 36.5, 14, jy - 35.5, 2.2, (Paint){ C_SLEEVE, 0 });
  S(14, jy - 36.5, 2, 2, H(f2c9a0));                                                 // hand
  S(16, jy - 36, 4, 0.7, H(3a2416));                                                 // reins
  // head: face, nose, white mustache and sideburn, round specs
  S(8, jy - 48.2, 6.8, 6.6, H(f2c9a0)); S(8, jy - 42.2, 6.8, 0.8, H(d8a882)); S(14.8, jy - 45.8, 1.4, 1.8, H(e0a888));
  S(11.5, jy - 43.2, 4, 1.8, H(f1ecf7)); S(8, jy - 46.5, 1.8, 3, H(f1ecf7));
  S(12, jy - 46.6, 2.2, 2, rgba(190, 230, 255, .8)); S(12, jy - 46.6, 2.2, 0.6, H(14101f)); S(14.2, jy - 46, 0.8, 0.6, H(14101f));
  S(7.5, jy - 42.5, 3, 1.4, trim);                                                   // collar
  double fl = sin(wave * 16) * 0.8;                                                  // scarf end flapping behind him
  S(4.6, jy - 42.4 + fl * 0.4, 3, 1, H(c0242e)); S(2, jy - 42 + fl, 3, 1, H(c0242e)); S(0, jy - 41.6 + fl * 1.4, 2, 1, H(c0242e));
  cv_save(); cv_translate(rnd(11.4 * K) - 15, rnd((jy - 44.9) * K) + 36); look_cap(r->cap); cv_restore();   // a garage cap, on the bike's head
}

/* One horse and its old-man jockey, galloping right, feet on the current origin. wave stands in for the web's clock t
   in the tail, mane and scarf (so each pose of the stride gets its own swish). The shadow is drawn by the caller. */
static void horseBody(const DaHorse *hz, double phase, bool standing, double wave) {
  const DaLook *look = &hz->look; lk = look;
  const CoatC *cc = coatC(look->coat); col_t base = cc->base, dark = cc->dark, light = cc->light; lkPoints = cc->points;
  col_t mane = maneC(look->mane), s1 = look->silk1, s2 = look->silk2;
  const char *marks = look->marks;
  double by = bounce(phase, standing);
  // far legs, a shade darker
  leg(HR, by, phase, standing, 4, dark, dark);
  leg(FR, by, phase, standing, 3.4, dark, dark);
  // tail, streaming back and swishing
  for (int i = 0; i < 13; i++) {
    double w = sin(wave * 9 + i * 0.5) * (i / 4.0);
    S(-20 - i * 1.1, by - 25 + i * 0.55 + w, 2, 2 + (i > 6 ? 1 : 0), i > 9 ? shade(mane) : mane);
  }
  // body: rump, barrel and chest, shaded underneath, one highlight along the back
  static const double BL[3][4] = { { -12, -22, 9, 7 }, { 0, -21, 13, 6 }, { 10, -21, 7, 6.5 } };
  for (int b = 0; b < 3; b++) {
    double cx = BL[b][0], cy = by + BL[b][1], rx = BL[b][2], ry = BL[b][3];
    for (double yy = -ry; yy <= ry; yy += 0.5) {
      double hw = rx * sqrt(fmax(0, 1 - pow(yy / (ry + 0.3), 2)));
      S(cx - hw, cy + yy, hw * 2, 0.7, yy > ry * 0.35 ? dark : yy < -ry * 0.62 ? light : base);
    }
  }
  bool pinto = !strcmp(look->coat, "pinto"), tobiano = !strcmp(look->coat, "tobiano");
  if (pinto || tobiano) for (int b = 0; b < 3; b++) {                                // white over the coat, cell by cell
    double cx = BL[b][0], cy = BL[b][1], rx = BL[b][2], ry = BL[b][3];
    for (double yy = -ry; yy <= ry; yy += 0.5) {
      double hw = rx * sqrt(fmax(0, 1 - pow(yy / (ry + 0.3), 2)));
      for (double xx = ceil(cx - hw); xx < cx + hw; xx++)
        if (pinto ? patchPinto(xx, cy + yy) : patchTobiano(xx, cy + yy)) S(xx, by + cy + yy, 1, 0.7, yy > ry * 0.35 ? H(c9c6d2) : SOCK);
    }
  }
  S(-6, by - 17, 12, 1, dark);                                                       // belly line
  if (!strcmp(look->coat, "dun") || !strcmp(look->coat, "grulla")) S(-16, by - 28.5, 24, 1, !strcmp(look->coat, "grulla") ? H(2e2824) : dark);  // dorsal stripe
  if (!strcmp(look->coat, "dapple")) for (int i = 0; i < 9; i++) {                   // dapples: light rings on the rump and barrel
    double dx = -18 + hash(i, 11) * 26, dy = by - 26 + hash(i, 12) * 7;
    S(dx, dy - 1, 2, 0.6, light); S(dx, dy + 1, 2, 0.6, light); S(dx - 0.8, dy - 0.4, 0.8, 1.4, light); S(dx + 2, dy - 0.4, 0.8, 1.4, light);
  }
  if (!strcmp(look->coat, "strawberry")) for (int i = 0; i < 30; i++) S(-18 + hash(i, 13) * 34, by - 27 + hash(i, 14) * 11, 0.8, 0.8, H(f2dcd6));
  if (!strcmp(look->coat, "grey")) for (int i = 0; i < 16; i++) S(-17 + hash(i, 3) * 32, by - 25 + hash(i, 4) * 6, 1.4, 1, light);
  if (!strcmp(look->coat, "roan")) for (int i = 0; i < 30; i++) S(-18 + hash(i, 5) * 34, by - 26 + hash(i, 6) * 10, 0.8, 0.8, H(d6d8e2));
  if (!strcmp(marks, "spots") || !strcmp(look->coat, "appaloosa")) for (int i = 0; i < 26; i++) S(-21 + hash(i, 7) * 34, by - 27 + hash(i, 8) * 10, 1.6, 1.4, H(3a2a2a));  // leopard spots all over
  // neck up to the head, mane along the top
  stroke(11, by - 22, 18, by - 37, 10, P(base));
  stroke(16, by - 21, 21.5, by - 34, 2.4, P(dark));
  // head: a long face sloping down to the muzzle, cheek, ears, eye, nostril
  stroke(17.5, by - 41, 24, by - 37.5, 6.6, P(base));
  stroke(24, by - 37.5, 29, by - 34.5, 4.6, P(base));
  stroke(18.5, by - 38, 22, by - 36, 3.6, P(dark));                                 // cheek in shadow
  stroke(18, by - 43, 25, by - 40, 1, P(light));                                    // light down the face
  S(28.4, by - 33.4, 1.8, 0.8, dark);                                                // mouth
  S(16.4, by - 47, 1.8, 4, base); S(18.4, by - 46.4, 1.6, 3.4, dark);                 // ears
  for (int i = 0; i < 11; i++) {
    double tt = i / 10.0, fl = sin(wave * 14 + i) * 0.8;
    S(6.5 + tt * 10 - 2 + fl - (1 - tt) * 2.5, by - 27.5 - tt * 13, 3.2, 2.2, mane);
  }
  S(15, by - 44.5, 3.6, 3.4, mane);                                                  // forelock
  if (!strcmp(marks, "blaze")) stroke(21, by - 42, 28, by - 36, 1.8, P(SOCK));
  if (!strcmp(marks, "stripe")) stroke(22.5, by - 40, 27, by - 36.5, 0.9, P(SOCK));
  if (!strcmp(marks, "star")) S(20.6, by - 43, 2, 2, SOCK);
  if (!strcmp(marks, "snip")) S(28, by - 36.5, 1.6, 1.2, SOCK);
  S(20, by - 41.4, 1.6, 1.4, H(120c10)); S(20, by - 41.4, 0.7, 0.7, H(ffffff)); S(29.2, by - 36.2, 0.9, 0.9, H(120c10));
  // bridle and reins
  stroke(19.6, by - 44, 21, by - 36, 0.8, P(H(3a2416))); stroke(21, by - 36.4, 28, by - 35.4, 0.8, P(H(3a2416))); S(27.6, by - 36, 1, 1.4, H(c9c9d6));
  // saddle cloth in the silks, with the number on it
  for (int yy = 0; yy < 7; yy++) for (int xx = 0; xx < 12; xx++) S(-8 + xx, by - 29 + yy, 1, 1, silk(look->pattern, xx, yy) ? s2 : s1);
  S(-8, by - 23, 12, 1, rgba(0, 0, 0, .25));
  if (hz->num) { char n[4]; snprintf(n, sizeof n, "%d", hz->num); S(-7, by - 28, 5, 5, H(f4f2ec)); text(n, rnd(-6.2 * K), rnd((by - 27.2) * K), H(14101f), 1); }
  S(-5, by - 31, 12, 2, H(3a2416)); S(-5, by - 31, 12, 0.8, H(6a4a2e));               // saddle
  // near legs, in front of the body
  leg(HL, by, phase, standing, 4.6, base, base);
  leg(FL, by, phase, standing, 3.8, base, base);
  jockey(&hz->rider, by, wave);
}
static void shadow(void) {
  cv_fill(rgba(40, 20, 10, .28)); cv_rect(rnd(-21 * K), 1, rnd(44 * K), 2); cv_rect(rnd(-16 * K), 3, rnd(34 * K), 1);
}
static int cellOf(int h, double phase, bool standing) { return h * (NPH + 1) + (standing ? NPH : (int)floor(mdf(phase, 1) * NPH + 0.5) % NPH); }
static double phaseOfCell(int f) { return (double)f / NPH; }

/* ---------- the track ---------- */
static const col_t SKY[7] = { H(4f7fd0), H(6290d8), H(7aa3dc), H(98b8de), H(b9cddc), H(dcd8c4), H(f2d9a8) };
#define FINISH 3400
static double finish = FINISH;
static void skyStatic(void) {
  const int bandH = 18;
  for (int i = 0; i < 7; i++) {
    R(0, i * bandH, LW, bandH, SKY[i]);
    if (i) for (int x = 0; x < LW; x += 2) R(x + (i & 1), i * bandH, 1, 1, SKY[i - 1]);   // dithered seam
  }
  R(0, 7 * bandH, LW, 140 - 7 * bandH, SKY[6]);
  // the afternoon sun, high and to the right, with a soft ring
  disc(392, 34, 18, rgba(255, 248, 220, .35)); disc(392, 34, 10, H(fff6d8));
}
static void sky(const DaScene *s) {
  if (bgDone) cv_atlas(bAtl, SKY_X, SKY_Y, LW, 140, 0, 0); else skyStatic();
  // clouds drift by slowly
  for (int i = 0; i < 6; i++) {
    double w = 40 + hash(i, 1) * 50, cx = mdf(hash(i, 2) * 900 - s->camX * 0.04 - s->t * 3, LW + 160) - 80, cy = 14 + hash(i, 3) * 50;
    cv_fill(H(f6f4fa));
    for (int j = 0; j < 5; j++) { double bx = cx + j * w / 5, r = 5 + hash(i, j + 9) * 6; cv_begin(); cv_arc(rnd(bx), rnd(cy), r, M_PI, 2 * M_PI); cv_fillpath(); }
    R(cx - 5, cy, w + 8, 2, H(f6f4fa)); R(cx - 3, cy + 2, w + 4, 1, H(d6dcec));
  }
}
static void hills(double camX, double par, double base, double amp, col_t col, double seed) {
  cv_fill(col);
  for (int x = 0; x < LW; x += 2) {
    double wx = x + camX * par, h = sin(wx * 0.008 + seed) * amp + sin(wx * 0.021 + seed * 2) * amp * 0.45;
    cv_rect(x, rnd(base - h), 2, rnd(160 - base + h));
  }
}
static void trees(double camX) {
  double par = 0.32, off = camX * par;
  for (int k = (int)floor(off / 14) - 2; k < (int)floor((off + LW) / 14) + 2; k++) {
    double x = k * 14 - off + hash(k, 21) * 8, h = 10 + hash(k, 22) * 14; bool poplar = hash(k, 23) > 0.8;
    col_t c = hash(k, 24) > 0.5 ? H(3f6a48) : H(35603f);
    if (poplar) { R(x + 3, 150 - h - 12, 6, h + 12, H(2f5a3a)); R(x + 4, 150 - h - 14, 4, 2, H(2f5a3a)); }
    else { disc(rnd(x + 7), rnd(150 - h), 8 + hash(k, 25) * 4, c); R(x + 6, 150 - h, 2, h, H(4a3428)); }
  }
  R(0, 146, LW, 6, H(46704c));
}
static const col_t CROWD[10] = { H(e0242e), H(ffd21f), H(3a86ff), H(f1ecf7), H(55a630), H(ff7f11), H(6a2c70), H(2a2430), H(f2c9a0), H(8a5a2e) };
static void standDraw(double x0, int w, double t, int seed, bool cheer) {
  const double top = 74, roofH = 8, base = 150;
  // roof: a long cantilever with a white fascia and pennants flying along it
  R(x0 - 6, top, w + 12, roofH, H(2c4a66)); R(x0 - 6, top + roofH - 2, w + 12, 2, H(f1ecf7)); R(x0 - 6, top, w + 12, 1, H(3e6488));
  for (int i = 0; i < w; i += 30) {
    R(x0 + i, top - 12, 1, 12, H(c9c9d6));
    double fl = sin(t * 6 + i) * 1.5; col_t fc = CROWD[(i / 30 + seed) % 6];
    R(x0 + i + 1, top - 12, 6, 2, fc); R(x0 + i + 1 + 6, top - 11 + fl * 0.3, 3, 1, fc);
  }
  // shade under the roof, then the tiers of seats packed with people
  R(x0, top + roofH, w, base - top - roofH, H(3a3a52));
  for (int tier = 0; tier < 3; tier++) {
    double ty = top + roofH + 6 + tier * 18;
    R(x0, ty + 14, w, 4, tier == 2 ? H(e8e0cc) : H(c8c0ac));                         // tier wall
    for (int row = 0; row < 3; row++) {
      double ry = ty + row * 5;
      R(x0, ry + 4, w, 1, H(5a5a74));
      for (int i = 2; i < w - 2; i += 3) {
        int k = i + row * 7 + tier * 31 + seed * 97; col_t col = CROWD[(int)floor(hash(k, 1) * 10)];
        double jump = cheer && hash(k, 4) > 0.55 ? fmax(0, sin(t * 9 + hash(k, 5) * 6)) * 2 : 0;
        R(x0 + i, ry + 1 - jump, 2, 3, col);                                          // body
        R(x0 + i, ry - 1 - jump, 2, 2, hash(k, 2) > 0.85 ? H(f1ecf7) : hash(k, 3) > 0.5 ? H(f2c9a0) : H(c69064));  // head, the odd white hair
        if (jump > 1.2 && hash(k, 6) > 0.6) R(x0 + i + (hash(k, 7) > 0.5 ? 2 : -1), ry - 3 - jump, 1, 2, H(f2c9a0));  // an arm in the air
      }
    }
  }
  // columns and the ground-floor arches
  for (int i = 0; i <= w; i += 40) R(x0 + i - 1, top + roofH, 3, base - top - roofH, H(e8e0cc));
  R(x0, base - 12, w, 12, H(e8e0cc));
  for (int i = 8; i < w - 8; i += 20) { R(x0 + i, base - 9, 10, 9, H(3a3040)); R(x0 + i + 1, base - 10, 8, 1, H(3a3040)); }
}
#define CHEER_T(j) ((j) * (2 * M_PI / 9) / 4)          // the 4 cheering frames, a quarter of the crowd's bounce apart
static void stand(int which, double x0, double t, bool cheer) {
  int w = STAND_W[which];
  if (x0 + w + 6 < 0 || x0 - 6 > LW) return;
  if (!bgDone) { standDraw(x0, w, t, which + 1, cheer); return; }
  int f = cheer ? 1 + (int)floor(mdf(t * 9 / (2 * M_PI), 1) * 4) % 4 : 0;
  cv_atlas(bAtl, STAND_AT[which][f][0], STAND_AT[which][f][1], w + 12, STAND_H, rnd(x0) - 6, STAND_TOP);
}
static void clockTower(double x) {
  R(x, 52, 24, 98, H(e8e0cc)); R(x + 2, 52, 2, 98, H(d4ccb6)); R(x - 2, 46, 28, 6, H(2c4a66)); R(x + 6, 36, 12, 10, H(2c4a66)); R(x + 11, 28, 2, 8, H(c9c9d6));
  disc(x + 12, 66, 7, H(f6f4fa));
  cv_begin(); cv_move(x + 12, 66); cv_line(x + 12, 61); cv_move(x + 12, 66); cv_line(x + 16, 66); cv_stroke(H(14101f), 1);   // 3 o'clock
  R(x + 5, 80, 14, 3, H(2c4a66)); text("OMD", x + 7, 87, H(2c4a66), 1);
}
#define TOTE_W0 132
#define TOTE_Y0 76
static void toteDraw(double x, double y0) {   // the infield tote board: amber bulbs on black, the odds for the race (y0: where y = 76 lands)
  const double w = TOTE_W0, h = 66, y = y0, base = y0 + (150 - TOTE_Y0);
  R(x + 14, y + h, 4, base - y - h, H(5a5a74)); R(x + w - 18, y + h, 4, base - y - h, H(5a5a74));
  R(x - 2, y - 2, w + 4, h + 4, H(c9c9d6)); R(x, y, w, h, H(14101a));
  text("OLD MAN DERBY", x + 6, y + 4, H(ffd21f), 1);
  R(x + 4, y + 11, w - 8, 1, H(3a2a10));
  for (int i = 0; i < nboard; i++) {
    double yy = y + 15 + i * 8;
    text(boardTxt[i][0], x + 6, yy, H(ff8a1f), 1); text(boardTxt[i][1], x + 14, yy, H(ffd21f), 1);
    text(board[i].odds, x + w - 6 - textW(board[i].odds, 1), yy, H(ffd21f), 1);
  }
}
static void tote(double x, const DaScene *s) {
  if (x + TOTE_W0 + 2 < 0 || x - 2 > LW) return;
  if (!bgDone) toteDraw(x, TOTE_Y0);
  else cv_atlas(bAtl, TOTE_X, TOTE_Y, TOTE_W, TOTE_H, rnd(x) - 2, TOTE_Y0 - 2);
  if ((int)floor(s->t * 3) % 2) for (int i = 0; i < s->nb && i < nboard; i++) if (s->board[i].hot) {   // your pick blinks
    double yy = TOTE_Y0 + 15 + i * 8;
    R(x + 14, yy, textW(boardTxt[i][1], 1), 5, H(14101a)); text(boardTxt[i][1], x + 14, yy, H(fff2b0), 1);
  }
}
static void farRail(double camX) {
  double par = 0.86, off = camX * par;
  R(0, FAR_RAIL - 1, LW, 2, H(f6f4fa)); R(0, FAR_RAIL + 1, LW, 1, H(b8b6c8));
  for (int k = (int)floor(off / 12) - 1; k < (int)floor((off + LW) / 12) + 2; k++) {
    double x = k * 12 - off; R(x, FAR_RAIL, 1, 6, H(e8e6f0));
    if (md(k, 8) == 0) {                                                               // a lamp post every 8 posts, lit
      R(x, FAR_RAIL - 22, 1, 22, H(5a5a74)); R(x - 2, FAR_RAIL - 24, 5, 2, H(5a5a74));
      disc(rnd(x + 0.5), FAR_RAIL - 21, 4, rgba(255, 230, 150, .35));
      R(x - 1, FAR_RAIL - 22, 3, 2, H(fff2b0));
    }
  }
  // furlong poles: red and white, counting down to the line
  for (int f = 1; f <= 8; f++) {
    double wx = finish - f * finish / 8, x = wx * par - off;
    if (x < -10 || x > LW + 10) continue;
    for (int i = 0; i < 6; i++) R(x, FAR_RAIL - 30 + i * 5, 2, 5, i & 1 ? H(f6f4fa) : H(e0242e));
    char n[4]; snprintf(n, sizeof n, "%d", f);
    R(x - 4, FAR_RAIL - 38, 10, 8, H(f6f4fa)); text(n, x - 1, FAR_RAIL - 36, H(e0242e), 1);
  }
  // the winning post with its red disc
  double fx = finish * par - off;
  if (fx > -20 && fx < LW + 20) {
    R(fx, FAR_RAIL - 46, 2, 46, H(f6f4fa)); disc(rnd(fx + 1), FAR_RAIL - 48, 6, H(e0242e)); disc(rnd(fx + 1), FAR_RAIL - 48, 3, H(f6f4fa));
  }
}
static const col_t DIRT[8] = { H(9a6440), H(a46c46), H(ac744c), H(b07a50), H(b27c52), H(ae784e), H(a8724a), H(a06a44) };
static void track(double camX) {
  // every row of dirt scrolls at its own speed: the far side slower, the near side faster, for depth
  for (int y = TRACK_TOP; y < TRACK_BOT; y++) {
    double f = (double)(y - TRACK_TOP) / (TRACK_BOT - TRACK_TOP), par = 0.88 + f * 0.3, off = camX * par;
    R(0, y, LW, 1, DIRT[(int)fmin(7, floor(f * 8))]);
    const int step = 9; int k0 = (int)floor(off / step);
    for (int k = k0; k < k0 + LW / step + 2; k++) {
      double hv = hash(k, y);
      if (hv < 0.35) R(k * step - off + hash(k, y + 999) * step, y, 1 + (hv < 0.1 ? 2 : 0), 1, hv < 0.18 ? H(8a5636) : H(c08a5e));
    }
    if (y % 6 == 0) { double ox = mdf(-off, 40); for (double x = ox - 40; x < LW; x += 40) R(x, y, 18, 1, rgba(120, 70, 40, .18)); }  // harrow lines
    double fx = finish * par - off;                                                    // the finish line, chalked across
    if (fx > -4 && fx < LW + 4) R(fx, y, 2, 1, H(f6f4fa));
  }
  R(0, TRACK_TOP, LW, 2, rgba(60, 40, 30, .35));
}
static void nearRail(double camX) {
  double par = 1.22, off = camX * par;
  R(0, NEAR_RAIL - 2, LW, 14, H(5e8a4a));                                              // the grass verge
  R(0, NEAR_RAIL + 1, LW, 3, H(f6f4fa)); R(0, NEAR_RAIL + 4, LW, 1, H(b8b6c8));
  for (int k = (int)floor(off / 20) - 1; k < (int)floor((off + LW) / 20) + 2; k++) { double x = k * 20 - off; R(x, NEAR_RAIL + 1, 2, 10, H(e8e6f0)); R(x + 2, NEAR_RAIL + 5, 1, 6, H(b8b6c8)); }
  double fx = finish * par - off;
  if (fx > -20 && fx < LW + 20) { R(fx, NEAR_RAIL - 26, 3, 36, H(f6f4fa)); disc(rnd(fx + 1.5), NEAR_RAIL - 28, 7, H(e0242e)); }
}
static void hedge(double camX) {
  double par = 1.45, off = camX * par;
  static const col_t FL[4] = { H(e0242e), H(ffd21f), H(f6f4fa), H(ff70a6) };
  R(0, 250, LW, 20, H(2f5a34));
  for (int k = (int)floor(off / 6) - 1; k < (int)floor((off + LW) / 6) + 2; k++) {
    double x = k * 6 - off; disc(rnd(x), 251, 4 + hash(k, 32) * 3, hash(k, 31) > 0.5 ? H(3a6e3e) : H(2a5230));
    if (hash(k, 33) > 0.55) R(x + hash(k, 34) * 4, 248 + hash(k, 35) * 10, 2, 2, FL[(int)floor(hash(k, 36) * 4)]);
  }
}

/* ---------- dust kicked up by the hooves ---------- */
#define NDUST 240
static struct { double x, y, vx, vy, life, age; } dust[NDUST]; static int ndust;
static void kick(double x, double y) {
  if (ndust == NDUST) return;
  dust[ndust].x = x + (frand() - 0.5) * 3; dust[ndust].y = y; dust[ndust].vx = -30 - frand() * 40; dust[ndust].vy = -10 - frand() * 18;
  dust[ndust].life = 0.5 + frand() * 0.4; dust[ndust].age = 0; ndust++;
}
static void drawDust(double dt) {
  for (int i = ndust - 1; i >= 0; i--) {
    dust[i].age += dt; if (dust[i].age > dust[i].life) { dust[i] = dust[--ndust]; continue; }
    dust[i].x += dust[i].vx * dt; dust[i].y += dust[i].vy * dt; dust[i].vy += 60 * dt;
    double a = 1 - dust[i].age / dust[i].life; int sz = a > 0.5 ? 2 : 1;
    cv_fill(rgba(196, 150, 108, 0.7 * a)); cv_rect(rnd(dust[i].x), rnd(dust[i].y), sz, sz);
  }
}

/* A name plate floating over a runner: number in its silks, name hand-lettered in white. */
static void upper(const char *s, char *out, int n) { int i = 0; for (; s[i] && i < n - 1; i++) out[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]; out[i] = 0; }
static void plate(double x, double y, const DaHorse *h) {
  char name[32], n[4]; upper(h->name, name, sizeof name); snprintf(n, sizeof n, "%d", h->num);
  double w = textW(name, 1) + 14;
  R(x - w / 2, y, w, 9, rgba(20, 16, 31, .82)); R(x - w / 2, y, 9, 9, h->look.silk1); text(n, x - w / 2 + 3, y + 2, h->look.silk2, 1);
  text(name, x - w / 2 + 11, y + 2, H(f6f4fa), 1); R(x - 1, y + 9, 3, 2, rgba(20, 16, 31, .82));
}

/* The starting gate, side on, at screen x (the line of its front doors): see derby-art.js. open: 0 shut .. 1 swung wide. */
#define GATE_GREEN H(2f7a4a)
#define GATE_DARK H(1f5232)
#define GATE_LIGHT H(4fae6e)
static void gate(double x, double open, bool front) {
  double x0 = x - 70, w = 70;
  if (x0 - 6 > LW || x + 12 < 0) return;
  if (!front) {                                                                        // the far side, behind the horses
    R(x0, 100, 3, 128, GATE_DARK); R(x - 3, 100, 3, 128, GATE_DARK);
    for (int i = 0; i < 4; i++) R(x0, 118 + i * 18, w, 1, GATE_DARK);
    return;
  }
  R(x0 - 3, 92, 4, 140, GATE_DARK); R(x - 1, 92, 4, 140, GATE_DARK);                   // the end posts
  R(x0 - 6, 90, w + 12, 12, GATE_GREEN); R(x0 - 6, 90, w + 12, 2, GATE_LIGHT); R(x0 - 6, 101, w + 12, 1, GATE_DARK);
  text("OLD MAN DERBY", x0 + 9, 94, H(f6f4fa), 1);
  for (double bx = x0 + 6; bx < x - 2; bx += 8) R(bx, 102, 1, 72, rgba(31, 82, 50, .85));   // the cage bars
  R(x0, 136, w, 2, GATE_GREEN); R(x0, 136, w, 1, GATE_LIGHT);                         // the side rail
  R(x0, 174, w, 30, GATE_GREEN); R(x0, 174, w, 1, GATE_LIGHT); R(x0, 203, w, 1, GATE_DARK);  // the kick panel
  for (int i = 0; i < 6; i++) {                                                       // stall numbers on the panel
    char n[4]; snprintf(n, sizeof n, "%d", i + 1);
    R(x0 + 8 + i * 9, 180, 7, 9, H(f6f4fa)); text(n, x0 + 10 + i * 9, 182, H(14101f), 1);
  }
  for (double bx = x0 + 2; bx < x - 4; bx += 14) R(bx, 192, 9, 1, GATE_DARK);
  double d = rnd(10 * (1 - open));                                                    // the front doors fold back as they open
  if (d > 0) { R(x - 1, 104, d, 98, GATE_GREEN); R(x - 1, 104, d, 1, GATE_LIGHT); if (d > 3) R(x, 120, d - 2, 1, H(f6f4fa)); }
}
/* Your horse's marker, drawn over everything for the whole race: a gold flag that bobs over it, and when the camera has
   left it behind (or it has run out ahead), a flag pinned to the edge of the screen pointing at it. */
static void pickTag(const DaRunner *r, double t) {
  const char *str = r->tag ? r->tag : "YOU"; double w = textW(str, 1) + 8, bob = rnd(sin(t * 5));
  double hx = r->x + 14; int off = hx < 4 ? -1 : hx > LW - 4 ? 1 : 0;
  double x = fmax(w / 2 + 8, fmin(LW - w / 2 - 8, hx)), y = fmax(20, r->y - 100) + bob;
  R(x - w / 2 - 1, y - 1, w + 2, 12, H(14101f)); R(x - w / 2, y, w, 10, H(ffd21f));
  text(str, x - w / 2 + 4, y + 2, H(14101f), 1);
  if (!off) { R(hx - 2, y + 10, 5, 2, H(ffd21f)); R(hx - 1, y + 12, 3, 1, H(ffd21f)); }
  else for (int i = 0; i < 4; i++) R(off < 0 ? x - w / 2 - 3 - i : x + w / 2 + 2 + i, y + 1 + i, 1, 8 - i * 2, H(ffd21f));  // points at it, off the edge
}
/* A little flag over a runner: OLD MAN'S BET. */
static void tag(double x, double y, const char *str, col_t col) {
  double w = textW(str, 1) + 6;
  R(x - w / 2, y, w, 9, col); text(str, x - w / 2 + 3, y + 2, H(14101f), 1); R(x - 1, y + 9, 3, 2, col);
}

/* ---------- building the atlases, a few pictures a frame ---------- */
static void glyphs(void) { for (int g = 0; g < (int)sizeof GCH - 1; g++) { char s[2] = { GCH[g], 0 }; text(s, GLY_X + g * 4, GLY_Y, H(ffffff), 1); } }
void da_set(const DaHorse *hs, int n, const DaBoardRow *b, int nb) {
  if (n > DA_NH) n = DA_NH;
  if (nb > DA_NH) nb = DA_NH;
  for (int i = 0; i < n; i++) if (i >= ncur || memcmp(&hs[i], &cur[i], sizeof *hs)) { cur[i] = hs[i]; hb[i] = 0; }   // only a changed horse is drawn again
  ncur = n;
  bool sameBoard = nb == nboard;
  char up[32], name[DA_NH][32];
  for (int i = 0; i < nb; i++) { upper(b[i].name, up, sizeof up); snprintf(name[i], sizeof name[i], "%.14s", up); }   // as the tote shows it
  for (int i = 0; sameBoard && i < nb; i++) sameBoard = !strcmp(name[i], boardTxt[i][1]) && !strcmp(b[i].odds, boardOdds[i]) && b[i].num == board[i].num;
  if (!sameBoard) {
    nboard = nb;
    for (int i = 0; i < nb; i++) {
      board[i] = b[i];
      snprintf(boardTxt[i][0], sizeof boardTxt[i][0], "%d", b[i].num);
      snprintf(boardTxt[i][1], sizeof boardTxt[i][1], "%s", name[i]);
      snprintf(boardOdds[i], sizeof boardOdds[i], "%s", b[i].odds);
      board[i].name = boardTxt[i][1]; board[i].odds = boardOdds[i];
    }
    toBuild = 0; bgDone = false;     // the tote board is in the background atlas: draw it again (and everything else is cheap to redo)
  }
}
static int nextHorse(void) { for (int i = 0; i < ncur; i++) if (hb[i] < NPH + 1) return i; return -1; }
bool da_ready(void) { return nextHorse() < 0 && bgDone; }
#define CELLS_A_FRAME 5
bool da_build(void) {
  if (hAtl < 0) hAtl = atl_new(1024, 1024);
  if (bAtl < 0) bAtl = atl_new(1024, 512);
  building = true;
  if (!bgDone && atl_ok(bAtl)) {                    // the background: the font, the sky, the tote, then each stand frame
    if (atl_begin(bAtl, toBuild == 0)) {
      if (toBuild == 0) { glyphs(); skyStatic(); toteDraw(TOTE_X + 2, TOTE_Y + 2); }
      else {
        int which = (toBuild - 1) / 5, f = (toBuild - 1) % 5;
        standDraw(STAND_AT[which][f][0] + 6, STAND_W[which], f ? CHEER_T(f - 1) : 0, which + 1, f > 0);
      }
      atl_end();
      if (++toBuild == 11) { bgDone = true; glyphsDone = true; }
    }
  }
  if (nextHorse() >= 0 && atl_ok(hAtl) && atl_begin(hAtl, hFresh)) {
    hFresh = false;
    for (int k = 0, h; k < CELLS_A_FRAME && (h = nextHorse()) >= 0; k++) {
      int f = hb[h], c = h * (NPH + 1) + f;
      atl_clear_rect((c % CCOLS) * CW, (c / CCOLS) * CH, CW, CH);
      cv_save(); cv_translate((c % CCOLS) * CW + CX, (c / CCOLS) * CH + CY);
      double ph = f < NPH ? phaseOfCell(f) : 0;
      horseBody(&cur[h], ph, f == NPH, f < NPH ? ph * 2 * M_PI / 9 : 0);
      cv_restore();
      hb[h]++;
    }
    atl_end();
  }
  building = false;
  return da_ready();
}

/* One frame: s = {t, dt, camX, runners, board, cheer, gate} as scene() in derby-art.js */
static void runner(const DaRunner *r, double t, double dt) {
  if (r->mine) { R(r->x - 34, r->y, 70, 3, rgba(255, 210, 31, .75)); R(r->x - 28, r->y + 3, 58, 1, rgba(255, 210, 31, .5)); }  // your horse's spotlight
  double x = rnd(r->x), y = rnd(r->y);
  if (x < -70 || x > LW + 70) return;
  cv_save(); cv_translate(x, y); shadow(); cv_restore();
  int c = cellOf(r->h, r->phase, r->standing); double ph = r->standing ? 0 : phaseOfCell(c % (NPH + 1));
  if (atl_ok(hAtl) && hb[r->h] > c % (NPH + 1)) cv_atlas(hAtl, (c % CCOLS) * CW, (c / CCOLS) * CH, CW, CH, x - CX, y - CY);
  else { cv_save(); cv_translate(x, y); horseBody(&cur[r->h], ph, r->standing, ph * 2 * M_PI / 9); cv_restore(); }
  if (dt > 0 && !r->standing) {                                                         // dirt thrown back off the toe
    double by = bounce(ph, false);
    for (int n = HL; n <= FL; n += 2) {
      LegPose p = legPose(n, by, ph, false);
      if (p.h.planted && p.h.u > 0.18 && frand() < 0.6) kick(p.hx * K + x, r->y);
    }
  }
}
void da_scene(const DaScene *s) {
  finish = s->finish > 0 ? s->finish : FINISH;
  sky(s);
  hills(s->camX, 0.06, 112, 10, H(8aa6cc), 1);
  hills(s->camX, 0.14, 124, 8, H(6f9486), 4);
  trees(s->camX);
  // grandstands, the clock tower and the tote board repeat down the straight
  double par = 0.6, off = s->camX * par, span = 900;
  for (int k = (int)floor(off / span) - 1; k <= (int)floor((off + LW) / span) + 1; k++) {
    double x = k * span - off;
    stand(0, x, s->t, s->cheer); if (x + 268 < LW && x + 292 > 0) clockTower(x + 268); stand(1, x + 300, s->t, s->cheer);
    tote(x + 560 + 40, s);
    if (x + 740 < LW && x + 784 > 0) { R(x + 760, 120, 4, 30, H(5a5a74)); R(x + 740, 104, 44, 18, H(e8e0cc)); text("3PM", x + 752, 110, H(e0242e), 1); }   // a little sign
  }
  R(0, 150, LW, FAR_RAIL - 151, H(5e8a4a));                                            // the infield lawn
  farRail(s->camX);
  track(s->camX);
  int ord[DA_NH], n = s->nr; for (int i = 0; i < n; i++) ord[i] = i;
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && s->r[ord[j]].y < s->r[ord[j - 1]].y; j--) { int tmp = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = tmp; }
  if (s->hasGate) gate(s->gate - s->camX, s->gateOpen, false);
  for (int i = 0; i < n; i++) runner(&s->r[ord[i]], s->t, s->dt);
  drawDust(s->dt);
  if (s->hasGate) gate(s->gate - s->camX, s->gateOpen, true);
  for (int i = 0; i < n; i++) if (s->r[ord[i]].plate) plate(s->r[ord[i]].x + 6, s->r[ord[i]].y - 92, &cur[s->r[ord[i]].h]);
  for (int i = 0; i < n; i++) { const DaRunner *r = &s->r[ord[i]]; if (r->tag && !r->mine) tag(r->x + 14, r->y - 96, r->tag, r->tagCol); }
  for (int i = 0; i < n; i++) if (s->r[ord[i]].mine) pickTag(&s->r[ord[i]], s->t);
  nearRail(s->camX);
  hedge(s->camX);
}

// Where a horse is (0 = the gate, 1 = the line) t seconds after the gates open: the same curve as server/derby.py progress().
double da_progress(const DaHorse *h, double t) {
  double u = t / h->T;
  if (u >= 1) {        // past the line: no jolt at the post, its speed eases back to its average pace
    double d1 = 1; for (int k = 0; k < DA_NA; k++) d1 += h->a[k] * (k + 1) * M_PI * cos((k + 1) * M_PI);
    return u + (d1 - 1) * 0.05 * (1 - exp(-(u - 1) / 0.05));
  }
  if (u <= 0) return 0;
  double p = u; for (int k = 0; k < DA_NA; k++) p += h->a[k] * sin((k + 1) * M_PI * u);
  return p;
}
static const char *const CAPS[] = { "", "straw", "hardhat", "nightcap", "cowboy", "tophat", "wizard", "chef", "party", "viking", "propeller",
  "cone", "laurel", "pumpkin", "santa", "bow", "helmet", "tricorn", "halo", "crown", "redcap" };
void da_outfit(int raceId, int num, DaRider *out) {   // the same picks as outfit() in derby-art.js, so every screen dresses him alike
  int nc = look_coat_n(), ncap = sizeof CAPS / sizeof CAPS[0];
  snprintf(out->coat, sizeof out->coat, "%s", look_coat_id((int)floor(hash(raceId, num * 7) * nc)));
  snprintf(out->cap, sizeof out->cap, "%s", hash(raceId, num * 11) < 0.3 ? "" : CAPS[(int)floor(hash(raceId, num * 13) * ncap)]);
}

/* ---------- one race as the TV shows it (raceShow() in derby-art.js) ---------- */
#define STRIDE 100      // world pixels per stride, so hooves never skate
#define PACK 230        // how wide the field is drawn, at most, before the home stretch
#define CAM_W 3         // the camera spring's stiffness (per second)
void da_show_start(DaShow *v, const DaHorse *hs, int n, int mine, int oldMan, int winner, int second) {
  memset(v, 0, sizeof *v);
  v->hs = hs; v->n = n; v->mine = mine; v->oldMan = oldMan; v->winner = winner; v->second = second;
  v->cam = -300; v->lead = -1; ndust = 0;
}
static void say(DaShow *v, const char *s, double secs) { snprintf(v->said, sizeof v->said, "%s", s); v->sayUntil = v->t + secs; }
double da_show_end(const DaShow *v) {
  double mx = 0; for (int i = 0; i < v->n; i++) mx = fmax(mx, v->hs[i].T);
  return fmin(mx, v->hs[v->winner].T + 4) + DA_INTRO;
}
DaShowOut da_show_draw(DaShow *v, double rt, double dt) {
  DaShowOut out = { 0 };
  const DaHorse *hs = v->hs; int n = v->n; double winT = hs[v->winner].T;
  v->t += dt;
  double rs = rt - DA_INTRO;                                                        // seconds since the gates opened
  double ps[DA_NH], pm = 0, pmin = 1e9, pmax = -1e9;
  for (int i = 0; i < n; i++) { ps[i] = da_progress(&hs[i], fmax(0, rs)); pm += ps[i]; pmin = fmin(pmin, ps[i]); pmax = fmax(pmax, ps[i]); }
  pm /= n;
  // the camera can't fit a field strung out over a furlong, so the gaps are drawn squeezed until the home stretch,
  // then let out to their true size so every horse still hits the line at its own time (order never changes)
  double spread = (pmax - pmin) * DA_RACE_LEN, fit = fmin(1, PACK / fmax(1, spread)), home = clampd((pm - 0.7) / 0.22, 0, 1);
  double k = fit + (1 - fit) * home * home * (3 - 2 * home);
  double xs[DA_NH], pp[DA_NH]; bool done[DA_NH];
  for (int i = 0; i < n; i++) {
    double p = fmin(1, pm) + k * (ps[i] - fmin(1, pm));
    xs[i] = p * (DA_RACE_LEN + 24) - 24; pp[i] = p; done[i] = rs >= hs[i].T;            // from inside the stalls
  }
  int *ord = out.order; for (int i = 0; i < n; i++) ord[i] = i;
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && xs[ord[j]] > xs[ord[j - 1]]; j--) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
  // the camera rides with the front of the pack, the leader on the right third of the screen. Once the winner is home
  // it stops on the line and lets the rest gallop through the shot
  double lx = xs[ord[0]], mid = xs[ord[n > 2 ? 2 : n - 1]];
  double target = fmax(lx - 400, fmin(lx - 300, (lx + mid) / 2 - 240));
  if (rs >= winT - 1) target = fmin(target, DA_RACE_LEN - 320);
  // a critically damped spring on the shot, fed the shot's own (smoothed) speed so it keeps up at a gallop
  if (rs < 0 || !v->havePrev) { v->prevTarget = target; v->camV = 0; v->tvs = 0; v->havePrev = true; }
  v->tvs += ((target - v->prevTarget) / fmax(dt, 1e-3) - v->tvs) * fmin(1, dt * 3); v->prevTarget = target;
  v->camV += (CAM_W * CAM_W * (target - v->cam) + 2 * CAM_W * (v->tvs - v->camV)) * dt;
  v->cam += v->camV * dt;
  if (rs < 0) v->cam = -300;
  // the calls
  char b[96], up[40];
  if (rs < 0 && !v->cGate) { v->cGate = true; say(v, "THEY'RE IN THE GATE...", DA_INTRO); }
  if (rs >= 0 && !v->cOff) { v->cOff = true; say(v, "AND THEY'RE OFF!", 3); }
  if (rs > 4 && ord[0] != v->lead && !done[ord[0]]) {
    if (v->lead >= 0 && v->t - v->leadAt > 2.5) {
      upper(hs[ord[0]].name, up, sizeof up);
      snprintf(b, sizeof b, "%s%s", up, ord[0] == v->oldMan ? " TAKES IT UP! THE OLD MAN'S ON HIS FEET!" : " TAKES THE LEAD!"); say(v, b, 3);
    }
    v->lead = ord[0]; v->leadAt = v->t;
  }
  if (pp[ord[0]] > 0.82 && !v->cFinal) { v->cFinal = true; say(v, "INTO THE FINAL FURLONG!", 3); }
  if (rs >= winT && !v->cWon) {
    v->cWon = true;
    bool close = hs[v->second].T - winT < 0.1;
    v->flash = close ? 0.35 : 0;
    upper(hs[v->winner].name, up, sizeof up);
    snprintf(b, sizeof b, close ? "IT'S A PHOTO! ... %s BY A NOSE!" : "%s WINS IT!", up); say(v, b, 5);
  }
  DaScene s = { .t = v->t, .dt = dt, .camX = v->cam, .finish = DA_RACE_LEN, .hasGate = true, .gate = 0, .gateOpen = clampd(rs * 3, 0, 1),
                .cheer = pp[ord[0]] > 0.7 || rs < 0, .nb = 0, .nr = n };
  static char tags[DA_NH][16];
  for (int i = 0; i < n; i++) {
    DaRunner *r = &s.r[i];
    r->x = xs[i] - v->cam; r->y = DA_LANE0 + (hs[i].num - 1) * DA_LANE; r->h = i; r->standing = rs < 0;
    r->phase = mdf(xs[i] / STRIDE + hs[i].num * 0.17, 1); r->mine = i == v->mine;
    if (i == v->mine) { snprintf(tags[i], sizeof tags[i], "YOU #%d", hs[i].num); r->tag = tags[i]; }
    else r->tag = i == v->oldMan ? "OLD MAN'S BET" : NULL;
    r->tagCol = H(22e6d8);
  }
  for (int i = 0; i < nboard && i < DA_NH; i++) { s.board[i] = board[i]; s.board[i].hot = v->mine >= 0 && board[i].num == hs[v->mine].num; }
  s.nb = nboard;
  da_scene(&s);
  // the running order across the top, like the TV graphic
  R(4, 4, 124, 8 + n * 8, rgba(20, 16, 31, .82));
  for (int i = 0; i < n; i++) {
    const DaHorse *h = &hs[ord[i]]; char num[4];
    if (ord[i] == v->mine) R(5, 7 + i * 8, 118, 9, rgba(255, 210, 31, .28));
    snprintf(num, sizeof num, "%d", i + 1); text(num, 8, 9 + i * 8, H(b8a9d6), 1);
    R(15, 8 + i * 8, 7, 7, h->look.silk1);
    snprintf(num, sizeof num, "%d", h->num); text(num, 17, 9 + i * 8, h->look.silk2, 1);
    upper(h->name, up, 23); text(up, 25, 9 + i * 8, ord[i] == v->mine ? H(ffd21f) : H(f6f4fa), 1);
  }
  // the clock and the distance to go
  double sec = fmax(0, rs); int togo = (int)fmax(0, ceil((1 - pp[ord[0]]) * 8));
  snprintf(b, sizeof b, "%d:%02d.%d", (int)floor(sec / 60), (int)floor(fmod(sec, 60)), (int)floor(fmod(sec * 10, 10)));
  R(400, 4, 76, 18, rgba(20, 16, 31, .82));
  text(b, 404, 8, H(ffd21f), 1);
  if (togo) snprintf(b, sizeof b, "%dF TO GO", togo); else snprintf(b, sizeof b, "FINISH");
  text(b, 404, 15, H(f6f4fa), 1);
  if (v->said[0] && v->t < v->sayUntil) {
    double w = textW(v->said, 2) + 16;
    R(240 - w / 2, 244, w, 18, rgba(20, 16, 31, .86));
    text(v->said, 240 - w / 2 + 8, 248, H(f6f4fa), 2);
  }
  if (v->flash > 0) { R(0, 0, LW, LH, rgba(255, 255, 255, fmin(1, v->flash * 3))); v->flash -= dt; }   // the photo finish camera
  out.rs = rs; out.lead = pp[ord[0]]; out.won = v->cWon; out.photo = v->flash > 0.3;
  return out;
}

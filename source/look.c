// The Old Man's cosmetics, ported from web/game.js (PAINTS, COATS, drawWheel, drawDecal, drawGear, drawCane, drawCarrier,
// drawCap, drawCatPal, emitTrail). Everything that never moves is drawn into the SPR_BIKE sprite at twice the size, as the
// default look always was. What moves is handled the cheapest way for where it sits in the drawing order:
//  - animated wheels are drawn first in the web game, so they are drawn live under the stamped sprite (the swingarm, fork
//    and exhaust in the sprite still cross them);
//  - animated hats, gear and the Cat Pal are drawn last, so they are drawn live on top (with the specs, which sit over the
//    hat; and with any gear, which sits over the specs);
//  - the rest sits in the middle of the order (the purple/disco/flame coats, holo paint and the precious paints' twinkle,
//    the sun and batmoon decals, the canes that glow, sway or glint, the cauldron, chest, boombox and piggy carriers), so
//    the sprite is re-rendered about 10 times a second while one of them is worn.
#include "look.h"
#include "draw.h"
#include "sim.h"
#include "gfx.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define H(x) RGB(0x##x)
#define F(x, y, w, h) cv_rect((x), (y), (w), (h))
#define PI M_PI
static void R(col_t c, double x, double y, double w, double h) { cv_fill(c); cv_rect(x, y, w, h); }
static void C(col_t c, double x, double y, double r) { cv_fill(c); cv_circle(x, y, r); }
static void L(col_t c, double w, double x1, double y1, double x2, double y2) { cv_begin(); cv_move(x1, y1); cv_line(x2, y2); cv_stroke(c, w); }
static void E(col_t c, double cx, double cy, double rx, double ry, double rot, double a0, double a1) { cv_fill(c); cv_begin(); cv_ellipse(cx, cy, rx, ry, rot, a0, a1); cv_fillpath(); }
static void A(col_t c, double w, double cx, double cy, double r, double a0, double a1) { cv_begin(); cv_arc(cx, cy, r, a0, a1); cv_stroke(c, w); }
static void ring(col_t c, double w, double cx, double cy, double r) { A(c, w, cx, cy, r, 0, 6.2831); }   // just short of a whole turn, so the outline closes
#define POLY(...) do { const double p_[] = { __VA_ARGS__ }; cv_poly(p_, sizeof p_ / sizeof *p_ / 2); } while (0)
static col_t hsla(double h, double s, double l, double a) { return (hslc(h, s, l) & ~0xffu) | (col_t)(clampd(a, 0, 1) * 255 + 0.5); }
static int md(int a, int n) { return ((a % n) + n) % n; }
static double mdf(double a, double n) { return fmod(fmod(a, n) + n, n); }
static int fdiv(int a, int n) { return (int)floor((double)a / n); }
static double tmod(double a) { return fmod(a, 1); }                  // (T * k) % 1 for T >= 0
static void star5(double cx, double cy, double r, double rot) {
  double p[20]; for (int i = 0; i < 10; i++) { double a = rot + i * PI / 5 - PI / 2, rr = i & 1 ? r * 0.45 : r; p[i * 2] = cx + cos(a) * rr; p[i * 2 + 1] = cy + sin(a) * rr; }
  cv_poly(p, 10);
}

/* ---------- the items ---------- */
static const char *const ITEMS[NSLOT][20] = {
  { "red", "gold", "black", "pink", "cyan", "sunrise", "goldenhour", "money", "holo", "chrome" },
  { "navy", "green", "mustard", "purple", "hivis", "plaid", "checker", "zebra", "tiedye", "disco", "flame", "rainbow", "varsity", "sunset", "greaser", "skeleton", "xmas" },
  { "redcap", "straw", "hardhat", "nightcap", "crown", "cowboy", "tophat", "wizard", "chef", "party", "viking", "propeller", "cone", "laurel", "pumpkin", "santa" },
  { "white", "gold", "cyan", "spokes", "spinner", "rainbow", "flame", "pizza", "vinyl", "star", "sunwheel", "web" },
  { "stripe", "flames", "plate", "checker", "lightning", "skull", "stars", "eight", "hearts", "kanji", "sun", "roundel", "batmoon" },
  { "shades", "streamers", "charm", "scarf", "beer", "monocle", "pipe", "dice", "pirate", "foxtail", "goggles", "bowtie", "pigeon", "mug", "watch", "medal", "chain", "spider" },
  { "crate", "wicker", "pizza", "toolbox", "cooler", "guitar", "duffel", "chest", "boombox", "lunch", "paperbag", "piggy", "cauldron" },
  { "eightball", "candy", "golden", "neon", "poolcue", "skull", "flamingo", "wizard", "fishing", "lollipop", "golf", "bouquet", "cat", "trophy", "broom" },
  { "sparks", "rainbow", "flames", "gold", "lines", "coins", "bubbles", "smoke", "bats", "snow" }
};
static bool inList(const char *id, const char *const *list) { for (; *list; list++) if (!strcmp(id, *list)) return true; return false; }
static char raw[NSLOT][16], cur[NSLOT][16];                     // what was set, and what is drawn (unknown ids draw the default)
#define IS(sl, s) (!strcmp(cur[sl], s))
static bool dirty = true, fAnim, fRims, fCap, fGear, fCat;      // sprite needs a redraw; which parts move (see the top)
static double lastBuild = -1;

/* ---------- paint ---------- */
static const struct { const char *id; col_t a, b; } PAINTS[] = {
  { "red", H(e0242e), H(ff8a4a) }, { "gold", H(e9b923), H(fff3a8) }, { "black", H(1c1a24), H(6c5cff) }, { "pink", H(ff3ea5), H(ffc2e6) },
  { "cyan", H(22e6d8), H(c8fffa) }, { "sunrise", H(ff6a3a), H(ffd9a0) }, { "goldenhour", H(ffb347), H(fff3c0) }, { "money", H(1f9a4a), H(b8ffb0) },
  { "chrome", H(b8bccb), H(ffffff) }, { "holo", H(8a7aff), H(7dfff0) } };
static col_t tank1 = H(ffd21f), tank2 = H(ff5a1f);

/* ---------- cardigans: each pixel's colour comes from px(x, y, sleeve), as in the web game ---------- */
enum { CS_CARDIGAN, CS_JACKET, CS_SHIRT, CS_VEST, CS_VARSITY, CS_GREASER };
typedef col_t (*PxFn)(int x, int y, bool sl);
typedef struct { const char *id; col_t base, trim, hem, btn; int style; bool noCuff; PxFn px; } Coat;   // hem 0 = the trim
static col_t pxKnit(int x, int y, bool sl) { (void)sl; return md(x, 4) == 0 ? H(714822) : md(x, 4) == 2 && md(y, 2) == 0 ? H(9c6a3a) : H(8a5a2e); }
static col_t pxNavy(int x, int y, bool sl) {
  int a = x + y + 40, b = x - y + 40;
  if ((md(a, 6) == 3 && md(b, 2) == 0) || (md(b, 6) == 3 && md(a, 2) == 0)) return H(e9b923);
  return (fdiv(a, 6) + fdiv(b, 6)) & 1 ? H(4a6eae) : H(2a4272);
}
static col_t pxGreen(int x, int y, bool sl) {
  double v = hash(fdiv(x + 40, 3) * 7 + fdiv(y + 40, 2), 91), w = hash(fdiv(x + 41, 2) * 5 + fdiv(y + 41, 3), 92);
  return v < 0.28 ? H(2f3a1a) : w < 0.22 ? H(8a9a52) : v > 0.82 ? H(4a3620) : H(56663a);
}
static col_t pxMustard(int x, int y, bool sl) {
  int r = fdiv(y + 40, 5), cx = md(x + (r & 1) * 3, 6) - 2, cy = md(y + 40, 5) - 2;
  if (cx == 0 && cy == 0) return H(ffe14a);
  if (abs(cx) + abs(cy) == 1) return H(ff5ab0);
  if ((cx == 2 && cy == 2) || (cx == 3 && cy == 1)) return H(2fbf6a);
  return H(1f8a9a);
}
static col_t pxPurple(int x, int y, bool sl) {
  double s = hash(x * 131 + y, 77), d = fabs((x - 4) * 0.55 + (y + 22) + sin(x * 0.8) * 0.8);
  if (s < 0.06) return s < 0.015 + 0.02 * (1 + sin(T * 4 + x * 3 + y)) ? H(ffffff) : H(a99ae8);
  return d < 1.2 ? H(d07af0) : d < 2.6 ? H(8a3ac8) : d < 4.2 ? H(4a2290) : d < 5.6 ? H(2a1a68) : H(160e38);
}
static col_t pxHivis(int x, int y, bool sl) {
  if (sl || x >= 15) return pxKnit(x, y, sl);
  if (y == -22 || y == -18) return H(e8eaf2);
  if (y == -23 || y == -17) return H(9aa0b0);
  return H(c8f21a);
}
static col_t pxPlaid(int x, int y, bool sl) {
  int a = fdiv(x + 40, 3) & 1, b = fdiv(y + 40, 3) & 1;
  return a && b ? H(1a1218) : a || b ? H(6a121a) : md(x + y, 3) == 0 ? H(d23038) : H(c0242e);
}
static col_t pxChecker(int x, int y, bool sl) {
  col_t chk = ((x >> 1) + (y >> 1)) & 1 ? H(f1ecf7) : H(14101f);
  if (sl) return x >= 20 ? chk : y == -27 ? H(e0242e) : H(14101f);
  if (y >= -21) return chk;
  return y == -22 ? H(e0242e) : H(14101f);
}
static col_t pxZebra(int x, int y, bool sl) {
  double u = sl ? x * 0.5 - y * 0.9 : x * 0.8 + y * 0.6, w = mdf(u + sin((sl ? x : y) * 0.7 + x * 0.25) * 1.1, 4.5);
  return w < 1.6 ? H(14101f) : H(f4efe6);
}
static col_t pxTiedye(int x, int y, bool sl) {
  static const col_t cs[6] = { H(ff8ac0), H(ffc48a), H(fff08a), H(9cf0c0), H(8ad0ff), H(c4a0ff) };
  double dx = x - 9, dy = y + 20, a = atan2(dy, dx), r = hypot(dx, dy), k = a / 6.283 * 6 + r * 0.28, f = k - floor(k);
  return f < 0.1 && md(x + y, 2) ? H(fff6fb) : cs[md((int)floor(k), 6)];
}
static col_t pxDisco(int x, int y, bool sl) {
  static const col_t tones[5] = { H(5a5c74), H(8486a0), H(aeb0c6), H(dcdeec), H(f4f6ff) };
  int i = x >> 1, j = y >> 1, sweep = md(i - j - (int)floor(T * 7), 14); double gg = hash(i * 57 + j * 13 + (int)floor(T * 5), 33);
  if (sweep == 0) return H(ffffff);
  if (gg < 0.04) return md(x, 2) || md(y, 2) ? (gg < 0.02 ? H(ff8ad0) : H(8afff4)) : H(ffffff);
  int tone = (int)(hash(i * 7 + j * 31, 34) * 3) + (sweep < 3 ? 1 : 0);
  return tones[tone + (y < -26 ? 1 : 0)];
}
static col_t pxFlame(int x, int y, bool sl) {
  double d = sl ? 24 - x : -14 - y, h = sl ? 8 + 2 * sin(y * 2.1 + T * 11) : 7 + 3 * sin(x * 1.3 + T * 9) + 2 * sin(x * 0.7 - T * 5);
  return d < h * 0.4 ? H(ffe14a) : d < h * 0.7 ? H(ff8a1f) : d < h ? H(d8241e) : H(18121e);
}
static const col_t RB[6] = { H(ff4a4a), H(ff9a2f), H(ffd21f), H(4ad86a), H(3a9aff), H(a35bff) };
static col_t pxRainbow(int x, int y, bool sl) {
  if (sl) return x >= 16 && x < 22 ? RB[x - 16] : H(1c1a28);
  return y >= -23 && y < -17 ? RB[y + 23] : md(x, 2) == 0 && md(y, 3) == 0 ? H(262434) : H(1c1a28);
}
static col_t pxVarsity(int x, int y, bool sl) { return sl ? (x >= 22 ? H(1d3f8f) : H(efe6d0)) : H(1d3f8f); }
static col_t pxSunset(int x, int y, bool sl) {
  static const col_t band[5] = { H(1a1030), H(2a1450), H(5a1a6a), H(a02a7a), H(ff3e8a) };
  if (sl) return y == -27 ? H(ff5a8a) : y == -26 ? H(ff9a2f) : H(1a1030);
  int sx = x - 6, sy = y + 19;
  if (sx * sx + sy * sy < 22 && sy < 2) return sy > -2 && md(sy, 2) == 0 ? H(ff3e8a) : sy < -2 ? H(ffd21f) : H(ff9a2f);
  int b = fdiv(y + 30, 3); return band[b < 0 ? 0 : b > 4 ? 4 : b];
}
static col_t pxSkeleton(int x, int y, bool sl) {                    // Halloween: a ribcage and arm bones
  const col_t BONE = H(efeadf), DIM = H(bdb6a6);
  if (sl) return y == -28 && x < 22 ? (md(x, 7) == 0 ? DIM : BONE) : y == -27 && (x == 11 || x == 21) ? BONE : H(16121e);
  if (x >= -4 && x <= -3 && y > -32 && y < -14) return md(y, 3) == 0 ? DIM : BONE;   // spine
  static const int ribs[4] = { -30, -27, -24, -21 };
  for (int i = 0; i < 4; i++) if (x > -3 && x < 14 && y == ribs[i] + (int)rnd((x + 3) * 0.18)) return x > 10 ? DIM : BONE;
  return H(16121e);
}
static col_t pxXmas(int x, int y, bool sl) {                        // Christmas: a loud knit sweater
  const col_t W = H(f4f0e8), G = H(1f6a3a), RD = H(c0242e);
  if (sl) return x >= 17 && x < 19 ? G : md(x + y, 4) == 0 ? H(a81e28) : RD;
  if (y == -31 || y == -26) return G;
  bool dots = y == -29 || y == -28;
  if (y >= -30 && y <= -27) return md(x + (dots ? 1 : 0), 3) == 0 && dots ? W : G;   // white dots on a green band
  int tx = md(x + 2, 9) - 4, ty = y + 18;                            // little white trees and snowflakes below
  if (ty >= -4 && ty <= 0 && abs(tx) <= (ty + 4) >> 1) return md(fdiv(x + 2, 9), 2) ? W : H(2f8a4a);
  return md(x, 6) == 0 && md(y, 6) == 0 ? W : RD;
}
static col_t pxGreaser(int x, int y, bool sl) {
  if (sl) return y == -29 && x < 21 ? H(3a3448) : H(16131c);
  return (x > -6 && x < 8 && y == (int)rnd(-23 - (x + 6) * 4 / 13.0)) || (x >= 8 && x < 12 && y == -30) ? H(4a4458) : H(16131c);
}
static const Coat COATS[] = {
  { "", H(8a5a2e), H(5e3a1a), 0, H(e9dcb8), CS_CARDIGAN, false, pxKnit },
  { "navy", H(2a4272), H(1a2a4a), 0, H(e9b923), CS_CARDIGAN, false, pxNavy },
  { "green", H(56663a), H(2c3519), 0, H(a89a6a), CS_JACKET, false, pxGreen },
  { "mustard", H(1f8a9a), H(f4ead2), 0, H(f4ead2), CS_SHIRT, false, pxMustard },
  { "purple", H(160e38), H(0b0722), 0, H(ffd21f), CS_CARDIGAN, false, pxPurple },
  { "hivis", H(c8f21a), H(8aa80e), 0, H(c8f21a), CS_VEST, false, pxHivis },
  { "plaid", H(c0242e), H(efe2c8), H(1a1218), H(1a1218), CS_CARDIGAN, false, pxPlaid },
  { "checker", H(14101f), H(14101f), 0, H(d8dae8), CS_JACKET, true, pxChecker },
  { "zebra", H(f4efe6), H(14101f), H(ff3ea5), H(ff3ea5), CS_JACKET, false, pxZebra },
  { "tiedye", H(ffb0d0), H(2a2240), 0, H(ffffff), CS_CARDIGAN, false, pxTiedye },
  { "disco", H(a8aac0), H(14101f), 0, H(ffffff), CS_JACKET, false, pxDisco },
  { "flame", H(18121e), H(2c2430), 0, H(ff8a1f), CS_JACKET, false, pxFlame },
  { "rainbow", H(1c1a28), H(0e0c16), 0, H(f1ecf7), CS_CARDIGAN, false, pxRainbow },
  { "varsity", H(1d3f8f), H(ffd21f), 0, H(f1ecf7), CS_VARSITY, false, pxVarsity },
  { "sunset", H(1a1030), H(1a1030), 0, H(ff9a2f), CS_CARDIGAN, false, pxSunset },
  { "skeleton", H(16121e), H(2a2238), 0, H(efeadf), CS_JACKET, false, pxSkeleton },
  { "xmas", H(c0242e), H(1f6a3a), H(1f6a3a), H(ffd21f), CS_CARDIGAN, false, pxXmas },
  { "greaser", H(16131c), H(2c2836), 0, H(c8c8d8), CS_GREASER, false, pxGreaser },
};
static const Coat *CO = COATS;
// The default cardigan, knitted: dark ribs every fourth column and light stitches between. The port always drew it like
// this (not cell by cell), so the default look stays exactly as it was.
static void knit(int x0, int y0, int x1, int y1) {
  R(H(8a5a2e), x0, y0, x1 - x0, y1 - y0);
  for (int x = x0; x < x1; x++) {
    int m = md(x, 4); double w = x + 1 < x1 ? 1.5 : 1;
    if (m == 0) R(H(714822), x, y0, w, y1 - y0);
    else if (m == 2) for (int y = y0; y < y1; y++) if (md(y, 2) == 0) R(H(9c6a3a), x, y, w, y + 1 < y1 ? 1.5 : 1);
  }
}
// In play the bike is scaled and tilted, so 1x1 cells land between screen pixels and their soft edges let the road show
// through. A solid underlayer plus cells that overlap their right and lower neighbours keeps the coat opaque. Runs of one
// colour in a row are one rect (the same picture, in a fraction of the rects: a coat is ~600 cells).
static col_t bodyCell(int x, int y) { return CO->px(x, y, false); }
static col_t sleeveCell(int x, int y) {
  if (CO->style == CS_SHIRT && x > 17) return y == -26 && x < 20 ? H(d8a882) : H(f2c9a0);   // bare forearm
  return CO->px(x, y, true);
}
static void paintCells(int x0, int y0, int x1, int y1, col_t (*f)(int, int), col_t under) {
  R(under, x0, y0, x1 - x0, y1 - y0);
  for (int y = y0; y < y1; y++) {
    double h = y + 1 < y1 ? 1.5 : 1; col_t c = f(x0, y); int s = x0;
    for (int x = x0 + 1; x <= x1; x++) {
      col_t n = x < x1 ? f(x, y) : 0;
      if (x < x1 && n == c) continue;
      R(c, s, y, x - s + (x < x1 ? 0.5 : 0), h); s = x; c = n;
    }
  }
}
static void drawCoat(void) {
  const Coat *c = CO; col_t hem = c->hem ? c->hem : c->trim; int st_ = c->style;
  cv_save(); cv_begin(); cv_move(6, -15); cv_line(-8, -14); cv_line(-6, -24); cv_line(7, -28);
  cv_move(11 + 6 * cos(0.25), -24 + 6 * sin(0.25)); cv_ellipse(11, -24, 6, 9, 0.25, 0, 7); cv_clip();
  if (c == COATS) knit(-9, -34, 19, -12); else paintCells(-9, -34, 19, -12, bodyCell, c->base);
  cv_fill(rgba(20, 10, 30, .28)); F(-9, -25, 6, 12); F(12, -25, 7, 1);   // shade the back and under the arm
  if (st_ != CS_SHIRT && st_ != CS_VEST) { R(hem, -9, -16, 28, 4); cv_fill(rgba(0, 0, 0, .25)); for (int x = -9; x < 19; x += 2) F(x, -16, 1, 4); }
  if (st_ == CS_VARSITY) { R(H(f1ecf7), -9, -15, 28, 1); R(H(ffd21f), 14, -23, 2, 1); R(H(ffd21f), 13, -22, 1, 3); R(H(ffd21f), 14, -19, 2, 1); }
  if (st_ == CS_CARDIGAN) { R(hem, 15, -26, 1, 10); R(c->btn, 16, -23, 1, 1); R(c->btn, 16, -19, 1, 1); }
  if (st_ == CS_JACKET) { R(c->btn, 15, -26, 1, 10); R(rgba(0, 0, 0, .35), 14, -26, 1, 10); }
  if (st_ == CS_VEST) R(c->trim, 14, -25, 1, 10);
  if (st_ == CS_SHIRT) { R(rgba(255, 255, 255, .25), 15, -25, 1, 10); R(c->btn, 16, -22, 1, 1); R(c->btn, 16, -18, 1, 1); }
  if (st_ == CS_GREASER) {
    cv_fill(H(c8c8d8)); for (int y = -26; y < -16; y++) F(13 + rnd((y + 26) * 0.3), y, 1, 1);
    R(H(2c2836), -9, -16, 28, 1); R(H(c8c8d8), 15, -16, 2, 1); R(H(2c2836), 13, -27, 3, 1); R(H(2c2836), 14, -26, 2, 2);
  }
  cv_restore();
  // the collar sits up against his neck
  if (st_ == CS_SHIRT) { R(c->trim, 10, -32, 4, 2); R(c->trim, 13, -30, 2, 2); }
  else if (st_ == CS_GREASER) { R(H(2c2836), 9, -33, 4, 3); R(H(2c2836), 12, -30, 3, 3); R(H(c8c8d8), 13, -29, 1, 1); }
  else if (st_ == CS_VEST) R(H(5e3a1a), 10, -32, 4, 2);
  else R(c->trim, 9, -32, 5, 2);
}
static void drawSleeve(void) {
  const Coat *c = CO; bool shortSl = c->style == CS_SHIRT;
  cv_save(); cv_begin(); cv_move(11, -30); cv_line(24.5, -28); cv_line(24.5, -24.5); cv_line(11, -26); cv_clip();
  if (c == COATS) knit(11, -31, 25, -24); else paintCells(11, -31, 25, -24, sleeveCell, c->base);
  cv_fill(rgba(20, 10, 30, .25)); F(11, -26, 14, 2);
  if (shortSl) R(c->trim, 17, -30, 1, 5);
  else if (c->noCuff) { }
  else if (c->style != CS_VEST) { R(c->style == CS_VARSITY ? H(1d3f8f) : c->trim, 22, -29, 2, 5); if (c->style == CS_VARSITY) R(H(ffd21f), 22, -29, 1, 5); }
  else R(H(5e3a1a), 22, -29, 2, 5);
  cv_restore();
}

/* ---------- wheels ---------- */
static double wheelAngle(void) { return st == ST_PLAY || st == ST_CRASH ? g.cam / 8 : T * 9; }
static void disc(double r, col_t c) { C(c, 0, 0, r); }
static void spokes(int n, double step, double len, double a, col_t c, double w) {
  cv_begin(); for (int i = 0; i < n; i++) { cv_move(0, 0); cv_line(cos(a + i * step) * len, sin(a + i * step) * len); } cv_stroke(c, w);
}
static void drawWheel(double cx) {
  const char *k = cur[SL_RIMS]; double a = wheelAngle();
  cv_save(); cv_translate(cx, 0);
  if (!strcmp(k, "flame")) {                                    // the tyre burns: tongues swept back by the wind, longest at the top and rear
    static const col_t fc[3] = { H(e0242e), H(ff8a1f), H(ffe14a) }; static const double fs[3] = { 1, 0.72, 0.42 };
    for (int l = 0; l < 3; l++) for (int i = 0; i < 12; i++) {
      double ang = i * 0.5236 + 0.2, back = fmax(0, -cos(ang) * 0.6 - sin(ang)), len = (2 + 5 * back + sin(T * 17 + i * 2.3) * 1.3) * fs[l];
      if (len < 0.6) continue;
      double tx = cos(ang) * (7.6 + len) - len * 0.9, ty = sin(ang) * (7.6 + len) - len * 0.35;
      cv_fill(fc[l]); cv_begin(); cv_move(cos(ang - 0.32) * 7.4, sin(ang - 0.32) * 7.4); cv_quad(cos(ang) * (8 + len * 0.6), sin(ang) * (8 + len * 0.6), tx, ty);
      cv_line(cos(ang + 0.32) * 7.4, sin(ang + 0.32) * 7.4); cv_fillpath();
    }
  }
  if (!strcmp(k, "pizza")) {
    disc(8, H(d9a24a)); disc(6.8, H(ffd45a)); for (int i = 0; i < 4; i++) C(H(c0392b), cos(a + i * 1.57) * 4, sin(a + i * 1.57) * 4, 1.5);
    disc(1.2, H(8a2418)); cv_restore(); return;
  }
  if (!strcmp(k, "vinyl")) {
    disc(8, H(14101f)); static const double rr[3] = { 7, 5.8, 4.6 }; for (int i = 0; i < 3; i++) ring(rgba(255, 255, 255, .16), 0.6, 0, 0, rr[i]);
    A(rgba(255, 255, 255, .55), 1, 0, 0, 6.4, a, a + 0.8); disc(2.8, H(ff3ea5)); disc(0.8, H(14101f)); cv_restore(); return;
  }
  if (!strcmp(k, "web")) {                                      // Halloween: an orange rim strung with a spiderweb that turns with the wheel
    disc(8, H(14101f)); disc(6.6, H(ff8a1f)); disc(5.8, H(1a1024));
    spokes(6, 1.047, 5.8, a, H(efeadf), 0.5);
    static const double rr[3] = { 1.8, 3.2, 4.6 };
    for (int j = 0; j < 3; j++) { cv_begin(); for (int i = 0; i <= 6; i++) { double t = a + i * 1.047; cv_line(cos(t) * rr[j], sin(t) * rr[j]); } cv_stroke(H(efeadf), 0.5); }
    C(H(14101f), cos(a + 0.5) * 3.6, sin(a + 0.5) * 3.6, 0.9);
    R(H(c8c8d8), -1, -1, 2, 2); cv_restore(); return;
  }
  disc(8, H(14101f));
  if (!strcmp(k, "white")) { disc(7.2, H(f1ecf7)); disc(5.8, H(14101f)); }
  col_t rimC = H(6c6a86);
  static const struct { const char *k; col_t c; } RIMC[] = { { "gold", H(e9b923) }, { "cyan", H(22e6d8) }, { "white", H(6c6a86) }, { "spokes", H(d9dcf0) },
    { "spinner", H(e9b923) }, { "flame", H(ff5a1f) }, { "star", H(ff3ea5) }, { "sunwheel", H(ff8a1f) } };
  for (int i = 0; i < 8; i++) if (!strcmp(k, RIMC[i].k)) rimC = RIMC[i].c;
  bool rainbow = !strcmp(k, "rainbow");
  disc(5, rainbow ? hslc(T * 120, 95, 58) : rimC); disc(3.5, H(14101f));
  if (rainbow) ring(hslc(T * 120 + 140, 95, 60), 1, 0, 0, 3);
  if (!strcmp(k, "spokes")) spokes(8, 0.785, 4.8, a, H(d9dcf0), 0.7);
  if (!strcmp(k, "spinner")) { cv_fill(H(ffd21f)); for (int i = 0; i < 4; i++) { double t = a * 2 + i * 1.57; POLY(0, 0, cos(t - 0.45) * 4.6, sin(t - 0.45) * 4.6, cos(t + 0.2) * 4.6, sin(t + 0.2) * 4.6); } }
  if (!strcmp(k, "gold")) { spokes(5, 1.2566, 4, a, H(ffd21f), 0.9); A(H(fff3a8), 0.8, 0, 0, 4.4, -2.4, -1.4); disc(1.6, H(e9b923)); }
  if (!strcmp(k, "cyan")) {
    double gl = 0.55 + 0.35 * sin(T * 6);
    ring(rgba(34, 230, 216, gl * 0.35), 3.4, 0, 0, 4.3); ring(rgba(200, 255, 250, gl), 0.6, 0, 0, 4.3);
    A(rgba(34, 230, 216, .6), 0.6, 0, 0, 7.6, a, a + 1.2); A(rgba(34, 230, 216, .6), 0.6, 0, 0, 7.6, a + 3.14, a + 4.34);
  }
  if (!strcmp(k, "star")) { cv_fill(H(fff3a8)); star5(0, 0, 4.4, a); }
  if (!strcmp(k, "sunwheel")) { disc(3.6, H(ffd21f)); spokes(8, 0.785, 3.6, a, H(ff8a1f), 0.9); disc(1.2, H(fff3a8)); }
  R(H(c8c8d8), -1, -1, 2, 2); cv_restore();
}

/* ---------- the tank and its decals ---------- */
static void drawTank(void) {                                    // base paint, a darker belly, the second colour as a swoosh, a glint
  bool holo = IS(SL_PAINT, "holo"), black = IS(SL_PAINT, "black");   // the hologram paint shifts colour as it rides
  col_t c1 = holo ? hslc(T * 70, 85, 66) : tank1, c2 = holo ? hslc(T * 70 + 150, 90, 78) : tank2;
  cv_save(); cv_begin(); cv_ellipse(20, -14, 9, 5, 0, 0, 7); cv_fill(c1); cv_fillpath(); cv_clip();
  R(holo ? hslc(T * 70 + 60, 80, 48) : black ? H(0c0a12) : shadec(c1, 0.72), 10, -11.4, 20, 3);
  if (IS(SL_PAINT, "chrome")) { R(H(6c6f84), 10, -11.6, 20, 1.4); R(H(ffffff), 14, -16.6, 13, 0.9); R(H(8d90a6), 12, -14.6, 16, 0.7); }
  if (black) R(rgba(108, 92, 255, .5), 11, -14.8, 18, 0.8);
  cv_fill(c2); POLY(13, -13, 20, -17, 24, -13, 18, -12);
  cv_fill(rgba(255, 255, 255, .75)); F(24, -17.6, 2, 1); F(26.4, -16.8, 1, 1);
  if (IS(SL_PAINT, "gold") || IS(SL_PAINT, "goldenhour") || IS(SL_PAINT, "money")) {   // the precious paints catch the light now and then
    double tw = tmod(T * 0.8);
    if (tw < 0.35) { double sx = 14 + tw * 40; cv_fill(rgba(255, 255, 255, .85)); F(sx, -18, 1, 3); F(sx - 1, -16.5, 3, 1); }
  }
  cv_restore();
}
static void textOutlined(const char *s, double x, double y, double size, col_t fill, col_t edge, double ew) {   // strokeText, then fillText
  for (int i = 0; i < 8; i++) { double a = i * PI / 4; cv_text(s, x + cos(a) * ew, y + sin(a) * ew, size, edge, AL_CENTER, BL_MIDDLE); }
  cv_text(s, x, y, size, fill, AL_CENTER, BL_MIDDLE);
}
static void drawDecal2(const char *k) {
  cv_save(); cv_begin(); cv_ellipse(20, -14, 9, 5, 0, 0, 7); cv_clip();
  if (!strcmp(k, "checker")) { for (int i = 0; i < 9; i++) for (int j = 0; j < 3; j++) R((i + j) & 1 ? H(14101f) : H(f1ecf7), 11 + i * 2.2, -18 + j * 2.2, 2.2, 2.2); }
  else if (!strcmp(k, "lightning")) {
    static const double q[14] = { 21, -19.5, 15, -13.5, 19.5, -13.5, 17, -8.5, 25, -15.5, 20.5, -15.5, 24, -19.5 };
    cv_fill(H(ffe14a)); cv_poly(q, 7); cv_line(q[0], q[1]); cv_stroke(H(14101f), 0.6);
  }
  else if (!strcmp(k, "skull")) {                               // a skull and crossbones, outlined so it reads on any paint
    cv_begin(); cv_move(17.5, -18); cv_line(27.5, -9.5); cv_move(27.5, -18); cv_line(17.5, -9.5); cv_stroke(H(14101f), 2.4); cv_stroke(H(f1ecf7), 1.1);
    R(H(14101f), 19.4, -18.6, 6.2, 5.8); R(H(14101f), 20.4, -13.2, 4.2, 3.2);
    R(H(f1ecf7), 20, -18, 5, 4.8); R(H(f1ecf7), 21, -13.4, 3, 2.6);
    R(H(14101f), 20.6, -16.6, 1.5, 1.6); R(H(14101f), 22.9, -16.6, 1.5, 1.6); R(H(14101f), 22.1, -14.6, 0.8, 0.9); R(H(14101f), 21.9, -12.2, 0.5, 1.2); R(H(14101f), 22.9, -12.2, 0.5, 1.2);
  }
  else if (!strcmp(k, "stars")) {                               // one big outlined star with two little ones riding along
    cv_fill(H(14101f)); star5(22.4, -13.6, 5, 0.12); cv_fill(H(ffe14a)); star5(22.4, -13.6, 3.8, 0.12); cv_fill(H(fff3a8)); star5(22, -14.4, 1.4, 0.12);
    cv_fill(H(14101f)); star5(27.2, -16.8, 2.2, 0.4); cv_fill(H(ffffff)); star5(27.2, -16.8, 1.5, 0.4);
    cv_fill(H(14101f)); star5(17.4, -16.6, 1.9, 0.2); cv_fill(H(ff3ea5)); star5(17.4, -16.6, 1.2, 0.2);
  }
  else if (!strcmp(k, "batmoon")) {                             // Halloween: a bat flying across a full orange moon
    C(H(14101f), 22.5, -14, 4.8); C(H(ffb347), 22.5, -14, 4.1); C(H(f09a2a), 24, -12.4, 1); C(H(f09a2a), 21, -15.8, 0.7);
    double f = sin(T * 6) * 0.5 - 0.1; cv_fill(H(14101f));
    POLY(22.5, -14.6, 19.6, -15.8 + f, 18.6, -13.8 + f, 20.4, -14.2, 21.6, -13, 22.5, -13.6, 23.4, -13, 24.6, -14.2, 26.4, -13.8 + f, 25.4, -15.8 + f);
    F(22, -15.4, 1, 0.8);
  }
  else if (!strcmp(k, "eight")) { C(H(14101f), 22.5, -14, 4); C(rgba(255, 255, 255, .35), 21, -15.8, 1); C(H(f1ecf7), 22.5, -14, 2); C(H(14101f), 22.5, -14.7, 0.7); C(H(14101f), 22.5, -13.2, 0.8); }
  else if (!strcmp(k, "hearts")) {
    static const double hs[3][3] = { { 15.5, -13.5, 1 }, { 21.5, -15.5, 1.4 }, { 25.5, -11, 0.9 } };
    for (int i = 0; i < 3; i++) { double hx = hs[i][0], hy = hs[i][1], s = hs[i][2]; C(H(ff3ea5), hx - s, hy, s * 1.1); C(H(ff3ea5), hx + s, hy, s * 1.1); POLY(hx - s * 2, hy + 0.4, hx + s * 2, hy + 0.4, hx, hy + s * 2.8); }
  }
  else if (!strcmp(k, "sun")) {                                 // a sunburst: wedge rays that turn slowly round a hot core
    for (int i = 0; i < 12; i++) { double a = i * PI / 6 + T * 0.4; cv_fill(i & 1 ? H(ff8a1f) : H(e0242e)); POLY(22.5, -14, 22.5 + cos(a - 0.17) * 11, -14 + sin(a - 0.17) * 11, 22.5 + cos(a + 0.17) * 11, -14 + sin(a + 0.17) * 11); }
    C(H(14101f), 22.5, -14, 3.6); C(H(ffd21f), 22.5, -14, 2.8); C(H(fff3a8), 21.8, -14.8, 1);
  }
  else if (!strcmp(k, "roundel")) {
    C(H(14101f), 22.5, -14, 4.4); C(H(f1ecf7), 22.5, -14, 3.6);
    cv_font(FONT_MONO); cv_text("1", 22.5, -13.6, 6, H(e0242e), AL_CENTER, BL_MIDDLE); cv_text("1", 22.8, -13.6, 6, H(e0242e), AL_CENTER, BL_MIDDLE); cv_font(FONT_SYS);   // twice: bold
  }
  else if (!strcmp(k, "kanji")) {                               // 激 in white with a heavy black edge, so it reads on gold and on black
    cv_font(FONT_SYS); textOutlined("激", 22.5, -13.5, 10, H(f1ecf7), H(14101f), 0.8);
    R(H(e0242e), 27.6, -18, 1.4, 1.4);
  }
  cv_restore();
}
static void drawDecal(const char *k) {
  if (strcmp(k, "stripe") && strcmp(k, "flames") && strcmp(k, "plate")) { drawDecal2(k); return; }
  if (!strcmp(k, "plate")) {
    R(H(14101f), -14, -12, 11, 7); R(H(f1ecf7), -13, -11, 9, 5);
    cv_font(FONT_MONO); cv_text("52", -11.5, -7, 5, H(14101f), AL_LEFT, BL_ALPHA); cv_text("52", -11.3, -7, 5, H(14101f), AL_LEFT, BL_ALPHA); cv_font(FONT_SYS);
    return;
  }
  cv_save(); cv_begin(); cv_ellipse(20, -14, 9, 5, 0, 0, 7); cv_clip();
  if (!strcmp(k, "stripe")) {                                   // twin racing stripes nose to tail, edged so they pop
    const double sy[2] = { -16.4, -13.4 }; for (int i = 0; i < 2; i++) { R(H(14101f), 10, sy[i] - 0.6, 20, 2.6); R(H(f1ecf7), 10, sy[i], 20, 1.4); }
  } else {                                                      // hot-rod flames licking back from the nose of the tank
    const double dxs[4] = { -1.2, 0, 3, 6 }; const col_t cs[4] = { H(14101f), H(e0242e), H(ff8a1f), H(ffe14a) };
    for (int i = 0; i < 4; i++) {
      double dx = dxs[i]; cv_fill(cs[i]); cv_begin(); cv_move(30, -19); cv_line(23 + dx, -18); cv_quad(17 + dx, -17.6, 13 + dx, -17); cv_quad(18 + dx, -15.6, 21 + dx, -15.2);
      cv_quad(15 + dx, -14.4, 10.5 + dx, -13.2); cv_quad(16 + dx, -12, 20 + dx, -12); cv_quad(16 + dx, -10.6, 13.5 + dx, -9.2); cv_line(30, -9); cv_fillpath();
    }
  }
  cv_restore();
}

/* ---------- gear ---------- */
static void drawGear3(void) {                                   // the beer mug, drawn at its own size
  cv_fill(H(f0a81f)); POLY(17.2, -24, 22.4, -24, 21.8, -16.6, 17.8, -16.6);
  R(H(c47d10), 17.8, -18.4, 4, 1.8); R(H(fff6d8), 16.8, -26, 6, 2.4); C(H(fff6d8), 18, -26.4, 1.3); C(H(fff6d8), 21.2, -26.6, 1.4); R(rgba(255, 255, 255, .55), 18.2, -23, 0.9, 5);
  for (int i = 0; i < 3; i++) { double p = tmod(T * 0.9 + i / 3.0); R(rgba(255, 246, 216, (1 - p) * 0.9), 18.6 + i * 1.2, -17.5 - p * 6, 0.7, 0.7); }
  A(H(d9dcf0), 0.7, 22.8, -20.5, 1.7, -1.3, 1.3);
}
static void fuzz(double x, double y, double sz, col_t c) {
  R(rgba(255, 255, 255, .28), x - 0.6, y - 0.6, sz + 1.2, sz + 1.2); R(c, x, y, sz, sz);
  for (int i = 0; i < 4; i++) R(c, x - 0.5 + fmod(i * 1.7, sz), i & 1 ? y - 0.5 : y + sz - 0.2, 0.7, 0.7);
}
static void pips(double x, double y, col_t c, int n) {
  static const double p1[2] = { 1.6, 1.6 }, p3[6] = { 0.5, 0.5, 1.6, 1.6, 2.7, 2.7 }, p5[10] = { 0.5, 0.5, 2.7, 0.5, 1.6, 1.6, 0.5, 2.7, 2.7, 2.7 };
  const double *at = n == 1 ? p1 : n == 3 ? p3 : p5;
  for (int i = 0; i < n; i++) R(c, x + at[i * 2], y + at[i * 2 + 1], 0.9, 0.9);
}
static void drawGear2(const char *k) {
  double w = T * 14, a0 = cv_get_alpha();
  if (!strcmp(k, "beer")) {
    R(H(8d8fa8), 30.2, -18.6, 2.4, 2.6); R(H(8d8fa8), 24.2, -17.2, 6.2, 1); R(H(8d8fa8), 24.2, -18.8, 0.8, 1.8); R(H(8d8fa8), 29.4, -18.8, 0.8, 1.8); R(H(5e5a76), 24.2, -16.4, 6.2, 0.6);
    cv_save(); cv_translate(27, -17.2); cv_scale(0.8, 0.8); cv_translate(-20, 16.6); drawGear3(); cv_restore();
  }
  else if (!strcmp(k, "spider")) {                              // Halloween: a fat spider bobbing on a thread off the handlebar
    double dy = sin(T * 2.2) * 2.2, sx = 34, sy = -15 + dy;     // out in front of the fork, where it shows against the sky
    cv_begin(); cv_move(29, -27); cv_quad(33, -27, sx, sy - 1.6); cv_stroke(rgba(239, 234, 223, .85), 0.4);
    C(rgba(239, 234, 223, .35), sx, sy, 3);
    for (int i = 0; i < 4; i++) {
      double kk = sin(T * 9 + i) * 0.4; cv_begin();
      cv_move(sx, sy); cv_line(sx - 2.4, sy - 1.6 + i * 1.1 + kk); cv_line(sx - 3.2, sy - 0.4 + i * 1.3);
      cv_move(sx, sy); cv_line(sx + 2.4, sy - 1.6 + i * 1.1 - kk); cv_line(sx + 3.2, sy - 0.4 + i * 1.3); cv_stroke(H(14101f), 0.6);
    }
    C(H(14101f), sx, sy + 0.6, 1.9); C(H(14101f), sx, sy - 1.2, 1.2); R(H(ff3a3a), sx - 0.7, sy - 1.5, 0.5, 0.5); R(H(ff3a3a), sx + 0.2, sy - 1.5, 0.5, 0.5); R(H(ff8a1f), sx - 0.4, sy + 0.4, 0.8, 1);
  }
  else if (!strcmp(k, "watch")) {
    cv_begin(); cv_move(5, -26); cv_quad(4, -21, 8.4, -19.6); cv_stroke(H(ffd21f), 0.6);
    C(H(ffd21f), 9.4, -18.2, 2.6); C(H(f1ecf7), 9.4, -18.2, 1.9);
    cv_begin(); cv_move(9.4, -18.2); cv_line(9.4 + cos(T * 2) * 1.5, -18.2 + sin(T * 2) * 1.5); cv_move(9.4, -18.2); cv_line(9.4 + cos(T * 0.2) * 1, -18.2 + sin(T * 0.2) * 1); cv_stroke(H(14101f), 0.4);
    R(H(ffd21f), 9, -21, 0.8, 0.8);
  }
  else if (!strcmp(k, "monocle")) { ring(H(e9b923), 0.9, 20.5, -36, 3); L(rgba(233, 185, 35, .9), 0.5, 22, -33.5, 23 + sin(w) * 0.5, -27); R(rgba(255, 255, 255, .5), 19, -37.5, 1, 1); }
  else if (!strcmp(k, "pipe")) {
    R(H(5a3a1a), 19.5, -32, 5.5, 1.6); R(H(3a2410), 24, -35.5, 3.6, 4.6); R(H(ff8a2f), 24.8, -35.8, 1.6, 0.8);
    for (int i = 0; i < 3; i++) { double p = tmod(T * 1.2 + i / 3.0); cv_alpha(a0 * (1 - p) * 0.6); C(H(e8e8f0), 26 + sin(p * 6 + i) * 2, -37 - p * 11, 1 + p * 1.6); }
    cv_alpha(a0);
  }
  else if (!strcmp(k, "dice")) {                                // a pair of fuzzy dice swinging off the bars, a little out of step
    double s1 = sin(T * 6) * 1.3, s2 = sin(T * 6 - 0.9) * 1.3, ax = 27.6;
    cv_begin(); cv_move(ax, -24.6); cv_line(ax - 1.6 + s1, -19.8); cv_move(ax, -24.6); cv_line(ax + 2.6 + s2, -20.8); cv_stroke(H(f1ecf7), 0.5);
    fuzz(ax - 3.6 + s1, -19.8, 4, H(f1ecf7)); pips(ax - 3.6 + s1, -19.8, H(14101f), 5);
    fuzz(ax + 0.8 + s2, -20.8, 4, H(e0242e)); pips(ax + 0.8 + s2, -20.8, H(f1ecf7), 3);
  }
  else if (!strcmp(k, "pirate")) {
    L(H(b8b8c8), 1, -3, -27, -3, -52);
    cv_fill(H(14101f)); POLY(-3, -52, -16, -51 + sin(w) * 1.4, -14, -46 + sin(w + 1) * 1.6, -3, -45.5);
    C(H(f1ecf7), -9, -48.5 + sin(w + 0.5) * 0.8, 1.6);
    cv_begin(); cv_move(-12, -46.5); cv_line(-6, -50); cv_move(-12, -50); cv_line(-6, -46.5); cv_stroke(H(f1ecf7), 0.6);
  }
  else if (!strcmp(k, "foxtail")) {
    L(H(b8b8c8), 0.8, -5, -22, -8, -44); double sw = sin(w) * 1.5;
    E(H(ff7a1f), -9 + sw, -47, 2.4, 5, 0.5 + sw * 0.05, 0, 7); E(H(f1ecf7), -11 + sw * 1.4, -51.5, 1.6, 2.4, 0.5, 0, 7);
  }
  else if (!strcmp(k, "goggles")) {
    L(H(3a2a1a), 1.2, 9, -39.5, 22, -40);
    C(H(14101f), 15.5, -41.5, 2.9); C(rgba(255, 170, 40, .95), 15.5, -41.5, 2.1); C(H(14101f), 20, -41.2, 2.9); C(rgba(255, 170, 40, .95), 20, -41.2, 2.1);
    R(rgba(255, 255, 255, .7), 14.8, -42.4, 1, 1); R(rgba(255, 255, 255, .7), 19.3, -42.1, 1, 1);
  }
  else if (!strcmp(k, "bowtie")) {
    cv_fill(H(e0242e)); cv_begin(); cv_move(11, -30); cv_line(6.5, -32.5); cv_line(6.5, -27.5); cv_move(11, -30); cv_line(15.5, -32.5); cv_line(15.5, -27.5); cv_fillpath();
    R(H(7a1018), 10, -31, 2, 2.2); R(rgba(255, 255, 255, .7), 8, -31.5, 1, 1); R(rgba(255, 255, 255, .7), 13.2, -31.5, 1, 1);
  }
  else if (!strcmp(k, "pigeon")) {
    double b = sin(T * 9) * 0.5;
    E(H(8d8fa8), 27, -31.5 + b, 4.2, 2.8, -0.1, 0, 7); C(H(6a6c88), 25.5, -31.5 + b, 2.1); C(H(5a6a9a), 30, -34 + b, 2);
    R(H(ff8a2f), 31.6, -34 + b, 1.6, 0.8); R(H(14101f), 30.2, -34.6 + b, 0.8, 0.8);
    cv_begin(); cv_move(26, -29); cv_line(26, -27.5); cv_move(28.5, -29); cv_line(28.5, -27.5); cv_stroke(H(ff8a2f), 0.6);
  }
  else if (!strcmp(k, "medal")) {
    cv_fill(H(3a6ae0)); POLY(8, -31.5, 10.4, -31.5, 11.6, -24.6, 10.2, -24.6);
    cv_fill(H(e0242e)); POLY(13.6, -31.5, 16, -31.5, 12.6, -24.6, 11.2, -24.6);
    double sw = sin(T * 5) * 0.4; C(H(8a4a1a), 11.6 + sw, -21.8, 2.9); C(H(d4874a), 11.6 + sw, -21.8, 2.2); cv_fill(H(ffd9a8)); star5(11.6 + sw, -21.8, 1.3, 0);
    R(rgba(255, 255, 255, 0.5 + 0.5 * sin(T * 4)), 10.2 + sw, -23.4, 0.8, 0.8);
  }
  else if (!strcmp(k, "chain")) {
    cv_begin(); cv_move(8, -31.5); cv_quad(11.6, -22, 16, -31.5); cv_stroke_dash(H(ffd21f), 1.1, 1.2, 0.6);
    double sw = sin(T * 5) * 0.5; R(H(ffd21f), 10 + sw, -26, 3.6, 4.4);
    int f0 = cv_get_font(); cv_font(FONT_MONO); cv_text("$", 11.8 + sw, -23.7, 4, H(a87400), AL_CENTER, BL_MIDDLE); cv_font(f0);
    R(rgba(255, 255, 255, 0.5 + 0.5 * sin(T * 6)), 10.4 + sw, -25.6, 0.8, 0.8);
  }
  else if (!strcmp(k, "mug")) {
    R(H(f1ecf7), 17.5, -23, 4.6, 6.2); R(H(14101f), 17, -24, 5.6, 1.4); R(H(e0242e), 17.5, -20.6, 4.6, 1.2); A(H(f1ecf7), 0.9, 23, -20, 1.8, -1.2, 1.2);
    for (int i = 0; i < 2; i++) { double p = tmod(T * 1.3 + i / 2.0); cv_alpha(a0 * (1 - p) * 0.55); C(H(e8e8f0), 19.8 + sin(p * 7 + i * 3) * 1.4, -25 - p * 8, 0.9 + p); }
    cv_alpha(a0);
  }
}
static void drawGear(const char *k) {
  if (strcmp(k, "shades") && strcmp(k, "streamers") && strcmp(k, "charm") && strcmp(k, "scarf")) { drawGear2(k); return; }
  double w = T * 14;
  if (!strcmp(k, "shades")) { cv_fill(H(0a0612)); F(17, -38, 7, 3); F(15, -37.5, 3, 1); R(rgba(255, 255, 255, .55), 18, -37.5, 2, 1); }
  else if (!strcmp(k, "streamers")) {
    const col_t cs[2] = { H(ff3ea5), H(22e6d8) }; const double ph[2] = { 0, 1.2 };
    for (int j = 0; j < 2; j++) { cv_begin(); cv_move(27, -27 + ph[j]); for (int i = 1; i <= 5; i++) cv_line(27 - i * 3.2, -27 + ph[j] + i * 0.4 + sin(w + i * 0.9 + ph[j]) * 1.6); cv_stroke(cs[j], 1.4); }
  }
  else if (!strcmp(k, "charm")) {
    double sx = 28.5 + sin(T * 7) * 1.4, cy = -3.4;              // a lucky gold horseshoe on a chain, open end up so the luck stays in
    cv_begin(); cv_move(28, -11); cv_line(sx, cy - 2.4); cv_stroke_dash(H(d9dcf0), 0.6, 0.8, 0.5);
    A(H(a87400), 2.2, sx, cy, 2.2, -0.3, PI + 0.3); A(H(ffd21f), 1.3, sx, cy, 2.2, -0.3, PI + 0.3);
    cv_fill(H(7a5200)); F(sx - 2.4, cy - 0.2, 0.7, 0.7); F(sx + 1.7, cy - 0.2, 0.7, 0.7); F(sx - 0.35, cy + 1.8, 0.7, 0.7);
    R(rgba(255, 255, 255, 0.5 + 0.5 * sin(T * 5)), sx - 2, cy + 0.8, 0.8, 0.8);
  }
  else {                                                        // a long knit scarf cracking in the wind
    R(H(e0242e), 10, -33, 8, 2.4); R(H(a01828), 10, -31.2, 8, 0.6);
    double px[7], py[7]; for (int i = 0; i < 7; i++) { px[i] = 11 - i * 2.4; py[i] = -32.6 + i * 0.5 + sin(w - i * 0.8) * i * 0.32; }
    for (int i = 0; i < 6; i++) { cv_fill(i == 3 || i == 5 ? H(f1ecf7) : H(e0242e)); POLY(px[i], py[i], px[i + 1], py[i + 1], px[i + 1], py[i + 1] + 2.4, px[i], py[i] + 2.4); }
    cv_begin(); for (int j = 0; j < 4; j++) { cv_move(px[6], py[6] + 0.3 + j * 0.6); cv_line(px[6] - 1.4 - (j & 1) * 0.4, py[6] + 0.5 + j * 0.7 + sin(w + j) * 0.3); } cv_stroke(H(e0242e), 0.5);
  }
}

/* ---------- canes ---------- */
static void neonTube(col_t c, double w) {
  cv_begin(); cv_move(-9, -22); cv_line(-11, -37); cv_quad(-11.4, -40.6, -8.4, -40.6); cv_quad(-5.6, -40.4, -5.8, -37.4); cv_stroke(c, w);
  C(c, -9, -22, w / 2); C(c, -5.8, -37.4, w / 2);               // round caps
}
static void drawCane(const char *k) {
  if (!strcmp(k, "cat")) return;                                // the Cat Pal is drawn last, on top (look_over)
  if (!*k) { L(H(8a5a2e), 2, -9, -22, -11, -37); cv_begin(); cv_move(-11, -37); cv_line(-7, -38); cv_line(-6, -35); cv_stroke(H(e8d9b0), 2); return; }
  if (!strcmp(k, "eightball")) { L(H(2a2236), 2, -9, -22, -11, -37); C(H(14101f), -11.2, -40, 4); C(H(f1ecf7), -11.2, -40, 2); C(H(14101f), -11.2, -40.8, 0.8); C(H(14101f), -11.2, -39.1, 0.9); C(rgba(255, 255, 255, .7), -13.2, -42.2, 0.9); }
  else if (!strcmp(k, "candy")) {
    L(H(f1ecf7), 2, -9, -22, -11, -37); cv_begin(); cv_move(-9, -22); cv_line(-11, -37); cv_stroke_dash(H(e0242e), 2, 2, 2);
    A(H(f1ecf7), 2, -8, -37, 3, PI, 0.5 + 2 * PI); cv_begin(); cv_arc(-8, -37, 3, PI, 0.5 + 2 * PI); cv_stroke_dash(H(e0242e), 2, 2, 2);
  }
  else if (!strcmp(k, "golden")) {                              // a gilded walking stick: banded shaft and a ruby-set knob that glints
    L(H(a87400), 2.4, -9, -22, -11, -37); L(H(e9b923), 1.6, -9, -22, -11, -37); L(H(fff3a8), 0.5, -9.5, -23, -11.3, -36);
    const double ts[2] = { 0.25, 0.6 }; for (int i = 0; i < 2; i++) R(H(fff3a8), -9 - 2 * ts[i] - 1.2, -22 - 15 * ts[i], 2.4, 0.8);
    C(H(a87400), -11.3, -39.6, 3.3); C(H(ffd21f), -11.3, -39.6, 2.6); C(H(fff3a8), -12.3, -40.6, 0.9);
    C(H(e0242e), -11.1, -39.4, 1.1); R(H(ff9a9a), -11.6, -39.9, 0.5, 0.5); R(H(ffd21f), -11.8, -43.6, 1.2, 1.4);
    if (tmod(T * 0.6) < 0.15) { R(H(ffffff), -13.6, -42.6, 0.8, 2.6); R(H(ffffff), -14.5, -41.7, 2.6, 0.8); }
  }
  else if (!strcmp(k, "neon")) {                                // a bent glass tube: soft glow, coloured gas, a hot white core
    double h = 180 + sin(T * 2) * 70, fl = fmod(T, 2.7) < 0.08 ? 0.4 : 1;
    neonTube(hsla(h, 100, 60, 0.12 * fl), 6); neonTube(hsla(h, 100, 60, 0.25 * fl), 3.6);
    neonTube(hsla(h, 100, 62, fl), 1.7); neonTube(rgba(255, 255, 255, 0.8 * fl), 0.6);
    R(H(3a3556), -9.8, -24, 2, 1.6);
  }
  else if (!strcmp(k, "broom")) {                               // Halloween: a witch's broom, straw end up and twitching
    L(H(6a4a2a), 1.6, -9, -22, -11.4, -38); L(H(8a6440), 0.5, -9.4, -23, -11.4, -37);
    R(H(c0242e), -12.6, -39.4, 2.8, 1.4);
    double sw = sin(T * 4) * 0.5;
    for (int i = 0; i < 9; i++) { double t = (i - 4) / 4.0; L(i & 1 ? H(d9a84a) : H(b8862a), 0.9, -11.4 + t * 1.2, -39.2, -11.8 + t * 3.6 + sw, -46.4 + fabs(t) * 0.8); }
    L(H(7a4a1a), 0.7, -13.4, -41.6, -9.6, -41.6);
  }
  else if (!strcmp(k, "poolcue")) { L(H(2a1a10), 2.2, -9, -22, -10, -29); L(H(d9b27a), 1.6, -10, -29, -12.4, -42); L(H(f1ecf7), 0.8, -10.1, -30, -10.5, -31.5); R(H(3a8ae0), -13.4, -43.6, 2, 1.8); }
  else if (!strcmp(k, "skull")) {
    L(H(3a2a2a), 2, -9, -22, -11, -36); C(H(f1ecf7), -11.4, -39.5, 3.4); R(H(f1ecf7), -13.2, -37.5, 3.6, 2);
    cv_fill(H(14101f)); F(-13.2, -40.6, 1.4, 1.4); F(-10.4, -40.6, 1.4, 1.4); F(-12, -38, 0.5, 1.6); F(-10.6, -38, 0.5, 1.6);
  }
  else if (!strcmp(k, "flamingo")) {
    L(H(ff7ab8), 1.6, -9, -22, -11, -36); cv_begin(); cv_move(-11, -36); cv_quad(-14, -40, -10, -42); cv_quad(-8, -43, -7, -41.6); cv_stroke(H(ff7ab8), 1.5);
    C(H(ff7ab8), -7, -41.6, 1.5); cv_fill(H(14101f)); POLY(-6, -42.3, -4, -40.8, -6.4, -40.6); R(H(f1ecf7), -7.6, -42.4, 0.8, 0.8);
  }
  else if (!strcmp(k, "wizard")) {
    L(H(6a4a2a), 2, -9, -22, -11, -37); L(H(4a3018), 1, -10, -28, -9.4, -26);
    double p = 0.6 + 0.4 * sin(T * 4); C(rgba(125, 249, 255, 0.25 * p), -11.6, -40, 5.2); C(H(7df9ff), -11.6, -40, 2.8); C(H(f1ecf7), -12.6, -41, 1);
    A(H(6a4a2a), 1, -11.6, -40, 3.6, PI * 0.9, PI * 2.1);
  }
  else if (!strcmp(k, "fishing")) {
    L(H(3a2a1a), 1.4, -9, -22, -13, -45); double sw = sin(T * 3) * 1.5;
    cv_begin(); cv_move(-13, -45); cv_quad(-19 + sw, -40, -17 + sw, -33); cv_stroke(rgba(241, 236, 247, .8), 0.4);
    C(H(e0242e), -17 + sw, -33, 1.4); R(H(f1ecf7), -17.6 + sw, -33.2, 1.2, 0.8); C(H(8d8fa8), -10, -29, 1.4);
  }
  else if (!strcmp(k, "golf")) {
    L(H(14101f), 2, -9, -22, -9.8, -28); L(H(c8c8d8), 1.2, -9.8, -28, -12.4, -41);
    cv_fill(H(8d8fa8)); POLY(-12.4, -41.4, -7.2, -43, -6.4, -40.4, -12.8, -39.4); L(H(e8e8f0), 0.6, -12, -40.3, -7, -41.8); C(H(f1ecf7), -9.4, -45.6, 1.5);
  }
  else if (!strcmp(k, "bouquet")) {
    double sw = sin(T * 2.5) * 0.6; L(H(2f8a45), 1.6, -9, -22, -11, -37); cv_fill(H(2f8a45)); POLY(-13, -36, -9, -36, -11, -32);
    static const struct { col_t c; double x, y; } fl[4] = { { H(ff3ea5), -13.6, -40.5 }, { H(ffe14a), -9, -41 }, { H(f1ecf7), -11.4, -43.5 }, { H(ff6a3a), -11.4, -39.4 } };
    for (int j = 0; j < 4; j++) { for (int i = 0; i < 5; i++) C(fl[j].c, fl[j].x + sw + cos(i * 1.257) * 1.7, fl[j].y + sin(i * 1.257) * 1.7, 1.1); C(H(ffd21f), fl[j].x + sw, fl[j].y, 0.9); }
  }
  else if (!strcmp(k, "trophy")) {
    L(H(c98a1f), 2, -9, -22, -11, -37); cv_fill(H(ffd21f)); POLY(-14.6, -45, -7.8, -45, -8.8, -39.6, -13.6, -39.6);
    cv_begin(); cv_arc(-15, -43.2, 1.6, PI * 0.5, PI * 1.5); cv_arc(-7.4, -43.2, 1.6, -PI * 0.5, PI * 0.5); cv_stroke(H(ffd21f), 0.9);
    cv_fill(H(c98a1f)); F(-12.6, -39.6, 2.8, 1.4); F(-13.6, -38.2, 4.8, 1); C(H(fff3a8), -12.4, -43.4, 0.9);
    R(rgba(255, 255, 255, 0.5 + 0.5 * sin(T * 5)), -9.8, -44.4, 1, 1);
  }
  else if (!strcmp(k, "lollipop")) {
    L(H(f1ecf7), 1.6, -9, -22, -11, -39); C(H(ff3ea5), -11.4, -42.5, 4.6);
    cv_begin(); for (int i = 0; i < 24; i++) { double a = i * 0.45 + T * 1.2, r = 0.4 + i * 0.16; cv_line(-11.4 + cos(a) * r, -42.5 + sin(a) * r); } cv_stroke(H(f1ecf7), 1);
  }
}

/* ---------- carriers ---------- */
static void drawCarrier(const char *k) {
  R(H(3a3556), -15, -13, 13, 1.6);
  if (!strcmp(k, "crate")) {                                    // a plastic milk crate: grid of slots, thick rim, a hand hole
    R(H(3a7ab8), -15, -26, 13, 13); R(H(5a9ad8), -15, -26, 13, 1.6); R(H(2a5a8a), -15, -14.4, 13, 1.4);
    cv_fill(H(14283e)); for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) F(-13.6 + c * 3, -21.2 + r * 2.4, 1.8, 1.4);
    R(H(14283e), -11.4, -24, 6, 1.2); R(H(5a9ad8), -15, -26, 1, 13); R(H(5a9ad8), -8.9, -24.4, 0.8, 10);
    R(H(f1ecf7), -6.2, -16.4, 3, 1.2);
  }
  else if (!strcmp(k, "cauldron")) {                            // Halloween: a black cauldron of bubbling green brew
    E(H(1a1622), -8.5, -19, 7, 6.2, 0, -PI * 0.05, PI * 1.05);   // the web game's anticlockwise arc from 1.05 pi to 1.95 pi: the bowl
    R(H(1a1622), -15.2, -24.6, 13.4, 2); R(H(3a3448), -15.2, -24.6, 13.4, 0.7); R(H(1a1622), -13.4, -14, 1.6, 1.4); R(H(1a1622), -5.2, -14, 1.6, 1.4);
    R(rgba(125, 255, 120, 0.7 + 0.3 * sin(T * 5)), -14.4, -25.4, 11.8, 1.2);
    for (int i = 0; i < 4; i++) { double p = tmod(T * 0.9 + i / 4.0); C(rgba(125, 255, 120, 1 - p), -13 + i * 3 + sin(i * 5) * 0.8, -26 - p * 7, 0.6 + p * 1.1); }
    R(H(3a3448), -12, -20, 1, 1); R(rgba(255, 255, 255, .18), -13.6, -22, 1, 5);
  }
  else if (!strcmp(k, "wicker")) {
    cv_fill(H(c9a05a)); POLY(-15, -24, -2, -24, -4, -13, -13, -13);
    cv_begin(); for (double y = -22; y < -13; y += 2.4) { cv_move(-14.5, y); cv_line(-2.5, y); } cv_stroke(H(8a6020), 0.7);
    A(H(8a6020), 1.2, -8.5, -24, 6, PI, 2 * PI);
    for (int i = 0; i < 6; i++) R(i & 1 ? H(f1ecf7) : H(e0242e), -14.5 + i * 2.1, -26.5, 2.1, 2.6);
  }
  else if (!strcmp(k, "pizza")) {
    for (int i = 0; i < 3; i++) { R(H(d9b27a), -15, -17 - i * 4.6, 13, 4.2); R(H(a9824a), -15, -14 - i * 4.6, 13, 1.1); }
    C(H(e0242e), -8.5, -26.3, 2.6); R(H(fff3a8), -9.4, -27, 1.8, 1.4); L(H(e0242e), 1, -15, -17, -2, -17); R(H(e8e8e8), -12, -31.5, 7, 1.2);
  }
  else if (!strcmp(k, "toolbox")) { R(H(d8243a), -15, -24, 13, 11); R(H(a01828), -15, -17, 13, 1.2); R(H(f1ecf7), -15, -24, 13, 1.2); A(H(14101f), 1.4, -8.5, -24, 3.6, PI, 2 * PI); R(H(c8c8d8), -9.5, -20, 2, 2); }
  else if (!strcmp(k, "paperbag")) {
    R(H(c8a96a), -15, -24, 13, 11); R(H(a9884a), -15, -24, 13, 3); R(H(8a6a30), -15, -17, 13, 0.8); R(H(f1ecf7), -13, -21.4, 9, 2.6); R(H(14101f), -12.4, -20.8, 3, 0.7); R(H(14101f), -12.4, -19.6, 7, 0.5);
    L(H(f1ecf7), 3, -12.6, -24, -14, -29.6); L(H(e0242e), 3, -13.2, -26, -12.6, -27); L(H(e8e4f4), 3, -4.4, -24, -3, -29.6); L(H(e0242e), 3, -4, -26, -3.6, -27);
    R(H(14101f), -14.4, -28.4, 1, 0.4); R(H(14101f), -3.6, -28.4, 1, 0.4);
  }
  else if (!strcmp(k, "cooler")) {
    R(H(d8243a), -15, -22, 13, 9); R(H(f1ecf7), -15, -26, 13, 4.4); R(H(c8c8d8), -15, -22.4, 13, 0.8); A(H(14101f), 1.2, -8.5, -26, 3, PI, 2 * PI);
    R(H(7fd8ff), -12, -19.5, 7, 3); R(H(f1ecf7), -11, -18.8, 1.6, 1.6);
  }
  else if (!strcmp(k, "guitar")) {
    cv_fill(H(3a2a1e)); cv_begin(); cv_ellipse(-8.5, -29, 4.6, 5.6, 0, 0, 7); cv_ellipse(-8.5, -19, 6.4, 6.2, 0, 0, 7); cv_fillpath();
    R(H(3a2a1e), -13, -29, 9, 10); R(H(5a4030), -13, -29, 1.4, 10); R(H(ffd21f), -10, -27, 3, 1.4); R(H(ffd21f), -7, -17, 3, 1.4); R(H(e0242e), -11, -22, 3, 2.4); R(H(22e6d8), -8, -24, 2.6, 2);
  }
  else if (!strcmp(k, "duffel")) {                              // a barrel gym bag: varsity stripes, round end caps, zip and strap
    A(H(14101f), 1.2, -8.5, -22.6, 4.4, PI, 2 * PI);
    R(H(1f6a40), -14, -23, 11, 10); cv_fill(H(1f6a40)); cv_begin(); cv_ellipse(-14, -18, 1.8, 5, 0, 0, 7); cv_ellipse(-3, -18, 1.8, 5, 0, 0, 7); cv_fillpath();
    R(H(2f8a55), -14, -23, 11, 1.6); R(H(144a2c), -14, -14.6, 11, 1.6);
    R(H(f1ecf7), -14, -20.4, 11, 1); R(H(ffd21f), -14, -19.2, 11, 1); R(H(f1ecf7), -14, -18, 11, 1);
    E(H(14402a), -2.6, -18, 1.4, 4.2, 0, 0, 7); C(H(ffd21f), -2.6, -18, 0.9);
    R(H(c8c8d8), -12.4, -22.8, 8, 0.5); R(H(d9dcf0), -5, -23.4, 1, 1.6);
  }
  else if (!strcmp(k, "chest")) {
    R(H(7a4a1a), -15, -22, 13, 9); cv_fill(H(9a6228)); cv_begin(); cv_move(-15, -22); cv_quad(-8.5, -31, -2, -22); cv_fillpath();
    R(H(ffd21f), -15, -19, 13, 1.4); R(H(ffd21f), -9.4, -26, 1.8, 6); R(H(14101f), -9, -20, 1, 1.4); R(H(ffd21f), -15, -22, 1.4, 9); R(H(ffd21f), -3.4, -22, 1.4, 9);
    cv_fill(rgba(255, 255, 255, 0.5 + 0.5 * sin(T * 6))); F(-12, -27, 1, 1); F(-5, -25, 1, 1);
  }
  else if (!strcmp(k, "boombox")) {
    R(H(6a6a80), -15, -25, 13, 11); R(H(8a8aa0), -15, -25, 13, 1.4); A(H(14101f), 1.2, -8.5, -25, 3.6, PI, 2 * PI);
    double b = 1 + 0.25 * sin(T * 12); const double xs[2] = { -11.3, -5.7 };
    for (int i = 0; i < 2; i++) { C(H(14101f), xs[i], -19, 3.2); ring(rgba(34, 230, 216, .9), 0.7, xs[i], -19, 2.2 * b); C(H(22e6d8), xs[i], -19, 0.8); }
    R(H(14101f), -9.6, -23.6, 2.2, 1);
  }
  else if (!strcmp(k, "piggy")) {
    E(H(ff9ac8), -8.5, -19.5, 7, 5.6, 0, 0, 7); R(H(ff9ac8), -14, -15.4, 2, 2.6); R(H(ff9ac8), -5, -15.4, 2, 2.6);
    E(H(ff7ab0), -1.8, -20.5, 1.8, 2.2, 0, 0, 7); R(H(c0407a), -2.4, -21.4, 0.6, 0.8); R(H(c0407a), -1.4, -21.4, 0.6, 0.8);
    cv_fill(H(ff7ab0)); POLY(-6, -24.6, -4, -27.4, -3.4, -23.8); R(H(14101f), -4.6, -22, 1, 1); R(H(8a2050), -10.4, -24.9, 3.6, 0.8);
    double p = tmod(T * 0.8); if (p < 0.5) { C(H(ffd21f), -8.6, -27.5 + p * 5, 1.5); R(H(fff3a8), -9.1, -28 + p * 5, 0.8, 0.8); }
    A(H(ff7ab0), 0.7, -16, -20.5, 1.2, 0, 5);
  }
  else if (!strcmp(k, "lunch")) {                               // a black dome-top worker's pail: chrome latches, handle, a thermos in the lid
    cv_begin(); cv_move(-12.6, -24.6); cv_line(-12.6, -27.6); cv_line(-4.4, -27.6); cv_line(-4.4, -24.6); cv_stroke(H(c8c8d8), 1);
    E(H(2a2a3a), -8.5, -21.4, 6.5, 4, 0, PI, 2 * PI); R(H(2a2a3a), -15, -21.4, 13, 8.4);
    E(H(4a4a62), -9.5, -23.2, 3.6, 1.2, -0.15, PI, 2 * PI);
    R(H(14141e), -15, -21.6, 13, 0.8); R(H(3a3a52), -15, -17.2, 13, 0.6);
    const double xs[2] = { -13.8, -4.4 }; for (int i = 0; i < 2; i++) { R(H(d9dcf0), xs[i], -22.2, 1.4, 2.6); R(H(8d8fa8), xs[i], -20, 1.4, 0.6); }
    R(H(e0242e), -10.2, -26, 3.4, 1.4); R(H(ff6a5a), -10.2, -26, 1, 1.4);
    R(H(ffd21f), -11, -19.2, 5, 2.6); R(H(14101f), -10.4, -18.4, 3.6, 0.5); R(H(14101f), -10.4, -17.4, 2.4, 0.5);
  }
}

/* ---------- hats ---------- */
static void drawCap(const char *k) {
  if (!strcmp(k, "straw")) {
    E(H(e3c872), 16, -39.5, 10, 2.2, 0, 0, 7); E(H(f0d98a), 16, -40.5, 5.5, 4.5, 0, PI, 2 * PI); R(H(8a5a2e), 10.5, -41.5, 11, 1.5);
  } else if (!strcmp(k, "hardhat")) {
    E(H(ffd21f), 15.5, -39.5, 6.5, 5.8, 0, PI, 2 * PI); cv_fill(H(e0b000)); F(8.5, -39.5, 17, 1.8); F(14.5, -45.5, 2, 6);
  } else if (!strcmp(k, "nightcap")) {
    cv_fill(H(3a4aa8)); POLY(9, -39, 22, -40, 19, -46, 9, -47, 2, -42, 4, -39);
    R(H(f1ecf7), 9, -40.5, 13, 1.6); cv_circle(3, -40.5, 2.2);
  } else if (!strcmp(k, "cowboy")) {
    E(H(8a5a2e), 15.5, -39.5, 12, 2.4, 0, 0, 7); cv_fill(H(a9703a)); POLY(10, -40, 11, -46, 14, -45, 16, -46.5, 19, -45, 21, -46, 21, -40);
    R(H(3a2410), 10.4, -42.4, 10.8, 1.4); R(H(ffd21f), 14.6, -42.6, 1.8, 1.8);
  } else if (!strcmp(k, "tophat")) {
    cv_fill(H(14101f)); F(7, -40.4, 17, 1.8); F(10, -53, 11, 12.6); R(H(e0242e), 10, -43, 11, 2.2); R(rgba(255, 255, 255, .22), 11.4, -52, 1.4, 8);
  } else if (!strcmp(k, "wizard")) {
    cv_fill(H(3a3ab0)); POLY(8, -39.5, 23, -39.5, 19, -49, 22, -57, 14.5, -50, 12, -44); E(H(3a3ab0), 15.5, -39.5, 10.5, 2.3, 0, 0, 7);
    cv_fill(H(ffd21f)); F(11, -42.6, 9, 1.2); F(14, -47, 1.4, 1.4); F(18, -45, 1.4, 1.4); F(16, -51, 1, 1);
  } else if (!strcmp(k, "chef")) {
    cv_fill(H(f7f4ff)); cv_circle(11, -45.5, 4); cv_circle(16, -48, 4.8); cv_circle(21, -45.5, 4); F(10, -45, 11, 5.6);
    cv_fill(H(d4d0e6)); F(10, -41.4, 11, 1.6); F(13.4, -46, 0.8, 4); F(17.4, -46, 0.8, 4);
  } else if (!strcmp(k, "party")) {
    cv_fill(H(ff3ea5)); POLY(10, -39.6, 21, -39.6, 15.5, -52); cv_fill(H(22e6d8)); POLY(11.6, -43.2, 14, -44.4, 13, -47); POLY(16.4, -44.8, 19, -42.4, 19.6, -40.6, 15.6, -42.6);
    C(H(ffe14a), 15.5, -52.5, 1.9); L(H(f1ecf7), 0.5, 10, -39.6, 10.5, -34);
  } else if (!strcmp(k, "viking")) {
    E(H(9a9ab0), 15.5, -39.5, 6.8, 5.8, 0, PI, 2 * PI); R(H(6a6a84), 8.6, -40.4, 13.8, 1.8); R(H(ffd21f), 14.6, -44.4, 1.8, 4);
    cv_fill(H(f1ecf7)); POLY(9, -41, 5, -43.5, 3.5, -49, 6.6, -45.5, 10, -43); POLY(22, -41, 26, -43.5, 27.5, -49, 24.4, -45.5, 21, -43);
  } else if (!strcmp(k, "propeller")) {
    E(H(e0242e), 15.5, -39.5, 6.2, 4.8, 0, PI, 2 * PI);
    cv_fill(H(3a8ae0)); cv_begin(); cv_ellipse(15.5, -39.5, 6.2, 4.8, 0, PI * 1.34, PI * 1.66); cv_line(15.5, -39.5); cv_fillpath();
    R(H(ffe14a), 9.4, -40.6, 12.2, 1.2); R(H(14101f), 15, -47.4, 1, 3); E(H(ffd21f), 15.5, -47.8, 1 + 5.4 * fabs(cos(T * 18)), 1, 0, 0, 7);
  } else if (!strcmp(k, "cone")) {
    cv_fill(H(ff6a1f)); POLY(11, -39.6, 20, -39.6, 17.4, -53, 13.6, -53); cv_fill(H(f1ecf7)); POLY(12.4, -44.6, 18.6, -44.6, 18, -47.6, 13, -47.6); R(H(e05510), 8.6, -40.6, 13.8, 1.4);
  } else if (!strcmp(k, "laurel")) {
    double fl = sin(T * 12);                                    // a gold-tied wreath, its red ribbon streaming out behind
    cv_fill(H(c0242e)); POLY(9.6, -40.4, 4, -40 + fl, 1.6, -38.6 + fl * 1.4, 3.6, -37.6 + fl, 9.4, -39); POLY(9.6, -40, 5, -37.6 - fl * 0.6, 3.4, -35.4 - fl, 5.6, -35.6 - fl * 0.6, 9.8, -38.8);
    cv_fill(H(2f7a3a)); cv_begin(); cv_ellipse(15.5, -39.4, 6.6, 5.4, 0, PI * 1.02, PI * 1.98); cv_line(21.6, -38.6); cv_line(9.4, -38.6); cv_fillpath();
    for (int i = 0; i < 12; i++) { double a = PI * 1.02 + i * PI * 0.087, lx = 15.5 + cos(a) * 6.5, ly = -39.2 + sin(a) * 5.5, side = i < 6 ? -1 : 1; E(i & 1 ? H(4fb85a) : H(8ae08a), lx, ly, 2.2, 1, a + PI / 2 + 0.6 * side, 0, 7); }
    E(H(f2c9a0), 15.5, -39, 4.6, 3.4, 0, PI, 2 * PI);
    cv_fill(H(ffd21f)); F(11 - 0.6, -42.6 - 0.6, 1.3, 1.3); F(20 - 0.6, -42.6 - 0.6, 1.3, 1.3); F(15.5 - 0.6, -44.8 - 0.6, 1.3, 1.3);
    R(H(e0242e), 8.8, -40.6, 1.8, 2.2);
  } else if (!strcmp(k, "pumpkin")) {                           // Halloween: a jack-o'-lantern worn as a helmet, candle flickering inside
    E(H(c85a12), 15.5, -43.5, 7.6, 6, 0, 0, 7); E(H(ff8a1f), 15.5, -43.5, 4.6, 6, 0, 0, 7);
    cv_fill(H(e0701a)); F(13.6, -49, 1, 11); F(17.2, -49, 1, 11);
    cv_fill(H(3e6a22)); POLY(15, -49, 16.4, -49, 17.6, -52.4, 16.4, -52.6);
    double a0 = cv_get_alpha(); cv_alpha(a0 * (0.7 + 0.3 * sin(T * 17))); cv_fill(H(ffe14a));
    POLY(18.6, -46, 21, -46, 19.8, -44); POLY(14.6, -46, 17, -46, 15.8, -44); F(15, -41.6, 7, 1.2); F(16, -40.6, 1, 1); F(19, -40.6, 1, 1);
    cv_alpha(a0);
  } else if (!strcmp(k, "santa")) {                             // Christmas: a floppy Santa hat with its pompom bouncing behind
    double bob = sin(T * 9) * 0.8;
    cv_fill(H(d8241e)); POLY(9.4, -40.4, 21.6, -40.4, 19, -46.6, 13, -49.6, 5.6, -47.4 + bob, 9.6, -45);
    cv_fill(H(a81a16)); POLY(13, -49.6, 5.6, -47.4 + bob, 9.6, -45, 12, -46.4);
    R(H(f7f4ff), 8.6, -41.8, 13.8, 2.6); cv_circle(5.2, -46.8 + bob, 2.1);
    R(H(d4d0e6), 8.6, -39.6, 13.8, 0.6);
  } else if (!strcmp(k, "crown")) {
    cv_fill(H(a87400)); POLY(8.6, -39.4, 8.4, -46.6, 12, -43.4, 15, -48, 18, -43.4, 21.6, -46.6, 21.4, -39.4);
    cv_fill(H(ffd21f)); POLY(9.4, -39.8, 9.2, -45.2, 12.2, -42.4, 15, -46.6, 17.8, -42.4, 20.8, -45.2, 20.6, -39.8);
    R(H(e9b923), 9.2, -41.8, 11.6, 2.2); cv_fill(H(fff3a8)); F(10, -44.4, 0.8, 3); F(9.2, -41.8, 11.6, 0.6);
    C(H(f1ecf7), 8.8, -46.8, 1.1); C(H(f1ecf7), 15, -48.2, 1.1); C(H(f1ecf7), 21.2, -46.8, 1.1);
    R(H(3a8ae0), 10.6, -41.4, 1.6, 1.4); R(H(e0242e), 14.2, -41.4, 1.6, 1.4); R(H(2fd07a), 17.8, -41.4, 1.6, 1.4);
    if (tmod(T * 0.7) < 0.18) { cv_fill(H(ffffff)); F(14.6, -46.4, 0.8, 2.4); F(13.8, -45.6, 2.4, 0.8); }
  } else {                                                      // the flat cap (green by default, or the red one)
    bool red = !strcmp(k, "redcap");
    cv_fill(red ? H(c0392b) : H(4a5a3a)); POLY(9, -39, 13, -43, 22, -42, 23, -39);
    R(red ? H(7a1a12) : H(2f3a24), 18, -40, 7, 2);
  }
}
static void drawSpecs(void) { cv_stroke_rect(18.5, -37.5, 3, 3, H(14101f), 1); R(rgba(190, 230, 255, .6), 19, -37, 3, 3); }

/* ---------- the Cat Pal cane: a black cat that lounges on top of his carrier, hops onto his back for a ride, then hops back ---------- */
static double carrierTop(void) {
  static const struct { const char *k; double y; } TOPS[] = { { "", -28 }, { "paperbag", -29.5 }, { "crate", -26 }, { "wicker", -26 }, { "pizza", -31.5 }, { "toolbox", -27.5 },
    { "cooler", -26 }, { "guitar", -34.5 }, { "duffel", -27.5 }, { "chest", -28 }, { "boombox", -27 }, { "lunch", -27 } };
  for (unsigned i = 0; i < sizeof TOPS / sizeof *TOPS; i++) if (IS(SL_CARRIER, TOPS[i].k)) return TOPS[i].y;
  return -28;
}
static void drawCatPal(void) {
  double t = fmod(T, 13), u; bool jump = false;
  if (t < 5.5 || t >= 10.7) u = 0; else if (t < 6.1) { u = (t - 5.5) / 0.6; jump = true; } else if (t < 10.1) u = 1; else { u = 1 - (t - 10.1) / 0.6; jump = true; }
  bool inCrate = u == 0; double top = carrierTop() - 3.2;
  double cx = -8.5 + 7 * u, cy = top + (-30 - top) * u - (jump ? 11 * sin(PI * u) : 0);
  double sway = sin(T * 1.7) * (inCrate ? 0.7 : 0.3), br = sin(T * 2.6) * 0.3, twitch = fmod(T, 4.3) < 0.25 ? 0.9 : 0; bool bl = fmod(T, 3.4) < 0.16;
  double hx = cx + 2.4 + sway, hy = cy - 3.6 + br, s3 = sin(T * 3);
  cv_save(); cv_translate(cx, cy + 3); cv_scale(1.3, 1.3); cv_translate(-cx, -cy - 3);
  if (jump) { cv_translate(cx, cy); cv_rotate((u < 0.5 ? -0.5 : 0.5) * sin(PI * u)); cv_scale(1.15, 0.9); cv_translate(-cx, -cy); }
  cv_begin(); cv_move(cx - 3, cy + 1.4); cv_bezier(cx - 7, cy + 1 + s3 * 2, cx - 6 + s3 * 1.5, cy - 4, cx - 4 + s3 * 2, cy - 6.5); cv_stroke(H(14101f), 1.6);
  E(H(1c1a24), cx, cy + br, 3.9, 3.3 + br * 0.4, 0, 0, 7);
  cv_circle(hx, hy, 2.8);
  cv_begin(); cv_move(hx - 2.4, hy - 1.4); cv_line(hx - 2, hy - 4.8 - twitch); cv_line(hx - 0.2, hy - 2.4); cv_move(hx + 0.4, hy - 2.4); cv_line(hx + 2.4, hy - 4.8); cv_line(hx + 2.8, hy - 1.2); cv_fillpath();
  R(H(ff8aa8), hx - 1.7, hy - 3.6 - twitch, 0.6, 1); R(H(ff8aa8), hx + 1.7, hy - 3.4, 0.6, 1);
  if (bl) { R(H(2c2a38), hx - 1.7, hy - 0.6, 1.3, 0.4); R(H(2c2a38), hx + 0.6, hy - 0.6, 1.3, 0.4); }
  else { R(H(ffe14a), hx - 1.8, hy - 1.2, 1.6, 1.6); R(H(ffe14a), hx + 0.6, hy - 1.2, 1.6, 1.6); R(H(14101f), hx - 1.2, hy - 1.2, 0.5, 1.6); R(H(14101f), hx + 1.2, hy - 1.2, 0.5, 1.6); R(rgba(255, 225, 74, .35), hx - 2.4, hy - 1.8, 5.6, 2.8); }
  R(H(ff8aa8), hx + 0.1, hy + 0.9, 0.8, 0.6);
  R(H(2c2a38), cx + 1.4, cy + 2.6, 1, 1.6); R(H(2c2a38), cx - 1.4, cy + 2.6, 1, 1.6);
  cv_restore();
}

/* ---------- the bike ---------- */
// Everything that tilts with the wheelie and doesn't move, in the bike's own coordinates, in the web game's drawBike order.
static void bikeBody(void) {
  if (!fRims) { drawWheel(0); drawWheel(34); }
  bool holo = IS(SL_PAINT, "holo"); col_t fc = holo ? hslc(T * 70, 85, 66) : tank1;   // the front fender wears the paint too
  cv_begin(); cv_arc(34, 0, 10, -PI * 0.97, -PI * 0.42); cv_stroke(IS(SL_PAINT, "black") ? tank2 : holo ? H(3a3556) : shadec(fc, 0.55), 2.6);
  cv_begin(); cv_arc(34, 0, 10.3, -PI * 0.95, -PI * 0.44); cv_stroke(fc, 1.6);
  cv_begin(); cv_move(30, -22); cv_line(34, 0); cv_move(24, -27); cv_line(29, -23); cv_stroke(H(cfd2e8), 2);
  L(H(3a3556), 2, 0, 0, 22, -6);
  R(H(8e8aa0), 10, -9, 14, 9); R(H(5e5a76), 10, -3, 14, 3);
  L(H(d9dcf0), 2, 16, -1, -10, 3);
  R(H(ff2d2d), -15, 1, 6, 4);
  drawTank();
  if (*cur[SL_DECAL]) drawDecal(cur[SL_DECAL]);
  R(H(2a1a3a), 2, -15, 14, 3);
  // the Old Man: flat cap, round specs, white mustache, cardigan, cane strapped on the back
  R(H(ffee99), 32, -22, 3, 3);
  drawCoat();
  drawCane(cur[SL_CANE]);
  if (*cur[SL_CARRIER]) drawCarrier(cur[SL_CARRIER]);
  else { R(H(c0392b), -14, -28, 11, 15); R(H(8e2418), -14, -17, 11, 4); cv_fill(H(ffd21f)); F(-12, -22, 7, 2); F(-9, -15, 3, 2); cv_fill(H(6a1a10)); F(-14, -28, 11, 1); F(-3, -27, 1, 12); }
  drawSleeve();
  R(H(f2c9a0), 24, -28, 4, 4);
  L(H(3a4a6a), 3, 8, -16, 17, -8);
  C(H(f2c9a0), 15, -36, 5);
  cv_fill(H(f1ecf7)); F(10, -38, 2, 5); F(18, -33, 5, 2); F(19, -31, 3, 1);
  if (!fCap) { drawCap(cur[SL_CAP]); drawSpecs(); }
  if (!fGear && *cur[SL_GEAR]) drawGear(cur[SL_GEAR]);
}
// The sprite is 136x152 at twice the size: x -22..46, y -62..14 around the rear wheel (room for the tall hats and the
// fishing line); its pivot lands on the same texel grid as before, so the default look stamps exactly as it did.
void look_sprite(void) { spr_begin(SPR_BIKE, 136, 152, 2, 22, 62); bikeBody(); lastBuild = T; dirty = false; }
void look_under(void) { if (fRims) { drawWheel(0); drawWheel(34); } }
void look_over(void) {
  if (fCap) { drawCap(cur[SL_CAP]); drawSpecs(); }
  if (fGear) drawGear(cur[SL_GEAR]);
  if (fCat) drawCatPal();
}
void look_frame(void) {
  if (!dirty && !(fAnim && (T - lastBuild >= 0.2 || T < lastBuild))) return;   // animated items: 5 redraws a second
  look_sprite(); spr_end();
}

/* ---------- choosing the look ---------- */
bool look_known(int slot, const char *id) { return slot >= 0 && slot < NSLOT && id && (!*id || inList(id, ITEMS[slot])); }
const char *look_get(int slot) { return slot >= 0 && slot < NSLOT ? raw[slot] : ""; }
static bool any(int slot, const char *const *list) { return inList(cur[slot], list); }
void look_set(int slot, const char *id) {
  if (slot < 0 || slot >= NSLOT) return;
  if (!id) id = "";
  snprintf(raw[slot], sizeof raw[slot], "%s", id);
  snprintf(cur[slot], sizeof cur[slot], "%s", look_known(slot, id) ? id : "");
  if (slot == SL_PAINT) { tank1 = H(ffd21f); tank2 = H(ff5a1f); for (unsigned i = 0; i < sizeof PAINTS / sizeof *PAINTS; i++) if (IS(SL_PAINT, PAINTS[i].id)) { tank1 = PAINTS[i].a; tank2 = PAINTS[i].b; } }
  if (slot == SL_COAT) { CO = COATS; for (unsigned i = 0; i < sizeof COATS / sizeof *COATS; i++) if (IS(SL_COAT, COATS[i].id)) CO = &COATS[i]; }
  static const char *const capLive[] = { "propeller", "laurel", "pumpkin", "santa", "crown", NULL };
  static const char *const gearStill[] = { "", "shades", "goggles", "bowtie", NULL };
  static const char *const coatAnim[] = { "purple", "disco", "flame", NULL }, *const paintAnim[] = { "holo", "gold", "goldenhour", "money", NULL };
  static const char *const decalAnim[] = { "sun", "batmoon", NULL }, *const carrierAnim[] = { "cauldron", "chest", "boombox", "piggy", NULL };
  static const char *const caneAnim[] = { "golden", "neon", "broom", "wizard", "fishing", "bouquet", "trophy", "lollipop", NULL };
  fRims = *cur[SL_RIMS] && !IS(SL_RIMS, "white");
  fCap = any(SL_CAP, capLive);
  fGear = *cur[SL_GEAR] && (!any(SL_GEAR, gearStill) || fCap);   // gear sits over the specs, so it goes live with a live hat
  fCat = IS(SL_CANE, "cat");
  fAnim = any(SL_COAT, coatAnim) || any(SL_PAINT, paintAnim) || any(SL_DECAL, decalAnim) || any(SL_CARRIER, carrierAnim) || any(SL_CANE, caneAnim);
  dirty = true;
}

/* ---------- trails: particles in world space. The camera runs at g.P, so a particle with vx near g.P hangs behind the bike
   and one with vx 0 streaks off to the left. They use their own random numbers, never the run's. ---------- */
typedef struct { double cam, bx, y, P, thr, *acc; Part *parts; int *n, max; double (*R)(void); } Emit;
static uint32_t trailSeed = 2463534242u;
static double trailRand(void) { trailSeed ^= trailSeed << 13; trailSeed ^= trailSeed >> 17; trailSeed ^= trailSeed << 5; return trailSeed / 4294967296.0; }
static Part *newPart(Emit *e) { if (*e->n >= e->max) return NULL; Part *p = &e->parts[(*e->n)++]; memset(p, 0, sizeof *p); return p; }
static void emitTrail(double dt, Emit *e) {
  const char *k = cur[SL_TRAIL]; if (!*k) return;
  double (*R)(void) = e->R, wx = e->cam + e->bx, gy = e->y, B = BIKE_SCALE;
  double ex = wx - 32 * B, ey = gy - 5 * B, rw = wx - 17 * B;    // exhaust tip and rear-wheel contact patch
  if (!strcmp(k, "rainbow")) {                                  // one solid ribbon. The web lays a slice every frame; here every
    static const col_t cols[6] = { H(ff2d2d), H(ff9a1f), H(ffe14a), H(4dff7a), H(3db7ff), H(a35bff) };   // third frame, three times as
    static double rbAcc, rbLast; rbAcc += dt; if (rbAcc < 3 / 60.0 - 1e-6) return;                       // wide: a third of the rects on an Old 3DS
    double moved = wx - rbLast; if (moved < 0 || moved > 80) moved = e->P * rbAcc;                         // as wide as the bike went since the last
    double w = ceil(moved) + 2, wave = rnd(sin(T * 7) * 1.4); rbAcc = 0; rbLast = wx;
    for (int i = 0; i < 6; i++) { Part *p = newPart(e); if (!p) return; p->x = wx - 20 * B - (w - 2); p->y = gy - 19 * B + i * 2 + wave; p->life = p->max = 0.9; p->c = cols[i]; p->s = 2; p->w = w; }
    return;
  }
  // sparks and flames at about half the web's rate, for the Old 3DS
  double rate = !strcmp(k, "sparks") ? 60 : !strcmp(k, "flames") ? 80 + 30 * e->thr : !strcmp(k, "gold") ? 34 : !strcmp(k, "lines") ? 38 : !strcmp(k, "coins") ? 26
              : !strcmp(k, "bubbles") ? 22 : !strcmp(k, "smoke") ? 7 : !strcmp(k, "bats") ? 7 : !strcmp(k, "snow") ? 34 : 0;
  *e->acc += dt * rate;
  while (*e->acc >= 1) {
    *e->acc -= 1;
    Part *p = newPart(e); if (!p) { *e->acc = 0; return; }
    if (!strcmp(k, "sparks")) {
      bool hot = R() < 0.25; p->x = rw - R() * 3; p->y = gy - 1; p->vx = e->P * (0.2 + R() * 0.35); p->vy = -(30 + R() * 110); p->life = 0.3 + R() * 0.2; p->max = 0.5;
      p->c = hot ? H(ffffff) : R() < 0.5 ? H(7df9ff) : H(3db7ff); p->s = hot ? 2 : 1; p->kind = PK_SPARK; p->grav = 380;
    } else if (!strcmp(k, "flames")) {
      p->x = ex; p->y = ey + (R() - 0.5) * 2; p->vx = e->P * (0.78 + R() * 0.08); p->vy = (R() - 0.5) * 14; p->life = 0.14 + R() * 0.2; p->max = 0.34;
      p->c = H(ffe14a); p->s = 4 + (int)(R() * 3); p->kind = PK_FIRE; p->grav = -40;
    } else if (!strcmp(k, "gold")) {
      p->x = wx - 14 * B - R() * 12; p->y = gy - 4 - R() * 22; p->vx = e->P * (0.55 + R() * 0.3); p->vy = -(4 + R() * 16); p->life = 0.7 + R() * 0.5; p->max = 1.2;
      p->c = R() < 0.5 ? H(ffd21f) : H(fff3a8); p->s = 1 + (int)(R() * 2); p->ph = R() * 6; p->kind = PK_GLINT;
    } else if (!strcmp(k, "lines")) {
      p->x = wx - 22 - R() * 6; p->y = gy - 3 - R() * 16; p->vx = -(160 + R() * 160); p->life = p->max = 0.28; p->c = H(f1ecf7); p->s = 1; p->w = 10 + (int)(R() * 14);
    } else if (!strcmp(k, "coins")) {
      p->x = wx - 18 * B - R() * 6; p->y = gy - 22 * B - R() * 4; p->vx = e->P * (0.5 + R() * 0.25); p->vy = -(70 + R() * 50); p->life = p->max = 0.8;
      p->c = H(ffd21f); p->s = 4; p->ph = R() * 6; p->kind = PK_COIN; p->grav = 300;
    } else if (!strcmp(k, "bubbles")) {
      p->x = wx - 20 * B - R() * 6; p->y = gy - 6 - R() * 14; p->vx = e->P * (0.55 + R() * 0.2); p->vy = -(10 + R() * 18); p->life = p->max = 1.1;
      p->c = H(9ff4ff); p->s = 4 + (int)(R() * 3); p->kind = PK_BUBBLE;
    } else if (!strcmp(k, "bats")) {
      p->x = wx - 22 * B - R() * 6; p->y = gy - 14 - R() * 10; p->vx = e->P * (0.45 + R() * 0.2); p->vy = -(18 + R() * 22); p->life = p->max = 1.3;
      p->c = R() < 0.5 ? H(2a1838) : H(3e2450); p->s = 1; p->ph = R() * 6; p->kind = PK_BAT; p->grav = -6;
    } else if (!strcmp(k, "snow")) {
      p->x = wx - 14 * B - R() * 14; p->y = gy - 6 - R() * 24; p->vx = e->P * (0.6 + R() * 0.25); p->vy = 8 + R() * 18; p->life = 1.0 + R() * 0.4; p->max = 1.4;
      p->c = H(ffffff); p->s = R() < 0.3 ? 2 : 1; p->ph = R() * 6; p->kind = PK_FLAKE;
    } else {                                                    // smoke rings
      p->x = ex; p->y = ey - 1; p->vx = e->P * (0.6 + R() * 0.1); p->vy = -(16 + R() * 10); p->life = p->max = 1.0;
      p->c = R() < 0.5 ? H(f1ecf7) : H(c8c4d8); p->s = 4; p->kind = PK_RING; p->grow = 12;
    }
  }
}
static double trailAcc;
void look_trail(double dt) {
  Emit e = { g.cam, g.bx, g.y, g.P, g.thr, &trailAcc, g.parts, &g.nparts, MAX_PARTS, trailRand };
  emitTrail(dt, &e);
}

/* ---------- the garage preview: runs the real emitter for a moment behind a bike parked at the origin ---------- */
static int64_t sampleSeed; static double sampleRand(void) { sampleSeed = sampleSeed * 16807 % 2147483647; return sampleSeed / 2147483647.0; }
static void trailSample(double bx, double by) {
  static Part ps[320]; static int n; static int lastSeed = -1; static char lastK[16]; static double lastB, cam;
  if (!*cur[SL_TRAIL]) return;
  int seed = 7 + (int)floor(T * 8) % 40;
  if (seed != lastSeed || strcmp(lastK, cur[SL_TRAIL]) || lastB != BIKE_SCALE) {   // re-run only when the sample changes (8 times a second)
    lastSeed = seed; snprintf(lastK, sizeof lastK, "%s", cur[SL_TRAIL]); lastB = BIKE_SCALE;
    sampleSeed = seed; n = 0; cam = 0; double acc = 0;
    Emit e = { 0, bx, by, 520, 0.6, &acc, ps, &n, 320, sampleRand };
    for (int i = 0; i < 48; i++) {
      cam += 520 / 60.0; e.cam = cam; emitTrail(1 / 60.0, &e);
      int j = 0; for (int q = 0; q < n; q++) {
        Part p = ps[q]; p.life -= 1 / 60.0; if (p.life <= 0) continue;
        p.vy += p.grav / 60.0; p.x += p.vx / 60.0; p.y += p.vy / 60.0;
        if (p.grow) { p.s += p.grow / 60.0; p.vx *= exp(-1.2 / 60.0); }
        ps[j++] = p;
      }
      n = j;
    }
  }
  draw_parts(ps, n, cam);
}
void look_preview(double x, double y, double scale) {
  double bx = 17 * BIKE_SCALE;                                  // drawBike's point is 17 bike units ahead of the rear wheel
  cv_save(); cv_translate(x, y); cv_scale(scale, scale);
  trailSample(bx, 0); drawBike(bx, 0, 0.22, 0, true);
  cv_restore();
}

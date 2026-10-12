// The canvas layer on citro2d. The game picture is drawn at the web game's 480x270 into an off-screen texture, then put
// on the 400x240 top screen with linear filtering, so every one-pixel detail survives the scale-down.
#include "gfx.h"
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef struct { double a, b, c, d, e, f; } Mx;                 // x' = a x + c y + e, y' = b x + d y + f
typedef struct { Mx m; double alpha; col_t fill; int nclip, clipTop; } State;
static State cur, stk[48]; static int sp;

#define MAXCLIP 8
#define MAXCP 64
static float clipXY[MAXCLIP][MAXCP * 2]; static int clipN[MAXCLIP];

#define MAXPATH 512
#define MAXSUB 16
static float pathXY[MAXPATH * 2]; static int pathN, subStart[MAXSUB + 1], nsub;

static C3D_RenderTarget *rt, *top, *bot;
static C3D_Tex rtTex;
static C3D_Tex patTex[NPAT];
static double patOX;
static C2D_TextBuf tbuf;
static float fontPx = 30;
#define TILE_PX 128

col_t rgba(int r, int g, int b, double a) { int A = (int)(a * 255 + 0.5); if (A < 0) A = 0; if (A > 255) A = 255; return (col_t)r << 24 | (col_t)g << 16 | (col_t)b << 8 | A; }
col_t shadec(col_t c, double f) {
  int r = (int)floor((c >> 24) * f + 0.5), g = (int)floor((c >> 16 & 255) * f + 0.5), b = (int)floor((c >> 8 & 255) * f + 0.5);
  return (col_t)(r > 255 ? 255 : r) << 24 | (col_t)(g > 255 ? 255 : g) << 16 | (col_t)(b > 255 ? 255 : b) << 8 | (c & 255);
}
col_t hslc(double h, double s, double l) {
  h = fmod(fmod(h, 360) + 360, 360) / 360; s /= 100; l /= 100;
  double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q, rgb[3];
  for (int i = 0; i < 3; i++) { double t = h + (1 - i) / 3.0; if (t < 0) t += 1; if (t > 1) t -= 1;
    rgb[i] = t < 1 / 6.0 ? p + (q - p) * 6 * t : t < 0.5 ? q : t < 2 / 3.0 ? p + (q - p) * (2 / 3.0 - t) * 6 : p; }
  return rgba((int)(rgb[0] * 255 + 0.5), (int)(rgb[1] * 255 + 0.5), (int)(rgb[2] * 255 + 0.5), 1);
}
static inline u32 c2d(col_t c) {
  double a = (c & 255) * cur.alpha; if (a > 255) a = 255;
  return C2D_Color32(c >> 24, c >> 16 & 255, c >> 8 & 255, (u8)(a + 0.5));
}
static inline u32 c2dA(col_t c, double alpha) {
  double a = (c & 255) * alpha; if (a > 255) a = 255;
  return C2D_Color32(c >> 24, c >> 16 & 255, c >> 8 & 255, (u8)(a + 0.5));
}

/* ---------- state ---------- */
void cv_reset(void) { memset(&cur, 0, sizeof cur); cur.m.a = cur.m.d = 1; cur.alpha = 1; cur.fill = 0x000000ff; sp = 0; }
void cv_save(void) { if (sp < 48) stk[sp++] = cur; cur.clipTop = cur.nclip; }
void cv_restore(void) { if (sp > 0) cur = stk[--sp]; }
void cv_translate(double x, double y) { cur.m.e += cur.m.a * x + cur.m.c * y; cur.m.f += cur.m.b * x + cur.m.d * y; }
void cv_rotate(double r) {
  double cs = cos(r), sn = sin(r), a = cur.m.a, b = cur.m.b, c = cur.m.c, d = cur.m.d;
  cur.m.a = a * cs + c * sn; cur.m.b = b * cs + d * sn; cur.m.c = c * cs - a * sn; cur.m.d = d * cs - b * sn;
}
void cv_scale(double sx, double sy) { cur.m.a *= sx; cur.m.b *= sx; cur.m.c *= sy; cur.m.d *= sy; }
void cv_alpha(double a) { cur.alpha = a; }
double cv_get_alpha(void) { return cur.alpha; }
void cv_fill(col_t c) { cur.fill = c; }
static inline void xf(double x, double y, float *ox, float *oy) { *ox = (float)(cur.m.a * x + cur.m.c * y + cur.m.e); *oy = (float)(cur.m.b * x + cur.m.d * y + cur.m.f); }

/* ---------- triangles, with clipping to convex polygons ---------- */
static int clipPoly(const float *in, int n, const float *cp, int cn, float *out) {   // Sutherland-Hodgman, cp counter-clockwise or clockwise
  static float buf[2][(MAXCP + 16) * 2]; const float *src = in; int sn = n;
  double area = 0; for (int i = 0; i < cn; i++) { int j = (i + 1) % cn; area += cp[i * 2] * cp[j * 2 + 1] - cp[j * 2] * cp[i * 2 + 1]; }
  double sg = area >= 0 ? 1 : -1;
  for (int e = 0; e < cn && sn > 0; e++) {
    float ax = cp[e * 2], ay = cp[e * 2 + 1], bx = cp[(e + 1) % cn * 2], by = cp[(e + 1) % cn * 2 + 1];
    float *dst = (e == cn - 1) ? out : buf[e & 1]; int dn = 0;
    for (int i = 0; i < sn; i++) {
      float px = src[i * 2], py = src[i * 2 + 1], qx = src[(i + 1) % sn * 2], qy = src[(i + 1) % sn * 2 + 1];
      double dp = sg * ((bx - ax) * (py - ay) - (by - ay) * (px - ax)), dq = sg * ((bx - ax) * (qy - ay) - (by - ay) * (qx - ax));
      if (dp >= 0) { dst[dn * 2] = px; dst[dn * 2 + 1] = py; dn++; }
      if ((dp >= 0) != (dq >= 0)) { double t = dp / (dp - dq); dst[dn * 2] = px + (qx - px) * t; dst[dn * 2 + 1] = py + (qy - py) * t; dn++; }
      if (dn >= MAXCP + 14) break;
    }
    src = dst; sn = dn;
  }
  if (src != out) memcpy(out, src, sn * 2 * sizeof(float));
  return sn;
}
static void fan(const float *p, int n, u32 col) {
  for (int i = 1; i + 1 < n; i++) C2D_DrawTriangle(p[0], p[1], col, p[i * 2], p[i * 2 + 1], col, p[i * 2 + 2], p[i * 2 + 3], col, 0);
}
static void convexOut(const float *p, int n, u32 col) {          // a convex polygon in screen space, through the clip
  if (n < 3) return;
  if (!cur.nclip) { fan(p, n, col); return; }
  static float out[(MAXCP + 16) * 2];
  for (int k = 0; k < cur.nclip; k++) { int m = clipPoly(p, n, clipXY[k], clipN[k], out); if (m >= 3) fan(out, m, col); }
}
static bool isConvex(const float *p, int n) {
  int sgn = 0;
  for (int i = 0; i < n; i++) {
    const float *a = p + i * 2, *b = p + (i + 1) % n * 2, *c = p + (i + 2) % n * 2;
    double cr = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
    if (fabs(cr) < 1e-9) continue;
    int s = cr > 0 ? 1 : -1; if (!sgn) sgn = s; else if (s != sgn) return false;
  }
  return true;
}
static void polyOut(const float *p, int n, u32 col) {
  if (n < 3) return;
  if (n <= 3 || isConvex(p, n)) { if (n > MAXCP + 8) { fan(p, n, col); return; } convexOut(p, n, col); return; }
  // ear clipping for the odd concave outline (snow caps, crowns, banners)
  int idx[MAXPATH], m = n; for (int i = 0; i < n; i++) idx[i] = i;
  double area = 0; for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += p[i * 2] * p[j * 2 + 1] - p[j * 2] * p[i * 2 + 1]; }
  double sg = area >= 0 ? 1 : -1; int guard = 0;
  while (m > 3 && guard++ < 4 * n) {
    bool cut = false;
    for (int i = 0; i < m; i++) {
      int ia = idx[(i + m - 1) % m], ib = idx[i], ic = idx[(i + 1) % m];
      const float *a = p + ia * 2, *b = p + ib * 2, *c = p + ic * 2;
      double cr = sg * ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
      if (cr <= 0) continue;
      bool inside = false;
      for (int j = 0; j < m && !inside; j++) { int q = idx[j]; if (q == ia || q == ib || q == ic) continue; const float *t = p + q * 2;
        double d1 = sg * ((b[0] - a[0]) * (t[1] - a[1]) - (b[1] - a[1]) * (t[0] - a[0])), d2 = sg * ((c[0] - b[0]) * (t[1] - b[1]) - (c[1] - b[1]) * (t[0] - b[0])), d3 = sg * ((a[0] - c[0]) * (t[1] - c[1]) - (a[1] - c[1]) * (t[0] - c[0]));
        inside = d1 >= 0 && d2 >= 0 && d3 >= 0; }
      if (inside) continue;
      float tri[6] = { a[0], a[1], b[0], b[1], c[0], c[1] }; convexOut(tri, 3, col);
      for (int j = i; j < m - 1; j++) idx[j] = idx[j + 1];
      m--; cut = true; break;
    }
    if (!cut) break;
  }
  if (m == 3) { float tri[6] = { p[idx[0] * 2], p[idx[0] * 2 + 1], p[idx[1] * 2], p[idx[1] * 2 + 1], p[idx[2] * 2], p[idx[2] * 2 + 1] }; convexOut(tri, 3, col); }
}

/* ---------- shapes ---------- */
void cv_rect(double x, double y, double w, double h) {
  if (w <= 0 || h <= 0) return;
  u32 col = c2d(cur.fill); if (!(col >> 24)) return;
  if (cur.m.b == 0 && cur.m.c == 0 && !cur.nclip) {
    float x0, y0; xf(x, y, &x0, &y0); float ww = (float)(w * cur.m.a), hh = (float)(h * cur.m.d);
    if (ww < 0) { x0 += ww; ww = -ww; } if (hh < 0) { y0 += hh; hh = -hh; }
    C2D_DrawRectSolid(x0, y0, 0, ww, hh, col); return;
  }
  float p[8]; xf(x, y, p, p + 1); xf(x + w, y, p + 2, p + 3); xf(x + w, y + h, p + 4, p + 5); xf(x, y + h, p + 6, p + 7);
  convexOut(p, 4, col);
}
void cv_begin(void) { pathN = 0; nsub = 0; }
void cv_move(double x, double y) {
  if (nsub < MAXSUB) subStart[nsub++] = pathN;
  if (pathN < MAXPATH) { xf(x, y, &pathXY[pathN * 2], &pathXY[pathN * 2 + 1]); pathN++; }
}
void cv_line(double x, double y) {
  if (!nsub) { cv_move(x, y); return; }
  if (pathN < MAXPATH) { xf(x, y, &pathXY[pathN * 2], &pathXY[pathN * 2 + 1]); pathN++; }
}
void cv_close(void) { }
static double scaleOf(void) { return sqrt(fabs(cur.m.a * cur.m.d - cur.m.b * cur.m.c)); }
void cv_ellipse(double cx, double cy, double rx, double ry, double rot, double a0, double a1) {
  // canvas: ellipse(x, y, rx, ry, rot, start, end); 7 (as the web game writes it) means a whole turn
  double span = a1 - a0; bool full = span >= 2 * M_PI - 1e-6; if (full) { a0 = 0; span = 2 * M_PI; }
  int segs = (int)(fmax(rx, ry) * scaleOf() * 1.1 * span / (2 * M_PI)) + 6; if (segs > 48) segs = 48;
  double cr = cos(rot), sr = sin(rot);
  int n = full ? segs : segs + 1;
  bool fresh = full;   // a whole ellipse starts its own subpath, like the web game's beginPath(); arc(...)
  for (int i = 0; i < n; i++) {
    double t = a0 + span * i / segs, ex = cos(t) * rx, ey = sin(t) * ry;
    double x = cx + ex * cr - ey * sr, y = cy + ex * sr + ey * cr;
    if (i == 0 && (fresh || !nsub)) cv_move(x, y); else cv_line(x, y);
  }
}
void cv_arc(double cx, double cy, double r, double a0, double a1) { cv_ellipse(cx, cy, r, r, 0, a0, a1); }
void cv_fillpath(void) {
  u32 col = c2d(cur.fill); if (!(col >> 24)) return;
  for (int s = 0; s < nsub; s++) { int a = subStart[s], b = s + 1 < nsub ? subStart[s + 1] : pathN; polyOut(pathXY + a * 2, b - a, col); }
}
void cv_poly(const double *xy, int n) { cv_begin(); for (int i = 0; i < n; i++) i ? cv_line(xy[i * 2], xy[i * 2 + 1]) : cv_move(xy[0], xy[1]); cv_fillpath(); }
void cv_circle(double cx, double cy, double r) { cv_begin(); cv_arc(cx, cy, r, 0, 7); cv_fillpath(); }
static void segment(float x0, float y0, float x1, float y1, float hw, u32 col) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy); if (l < 1e-4f) return;
  float nx = -dy / l * hw, ny = dx / l * hw;
  float p[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
  convexOut(p, 4, col);
}
void cv_stroke(col_t c, double w) {
  u32 col = c2d(c); if (!(col >> 24)) return;
  float hw = (float)(w * scaleOf() / 2);
  for (int s = 0; s < nsub; s++) {
    int a = subStart[s], b = s + 1 < nsub ? subStart[s + 1] : pathN;
    for (int i = a; i + 1 < b; i++) segment(pathXY[i * 2], pathXY[i * 2 + 1], pathXY[i * 2 + 2], pathXY[i * 2 + 3], hw, col);
  }
}
void cv_stroke_rect(double x, double y, double w, double h, col_t c, double lw) {
  cv_begin(); cv_move(x, y); cv_line(x + w, y); cv_line(x + w, y + h); cv_line(x, y + h); cv_line(x, y); cv_stroke(c, lw);
}
void cv_clip(void) {
  int k = cur.nclip;
  if (k > cur.clipTop) k = cur.clipTop;        // a second clip in the same save() replaces the first (the web game never nests them)
  for (int s = 0; s < nsub && k < MAXCLIP; s++) {
    int a = subStart[s], b = s + 1 < nsub ? subStart[s + 1] : pathN, n = b - a; if (n < 3) continue; if (n > MAXCP) n = MAXCP;
    memcpy(clipXY[k], pathXY + a * 2, n * 2 * sizeof(float)); clipN[k] = n; k++;
  }
  cur.nclip = k;
}
void cv_grad(double x, double y, double w, double h, bool vert, double g0, double g1, int n, const double *stops, const col_t *cols) {
  // split the rect at each colour stop; each band is a four-colour rectangle
  double lo = vert ? y : x, hi = vert ? y + h : x + w;
  for (int i = -1; i < n; i++) {
    double p0 = i < 0 ? -1e9 : g0 + (g1 - g0) * stops[i], p1 = i + 1 < n ? g0 + (g1 - g0) * stops[i + 1] : 1e9;
    col_t c0 = cols[i < 0 ? 0 : i], c1 = cols[i + 1 < n ? i + 1 : n - 1];
    double s0 = fmax(lo, fmin(p0, p1)), s1 = fmin(hi, fmax(p0, p1)); if (s1 <= s0) continue;
    // colour at s0 and s1 (p0 > p1 when the gradient runs backwards)
    double span = p1 - p0; double t0 = fabs(span) > 1e8 || span == 0 ? 0 : (s0 - p0) / span, t1 = fabs(span) > 1e8 || span == 0 ? 1 : (s1 - p0) / span;
    if (i < 0 || i + 1 >= n) { t0 = 0; t1 = 0; }
    col_t ca, cb; {
      int r0 = c0 >> 24, gg0 = c0 >> 16 & 255, b0 = c0 >> 8 & 255, a0 = c0 & 255, r1 = c1 >> 24, gg1 = c1 >> 16 & 255, b1 = c1 >> 8 & 255, a1 = c1 & 255;
      #define LERPC(t) ((col_t)(r0 + (r1 - r0) * (t)) << 24 | (col_t)(gg0 + (gg1 - gg0) * (t)) << 16 | (col_t)(b0 + (b1 - b0) * (t)) << 8 | (col_t)(a0 + (a1 - a0) * (t)))
      ca = LERPC(t0); cb = LERPC(t1);
    }
    float X0, Y0, X1, Y1;
    if (vert) { xf(x, s0, &X0, &Y0); xf(x + w, s1, &X1, &Y1); } else { xf(s0, y, &X0, &Y0); xf(s1, y + h, &X1, &Y1); }
    u32 A = c2d(ca), B = c2d(cb);
    if (vert) C2D_DrawRectangle(X0, Y0, 0, X1 - X0, Y1 - Y0, A, A, B, B);
    else C2D_DrawRectangle(X0, Y0, 0, X1 - X0, Y1 - Y0, A, B, A, B);
  }
}

/* ---------- patterns: 128x128 speckle tiles, as makeTile() paints them ---------- */
static inline u32 tileOff(int x, int y, int w) {                 // 8x8 tiles, Morton order inside each
  static const u8 mort[64] = { 0,1,4,5,16,17,20,21, 2,3,6,7,18,19,22,23, 8,9,12,13,24,25,28,29, 10,11,14,15,26,27,30,31,
    32,33,36,37,48,49,52,53, 34,35,38,39,50,51,54,55, 40,41,44,45,56,57,60,61, 42,43,46,47,58,59,62,63 };
  return ((y >> 3) * (w >> 3) + (x >> 3)) * 64 + mort[(y & 7) * 8 + (x & 7)];
}
void pat_make(int which, col_t base, col_t a, col_t b, int count, int size) {
  static u32 img[TILE_PX * TILE_PX];
  for (int i = 0; i < TILE_PX * TILE_PX; i++) img[i] = base;
  uint32_t s = 12345 + count;
  #define R() ((s = s * 1664525u + 1013904223u) / 4294967296.0)
  for (int i = 0; i < count; i++) {
    col_t c = R() < 0.6 ? a : b; int x = (int)floor(R() * TILE_PX), y = (int)floor(R() * TILE_PX), w = R() < 0.7 ? 1 : size;
    for (int k = 0; k < w && x + k < TILE_PX; k++) img[y * TILE_PX + x + k] = c;
  }
  #undef R
  C3D_Tex *t = &patTex[which];
  if (!t->data) { C3D_TexInit(t, TILE_PX, TILE_PX, GPU_RGBA8); C3D_TexSetFilter(t, GPU_NEAREST, GPU_NEAREST); C3D_TexSetWrap(t, GPU_REPEAT, GPU_REPEAT); }
  u32 *d = (u32 *)t->data;
  for (int y = 0; y < TILE_PX; y++) for (int x = 0; x < TILE_PX; x++) d[tileOff(x, TILE_PX - 1 - y, TILE_PX)] = img[y * TILE_PX + x];
  C3D_TexFlush(t);
}
void pat_offset(double ox) { patOX = ox; }
void cv_pattern(int which, double x, double y, double w, double h) {
  if (w <= 0 || h <= 0) return;
  static Tex3DS_SubTexture sub; C2D_Image img = { &patTex[which], &sub };
  sub.width = TILE_PX; sub.height = TILE_PX;
  sub.left = (float)((x + patOX) / TILE_PX); sub.right = (float)((x + w + patOX) / TILE_PX);
  sub.top = (float)(1 - y / TILE_PX); sub.bottom = (float)(1 - (y + h) / TILE_PX);
  float X0, Y0; xf(x, y, &X0, &Y0);
  C2D_DrawParams p = { { X0, Y0, (float)(w * cur.m.a), (float)(h * cur.m.d) }, { 0, 0 }, 0, 0 };
  if (cur.alpha < 0.999) { C2D_ImageTint tint; C2D_AlphaImageTint(&tint, (float)cur.alpha); C2D_DrawImage(img, &p, &tint); }
  else C2D_DrawImage(img, &p, NULL);
}

/* ---------- text in the console's own font (it has the kana and kanji for the crash stamp) ---------- */
static float textScale(double size) { return (float)(size / fontPx); }
// Parsing text is slow on the 3DS, so each string is parsed once and kept (the cache empties itself when full).
#define TCACHE 96
static C2D_TextBuf tcBuf; static struct { uint32_t h; C2D_Text t; } tc[TCACHE]; static int ntc;
static const C2D_Text *textOf(const char *s) {
  uint32_t h = 2166136261u; for (const char *p = s; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  for (int i = 0; i < ntc; i++) if (tc[i].h == h) return &tc[i].t;
  if (ntc == TCACHE || C2D_TextBufGetNumGlyphs(tcBuf) > 3500) { C2D_TextBufClear(tcBuf); ntc = 0; }
  C2D_Text *t = &tc[ntc].t; if (!C2D_TextParse(t, tcBuf, s)) { C2D_TextBufClear(tcBuf); ntc = 0; t = &tc[0].t; if (!C2D_TextParse(t, tcBuf, s)) return NULL; }
  C2D_TextOptimize(t); tc[ntc].h = h; ntc++;
  return t;
}
double cv_text_width(const char *s, double size) {
  const C2D_Text *t = textOf(s); if (!t) return 0; float w, h; C2D_TextGetDimensions(t, textScale(size), textScale(size), &w, &h); return w;
}
void cv_text(const char *s, double x, double y, double size, col_t c, int align, int base) {
  const C2D_Text *t = textOf(s); if (!t) return;
  float sc = textScale(size), w, h; C2D_TextGetDimensions(t, sc, sc, &w, &h);
  double ox = align == AL_CENTER ? -w / 2 : align == AL_RIGHT ? -w : 0;
  double oy = base == BL_MIDDLE ? size * 0.36 : 0;
  float X, Y; xf(x + ox, y + oy, &X, &Y);
  float sx = (float)(sc * cur.m.a), sy = (float)(sc * cur.m.d);
  C2D_DrawText(t, C2D_WithColor | (base == BL_TOP ? 0 : C2D_AtBaseline), X, Y, 0, sx, sy, c2d(c));
}
void cv_text_wrap(const char *s, double x, double y, double size, col_t c, double width) {
  const C2D_Text *t = textOf(s); if (!t) return;
  float sc = textScale(size), X, Y; xf(x, y, &X, &Y);
  C2D_DrawText(t, C2D_WithColor | C2D_WordWrap, X, Y, 0, sc, sc, c2d(c), (float)width);
}

/* ---------- sprites: pictures drawn once into a texture and then stamped, rotated and scaled ---------- */
typedef struct { C3D_Tex tex; C3D_RenderTarget *t; int w, h; float sc, px, py; Tex3DS_SubTexture sub; } Sprite;
static Sprite spr[NSPR];
static int pow2(int v) { int p = 8; while (p < v) p <<= 1; return p; }
void spr_begin(int id, int w, int h, double sc, double px, double py) {
  Sprite *S = &spr[id];
  if (!S->t) { int tw = pow2(w), th = pow2(h); C3D_TexInitVRAM(&S->tex, tw, th, GPU_RGBA8); C3D_TexSetFilter(&S->tex, GPU_LINEAR, GPU_LINEAR);
    S->t = C3D_RenderTargetCreateFromTex(&S->tex, GPU_TEXFACE_2D, 0, -1);
    S->sub = (Tex3DS_SubTexture){ (u16)w, (u16)h, 0.0f, 1.0f, (float)w / tw, 1.0f - (float)h / th }; }
  S->w = w; S->h = h; S->sc = (float)sc; S->px = (float)px; S->py = (float)py;
  C2D_TargetClear(S->t, C2D_Color32(40, 30, 50, 0));
  C2D_SceneBegin(S->t);
  C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
  cv_reset(); cv_scale(sc, sc); cv_translate(px, py);
}
void cv_sprite(int id) {                                     // the sprite's pivot (its local origin) lands on the current origin
  Sprite *S = &spr[id]; if (!S->t) return;
  double s = hypot(cur.m.a, cur.m.b) / S->sc, ang = atan2(cur.m.b, cur.m.a);
  C2D_Image img = { &S->tex, &S->sub };
  C2D_DrawParams p = { { (float)cur.m.e, (float)cur.m.f, (float)(S->w * s), (float)(S->h * s) }, { (float)(S->px * S->sc * s), (float)(S->py * S->sc * s) }, 0, (float)ang };
  if (cur.alpha < 0.999) { C2D_ImageTint tint; C2D_AlphaImageTint(&tint, (float)cur.alpha); C2D_DrawImage(img, &p, &tint); }
  else C2D_DrawImage(img, &p, NULL);
}
void gfx_build(void (*fn)(void)) {
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C2D_TextBufClear(tbuf); fn(); C3D_FrameEnd(0);
}

/* ---------- frames ---------- */
void gfx_init(void) {
  gfxInitDefault();
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);
  C2D_Init(16000);
  C2D_Prepare();
  top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  C3D_TexInitVRAM(&rtTex, 512, 512, GPU_RGBA8);
  C3D_TexSetFilter(&rtTex, GPU_LINEAR, GPU_LINEAR);
  rt = C3D_RenderTargetCreateFromTex(&rtTex, GPU_TEXFACE_2D, 0, -1);
  tbuf = C2D_TextBufNew(4096); tcBuf = C2D_TextBufNew(4096);
  fontPx = C2D_FontGetInfo(NULL)->lineFeed * 0.82f;
  cv_reset();
}
void gfx_exit(void) { C2D_TextBufDelete(tbuf); C2D_TextBufDelete(tcBuf); C2D_Fini(); C3D_Fini(); gfxExit(); }
static void blendNormal(void) {    // keep the off-screen picture opaque: alpha accumulates towards 1
  C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
}
void gfx_frame_begin(void) {
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  C2D_TextBufClear(tbuf);
  C2D_TargetClear(rt, C2D_Color32(0x17, 0x0d, 0x26, 255));
  C2D_SceneBegin(rt);
  blendNormal();
  cv_reset();
}
void gfx_top_begin(void) {
  C2D_TargetClear(top, C2D_Color32(0, 0, 0, 255));
  C2D_SceneBegin(top);
  C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
  static Tex3DS_SubTexture sub = { 480, 270, 0.0f, 1.0f, 480 / 512.0f, 1.0f - 270 / 512.0f };
  C2D_Image img = { &rtTex, &sub };
  C2D_DrawImageAt(img, 0, 0, 0, NULL, 400.0f / 480.0f, 240.0f / 270.0f);
  C2D_Flush();
  blendNormal();
  cv_reset();
}
void gfx_bottom_begin(col_t clear) {
  C2D_TargetClear(bot, C2D_Color32(clear >> 24, clear >> 16 & 255, clear >> 8 & 255, 255));
  C2D_SceneBegin(bot);
  blendNormal();
  cv_reset();
}
void gfx_frame_end(void) { C3D_FrameEnd(0); }

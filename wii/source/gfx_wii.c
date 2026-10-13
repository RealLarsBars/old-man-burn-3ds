// The canvas layer (gfx.h) on the Wii's GX. Same idea as the 3DS's gfx.c: a small slice of the HTML canvas API, so
// draw.c, look.c and ui.c draw the same pictures. Everything ends up as coloured or textured triangles.
//
// One TV screen: the game is drawn at the web game's 480x270, scaled 4/3 into a 640x360 picture, and the menus and HUD
// are drawn over it in those 640x360 units. On a 4:3 TV that picture is letterboxed; on a Wii set to 16:9 it fills the
// screen (the TV stretches the 640-wide picture to widescreen).
//
// Sprites are drawn into the EFB at the start of a frame (before anything else), copied out to a texture, and stamped
// from then on, like the 3DS's render targets.
#include "gfx.h"
#include "gfx_wii.h"
#include <gccore.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct { double a, b, c, d, e, f; } Mx;                 // x' = a x + c y + e, y' = b x + d y + f
typedef struct { Mx m; double alpha; col_t fill; int nclip, clipTop; } State;
static State cur, stk[48]; static int sp;
static Mx base;                                                  // what cv_reset() goes back to: the game's 4/3, or the UI's 1

#define MAXCLIP 8
#define MAXCP 64
static float clipXY[MAXCLIP][MAXCP * 2]; static int clipN[MAXCLIP];

#define MAXPATH 512
#define MAXSUB 16
static float pathXY[MAXPATH * 2]; static int pathN, subStart[MAXSUB + 1], nsub;
static double lastX, lastY;

static GXRModeObj *rmode; static void *xfb[2]; static int fb; static void *fifo;
static int efbH, picY, picH;                                     // the 640 x picH game picture starts picY lines down
static bool wide;
static double patOX;
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
static inline u32 colA(col_t c, double alpha) {                  // 0xRRGGBBAA with the global alpha applied: GX's own order
  double a = (c & 255) * alpha; if (a > 255) a = 255;
  return (c & 0xffffff00u) | (u32)(a + 0.5);
}
static inline u32 colOf(col_t c) { return colA(c, cur.alpha); }

/* ---------- the triangle batch ---------- */
typedef struct { float x, y; u32 c; float u, v; } Vert;
#define MAXVB 6000
static Vert vb[MAXVB]; static int nv;
static GXTexObj *batchTex;                                       // NULL: plain colour
static void flush(void) {
  if (!nv) return;
  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT); GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  if (batchTex) {
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_LoadTexObj(batchTex, GX_TEXMAP0);
    GX_SetNumTexGens(1); GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0); GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
  } else {
    GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0); GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
  }
  GX_Begin(GX_TRIANGLES, GX_VTXFMT0, nv);
  for (int i = 0; i < nv; i++) {
    GX_Position2f32(vb[i].x, vb[i].y); GX_Color1u32(vb[i].c);
    if (batchTex) GX_TexCoord2f32(vb[i].u, vb[i].v);
  }
  GX_End();
  nv = 0;
}
static inline void useTex(GXTexObj *t) { if (t != batchTex) { flush(); batchTex = t; } if (nv + 6 > MAXVB) flush(); }
static inline void vtx(float x, float y, u32 c, float u, float v) { Vert *p = &vb[nv++]; p->x = x; p->y = y; p->c = c; p->u = u; p->v = v; }
static void triC(float x0, float y0, float x1, float y1, float x2, float y2, u32 c) {
  useTex(NULL); vtx(x0, y0, c, 0, 0); vtx(x1, y1, c, 0, 0); vtx(x2, y2, c, 0, 0);
}
static void quadC4(const float *p, u32 c0, u32 c1, u32 c2, u32 c3) {   // p: 4 corners in order, colours per corner
  useTex(NULL);
  vtx(p[0], p[1], c0, 0, 0); vtx(p[2], p[3], c1, 0, 0); vtx(p[4], p[5], c2, 0, 0);
  vtx(p[0], p[1], c0, 0, 0); vtx(p[4], p[5], c2, 0, 0); vtx(p[6], p[7], c3, 0, 0);
}
static void quadT(GXTexObj *t, const float *p, const float *uv, u32 c) {
  useTex(t);
  vtx(p[0], p[1], c, uv[0], uv[1]); vtx(p[2], p[3], c, uv[2], uv[3]); vtx(p[4], p[5], c, uv[4], uv[5]);
  vtx(p[0], p[1], c, uv[0], uv[1]); vtx(p[4], p[5], c, uv[4], uv[5]); vtx(p[6], p[7], c, uv[6], uv[7]);
}

/* ---------- state ---------- */
static int curFont;
void cv_reset(void) { curFont = FONT_MONO; memset(&cur, 0, sizeof cur); cur.m = base; cur.alpha = 1; cur.fill = 0x000000ff; sp = 0; }
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

/* ---------- triangles, with clipping to convex polygons (as on the 3DS) ---------- */
static int clipPoly(const float *in, int n, const float *cp, int cn, float *out) {
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
static void fan(const float *p, int n, u32 col) { for (int i = 1; i + 1 < n; i++) triC(p[0], p[1], p[i * 2], p[i * 2 + 1], p[i * 2 + 2], p[i * 2 + 3], col); }
static void convexOut(const float *p, int n, u32 col) {
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
  u32 col = colOf(cur.fill); if (!(col & 255)) return;
  float p[8]; xf(x, y, p, p + 1); xf(x + w, y, p + 2, p + 3); xf(x + w, y + h, p + 4, p + 5); xf(x, y + h, p + 6, p + 7);
  if (!cur.nclip) { quadC4(p, col, col, col, col); return; }
  convexOut(p, 4, col);
}
void cv_begin(void) { pathN = 0; nsub = 0; }
void cv_move(double x, double y) {
  lastX = x; lastY = y;
  if (nsub < MAXSUB) subStart[nsub++] = pathN;
  if (pathN < MAXPATH) { xf(x, y, &pathXY[pathN * 2], &pathXY[pathN * 2 + 1]); pathN++; }
}
void cv_line(double x, double y) {
  if (!nsub) { cv_move(x, y); return; }
  lastX = x; lastY = y;
  if (pathN < MAXPATH) { xf(x, y, &pathXY[pathN * 2], &pathXY[pathN * 2 + 1]); pathN++; }
}
void cv_close(void) { }
void cv_quad(double cx, double cy, double x, double y) {
  double x0 = lastX, y0 = lastY;
  for (int i = 1; i <= 6; i++) { double t = i / 6.0, u = 1 - t; cv_line(u * u * x0 + 2 * u * t * cx + t * t * x, u * u * y0 + 2 * u * t * cy + t * t * y); }
}
void cv_bezier(double c1x, double c1y, double c2x, double c2y, double x, double y) {
  double x0 = lastX, y0 = lastY;
  for (int i = 1; i <= 8; i++) { double t = i / 8.0, u = 1 - t, a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    cv_line(a * x0 + b * c1x + c * c2x + d * x, a * y0 + b * c1y + c * c2y + d * y); }
}
static double scaleOf(void) { return sqrt(fabs(cur.m.a * cur.m.d - cur.m.b * cur.m.c)); }
void cv_ellipse(double cx, double cy, double rx, double ry, double rot, double a0, double a1) {
  double span = a1 - a0; bool full = span >= 2 * M_PI - 1e-6; if (full) { a0 = 0; span = 2 * M_PI; }
  int segs = (int)(fmax(rx, ry) * scaleOf() * 1.1 * span / (2 * M_PI)) + 6; if (segs > 48) segs = 48;
  double cr = cos(rot), sr = sin(rot);
  int n = full ? segs : segs + 1;
  bool fresh = full;
  for (int i = 0; i < n; i++) {
    double t = a0 + span * i / segs, ex = cos(t) * rx, ey = sin(t) * ry;
    double x = cx + ex * cr - ey * sr, y = cy + ex * sr + ey * cr;
    if (i == 0 && (fresh || !nsub)) cv_move(x, y); else cv_line(x, y);
  }
}
void cv_arc(double cx, double cy, double r, double a0, double a1) { cv_ellipse(cx, cy, r, r, 0, a0, a1); }
void cv_fillpath(void) {
  u32 col = colOf(cur.fill); if (!(col & 255)) return;
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
  u32 col = colOf(c); if (!(col & 255)) return;
  float hw = (float)(w * scaleOf() / 2);
  for (int s = 0; s < nsub; s++) {
    int a = subStart[s], b = s + 1 < nsub ? subStart[s + 1] : pathN;
    for (int i = a; i + 1 < b; i++) segment(pathXY[i * 2], pathXY[i * 2 + 1], pathXY[i * 2 + 2], pathXY[i * 2 + 3], hw, col);
  }
}
void cv_stroke_dash(col_t c, double w, double on, double off) {
  u32 col = colOf(c); if (!(col & 255)) return;
  float sc = (float)scaleOf(), hw = (float)(w * sc / 2), pat[2] = { (float)(on * sc), (float)(off * sc) };
  if (pat[0] + pat[1] < 0.05f) { cv_stroke(c, w); return; }
  for (int s = 0; s < nsub; s++) {
    int a = subStart[s], b = s + 1 < nsub ? subStart[s + 1] : pathN, k = 0; float left = pat[0];
    for (int i = a; i + 1 < b; i++) {
      float x0 = pathXY[i * 2], y0 = pathXY[i * 2 + 1], dx = pathXY[i * 2 + 2] - x0, dy = pathXY[i * 2 + 3] - y0, l = sqrtf(dx * dx + dy * dy), t = 0;
      if (l < 1e-4f) continue;
      while (t < l) {
        float e = fminf(l, t + left);
        if (!k) segment(x0 + dx * t / l, y0 + dy * t / l, x0 + dx * e / l, y0 + dy * e / l, hw, col);
        left -= e - t; t = e;
        if (left <= 1e-4f) { k ^= 1; left = pat[k]; }
      }
    }
  }
}
void cv_stroke_rect(double x, double y, double w, double h, col_t c, double lw) {
  cv_begin(); cv_move(x, y); cv_line(x + w, y); cv_line(x + w, y + h); cv_line(x, y + h); cv_line(x, y); cv_stroke(c, lw);
}
void cv_clip(void) {
  int k = cur.nclip;
  if (k > cur.clipTop) k = cur.clipTop;
  for (int s = 0; s < nsub && k < MAXCLIP; s++) {
    int a = subStart[s], b = s + 1 < nsub ? subStart[s + 1] : pathN, n = b - a; if (n < 3) continue; if (n > MAXCP) n = MAXCP;
    memcpy(clipXY[k], pathXY + a * 2, n * 2 * sizeof(float)); clipN[k] = n; k++;
  }
  cur.nclip = k;
}
void cv_grad(double x, double y, double w, double h, bool vert, double g0, double g1, int n, const double *stops, const col_t *cols) {
  double lo = vert ? y : x, hi = vert ? y + h : x + w;
  for (int i = -1; i < n; i++) {
    double p0 = i < 0 ? -1e9 : g0 + (g1 - g0) * stops[i], p1 = i + 1 < n ? g0 + (g1 - g0) * stops[i + 1] : 1e9;
    col_t c0 = cols[i < 0 ? 0 : i], c1 = cols[i + 1 < n ? i + 1 : n - 1];
    double s0 = fmax(lo, fmin(p0, p1)), s1 = fmin(hi, fmax(p0, p1)); if (s1 <= s0) continue;
    double span = p1 - p0; double t0 = fabs(span) > 1e8 || span == 0 ? 0 : (s0 - p0) / span, t1 = fabs(span) > 1e8 || span == 0 ? 1 : (s1 - p0) / span;
    if (i < 0 || i + 1 >= n) { t0 = 0; t1 = 0; }
    col_t ca, cb; {
      int r0 = c0 >> 24, gg0 = c0 >> 16 & 255, b0 = c0 >> 8 & 255, a0 = c0 & 255, r1 = c1 >> 24, gg1 = c1 >> 16 & 255, b1 = c1 >> 8 & 255, a1 = c1 & 255;
      #define LERPC(t) ((col_t)(r0 + (r1 - r0) * (t)) << 24 | (col_t)(gg0 + (gg1 - gg0) * (t)) << 16 | (col_t)(b0 + (b1 - b0) * (t)) << 8 | (col_t)(a0 + (a1 - a0) * (t)))
      ca = LERPC(t0); cb = LERPC(t1);
      #undef LERPC
    }
    float p[8];
    if (vert) { xf(x, s0, p, p + 1); xf(x + w, s0, p + 2, p + 3); xf(x + w, s1, p + 4, p + 5); xf(x, s1, p + 6, p + 7); }
    else { xf(s0, y, p, p + 1); xf(s1, y, p + 2, p + 3); xf(s1, y + h, p + 4, p + 5); xf(s0, y + h, p + 6, p + 7); }
    u32 A = colOf(ca), B = colOf(cb);
    quadC4(p, A, vert ? A : B, B, vert ? B : A);
  }
}

/* ---------- textures ---------- */
static void toRGBA8(const u32 *img, int w, int h, u8 *dst) {    // GX RGBA8: 4x4 tiles, AR block then GB block
  for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 4) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
      u32 c = img[(ty + y) * w + tx + x]; int k = (y * 4 + x) * 2;
      dst[k] = c & 255; dst[k + 1] = c >> 24; dst[32 + k] = c >> 16 & 255; dst[32 + k + 1] = c >> 8 & 255;
    }
    dst += 64;
  }
}
static void toIA8(const u8 *alpha, int w, int h, u8 *dst) {     // GX IA8: 4x4 tiles of (alpha, intensity) pairs
  for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 4)
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) { *dst++ = alpha[(ty + y) * w + tx + x]; *dst++ = 255; }
}

/* ---------- patterns: 128x128 speckle tiles, as makeTile() paints them ---------- */
static GXTexObj patTex[NPAT]; static u8 *patData[NPAT];
void pat_make(int which, col_t base_, col_t a, col_t b, int count, int size) {
  static u32 img[TILE_PX * TILE_PX];
  for (int i = 0; i < TILE_PX * TILE_PX; i++) img[i] = base_;
  uint32_t s = 12345 + count;
  #define R() ((s = s * 1664525u + 1013904223u) / 4294967296.0)
  for (int i = 0; i < count; i++) {
    col_t c = R() < 0.6 ? a : b; int x = (int)floor(R() * TILE_PX), y = (int)floor(R() * TILE_PX), w = R() < 0.7 ? 1 : size;
    for (int k = 0; k < w && x + k < TILE_PX; k++) img[y * TILE_PX + x + k] = c;
  }
  #undef R
  flush();                                                       // a batch may still use the old picture
  GX_DrawDone();
  if (!patData[which]) patData[which] = memalign(32, TILE_PX * TILE_PX * 4);
  toRGBA8(img, TILE_PX, TILE_PX, patData[which]);
  DCFlushRange(patData[which], TILE_PX * TILE_PX * 4);
  GX_InitTexObj(&patTex[which], patData[which], TILE_PX, TILE_PX, GX_TF_RGBA8, GX_REPEAT, GX_REPEAT, GX_FALSE);
  GX_InitTexObjFilterMode(&patTex[which], GX_NEAR, GX_NEAR);
  GX_InvalidateTexAll();
}
void pat_offset(double ox) { patOX = ox; }
void cv_pattern(int which, double x, double y, double w, double h) {
  if (w <= 0 || h <= 0 || !patData[which]) return;
  float p[8]; xf(x, y, p, p + 1); xf(x + w, y, p + 2, p + 3); xf(x + w, y + h, p + 4, p + 5); xf(x, y + h, p + 6, p + 7);
  float u0 = (float)((x + patOX) / TILE_PX), u1 = (float)((x + w + patOX) / TILE_PX), v0 = (float)(y / TILE_PX), v1 = (float)((y + h) / TILE_PX);
  float uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
  quadT(&patTex[which], p, uv, colA(0xffffffffu, cur.alpha));
}

/* ---------- text: DM Mono and Dela Gothic One, rasterized once into a texture with FreeType ---------- */
#include "DMMono-Regular_ttf.h"
#include "DelaGothicOne-Regular_ttf.h"
typedef struct { uint32_t cp; short x, y, w, h, bx, by; float adv; } Glyph;
#define MAXG 160
typedef struct { GXTexObj tex; u8 *data; int tw, th; float px, em; int ng; Glyph g[MAXG]; } Font;
static Font fonts[NFONT];
static const char *EXTRA = "·—–’‘“”…→←‹›×✓■□▸▶◀▲▼•°";
static int utf8(const char **s) {                                // the next code point, advancing *s
  const unsigned char *p = (const unsigned char *)*s; int c = *p;
  if (c < 0x80) { *s += 1; return c; }
  int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1; c &= 0x3f >> n;
  for (int i = 1; i <= n; i++) { if ((p[i] & 0xc0) != 0x80) { *s += i; return '?'; } c = c << 6 | (p[i] & 0x3f); }
  *s += n + 1; return c;
}
static void fontLoad(Font *F, const u8 *ttf, int len, int px, float em) {
  static FT_Library lib; if (!lib && FT_Init_FreeType(&lib)) return;
  FT_Face face; if (FT_New_Memory_Face(lib, ttf, len, 0, &face)) return;
  FT_Set_Pixel_Sizes(face, 0, px);
  uint32_t cps[MAXG]; int n = 0;
  for (int c = 32; c < 127; c++) cps[n++] = c;
  for (const char *s = EXTRA; *s && n < MAXG; ) cps[n++] = utf8(&s);
  int tw = 1024, th = 0, x = 1, y = 1, rowH = 0;
  // first pass: place the glyphs to find the texture's height
  for (int pass = 0; pass < 2; pass++) {
    u8 *alpha = NULL;
    if (pass == 1) { th = 4; while (th < y + rowH + 1) th <<= 1; alpha = calloc(tw * th, 1); x = 1; y = 1; rowH = 0; F->ng = 0; }
    for (int i = 0; i < n; i++) {
      FT_UInt gi = FT_Get_Char_Index(face, cps[i]); if (!gi || FT_Load_Glyph(face, gi, FT_LOAD_RENDER)) continue;
      FT_Bitmap *bm = &face->glyph->bitmap; int w = bm->width, h = bm->rows;
      if (x + w + 2 > tw) { x = 1; y += rowH + 2; rowH = 0; }
      if (pass == 1) {
        for (int r = 0; r < h; r++) for (int c = 0; c < w; c++) alpha[(y + r) * tw + x + c] = bm->buffer[r * bm->pitch + c];
        Glyph *G = &F->g[F->ng++];
        G->cp = cps[i]; G->x = x; G->y = y; G->w = w; G->h = h; G->bx = face->glyph->bitmap_left; G->by = face->glyph->bitmap_top;
        G->adv = face->glyph->advance.x / 64.0f;
      }
      x += w + 2; if (h > rowH) rowH = h;
    }
    if (pass == 1) {
      F->data = memalign(32, tw * th * 2); toIA8(alpha, tw, th, F->data); free(alpha);
      DCFlushRange(F->data, tw * th * 2);
      GX_InitTexObj(&F->tex, F->data, tw, th, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
      GX_InitTexObjFilterMode(&F->tex, GX_LINEAR, GX_LINEAR);
      F->tw = tw; F->th = th;
    }
  }
  F->px = px; F->em = em;
  FT_Done_Face(face);
}
static const Glyph *glyph(const Font *F, uint32_t cp) {
  for (int i = 0; i < F->ng; i++) if (F->g[i].cp == cp) return &F->g[i];
  return NULL;
}
static void loadFonts(void) {
  // em: how big a glyph is for a cv_text size of 1, as the 3DS's fonts were scaled (Dela Gothic was drawn larger)
  fontLoad(&fonts[FONT_MONO], DMMono_Regular_ttf, DMMono_Regular_ttf_size, 40, 1.0f);
  fontLoad(&fonts[FONT_DISP], DelaGothicOne_Regular_ttf, DelaGothicOne_Regular_ttf_size, 64, 1.0f);
  fonts[FONT_SYS] = fonts[FONT_MONO];                             // no kana on the Wii: the one decal that has them shows nothing
}
int cv_get_font(void) { return curFont; }
void cv_font(int f) { curFont = f >= 0 && f < NFONT ? f : FONT_MONO; }
static float textScale(const Font *F, double size) { return (float)(size * F->em / F->px); }
static double widthOf(const Font *F, const char *s, const char *end, float sc) {
  double w = 0; while (*s && (!end || s < end)) { const Glyph *G = glyph(F, utf8(&s)); if (G) w += G->adv * sc; }
  return w;
}
double cv_text_width(const char *s, double size) { const Font *F = &fonts[curFont]; return F->ng ? widthOf(F, s, NULL, textScale(F, size)) : 0; }
static void drawRun(const Font *F, const char *s, const char *end, double x, double y, float sc, u32 col) {   // baseline at y
  double pen = 0;
  while (*s && (!end || s < end)) {
    const Glyph *G = glyph(F, utf8(&s)); if (!G) continue;
    if (G->w && G->h) {
      double gx = x + pen + G->bx * sc, gy = y - G->by * sc, gw = G->w * sc, gh = G->h * sc;
      float p[8]; xf(gx, gy, p, p + 1); xf(gx + gw, gy, p + 2, p + 3); xf(gx + gw, gy + gh, p + 4, p + 5); xf(gx, gy + gh, p + 6, p + 7);
      float u0 = (float)G->x / F->tw, v0 = (float)G->y / F->th, u1 = (float)(G->x + G->w) / F->tw, v1 = (float)(G->y + G->h) / F->th;
      float uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
      quadT((GXTexObj *)&F->tex, p, uv, col);
    }
    pen += G->adv * sc;
  }
}
void cv_text(const char *s, double x, double y, double size, col_t c, int align, int base_) {
  const Font *F = &fonts[curFont]; if (!F->ng) return;
  u32 col = colOf(c); if (!(col & 255)) return;
  float sc = textScale(F, size); double w = widthOf(F, s, NULL, sc);
  double ox = align == AL_CENTER ? -w / 2 : align == AL_RIGHT ? -w : 0;
  double oy = base_ == BL_MIDDLE ? size * 0.36 : base_ == BL_TOP ? size * 0.8 : 0;
  drawRun(F, s, NULL, x + ox, y + oy, sc, col);
}
void cv_text_wrap(const char *s, double x, double y, double size, col_t c, double width) {   // top-left, wrapped at width
  const Font *F = &fonts[curFont]; if (!F->ng) return;
  u32 col = colOf(c); if (!(col & 255)) return;
  float sc = textScale(F, size); double line = size * 1.25, by = y + size * 0.8;
  while (*s) {
    const char *end = s, *lastSpace = NULL;
    while (*end && *end != '\n') {                                 // as many words as fit
      const char *nx = end; utf8(&nx);
      if (*end == ' ') lastSpace = end;
      if (widthOf(F, s, nx, sc) > width && lastSpace) { end = lastSpace; break; }
      end = nx;
    }
    drawRun(F, s, end, x, by, sc, col);
    by += line; s = end; while (*s == ' ' || *s == '\n') s++;
  }
}

/* ---------- sprites: drawn into the EFB, copied to a texture, then stamped ---------- */
typedef struct { GXTexObj tex; u8 *data; int w, h, tw, th; float sc, px, py; bool ok; } Sprite;
static Sprite spr[NSPR]; static int pending = -1;
static bool inFrame;
static void setView(int x, int y, int w, int h, float lw, float lh) {
  flush();
  GX_SetViewport(x, y, w, h, 0, 1); GX_SetScissor(x, y, w, h);
  Mtx44 p; guOrtho(p, 0, lh, 0, lw, 0, 300); GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);
}
static void fillAll(float w, float h, u32 c) { float p[8] = { 0, 0, w, 0, w, h, 0, h }; quadC4(p, c, c, c, c); flush(); }
static void finishSprite(void) {                                 // copies the drawn sprite out of the EFB
  if (pending < 0) return;
  Sprite *S = &spr[pending]; pending = -1;
  flush();
  GX_SetTexCopySrc(0, 0, S->tw, S->th);
  GX_SetTexCopyDst(S->tw, S->th, GX_TF_RGBA8, GX_FALSE);
  GX_CopyTex(S->data, GX_FALSE);
  GX_PixModeSync();
  GX_InvalidateTexAll();
  S->ok = true;
}
static void mainMode(void);
bool spr_begin(int id, int w, int h, double sc, double px, double py) {
  finishSprite();
  Sprite *S = &spr[id];
  int tw = (w + 3) & ~3, th = (h + 3) & ~3;
  if (tw > 640 || th > efbH) return false;                       // bigger than the EFB: the caller draws it live
  if (!S->data || S->tw != tw || S->th != th) {
    free(S->data); S->data = memalign(32, GX_GetTexBufferSize(tw, th, GX_TF_RGBA8, GX_FALSE, 0)); if (!S->data) return false;
    GX_InitTexObj(&S->tex, S->data, tw, th, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&S->tex, GX_LINEAR, GX_LINEAR);
  }
  S->w = w; S->h = h; S->tw = tw; S->th = th; S->sc = (float)sc; S->px = (float)px; S->py = (float)py;
  GX_SetPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
  GX_SetAlphaUpdate(GX_TRUE);
  setView(0, 0, tw, th, tw, th);
  GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
  fillAll(tw, th, 0x281e3200);                                  // clear to see-through
  GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
  Mx was = base; base = (Mx){ 1, 0, 0, 1, 0, 0 }; cv_reset(); base = was;
  cv_scale(sc, sc); cv_translate(px, py);
  pending = id;
  return true;
}
void cv_sprite(int id) {                                         // the sprite's pivot (its local origin) lands on the current origin
  Sprite *S = &spr[id]; if (!S->ok) return;
  double lw = S->w / S->sc, lh = S->h / S->sc;
  float p[8]; xf(-S->px, -S->py, p, p + 1); xf(lw - S->px, -S->py, p + 2, p + 3); xf(lw - S->px, lh - S->py, p + 4, p + 5); xf(-S->px, lh - S->py, p + 6, p + 7);
  float u1 = (float)S->w / S->tw, v1 = (float)S->h / S->th, uv[8] = { 0, 0, u1, 0, u1, v1, 0, v1 };
  quadT(&S->tex, p, uv, colA(0xffffffffu, cur.alpha));
}
void spr_end(void) { finishSprite(); if (inFrame) mainMode(); cv_reset(); }
void gfx_build(void (*fn)(void)) { fn(); finishSprite(); GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR); GX_SetAlphaUpdate(GX_FALSE); GX_DrawDone(); }

/* ---------- frames ---------- */
bool gfx_wide(void) { return wide; }
int gfx_hz(void) { return (rmode->viTVMode >> 2) == VI_PAL ? 50 : 60; }
void gfx_init(void) {
  VIDEO_Init();
  rmode = VIDEO_GetPreferredMode(NULL);
  wide = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
  if (wide) { rmode->viWidth = 678; rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - 678) / 2; }   // the full width of a 16:9 TV
  VIDEO_Configure(rmode);
  for (int i = 0; i < 2; i++) { xfb[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode)); VIDEO_ClearFrameBuffer(rmode, xfb[i], COLOR_BLACK); }
  VIDEO_SetNextFramebuffer(xfb[0]); VIDEO_SetBlack(false); VIDEO_Flush(); VIDEO_WaitVSync();
  if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
  fifo = memalign(32, 256 * 1024); memset(fifo, 0, 256 * 1024);
  GX_Init(fifo, 256 * 1024);
  GXColor black = { 0, 0, 0, 255 }; GX_SetCopyClear(black, GX_MAX_Z24);
  efbH = rmode->efbHeight;
  float yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
  u32 xfbH = GX_SetDispCopyYScale(yscale);
  GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
  GX_SetDispCopyDst(rmode->fbWidth, xfbH);
  GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
  GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
  GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
  GX_SetCullMode(GX_CULL_NONE);
  GX_SetDispCopyGamma(GX_GM_1_0);
  GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
  GX_SetColorUpdate(GX_TRUE); GX_SetAlphaUpdate(GX_FALSE);
  GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
  GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  GX_SetNumChans(1);
  GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
  GX_SetNumTevStages(1);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
  Mtx mv; guMtxIdentity(mv); guMtxTransApply(mv, mv, 0, 0, -100); GX_LoadPosMtxImm(mv, GX_PNMTX0);
  Mtx tm; guMtxIdentity(tm); GX_LoadTexMtxImm(tm, GX_IDENTITY, GX_MTX2x4);
  // the 16:9 picture: the whole screen on a widescreen Wii, a letterboxed band on a 4:3 one
  picH = wide ? efbH : (efbH * 3 / 4) & ~1; picY = (efbH - picH) / 2;
  loadFonts();
  base = (Mx){ 1, 0, 0, 1, 0, 0 };
  cv_reset();
}
void gfx_exit(void) { GX_AbortFrame(); GX_Flush(); VIDEO_SetBlack(true); VIDEO_Flush(); }
static void mainMode(void) {                                     // the TV picture: black bars, then the game's background
  flush();
  GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR); GX_SetAlphaUpdate(GX_FALSE);
  GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
  setView(0, 0, 640, efbH, 640, efbH); fillAll(640, efbH, 0x000000ff);
  setView(0, picY, 640, picH, UI_W, UI_H); fillAll(UI_W, UI_H, 0x170d26ff);
}
void gfx_frame_begin(void) {
  inFrame = true;
  mainMode();
  base = (Mx){ UI_W / (double)LW_GAME, 0, 0, UI_H / (double)LH_GAME, 0, 0 };   // the game draws at 480x270
  cv_reset();
}
void gfx_top_begin(void) { finishSprite(); flush(); base = (Mx){ 1, 0, 0, 1, 0, 0 }; cv_reset(); }   // menus and HUD: 640x360
void gfx_bottom_begin(col_t clear) { (void)clear; gfx_top_begin(); }
void gfx_frame_end(void) {
  flush();
  GX_DrawDone();
  fb ^= 1;
  GX_CopyDisp(xfb[fb], GX_TRUE);
  GX_Flush();
  VIDEO_SetNextFramebuffer(xfb[fb]);
  VIDEO_Flush();
  VIDEO_WaitVSync();
  inFrame = false;
}
// a screenshot of the last frame, as 640 x efbH RGB (for the emulator tests): read back from the XFB (YUYV)
int gfx_shot(unsigned char *rgb, int *w, int *h) {
  const u8 *p = (const u8 *)xfb[fb]; int W = rmode->fbWidth, Hh = rmode->xfbHeight;
  for (int y = 0; y < Hh; y++) for (int x = 0; x < W; x += 2) {
    const u8 *q = p + (y * W + x) * 2; int y0 = q[0], u = q[1] - 128, y1 = q[2], v = q[3] - 128;
    for (int k = 0; k < 2; k++) {
      int Y = k ? y1 : y0, r = Y + (int)(1.402 * v), g = Y - (int)(0.344 * u + 0.714 * v), b = Y + (int)(1.772 * u);
      u8 *o = rgb + (y * W + x + k) * 3; o[0] = r < 0 ? 0 : r > 255 ? 255 : r; o[1] = g < 0 ? 0 : g > 255 ? 255 : g; o[2] = b < 0 ? 0 : b > 255 ? 255 : b;
    }
  }
  *w = W; *h = Hh; return W * Hh * 3;
}

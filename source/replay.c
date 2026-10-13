#include "replay.h"
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

Recording rec;
static double nextT;

void rec_start(int seed, int mode) {
  rec.seed = seed; rec.mode = mode; rec.n = 0; rec.score = 0; rec.end = 0; nextT = 0;
  for (int s = 0; s < NSLOT; s++) { strncpy(rec.look[s], look_get(s), 23); rec.look[s][23] = 0; }
}
static void push(void) {
  if (rec.n >= REC_MAX) return;
  rec.f[rec.n++] = (RecFrame){ g.t, g.cam, g.bx, g.y, g.thr, g.wheelie };
}
void rec_tick(void) { if (g.t >= nextT) { push(); nextT = g.t + REC_STEP; } }
void rec_end(int score, int cause) { push(); rec.score = score; rec.end = cause; }

/* ---------- the long code: see the format in the README ---------- */
static void u8(unsigned char **p, int v) { *(*p)++ = (unsigned char)v; }
static void u16(unsigned char **p, int v) { u8(p, v & 255); u8(p, (v >> 8) & 255); }
static void u32(unsigned char **p, unsigned v) { u16(p, v & 0xffff); u16(p, v >> 16); }
static int clampi(double v, int lo, int hi) { int i = (int)floor(v + 0.5); return i < lo ? lo : i > hi ? hi : i; }
static int flags(const RecFrame *f) { return (f->thr > 0.5 ? 128 : 0) | clampi(f->wheelie * 127, 0, 127); }
char *rec_code(void) {
  int n = (rec.n + 1) / 2; if (n > 6000) n = 6000;                // every other frame: 0.2 s apart
  size_t cap = 64 + NSLOT * 26 + n * 5;
  unsigned char *raw = malloc(cap), *p = raw;
  if (!raw) return NULL;
  static const int ENDS[4] = { 0, 1, 2, 3 };                       // CAUSE_NONE/BEHIND/AHEAD/RAIL map straight across
  u8(&p, 1); u8(&p, rec.mode); u32(&p, (unsigned)rec.seed); u32(&p, (unsigned)rec.score); u8(&p, ENDS[rec.end & 3]);
  int nl = 0; for (int s = 0; s < NSLOT; s++) nl += rec.look[s][0] != 0;
  u8(&p, nl);
  for (int s = 0; s < NSLOT; s++) if (rec.look[s][0]) { int l = strlen(rec.look[s]); u8(&p, s); u8(&p, l); memcpy(p, rec.look[s], l); p += l; }
  u16(&p, n);
  int cam0 = 0;
  for (int i = 0; i < n; i++) {
    const RecFrame *f = &rec.f[i * 2 < rec.n ? i * 2 : rec.n - 1];
    int cam = clampi(f->cam, -1000000, 100000000);
    if (i == 0) { u32(&p, (unsigned)clampi(f->cam * 4, -2000000000, 2000000000)); u16(&p, clampi(f->bx * 4, 0, 65535)); u16(&p, clampi(f->y * 4, 0, 65535)); }
    else { u16(&p, (unsigned)(clampi(cam - cam0, -32768, 32767) & 0xffff)); u8(&p, clampi(f->bx / 2, 0, 255)); u8(&p, clampi(f->y - 15, 0, 255)); }
    u8(&p, flags(f));
    cam0 = i == 0 ? clampi(f->cam, -1000000, 100000000) : cam0 + clampi(cam - cam0, -32768, 32767);
  }
  uLongf zn = compressBound(p - raw); unsigned char *z = malloc(zn);
  if (!z || compress2(z, &zn, raw, p - raw, 9) != Z_OK) { free(raw); free(z); return NULL; }
  free(raw);
  static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  char *out = malloc(6 + zn * 4 / 3 + 4), *o = out;
  if (!out) { free(z); return NULL; }
  memcpy(o, "OMB1.", 5); o += 5;
  for (uLongf i = 0; i < zn; i += 3) {
    unsigned v = z[i] << 16 | (i + 1 < zn ? z[i + 1] << 8 : 0) | (i + 2 < zn ? z[i + 2] : 0);
    *o++ = B64[v >> 18 & 63]; *o++ = B64[v >> 12 & 63];
    if (i + 1 < zn) *o++ = B64[v >> 6 & 63];
    if (i + 2 < zn) *o++ = B64[v & 63];
  }
  *o = 0; free(z);
  return out;
}

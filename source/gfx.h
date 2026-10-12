// A small slice of the HTML canvas API on top of citro2d, so the drawing code reads like web/game.js.
// Colours are 0xRRGGBBAA. Paths are convex pieces (plus ear clipping for the odd concave one).
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef uint32_t col_t;
#define RGB(h) ((col_t)(((uint32_t)(h) << 8) | 0xff))
col_t rgba(int r, int g, int b, double a);
col_t shadec(col_t c, double f);
col_t hslc(double h, double s, double l);

void gfx_init(void);
void gfx_exit(void);
void gfx_frame_begin(void);       // starts the frame: the game picture is drawn first, into the off-screen texture
void gfx_top_begin(void);         // switch to the top screen: puts the game picture on it, scaled to fit
void gfx_bottom_begin(col_t clear);
void gfx_frame_end(void);

void cv_reset(void);
void cv_save(void);
void cv_restore(void);
void cv_translate(double x, double y);
void cv_rotate(double a);
void cv_scale(double sx, double sy);
void cv_alpha(double a);          // globalAlpha
double cv_get_alpha(void);
void cv_fill(col_t c);            // fillStyle
void cv_rect(double x, double y, double w, double h);
void cv_begin(void);
void cv_move(double x, double y);
void cv_line(double x, double y);
void cv_arc(double cx, double cy, double r, double a0, double a1);
void cv_ellipse(double cx, double cy, double rx, double ry, double rot, double a0, double a1);
void cv_close(void);
void cv_quad(double cx, double cy, double x, double y);                                  // quadraticCurveTo
void cv_bezier(double c1x, double c1y, double c2x, double c2y, double x, double y);      // bezierCurveTo
void cv_fillpath(void);
void cv_stroke(col_t c, double w);
void cv_stroke_dash(col_t c, double w, double on, double off);   // stroke with setLineDash([on, off])
void cv_stroke_rect(double x, double y, double w, double h, col_t c, double lw);
void cv_clip(void);               // clip to the current path (each subpath must be convex; several = their union)
void cv_poly(const double *xy, int n);
void cv_circle(double cx, double cy, double r);
// a linear gradient between two points along x or y (axis-aligned only), filling the rect
void cv_grad(double x, double y, double w, double h, bool vertical, double g0, double g1, int n, const double *stops, const col_t *cols);

enum { PAT_ROAD, PAT_G0, PAT_G1, PAT_G2, NPAT };
void pat_make(int which, col_t base, col_t a, col_t b, int count, int size);
void pat_offset(double ox);
void cv_pattern(int which, double x, double y, double w, double h);

enum { AL_LEFT, AL_CENTER, AL_RIGHT };
enum { BL_ALPHA, BL_MIDDLE, BL_TOP };
enum { FONT_SYS, FONT_MONO, FONT_DISP, NFONT };   // the console's font (has the kana), and the web game's DM Mono / Dela Gothic One
void cv_font(int f);
int cv_get_font(void);               // the font the next cv_text calls use
void cv_text(const char *s, double x, double y, double size, col_t c, int align, int base);
double cv_text_width(const char *s, double size);
void cv_text_wrap(const char *s, double x, double y, double size, col_t c, double width);   // top-left, wrapped at width

// sprites: drawn once (inside gfx_build), then stamped with the current transform
enum { SPR_BIKE, SPR_HALFTONE, SPR_GARAGE, NSPR };
void gfx_build(void (*fn)(void));
void spr_begin(int id, int w, int h, double scale, double px, double py);   // draw it with the cv_ calls after this
void cv_sprite(int id);
void spr_end(void);                // after drawing a sprite in the middle of a frame: back to the game picture

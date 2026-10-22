#include "ui.h"
#include <3ds.h>
#include <math.h>

#define MAXB 48
static Box boxes[MAXB]; static int nb, focus, pressed = -1, lastN;

void ui_begin(void) { lastN = nb; nb = 0; pressed = -1; }
static int add(Box b) { if (nb == MAXB) return MAXB - 1; boxes[nb] = b; return nb++; }
int ui_focus(void) { return focus; }
void ui_set_focus(int id) { focus = id; }
bool ui_is_focus(int id) { return id == focus; }
void ui_reset_focus(void) { focus = 0; }
int ui_pressed(void) { return pressed; }

static void outline(Box b, unsigned col, double lw, double pad) { cv_stroke_rect(b.x - pad, b.y - pad, b.w + 2 * pad, b.h + 2 * pad, col, lw); }

int ui_button(Box b, const char *label, int style, double size) {
  int id = add(b); bool f = id == focus;
  if (style == BTN_MINI) {
    cv_fill(rgba(10, 6, 18, .7)); cv_rect(b.x, b.y, b.w, b.h);
    cv_stroke_rect(b.x + 0.5, b.y + 0.5, b.w - 1, b.h - 1, f ? UI_CYAN : UI_LINE, 1);
    cv_font(FONT_MONO); cv_text(label, b.x + b.w / 2, b.y + b.h / 2, size, UI_BONE, AL_CENTER, BL_MIDDLE);
    return id;
  }
  unsigned face = style == BTN_RED ? UI_RED : style == BTN_CYAN ? UI_CYAN : style == BTN_GOLD ? UI_GOLD : UI_INK2;
  unsigned shadow = style == BTN_RED ? UI_GOLD : style == BTN_CYAN ? UI_PINK : style == BTN_GOLD ? UI_RED : UI_CYAN;
  double d = f ? 4 : 3;                                          // the focused button lifts, like the web's hover
  double ox = f ? -1 : 0;
  cv_fill(shadow); cv_rect(b.x + d + ox, b.y + d + ox, b.w, b.h);
  cv_fill(face); cv_rect(b.x + ox, b.y + ox, b.w, b.h);
  if (f) outline((Box){ b.x + ox, b.y + ox, b.w + d, b.h + d }, UI_CYAN, 2, 3);
  cv_font(FONT_DISP);
  cv_text(label, b.x + ox + b.w / 2, b.y + ox + b.h / 2, size, style == BTN_ALT ? UI_BONE : UI_INK, AL_CENTER, BL_MIDDLE);
  return id;
}
int ui_hot(Box b) { return add(b); }

void ui_input(unsigned kd, int tx, int ty, bool touchDown) {
  if (nb == 0) return;
  if (focus >= nb) focus = nb - 1;
  if (touchDown) {
    for (int i = nb - 1; i >= 0; i--) { Box b = boxes[i]; if (tx >= b.x - 2 && tx < b.x + b.w + 4 && ty >= b.y - 2 && ty < b.y + b.h + 4) { focus = i; pressed = i; return; } }
  }
  if (kd & KEY_A) { pressed = focus; return; }
  int dx = (kd & KEY_RIGHT) ? 1 : (kd & KEY_LEFT) ? -1 : 0, dy = (kd & KEY_DOWN) ? 1 : (kd & KEY_UP) ? -1 : 0;
  if (!dx && !dy) return;
  Box c = boxes[focus]; double cx = c.x + c.w / 2, cy = c.y + c.h / 2, best = 1e9; int to = -1;
  for (int i = 0; i < nb; i++) {                                // the nearest button in that direction, favouring straight lines
    if (i == focus) continue;
    Box b = boxes[i]; double bx = b.x + b.w / 2, by = b.y + b.h / 2, ax = (bx - cx) * dx, ay = (by - cy) * dy;
    double along = dx ? ax : ay, across = dx ? fabs(by - cy) : fabs(bx - cx);
    bool overlap = dx ? (b.y < c.y + c.h && b.y + b.h > c.y) : (b.x < c.x + c.w && b.x + b.w > c.x);
    if (along <= 1) continue;
    double score = along + across * (overlap ? 0.2 : 2.5);
    if (score < best) { best = score; to = i; }
  }
  if (to >= 0) focus = to;
}

void ui_panel(void) {
  cv_fill(rgba(10, 6, 18, .82)); cv_rect(0, 0, 400, 240);
  double s[3] = { 0, 0.5, 1 }; col_t c[3] = { rgba(255, 45, 45, .20), rgba(255, 45, 45, .07), rgba(255, 45, 45, 0) };
  cv_grad(0, 0, 400, 240, true, 0, 240, 3, s, c);
}
void ui_kicker(const char *s, double x, double y, unsigned col) { cv_font(FONT_MONO); cv_text(s, x, y, 8, col, AL_LEFT, BL_ALPHA); }
void ui_heading(const char *s, double x, double y, double size, int align) {
  cv_font(FONT_DISP); double o = size * 0.05 + 0.5;
  cv_text(s, x + o, y + o, size, UI_GOLD, align, BL_ALPHA); cv_text(s, x, y, size, UI_RED, align, BL_ALPHA);
}
void ui_text(const char *s, double x, double y, double size, unsigned col, int align) { cv_font(FONT_MONO); cv_text(s, x, y, size, col, align, BL_ALPHA); }
void ui_wrap(const char *s, double x, double y, double size, unsigned col, double width) { cv_font(FONT_MONO); cv_text_wrap(s, x, y, size, col, width); }
void ui_check(double x, double y, bool on) {
  if (on) { cv_fill(UI_PINK); cv_rect(x, y, 10, 10); cv_begin(); cv_move(x + 2, y + 5); cv_line(x + 4.5, y + 7.5); cv_line(x + 8.5, y + 2.5); cv_stroke(UI_INK, 1.6); }
  else { cv_fill(UI_INK); cv_rect(x, y, 10, 10); cv_stroke_rect(x + 0.5, y + 0.5, 9, 9, 0x6f6790ffu, 1); }
}
void ui_slider(Box b, double t, bool focus) {
  double cy = b.y + b.h / 2;
  cv_fill(0x241838ffu); cv_rect(b.x, cy - 2, b.w, 4);
  cv_fill(UI_PINK); cv_rect(b.x, cy - 2, b.w * t, 4);
  double kx = b.x + b.w * t;
  cv_fill(focus ? UI_CYAN : UI_BONE); cv_rect(kx - 3, cy - 6, 6, 12);
}

#pragma once
// The menu look of the web game: dark panels, chunky buttons with an offset shadow, DM Mono text and Dela Gothic
// headings. Buttons are kept in a list each frame so the D-pad can move between them and taps can find them.
#include <stdbool.h>
#include "gfx.h"

#define UI_INK    0x0a0612ffu
#define UI_INK2   0x170d26ffu
#define UI_RED    0xff2d2dffu
#define UI_BONE   0xf1ecf7ffu
#define UI_GOLD   0xffd21fffu
#define UI_CYAN   0x22e6d8ffu
#define UI_PINK   0xff3ea5ffu
#define UI_SUB    0xb8a9d6ffu
#define UI_DIM    0x8d86a8ffu
#define UI_LINE   0x4a3874ffu

typedef struct { double x, y, w, h; } Box;
enum { BTN_RED, BTN_CYAN, BTN_ALT, BTN_MINI, BTN_GOLD };

void ui_begin(void);                          // start of a screen's frame: forget last frame's buttons
int ui_button(Box b, const char *label, int style, double size);   // returns its id; focus and taps are by id
int ui_hot(Box b);                            // an invisible tappable / focusable area (list rows, sliders)
void ui_input(unsigned kd, int tx, int ty, bool touchDown);        // D-pad focus moves and taps, after drawing
int ui_pressed(void);                         // the id activated this frame (A or a tap), or -1
int ui_focus(void);                           // the focused id
void ui_set_focus(int id);
bool ui_is_focus(int id);
void ui_reset_focus(void);                    // next screen: focus its first button

void ui_panel(void);                          // the panel background over a whole screen
void ui_kicker(const char *s, double x, double y, unsigned col);   // small caps-ish mono line
void ui_heading(const char *s, double x, double y, double size, int align);   // red Dela Gothic with a gold shadow
void ui_text(const char *s, double x, double y, double size, unsigned col, int align);
void ui_wrap(const char *s, double x, double y, double size, unsigned col, double width);
void ui_check(double x, double y, bool on);
void ui_slider(Box b, double t, bool focus);  // t in 0..1

// What the Wii's canvas layer adds to gfx.h: the menus and HUD are drawn in 640x360 units over the game picture.
#pragma once
#include <stdbool.h>
#define UI_W 640
#define UI_H 360
#define LW_GAME 480
#define LH_GAME 270
bool gfx_wide(void);
int gfx_hz(void);                                      // 60, or 50 on a PAL Wii at 50 Hz                                   // the Wii is set to 16:9
int gfx_shot(unsigned char *rgb, int *w, int *h);      // the last frame as RGB (tests)

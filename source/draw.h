#pragma once
#include <stdbool.h>
void draw_init(void);
void look_apply(void);
void render_game(const char *hint);
void drawBike(double x, double y, double wheelie, double spin, bool noShadow);
void draw_sprites(void);   // inside gfx_build()

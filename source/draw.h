#pragma once
#include <stdbool.h>
#include "sim.h"
void draw_init(void);
void look_apply(void);
void render_game(const char *hint);
void drawBike(double x, double y, double wheelie, double spin, bool noShadow);
void draw_sprites(void);   // inside gfx_build()
void draw_parts(const Part *ps, int n, double camX);   // paints particles (the run's, or the garage preview's trail sample)

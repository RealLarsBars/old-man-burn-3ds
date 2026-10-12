// The Old Man's cosmetics (paint, cardigan, hat, wheels, decal, gear, carrier, cane, trail), ported from web/game.js.
// Item ids are the web game's (achievements.json); "" is the default look.
#pragma once
#include <stdbool.h>

enum { SL_PAINT, SL_COAT, SL_CAP, SL_RIMS, SL_DECAL, SL_GEAR, SL_CARRIER, SL_CANE, SL_TRAIL, NSLOT };
void look_set(int slot, const char *id);   // "" = default. Marks the bike sprite dirty.
const char *look_get(int slot);
bool look_known(int slot, const char *id); // true if this port can draw that item
void look_frame(void);                     // each frame right after gfx_frame_begin(), before any other drawing: rebuilds SPR_BIKE
                                           // if dirty, and re-renders it about 10 times a second while an animated item needs it
void look_trail(double dt);                // emits the current trail's particles (from the game update while riding)
void look_preview(double x, double y, double scale);   // the bike, rider and a trail sample, rear-wheel ground point at (x, y)

// for draw.c
void look_sprite(void);                    // draws SPR_BIKE (from draw_sprites, inside gfx_build)
void look_under(void);                     // in the bike's coordinates, before the sprite is stamped: animated wheels
void look_over(void);                      // after it: animated hat (and the specs over it), gear, the Cat Pal

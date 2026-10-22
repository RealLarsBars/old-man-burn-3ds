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
void look_prebuild(void);                  // inside a frame, after look_frame: draws all the animation frames now (for the bench)
int look_count(int slot);                  // the items this port can draw in a slot
const char *look_item(int slot, int i);

// for draw.c
int look_spr(void);                        // which sprite to stamp for the bike this frame (SPR_BIKE or an animation frame)
void look_sprite(void);                    // draws SPR_BIKE (from draw_sprites, inside gfx_build)
void look_under(void);                     // in the bike's coordinates, before the sprite is stamped: animated wheels
void look_over(void);                      // after it: animated hat (and the specs over it), gear, the Cat Pal

// for the Derby's jockeys (derby_art.c): the cardigans in the web game's order, each one's pixel painter (the bike's coat
// coordinates, x -9..25, y -34..-12) and trim colour, and a cap drawn with the bike's head at (15, -36)
#include "gfx.h"
int look_coat_n(void);
const char *look_coat_id(int i);
col_t look_coat_px(const char *coat, int x, int y, bool sleeve);
col_t look_coat_trim(const char *coat);
void look_cap(const char *cap);

#pragma once
// The Old Man Derby on the 3DS: the day's card with your free pick and boost on the bottom screen, the post parade, the
// 3pm race live and the last race's replay on the top screen, then the results. It needs the console linked to Discord:
// the card, the pick and the coins are the linked rider's, through /api/3ds/derby (the same race everyone watches).
#include <stdbool.h>
#include <3ds.h>
#include <jansson.h>

void derby_open(void);          // from the title's DERBY button
bool derby_is_open(void);
void derby_close(void);
void derby_keys(u32 *kd);       // before the bottom screen is drawn: handles B, the D-pad and Y, and clears what it used
void derby_tick(double dt);     // every frame while open: the clock, refreshing the card, starting the 3pm race
void derby_top(double dt);      // the top screen's picture (instead of the bike game), right after gfx_frame_begin
void derby_bottom(void);        // the bottom screen
void derby_pressed(int id);     // a button on the bottom screen was pressed
void derby_reply(int what, int status, json_t *j);   // from sync_tick: an answer to one of the Derby's requests

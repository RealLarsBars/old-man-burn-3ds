// The Old Man Derby's pixel art, ported from the web game's web/derby-art.js: the track, the horses and their old-man
// jockeys on the same 480x270 picture as the bike game. On the 3DS every horse is drawn once per race into an atlas
// (12 strides of the gallop and a standing pose) and the sky, the grandstands and the tote board once per card, so a
// frame stamps those instead of painting thousands of pixels. da_scene() draws a whole frame; da_show_*() is one race
// as the TV shows it, the same calls, camera and timing as raceShow() in derby-art.js.
#pragma once
#include <stdbool.h>
#include "gfx.h"

#define DA_LW 480
#define DA_LH 270
#define DA_RACE_LEN 13200   // world pixels from the gate to the line
#define DA_INTRO 3          // seconds in the gate before the doors open
#define DA_LANE0 184        // horse #1 runs on the rail (furthest away), #6 on the outside
#define DA_LANE 8
#define DA_NH 6             // runners in a race (the server's DERBY_FIELD)
#define DA_NA 4             // the path's sine terms (the server draws 3)

typedef struct { char coat[16], mane[12], marks[10], pattern[12]; col_t silk1, silk2; } DaLook;
typedef struct { char coat[16], cap[16]; } DaRider;   // garage ids: the cardigan and the hat
typedef struct {
  char id[24], name[32], odds[8]; int num; DaLook look; DaRider rider;
  double T, a[DA_NA];                                 // the path, once the race has been drawn
} DaHorse;

typedef struct { double x, y, phase; int h; bool standing, mine, plate; const char *tag; col_t tagCol; } DaRunner;
typedef struct { int num; const char *name, *odds; bool hot; } DaBoardRow;
typedef struct {
  double t, dt, camX, finish; bool cheer;
  bool hasGate; double gate, gateOpen;
  int nb; DaBoardRow board[DA_NH];
  int nr; DaRunner r[DA_NH];
} DaScene;

double da_hash(int a, int b);
void da_outfit(int raceId, int num, DaRider *out);   // a jockey's garage outfit for this race and saddle cloth (outfit() on the web)
double da_progress(const DaHorse *h, double t);     // 0 = the gate, 1 = the line, t seconds after the off (server/derby.py)

// The horses (and the tote board's rows) for the card or race on screen. Building happens a few pictures at a time
// inside the frame: da_build() right after gfx_frame_begin(), every frame, until it says it's done.
void da_set(const DaHorse *hs, int n, const DaBoardRow *board, int nb);
bool da_build(void);
bool da_ready(void);
void da_scene(const DaScene *s);

// the 3x5 pixel font, as text() and textW() on the web (sc: how many screen pixels a font pixel is)
void da_text(const char *s, double x, double y, col_t c, int sc);
int da_textw(const char *s, int sc);

typedef struct {
  const DaHorse *hs; int n; int mine, oldMan, winner, second;   // indexes into hs (-1 none); winner/second from the order
  double t, cam, camV, tvs, prevTarget; bool havePrev;
  char said[64]; double sayUntil; int lead; double leadAt, flash;
  bool cGate, cOff, cFinal, cWon;
} DaShow;
typedef struct { double rs, lead; bool won, photo; int order[DA_NH]; } DaShowOut;
void da_show_start(DaShow *v, const DaHorse *hs, int n, int mine, int oldMan, int winner, int second);
DaShowOut da_show_draw(DaShow *v, double rt, double dt);
double da_show_end(const DaShow *v);              // when the show is over, in show seconds

// Old Man Burn for the 3DS: the road, the physics and the collisions.
// A line-for-line port of web/game.js so the same seed builds the same map with the same rules.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#define LW 480
#define LH 270
#define HOR 104
#define M_PX 6
#define START_BX 230
#define TILE 128
#define V2_FROM 20737
#define SW3_FROM 20737
#define SW4_FROM 20744
#define SW4_DAILY_FROM 20738
#define SPLIT_FROM 20744       // split roads: weekly maps from Oct 18 2026, Daily Dash from day 20740 (Oct 14 noon PT)
#define SPLIT_DAILY_FROM 20740
#define THIN_RAILS_FROM 20737
#define FIRST_WEEK 20730
#define DAILY_BASE 1000000
#define N_LANDS 8
#define FIRST_THEME 5       // landscapes from here on are themes with their own road and rails
#define PARKS_FROM 20758        // maps from Nov 1 2026 on pick from all five landscapes (look only)

typedef struct { double x, len; } Span;
typedef struct { double x, len; int side; int combo; int combo_i; } Rail;  // side -1 = a set piece's, picked by its swerve
typedef struct { double x, len, from, to; } Swerve;
typedef struct { double x, len; int rail; } Combo;          // rail -1 = a split gap's swerve

typedef struct { Span *a; int n, cap; } Spans;

enum { ST_TITLE, ST_PLAY, ST_CRASH, ST_OVER };
enum { CAUSE_NONE, CAUSE_BEHIND, CAUSE_AHEAD, CAUSE_RAIL };

// kind picks how drawParts paints a particle: the trails (look.c) have their own looks; ph is a coin's, bat's, flake's
// or glint's animation phase
enum { PK_PLAIN, PK_RING, PK_BUBBLE, PK_COIN, PK_BAT, PK_FLAKE, PK_GLINT, PK_FIRE, PK_SPARK };
typedef struct {
  double x, y, vx, vy, life, max, s, grow, grav, w;
  uint32_t c; uint8_t round, kind; float ph;
} Part;

#define MAX_PARTS 700
#define MAX_FX 400

typedef struct {
  double cam, bx, y, v, P, vy, vyD, thr, wheelie, t;
  double cx0, cy0, crashT, spin, shake, flash;
  int cause, dist; bool onRoad;
  Part parts[MAX_PARTS]; int nparts;
  Part fx[MAX_FX]; int nfx;
} Run;

// world
extern int SEED;
extern bool V2;
extern double BIKE_SCALE, BODY_MID, PROBE;
extern double RC, RW;
extern Spans gaps, edges, holds;
extern Rail *rails; extern int nrails;
extern int SEASON;       // 0 none, 1 halloween, 2 christmas, 3 thanksgiving
extern int ORDER[3], skyPerm[4];
extern int LAND;         // the landscape: 0 desert, 1 coast, 2 forest, 3 yosemite, 4 canyon, 5 library, 6 neon city, 7 boardwalk pier

// state
extern int st;
extern Run g;
extern double T;

double hash(int a, int b);
static inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
static inline double rnd(double v) { return floor(v + 0.5); }   // JS Math.round
double pace(double m);
void reseed(int seed, bool v2);
int mapDay(void);
bool thinRails(void);
void roadAt(double x);
void genTo(double x);
int lbSpans(const Spans *s, double x);
int lbRails(double x);
const Span *gapAt(double wx);
int railSide(int i);
double railY(int side);
double railBand(int side, double ry);
static inline double EDGE_TOP(void) { return RC - RW / 2 - 8; }
static inline double EDGE_BOT(void) { return RC + RW / 2 + 1; }
bool hitRail(double wx, double y, double py);
bool hitEdge(double wx, double y, double py);
int seasonOf(int seed);
void nameOf(int seed, int season, char *out, int n);
int zoneAt(void);
int landOf(int seed);
void newRun(void);
int scoreOf(void);

typedef struct { bool thr; double steer; double drag; } Input;   // steer -1..1 (arrows / circle pad), drag in logical px
void update(double dt, const Input *in);
void end_run_now(int cause);
extern bool simGod;         // test builds only: ride through everything
// Watching a recorded run: update() leaves the bike alone and sim_watch() puts it where the notes say (eased between
// them by the caller), with the dust and smoke a rider would kick up. end: CAUSE_* to crash right here, or 0.
extern bool simWatch;
void sim_watch(double dt, double cam, double bx, double y, double thr, double wheelie, int end);
// called by update(); the front end makes the noise and the pictures
void on_crash(void);
void on_over(void);
double frand(void);
void dirt_colors(uint32_t out[4]);   // the ground of the current landscape, darkened (draw.c)
int swervesPassed(double wx);

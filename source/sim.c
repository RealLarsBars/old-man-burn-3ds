// The road, the physics and the collisions, ported from web/game.js. Doubles and the same order of operations as the
// JavaScript, so a seed builds exactly the map the web game builds.
#include "sim.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int SEED = 0;
bool V2 = false;
double BIKE_SCALE = 1, BODY_MID = 16, PROBE = 20;
double RC = 189, RW = 46;
int SEASON = 0;
int ORDER[3] = { 0, 1, 2 }, skyPerm[4] = { 0, 1, 2, 3 }, LAND = 0;
int st = ST_TITLE;
bool paused = false;
Run g;
double T = 0;

static double pend = 0;
bool simGod = false;

double hash(int a, int b) {
  uint32_t h = (uint32_t)a * 374761393u ^ (uint32_t)b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (double)(h ^ (h >> 16)) / 4294967296.0;
}
double pace(double m) { return 520 + fmin(m * 0.1, 400); }   // world scroll speed, px/s. It keeps rising.
double frand(void) { return rand() / ((double)RAND_MAX + 1); }

static void setVersion(bool v2) { V2 = v2; BIKE_SCALE = v2 ? 0.8 : 1; BODY_MID = 10 * BIKE_SCALE; PROBE = 20 * BIKE_SCALE; }

/* ---------- growable arrays ---------- */
static void spanPush(Spans *s, double x, double len) {
  if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 256; s->a = realloc(s->a, s->cap * sizeof(Span)); }
  s->a[s->n++] = (Span){ x, len };
}
Spans gaps, edges, holds;
Rail *rails; int nrails; static int railCap;
static int railPush(Rail r) { if (nrails == railCap) { railCap = railCap ? railCap * 2 : 256; rails = realloc(rails, railCap * sizeof(Rail)); } rails[nrails] = r; return nrails++; }
static Swerve *sw; static int nsw, swCap;
static void swPush(Swerve e) { if (nsw == swCap) { swCap = swCap ? swCap * 2 : 256; sw = realloc(sw, swCap * sizeof(Swerve)); } sw[nsw++] = e; }
static Combo *combos; static int ncombos, comboCap;
static double *wp; static int nwp, wpCap;

int lbSpans(const Spans *s, double x) { int lo = 0, hi = s->n; while (lo < hi) { int mid = (lo + hi) >> 1; if (s->a[mid].x < x) lo = mid + 1; else hi = mid; } return lo; }
int lbRails(double x) { int lo = 0, hi = nrails; while (lo < hi) { int mid = (lo + hi) >> 1; if (rails[mid].x < x) lo = mid + 1; else hi = mid; } return lo; }
static int lbSw(double x) { int lo = 0, hi = nsw; while (lo < hi) { int mid = (lo + hi) >> 1; if (sw[mid].x < x) lo = mid + 1; else hi = mid; } return lo; }

/* ---------- road: a wandering band, plus gaps and edge guardrails generated from the seed ---------- */
#define S_ 680
#define CMIN 154
#define CMAX 224
#define LOOK 4200   // longer than any gap, rail or edge run, so a long one is never skipped
static void ensure(int i) {
  while (nwp <= i) {
    if (nwp == wpCap) { wpCap = wpCap ? wpCap * 2 : 256; wp = realloc(wp, wpCap * sizeof(double)); }
    int k = nwp;
    if (k < 2) { wp[nwp++] = 189; continue; }
    double m = (double)k * S_ / M_PX, step = fmin(16 + m * 0.012, 60), r = hash(SEED, k * 7 + 1) * 2 - 1;
    wp[nwp++] = clampd(wp[k - 1] + r * step, CMIN, CMAX);
  }
}
int mapDay(void) { return SEED >= DAILY_BASE ? SEED - DAILY_BASE : SEED; }
static bool sw3(void) { return mapDay() >= SW3_FROM; }
static bool sw4(void) { return mapDay() >= (SEED >= DAILY_BASE ? SW4_DAILY_FROM : SW4_FROM); }
bool thinRails(void) { return mapDay() >= THIN_RAILS_FROM; }
#define RAIL_BAND 3

static double swCursor = 2600, swLevel = 0; static int swK = 0; static bool swRetry = false;
static double cursor = 2900; static int fi = 0, swCi = 0, phK = 0; static bool phRest = false;
static void genSw(double x);

// A swerve never overlaps a barricade or a gap, and keeps clear of them on both sides.
static bool busyIn(const Span *a, int n, int i0, double x, double len, double pad) {
  for (int i = i0; i < n && a[i].x < x + len + pad; i++) if (a[i].x + a[i].len > x - pad) return true;
  return false;
}
static bool busyRoad(double x, double len) {
  genTo(x + len + 1500);
  double pr, pg, pe, ph; int useE, useH;
  if (sw4()) { pr = 450; pg = 300; pe = 250; ph = 300; useE = 1; useH = 1; }
  else if (sw3()) { pr = 600; pg = 400; pe = 300; ph = 0; useE = 1; useH = 0; }
  else { pr = 600; pg = 400; pe = 0; ph = 0; useE = 0; useH = 0; }
  { int i0 = lbRails(x - LOOK); if (i0 < 0) i0 = 0;
    for (int i = i0; i < nrails && rails[i].x < x + len + pr; i++) if (rails[i].x + rails[i].len > x - pr) return true; }
  if (busyIn(gaps.a, gaps.n, lbSpans(&gaps, x - LOOK), x, len, pg)) return true;
  if (useE && busyIn(edges.a, edges.n, lbSpans(&edges, x - LOOK), x, len, pe)) return true;
  if (useH && busyIn(holds.a, holds.n, lbSpans(&holds, x - LOOK), x, len, ph)) return true;
  return false;
}
static void genSw(double x) {
  while (swCursor < x) {
    int k = swK++; double m = swCursor / M_PX;
    #define R(a) hash(SEED, 9000 + k * 5 + (a))
    double mm = m;
    bool s3 = sw3(), s4 = sw4(); double from = swCursor;
    swCursor += swRetry ? 200 + R(0) * 200 : (s3 ? 500 + R(0) * 800 : 1100 + R(0) * 1900) * fmax(s4 ? 0.4 : 0.55, 1 - mm * 0.00008) / 1.0;
    double len = rnd(300 - fmin(mm * 0.03, 90) + R(1) * 60);                    // 210 to 360 px of travel: about a third of a second
    double mag = (24 + R(2) * 17 + fmin(mm * 0.007, 9)) * 1.0;                   // how far the road actually moves: about 24 to 50 px
    if (s4) { genTo(swCursor + 1500);                                          // a set piece's swerve goes exactly where it was planned
      if (swCi < ncombos && combos[swCi].x <= swCursor) {
        Combo *c = &combos[swCi]; swCi++;
        if (c->x < from) { rails[c->rail].side = R(4) < 0.5 ? 0 : 1; swCursor = from; continue; }   // (never expected) its spot was already taken
        double dir = swLevel > 10 ? -1 : swLevel < -10 ? 1 : (R(3) < 0.5 ? 1 : -1), to = clampd(swLevel + dir * fmax(mag, 30), -40, 40);
        rails[c->rail].side = to > swLevel ? 0 : 1;                                // the wall goes on the side the road just left
        swPush((Swerve){ c->x, c->len, swLevel, to }); swLevel = to; swCursor = c->x + c->len; swRetry = false;
        continue;
      } }
    if (busyRoad(swCursor, len)) { swRetry = s3; continue; }                     // swerves only happen on open road; newer maps look again soon after
    double dir = swLevel > 10 ? -1 : swLevel < -10 ? 1 : (R(3) < 0.5 ? 1 : -1);  // swing back the other way when it has drifted off centre
    double to = clampd(swLevel + dir * mag, -40, 40);
    swPush((Swerve){ swCursor, len, swLevel, to }); swLevel = to; swCursor += len; swRetry = false;
    #undef R
  }
}
static double swOff(double x) {
  genSw(x + 3000);
  int i = lbSw(x + 1e-6) - 1; if (i < 0) return 0;
  Swerve *e = &sw[i]; double t = (x - e->x) / e->len; if (t >= 1) return e->to;
  return e->from + (e->to - e->from) * t * t * (3 - 2 * t);
}
void roadAt(double x) {
  double f = fmax(0, x) / S_; int i = (int)floor(f); ensure(i + 1);
  double t = f - i, s = (1 - cos(M_PI * t)) / 2;
  RC = clampd(wp[i] + (wp[i + 1] - wp[i]) * s + (V2 ? swOff(x) : 0), 138, 236); RW = fmax(34, 46 - x / M_PX * 0.003);
}
static int railNew(double x, double len, int side) { return railPush((Rail){ x, len, side, 0, -1 }); }
static double barricadeLen(double m, double P, double f) { return rnd((240 + f * (140 + fmin(m * 0.05, 200))) * P / 170); }
// From the maps of 2026-10-18 on (sw4) the road is built in phrases: gauntlets of gaps, rests and set pieces.
static void genPhrase(void) {
  int k = phK++; double m = cursor / M_PX, P = pace(m);
  #define R(a) hash(SEED, 50000 + k * 13 + (a))
  if (!phRest) {                                               // after every gauntlet or set piece comes a rest
    phRest = true;
    if (k > 0 && m >= 600 && R(1) < 0.4) {                     // some rests have a barricade along one edge instead of swerves
      double len = barricadeLen(m, P, R(2));
      cursor += rnd(0.8 * P); railNew(cursor, len, R(3) < 0.5 ? 0 : 1); cursor += len + rnd(0.8 * P);
    } else cursor += rnd((k == 0 ? 1.5 + R(0) : fmax(2.6, 5.5 - m / 1500) + R(0) * 1.5) * P);   // ~6 s early, ~3 s late
    return;
  }
  phRest = false;
  if (m >= 2500 && R(4) < 0.3) {                               // a swerve, then a barricade on the side the road swerved away from
    double swLen = rnd(300 - fmin(m * 0.03, 90) + R(5) * 60), lead = swLen + rnd(0.35 * P);
    double len = barricadeLen(m, P, R(6));
    if (ncombos == comboCap) { comboCap = comboCap ? comboCap * 2 : 64; combos = realloc(combos, comboCap * sizeof(Combo)); }
    int ri = railPush((Rail){ cursor + lead, len, -1, 1, ncombos });
    combos[ncombos++] = (Combo){ cursor, swLen, ri };
    spanPush(&holds, cursor - 200, lead + 200);
    cursor += lead + len;
    return;
  }
  // A gauntlet: 2 or 3 gaps early, up to 6 late, every one boxed in by guardrails.
  int n = m < 1500 ? 2 + (R(7) < 0.5) : m < 3000 ? 3 + (R(7) < 0.5) : 4 + (int)floor(R(7) * 3);
  for (int i = 0; i < n; i++) {
    double mi = cursor / M_PX, Pi = pace(mi), q = hash(SEED, 60000 + k * 17 + i);
    double len = rnd((i % 2 == 0 ? 0.55 + q * 0.3 : 0.9 + q * 0.25 + clampd((mi - 1000) / 6000, 0, 1) * 0.5) * Pi);
    spanPush(&gaps, cursor, len); spanPush(&edges, cursor - 100, len + 200);
    cursor += len;
    if (i < n - 1) { double road = rnd((0.85 + hash(SEED, 61000 + k * 17 + i) * 0.25) * Pi);
      spanPush(&holds, cursor, road); cursor += road; }                 // swerves stay out of a gauntlet
  }
  #undef R
}
void genTo(double x) {
  while (cursor < x) {
    if (sw4()) { genPhrase(); continue; }
    int k = fi++; double m = cursor / M_PX, P = pace(m);
    #define R(a) hash(SEED, k * 11 + (a))
    double spacing = fmax(1300, 3300 - m * 0.4) * (0.8 + 0.5 * R(1));
    double gapP = m < 120 ? 0 : fmin(0.18 + m * 0.00012, 0.42);
    double railP = m < 220 ? 0 : fmin(0.10 + m * 0.00006, 0.24);
    double roll = R(2);
    if (roll < gapP) {
      double len = rnd((0.6 + R(3) * fmin(V2 ? 0.84 : 1.1, 0.3 + m / 5000)) * P);
      spanPush(&gaps, cursor, len); spanPush(&edges, cursor - 100, len + 200);
      cursor += len + fmax(800, spacing * 0.5);
    } else if (roll < gapP + railP) {
      double len = rnd((240 + R(5) * (140 + fmin(m * 0.05, 200))) * P / 170);
      railNew(cursor, len, R(4) < 0.5 ? 0 : 1);
      cursor += len + spacing * 0.6;
    } else {
      if (R(6) < 0.4) spanPush(&edges, cursor, 700);
      cursor += spacing * 0.7;
    }
    #undef R
  }
}
const Span *gapAt(double wx) {
  genTo(wx + 2200);
  int i0 = lbSpans(&gaps, wx - LOOK);
  for (int i = i0; i < gaps.n && gaps.a[i].x <= wx; i++) if (wx < gaps.a[i].x + gaps.a[i].len) return &gaps.a[i];
  return NULL;
}
// A set piece's barricade picks its side once its swerve is laid down (the wall goes where the road just left).
int railSide(int i) { if (rails[i].side < 0) genSw(combos[rails[i].combo_i].x + 1); return rails[i].side; }
double railY(int side) { return side == 0 ? RC - RW / 2 - 8 : RC + RW / 2 + 1; }
double railBand(int side, double ry) {
  double room = side == 0 ? ry - (HOR + 8 - BODY_MID) : LH - 4 - BODY_MID - ry;
  return clampd(room - 1, 0, RAIL_BAND);
}
// Thin rule for one wall line (call roadAt first): deadly from the old line out to the band, or jumping the band in one frame.
static bool thinHit(int side, double y, double py) {
  double ry = railY(side), out = side == 0 ? -1 : 1, o = (y - BODY_MID - ry) * out, po = (py - BODY_MID - ry) * out, bw = railBand(side, ry);
  return (o > 0 && o <= bw) || (po <= 0 && o > bw) || (po > bw && o <= 0);
}
bool hitRail(double wx, double y, double py) {
  genTo(wx + 2200);
  bool thin = thinRails();
  int i0 = lbRails(wx - LOOK);
  for (int i = i0; i < nrails && rails[i].x <= wx + 30; i++) {
    double ox3[3] = { -PROBE, 0, PROBE };
    for (int q = 0; q < 3; q++) {
      double x = wx + ox3[q]; if (x < rails[i].x || x > rails[i].x + rails[i].len) continue;
      int side = railSide(i);
      roadAt(x); double ry = railY(side);
      if (thin) { if (thinHit(side, y, py)) return true; }
      else if (V2) { double mid = y - BODY_MID; if (side == 0 ? mid < ry : mid > ry) return true; }
      else if (side == 0 ? y < ry + 16 : y > ry - 1) return true;
    }
  }
  return false;
}
bool hitEdge(double wx, double y, double py) {
  genTo(wx + 2200);
  bool thin = thinRails();
  int i0 = lbSpans(&edges, wx - LOOK);
  for (int i = i0; i < edges.n && edges.a[i].x <= wx + 30; i++) {
    double ox3[3] = { -PROBE, 0, PROBE };
    for (int q = 0; q < 3; q++) {
      double x = wx + ox3[q]; if (x < edges.a[i].x || x > edges.a[i].x + edges.a[i].len) continue;
      roadAt(x);
      if (thin) { if (thinHit(0, y, py) || thinHit(1, y, py)) return true; continue; }
      double mid = V2 ? y - BODY_MID : y; if (mid < EDGE_TOP() || mid > EDGE_BOT()) return true;
    }
  }
  return false;
}

/* ---------- seasons and names ---------- */
static void civil(int day, int *mo, int *d) {          // days since 1970-01-01 -> month, day (UTC)
  int z = day + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int mp = (5 * doy + 2) / 153; *d = doy - (153 * mp + 2) / 5 + 1; *mo = mp < 10 ? mp + 3 : mp - 9;
}
int seasonOf(int seed) {                                // dates as in web/achievements.json
  int day = seed >= DAILY_BASE ? seed - DAILY_BASE : seed, mo, d; civil(day, &mo, &d);
  int md = mo * 100 + d;
  if (md >= 1011 && md <= 1031) return 1;
  { int sun = day - (day + 4) % 7, smo, sd; civil(sun, &smo, &sd);   // Thanksgiving goes by the Sunday that starts the week ("by_week")
    int smd = smo * 100 + sd; if (smd >= 1118 && smd <= 1124) return 3; }
  if (md >= 1201 && md <= 1226) return 2;
  return 0;
}
static const char *NAMES[4][2][12] = {
  { { "Midnight","Chrome","Crimson","Thunder","Velvet","Neon","Typhoon","Paper","Burning","Electric","Lacquer","Smoke" },
    { "Emperor","Highway","Pompadour","Tide","Sermon","Kamikaze","Circuit","Lantern","Fuji","Static","Dragon","Motorway" } },
  { { "Haunted","Pumpkin","Phantom","Graveyard","Witching","Cobweb","Banshee","Moonlit","Creeping","Hollow","Cackling","Candy Corn" },
    { "Hayride","Hollow","Crypt","Turnpike","Bypass","Boneyard","Cauldron","Parkway","Manor","Moor","Express","Freeway" } },
  { { "Candy Cane","Jingle","Frosty","Mistletoe","Sleigh Bell","Tinsel","Eggnog","Gingerbread","Snowy","Holly","Yule","North Pole" },
    { "Causeway","Express","Turnpike","Lane","Run","Parkway","Bypass","Boulevard","Drift","Chimney","Motorway","Sleighway" } },
  { { "Gravy","Cranberry","Pumpkin Pie","Drumstick","Stuffing","Wishbone","Harvest","Maple","Acorn","Leafy","Sweet Potato","Turkey" },
    { "Lane","Court","Drive","Cul-de-sac","Way","Circle","Crescent","Avenue","Place","Terrace","Hollow","Grove" } }
};
void nameOf(int seed, int season, char *out, int n) {
  snprintf(out, n, "%s %s", NAMES[season][0][(int)floor(hash(seed, 1) * 12)], NAMES[season][1][(int)floor(hash(seed, 2) * 12)]);
}
int zoneAt(void) { return LAND; }                      // one landscape per track, fixed for the whole run

static void sortByC(int *a, int n, int c, int base) {   // [0..n).sort((a, b) => hash(c, base + a) - hash(c, base + b))
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && hash(c, base + a[j - 1]) > hash(c, base + a[j]); j--) { int t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
}
/* As landOf in web/game.js: from the Nov 1 2026 maps on, the landscape comes from a shuffled cycle of all five, so none
   repeats until every one has had its turn (weekly maps and the Daily Dash each run their own cycle). Earlier maps keep
   the one they always had. It only changes the look, never the road. */
int landOf(int seed) {
  bool daily = seed >= DAILY_BASE; int day = daily ? seed - DAILY_BASE : seed;
  if (day < PARKS_FROM) { int o[3] = { 0, 1, 2 }; sortByC(o, 3, seed, 10); return o[0]; }
  if (day > 400000) return 0;                           // not a real map day (the Wii's arcade seeds): the caller sets LAND
  int i = daily ? day - PARKS_FROM : (day - PARKS_FROM) / 7, salt = daily ? 60 : 50, cyc = i / 5;
  int o[5] = { 3, 4, 0, 1, 2 }; if (daily) { o[0] = 4; o[1] = 3; }   // the parks open the first cycle
  for (int c = 1; c <= cyc; c++) {
    int last = o[4], n[5] = { 0, 1, 2, 3, 4 }; sortByC(n, 5, c, salt);
    if (n[0] == last) { int t = n[0]; n[0] = n[1]; n[1] = t; }   // no repeat across the seam
    memcpy(o, n, sizeof o);
  }
  return o[i % 5];
}

static void sortBy(int *a, int n, int base) {           // [0..n).sort((a, b) => hash(SEED, base + a) - hash(SEED, base + b))
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && hash(SEED, base + a[j - 1]) > hash(SEED, base + a[j]); j--) { int t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
}
void reseed(int seed, bool v2) {
  SEED = seed; setVersion(v2);
  nwp = 0; nsw = 0; swCursor = 2600; swK = 0; swLevel = 0; swRetry = false;
  gaps.n = 0; nrails = 0; edges.n = 0; ncombos = 0; holds.n = 0; cursor = 2900; fi = 0; swCi = 0; phK = 0; phRest = false;
  for (int i = 0; i < 4; i++) skyPerm[i] = i;
  sortBy(skyPerm, 4, 20);
  for (int i = 0; i < 3; i++) ORDER[i] = i;
  sortBy(ORDER, 3, 10);
  LAND = landOf(SEED);
  SEASON = seasonOf(SEED);
  newRun();
}

/* ---------- state ---------- */
void newRun(void) {
  memset(&g, 0, sizeof g);
  g.bx = START_BX; g.y = 189; g.v = g.P = pace(0); g.onRoad = true;
  pend = 0;
}
int scoreOf(void) { double s = floor((g.cam + g.bx - START_BX) / M_PX); return s > 0 ? (int)s : 0; }

static Part *addPart(Part *arr, int *n, int max) { if (*n >= max) return NULL; Part *p = &arr[(*n)++]; memset(p, 0, sizeof *p); return p; }
static const uint32_t FXC[5] = { 0xff2d2dff, 0xffd21fff, 0xff3ea5ff, 0x22e6d8ff, 0xf1ecf7ff };
static void fxBurst(int n, double spd) {
  for (int i = 0; i < n; i++) {
    double a = frand() * M_PI * 2, v = (0.25 + frand() * 0.75) * spd;
    Part *p = addPart(g.fx, &g.nfx, MAX_FX); if (!p) return;
    p->x = LW / 2; p->y = LH / 2 + 14; p->vx = cos(a) * v; p->vy = sin(a) * v; p->life = 0.5 + frand() * 0.8; p->max = 1.3;
    p->c = FXC[(int)(frand() * 5)]; p->s = 1 + (int)(frand() * 3);
  }
}
static void crash(int cause) {
  g.dist = scoreOf(); st = ST_CRASH; g.crashT = 0; g.cause = cause;
  g.cx0 = clampd(g.bx, 30, LW - 30); g.cy0 = g.y;
  g.shake = 16; g.flash = 1;
  fxBurst(90, 420);
  on_crash();
}
static double dirtAcc = 0;
static void dirtCloud(double dt) {
  uint32_t cols[4]; dirt_colors(cols);
  dirtAcc += dt * (90 + 50 * g.thr + 0.03 * g.v) * 0.45;             // puffs per second: more on the throttle and at speed. Under half
                                                                     // the web's rate, a bit bigger: 180 growing puffs cost an Old 3DS ~4 ms
  while (dirtAcc >= 1) {
    dirtAcc -= 1;
    double life = 0.8 + frand() * 0.6;
    Part *p = addPart(g.parts, &g.nparts, MAX_PARTS); if (!p) return;
    p->x = g.cam + g.bx - 16 - frand() * 8; p->y = g.y - 2 - frand() * 4; p->vx = -(40 + frand() * 90); p->vy = -(18 + frand() * 46);
    p->life = life; p->max = life; p->c = cols[(int)(frand() * 4)]; p->s = 4; p->grow = 25 + frand() * 20; p->grav = -14; p->round = 1;
  }
}
static void puff(int dust) {
  double wx = g.cam + g.bx - 18;
  Part *p = addPart(g.parts, &g.nparts, MAX_PARTS); if (!p) return;
  if (dust) { p->x = wx; p->y = g.y - 2 - frand() * 3; p->vx = -(20 + frand() * 50); p->vy = -(10 + frand() * 30); p->life = p->max = 0.5; p->c = 0xd9c58aff; p->s = 2; }
  else { p->x = wx; p->y = g.y - 1; p->vx = -(30 + frand() * 60); p->vy = -(frand() * 14); p->life = p->max = 0.3; p->c = 0xe9e4f0ff; p->s = 1; }
}
static void stepParts(Part *parts, int *n, double dt) {
  int j = 0;
  for (int i = 0; i < *n; i++) {
    Part p = parts[i]; p.life -= dt; if (p.life <= 0) continue;
    p.vy += p.grav * dt; p.x += p.vx * dt; p.y += p.vy * dt;
    if (p.grow) { p.s += p.grow * dt; p.vx *= exp(-dt * 1.2); }
    parts[j++] = p;
  }
  *n = j;
}

void update(double dt, const Input *in) {
  g.t += dt; T += dt;
  if (st == ST_TITLE) {
    g.P = 520; g.cam += g.P * dt; g.bx = START_BX; g.v = g.P;
    roadAt(g.cam + g.bx); double ty = RC + sin(T * 1.3) * RW * 0.25, prev = g.y;
    g.y += (ty - g.y) * fmin(1, dt * 4); g.vyD = (g.y - prev) / dt; g.onRoad = true;
    bool want = sin(T * 0.9) > 0.35; g.thr += ((want ? 1 : 0) - g.thr) * fmin(1, dt * 6);
    g.wheelie += ((want ? 1 : 0) - g.wheelie) * fmin(1, dt * 6);
  } else if (st == ST_PLAY) {
    if (paused) return;
    double m = g.cam / M_PX, P = pace(m); g.P = P;
    g.cam += P * dt;
    bool want = in->thr;
    g.thr += ((want ? 1 : 0) - g.thr) * fmin(1, dt * 7);
    double wx = g.cam + g.bx;
    roadAt(wx);
    bool onRoad = !gapAt(wx) && fabs(g.y - RC) <= RW / 2;
    g.onRoad = onRoad;
    // The road scrolls at P. The bike only holds its spot while v is about P. Throttle works on the road only.
    if (g.t < 1.2) g.v = P;                                   // short grace at the start
    else if (onRoad) g.v += (want ? 1.6 : -0.8) * P * dt;     // idle on the road bleeds speed
    else g.v += ((want ? 0.9 * P : 0) - 1.5 * g.v) * dt;     // sand drags hard; throttle still pushes, but tops out around 0.6 P
    g.v = clampd(g.v, 0.2 * P, 2 * P);
    g.bx += (g.v - P) * dt;
    g.wheelie += ((want ? 1 : 0) - g.wheelie) * fmin(1, dt * 6);   // wheelie on any surface
    double dir = in->steer;
    if (dir != 0) g.vy += (dir * 230 - g.vy) * fmin(1, dt * 16); else g.vy *= exp(-dt * 14);
    pend = clampd(pend + in->drag, -60, 60);
    double prev = g.y;
    double step = pend * fmin(1, dt * 14); pend -= step;       // touch drags glide in over a few frames
    g.y = clampd(g.y + g.vy * dt + step, HOR + 8, LH - 4);
    if (g.y <= HOR + 8 || g.y >= LH - 4) { g.vy = 0; pend = 0; }
    g.vyD += ((g.y - prev) / dt - g.vyD) * fmin(1, dt * 18);
    wx = g.cam + g.bx;
    if (simGod) g.bx = clampd(g.bx, 0, LW);
    else if (hitRail(wx, g.y, prev) || hitEdge(wx, g.y, prev)) crash(CAUSE_RAIL);
    else if (g.bx < -10) crash(CAUSE_BEHIND);
    else if (g.bx > LW + 10) crash(CAUSE_AHEAD);
    if (st == ST_PLAY) {
      if (!onRoad && g.v > 0.15 * P) { if (V2) dirtCloud(dt); else if (frand() < 0.8) puff(1); }
      if (g.wheelie > 0.5 && frand() < 0.6) puff(0);
    }
  } else {
    g.crashT += dt; g.thr = 0; g.wheelie *= exp(-dt * 8);
    g.cam += g.crashT < 0.1 ? 0 : g.P * exp(-g.crashT * 2.2) * dt;   // brief hit-stop
    if (g.crashT < 0.9) fxBurst(3, 320);
    if (st == ST_CRASH && g.crashT > 1.7) { st = ST_OVER; on_over(); }
  }
  stepParts(g.parts, &g.nparts, dt);
  { int j = 0;
    for (int i = 0; i < g.nfx; i++) {
      Part p = g.fx[i]; p.life -= dt; if (p.life <= 0) continue;
      p.x += p.vx * dt; p.y += p.vy * dt; p.vx *= exp(-dt * 1.4); p.vy *= exp(-dt * 1.4);
      g.fx[j++] = p;
    }
    g.nfx = j; }
  g.shake *= exp(-dt * 5); g.flash = fmax(0, g.flash - dt * 2);
}

int swervesPassed(double wx) {                                  // swerves the bike is already past (the achievements count them)
  int i = lbSw(wx);
  if (i > 0 && sw[i - 1].x + sw[i - 1].len >= wx) i--;
  return i;
}

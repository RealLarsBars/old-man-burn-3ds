// Old Man Burn for the Nintendo 3DS: the weekly map, the Daily Dash and the old maps, offline.
// No coins, pot or leaderboards: your best on each map, your stats and achievements are kept on the SD card.
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include "sim.h"
#include "gfx.h"
#include "draw.h"
#include "audio.h"
#include "perf.h"
#include "ui.h"
#include "ach.h"
#include "sync.h"
#include "replay.h"
#include <citro3d.h>

#define H(x) RGB(0x##x)
enum { MODE_WEEK, MODE_DAILY, MODE_OLD };
static int mode = MODE_WEEK, liveWeek, liveDay, oldWeek, curSeed;
static char trackName[64];
static bool optMusic = true, optSfx = true; static int tzOff = 0;   // hours the console clock is ahead of Pacific time
static int dpadPct = 100, touchPct = 100;                       // steering sensitivity, percent (Settings sliders)
enum { UI_TITLE, UI_ARCHIVE, UI_SETTINGS, UI_GARAGE, UI_CODE };
static int screen = UI_TITLE, backTo = UI_TITLE;                // which menu the bottom screen shows (rides use st)
static int capturing = -1;                                       // the control (BIND_*) waiting for a button press
static int archSel, archTop;                                     // the old-maps list: highlighted week and scroll
static int garTab, garSel, garTop;                               // the garage: LOOK or ACHIEVEMENTS, highlighted row, scroll
static bool unlinkAsk;                                           // A once more on the Discord row unlinks
static bool garageNew;                                           // unlocked something since the garage was last opened
static int nUnlocked, unlocked[4];                               // what the last run unlocked, for the game over screen

/* ---------- controls: go, up and down can be put on any button; the touch screen and Circle Pad always steer ---------- */
typedef struct { u32 key; const char *name; } KeyName;
static const KeyName KEYS[] = { { KEY_A, "A" }, { KEY_B, "B" }, { KEY_X, "X" }, { KEY_Y, "Y" }, { KEY_L, "L" }, { KEY_R, "R" },
  { KEY_ZL, "ZL" }, { KEY_ZR, "ZR" }, { KEY_DUP, "D-pad up" }, { KEY_DDOWN, "D-pad down" }, { KEY_DLEFT, "D-pad left" },
  { KEY_DRIGHT, "D-pad right" }, { KEY_CSTICK_UP, "C-Stick up" }, { KEY_CSTICK_DOWN, "C-Stick down" } };
#define NKEYS (int)(sizeof KEYS / sizeof KEYS[0])
enum { BIND_GO, BIND_UP, BIND_DOWN };
static const u32 BIND_DEFAULT[3] = { KEY_L, KEY_DUP, KEY_DDOWN };
static u32 binds[3] = { KEY_L, KEY_DUP, KEY_DDOWN };
static const char *keyName(u32 k) { for (int i = 0; i < NKEYS; i++) if (KEYS[i].key == k) return KEYS[i].name; return "?"; }
static bool validKey(u32 k) { for (int i = 0; i < NKEYS; i++) if (KEYS[i].key == k) return true; return false; }
static double overAt = 0;
static int lastScore, prevBest; static bool newBest; static const char *causeTxt = "";

/* ---------- saves: one line per map best, plus the settings ---------- */
#define SAVE_DIR "sdmc:/3ds/oldmanburn"
#define SAVE_FILE SAVE_DIR "/save.txt"
static Best bests[512]; static int nbests;
static int bestOf(int seed) { for (int i = 0; i < nbests; i++) if (bests[i].seed == seed) return bests[i].best; return 0; }
static void save(void) {
  mkdir("sdmc:/3ds", 0777); mkdir(SAVE_DIR, 0777);
  FILE *f = fopen(SAVE_FILE, "w"); if (!f) return;
  fprintf(f, "music %d\nsfx %d\ntz %d\ngo %d\nup %d\ndown %d\ndpad %d\ntouch %d\n", optMusic, optSfx, tzOff, (int)binds[0], (int)binds[1], (int)binds[2],
    dpadPct, touchPct);
  for (int i = 0; i < nbests; i++) fprintf(f, "best %d %d\n", bests[i].seed, bests[i].best);
  fclose(f);
}
static void load(void) {
  FILE *f = fopen(SAVE_FILE, "r"); if (!f) return;
  char k[16]; int a, b;
  while (fscanf(f, "%15s %d", k, &a) == 2) {
    if (!strcmp(k, "best") && fscanf(f, "%d", &b) == 1) { if (nbests < 512) bests[nbests++] = (Best){ a, b }; }
    else if (!strcmp(k, "music")) optMusic = a; else if (!strcmp(k, "sfx")) optSfx = a; else if (!strcmp(k, "tz")) tzOff = a;
    else if (!strcmp(k, "dpad")) dpadPct = a < 50 ? 50 : a > 200 ? 200 : a; else if (!strcmp(k, "touch")) touchPct = a < 50 ? 50 : a > 200 ? 200 : a;
    else for (int i = 0; i < 3; i++) if (!strcmp(k, (const char *[]){ "go", "up", "down" }[i]) && validKey((u32)a)) binds[i] = (u32)a;
  }
  fclose(f);
}
static bool setBest(int seed, int d) {
  for (int i = 0; i < nbests; i++) if (bests[i].seed == seed) { if (d > bests[i].best) { bests[i].best = d; save(); return true; } return false; }
  if (nbests < 512 && d > 0) { bests[nbests++] = (Best){ seed, d }; save(); return true; }
  return false;
}

/* ---------- which maps are live: the week starts Sunday midnight, the Daily Dash at noon (Pacific, as the server) ---------- */
static void clockMaps(void) {
  long long ms = (long long)osGetTime() - 2208988800000LL - (long long)tzOff * 3600000LL;   // console time is 1900-based
  long long secs = ms / 1000; int day = (int)(secs / 86400), hour = (int)(secs % 86400 / 3600);
  liveWeek = day - ((day + 4) % 7);                              // 1970-01-01 was a Thursday
  liveDay = hour >= 12 ? day : day - 1;
  sync_clock(&liveWeek, &liveDay);                               // linked: the server's maps, whatever the console's clock says
  if (oldWeek <= 0 || oldWeek >= liveWeek) oldWeek = liveWeek - 7;
  if (oldWeek < FIRST_WEEK) oldWeek = FIRST_WEEK;
}
static void enterMode(int m) {
  mode = m;
  curSeed = m == MODE_DAILY ? DAILY_BASE + liveDay : m == MODE_OLD ? oldWeek : liveWeek;
  reseed(curSeed, m == MODE_DAILY || curSeed >= V2_FROM);
  look_apply();
  nameOf(SEED, SEASON, trackName, sizeof trackName);
  st = ST_TITLE; paused = false;
}
static void shortDay(int day, char *out, int n) {
  static const char *MO[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
  int z = day + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, mo = mp < 10 ? mp + 3 : mp - 9;
  snprintf(out, n, "%s %d", MO[mo - 1], d);
}

/* ---------- the old man's lines ---------- */
static const char *CAUSES[4][3] = { { "", "", "" },
  { "Slid off the back. Back in '73 we kept up with the Torino. Stay on the road and mind the throttle.", "Too slow. I once waited so long at the DMV they changed the carpet. Keep on the throttle.", "You nodded off, didn't you. Naps are for the fountain, not the highway." },
  { "Shot off the front. Slow down, you'll spill your thermos.", "Too fast. In 1964 we had a speed limit and a sense of shame.", "Went right past the screen. Wasn't that the Woolworth's?" },
  { "Hit the guardrail. They never had those on Route 9 in '66.", "Guardrail. Solid steel. Like my hip.", "You hit the barricade. Marge would have swerved." } };
void on_crash(void) { audio_crash(); rec_end(g.dist, g.cause); }
void on_over(void) {
  overAt = T; lastScore = g.dist; prevBest = bestOf(curSeed);
  newBest = setBest(curSeed, g.dist);
  perf_ride_end(g.dist);
  int n = ach_run_end(g.dist, g.cause, curSeed, mode == MODE_DAILY, mode == MODE_OLD, newBest, SEASON, bests, nbests);
  nUnlocked = n; for (int i = 0; i < n && i < 4; i++) unlocked[i] = ach_new[i];
  if (n) garageNew = true;
  causeTxt = CAUSES[g.cause][(int)(frand() * 3)];
  rec.score = g.dist; sync_run();
}
static void startRun(void) {
  if (st == ST_OVER) { reseed(curSeed, mode == MODE_DAILY || curSeed >= V2_FROM); }
  newRun(); st = ST_PLAY; paused = false;
  perf_ride_start(trackName, curSeed); ach_run_start();
  rec_start(curSeed, mode == MODE_DAILY ? RM_DAILY : mode == MODE_OLD ? RM_OLD : RM_WEEK);
#ifdef DEVTEST                                                     // test builds: start far down the road, ride through everything
  simGod = true; g.cam = DEVTEST * M_PX; roadAt(g.cam + g.bx); g.y = RC; g.v = g.P = pace(DEVTEST);
#endif
}


/* ---------- drawing the screens, in the web game's style ---------- */
static void fmtNum(int v, char *out) {                          // 12,345
  char tmp[16]; snprintf(tmp, sizeof tmp, "%d", v); int n = strlen(tmp), o = 0;
  for (int i = 0; i < n; i++) { out[o++] = tmp[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ','; }
  out[o] = 0;
}
static void upper(const char *s, char *out, int n) { int i = 0; for (; s[i] && i < n - 1; i++) out[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]; out[i] = 0; }
static int nOldWeeks(void) { return liveWeek - 7 >= FIRST_WEEK ? (liveWeek - 7 - FIRST_WEEK) / 7 + 1 : 0; }
static int archWeek(int i) { return liveWeek - 7 - i * 7; }      // the old-maps list, newest first
static const char *modeLabel(void) { return mode == MODE_DAILY ? "Today's Daily Dash" : mode == MODE_OLD ? "Old map" : "This week's track"; }

static void topGarage(void), topCode(void);
static bool topBoard(void) {                                     // linked: the Discord leaderboard where the blurb goes
  const BoardRow *rows; int pot; bool daily = mode == MODE_DAILY;
  int n = sync_board(daily, daily ? liveDay : liveWeek, &rows, &pot);
  if (n < 0) return false;
  char b[64], sc[24];
  if (daily) snprintf(b, sizeof b, "TODAY'S DAILY DASH BOARD");
  else { fmtNum(pot, sc); snprintf(b, sizeof b, "THIS WEEK'S BOARD · POT %s", sc); }
  ui_kicker(b, 16, 132, UI_PINK);
  if (!n) { ui_wrap(daily ? "Nobody's ridden today's map in Discord yet." : "Nobody's on the board yet. Be the first, sonny.", 16, 142, 9, UI_SUB, 200); return true; }
  for (int i = 0; i < n; i++) {
    const BoardRow *r = &rows[i]; double y = 152 + i * 17;
    if (r->me) { cv_fill(rgba(255, 210, 31, .12)); cv_rect(12, y - 12, 206, 16); }
    snprintf(b, sizeof b, "%d", i + 1); ui_text(b, 18, y, 9, i ? UI_DIM : UI_GOLD, AL_LEFT);
    ui_text(r->name, 32, y, 9, r->me ? UI_GOLD : UI_BONE, AL_LEFT);
    if (r->ds) ui_text("3DS", 36 + cv_text_width(r->name, 9), y - 1, 6, UI_CYAN, AL_LEFT);   // cv_text_width uses ui_text's font
    fmtNum(r->score, sc); snprintf(b, sizeof b, "%s m", sc); ui_text(b, 214, y, 9, UI_GOLD, AL_RIGHT);
  }
  return true;
}
static void topHud(void) {
#ifdef SHOWCASE
  if (st == ST_TITLE) return;
#endif
  char b[128], u[128];
  if (screen == UI_CODE) { topCode(); return; }
  if (st == ST_TITLE && screen == UI_GARAGE) { topGarage(); return; }
  if (st == ST_TITLE) {                                          // the title column of the web game, over the road
    cv_fill(rgba(10, 6, 18, .72)); cv_rect(0, 0, 230, 240);
    double gs[2] = { 0, 1 }; col_t gc[2] = { rgba(10, 6, 18, .72), rgba(10, 6, 18, 0) };
    cv_grad(230, 0, 110, 240, false, 230, 340, 2, gs, gc);
    if (screen == UI_ARCHIVE && nOldWeeks()) { snprintf(b, sizeof b, "Old map, week %d: %s", (archWeek(archSel) - FIRST_WEEK) / 7 + 1, trackName); }
    else snprintf(b, sizeof b, "%s: %s", modeLabel(), trackName);
    upper(b, u, sizeof u); ui_kicker(u, 16, 26, UI_CYAN);
    ui_heading("OLD MAN", 16, 70, 38, AL_LEFT); ui_heading("BURN", 16, 108, 38, AL_LEFT);
    if (screen != UI_ARCHIVE && mode != MODE_OLD && topBoard()) return;
    ui_wrap("Born in 1952 and still riding. The road never stops scrolling, sonny. Ease off the throttle and you slide off "
            "the back. Hold it too long and you shoot off the front. Mind the gout.", 16, 120, 9, UI_BONE, 250);
    return;
  }
  int s = st == ST_PLAY ? scoreOf() : g.dist;                   // the riding HUD: score top right, track best in the middle
  fmtNum(s, b);
  cv_font(FONT_DISP); cv_text(b, 362, 40, 32, H(a87400), AL_RIGHT, BL_ALPHA); cv_text(b, 360, 38, 32, H(ffd21f), AL_RIGHT, BL_ALPHA);
  fmtNum(bestOf(curSeed), b);
  cv_text("TRACK BEST", 200, 10, 7, rgba(255, 236, 220, .55), AL_CENTER, BL_ALPHA);
  cv_text(b, 200, 26, 14, rgba(255, 236, 220, .55), AL_CENTER, BL_ALPHA);
}

static void miniRow(void) {                                      // Settings bottom left, Sound bottom right, as on the web
  ui_button((Box){ 8, 214, 74, 20 }, "Settings", BTN_MINI, 8);
  ui_button((Box){ 238, 214, 74, 20 }, optMusic || optSfx ? "Sound: on" : "Sound: off", BTN_MINI, 8);
}
enum { T_RIDE, T_DAILY, T_OLD, T_GARAGE, T_SETTINGS, T_SOUND };
static void bottomTitle(void) {
  ui_panel();
  char b[24], line[96];
  fmtNum(bestOf(liveWeek), b); snprintf(line, sizeof line, bestOf(liveWeek) ? "Your best this week: %s m" : "No ride on this week's map yet", b);
  ui_text(line, 14, 20, 9, UI_GOLD, AL_LEFT);
  ui_button((Box){ 14, 32, 290, 44 }, "RIDE", BTN_RED, 20);
  ui_button((Box){ 14, 88, 290, 34 }, "DAILY DASH · TODAY'S MAP", BTN_CYAN, 11);
  ui_button((Box){ 14, 134, 139, 30 }, "OLD MAPS", BTN_ALT, 11);
  ui_button((Box){ 165, 134, 139, 30 }, garageNew ? "GARAGE · NEW" : "GARAGE", BTN_ALT, 11);
  miniRow();
  fmtNum(bestOf(DAILY_BASE + liveDay), b);
  snprintf(line, sizeof line, bestOf(DAILY_BASE + liveDay) ? "Daily Dash best today: %s m" : "Daily Dash: a new map every day at noon Pacific", b);
  ui_text(line, 14, 184, 8, UI_CYAN, AL_LEFT);
  snprintf(line, sizeof line, "Hold %s to go · %s / %s or drag to steer", keyName(binds[BIND_GO]), keyName(binds[BIND_UP]), keyName(binds[BIND_DOWN]));
  ui_text(line, 14, 198, 8, UI_SUB, AL_LEFT);
}
static void bottomArchive(void) {
  cv_fill(H(150c26)); cv_rect(0, 0, 320, 240);
  ui_heading("OLD MAPS", 14, 30, 22, AL_LEFT);
  ui_button((Box){ 236, 10, 70, 24 }, "BACK", BTN_RED, 10);       // id 0
  int n = nOldWeeks();
  if (!n) { ui_wrap("No old maps yet. The first weekly map is still this week's.", 14, 50, 9, UI_SUB, 290); return; }
  if (archSel < archTop) archTop = archSel;
  if (archSel > archTop + 4) archTop = archSel - 4;
  for (int r = 0; r < 5 && archTop + r < n; r++) {
    int i = archTop + r, w = archWeek(i), best = bestOf(w);
    Box bx = { 14, 44 + r * 34, 292, 30 };
    int id = ui_hot(bx); bool f = i == archSel; (void)id;
    cv_fill(rgba(255, 255, 255, f ? .08 : .04)); cv_rect(bx.x, bx.y, bx.w, bx.h);
    cv_stroke_rect(bx.x + 0.5, bx.y + 0.5, bx.w - 1, bx.h - 1, f ? UI_CYAN : best ? 0x4a3a70ffu : 0x2a1a44ffu, 1);
    char nm[64], a[16], c[16], line[96];
    int season = seasonOf(w); nameOf(w, season, nm, sizeof nm);
    snprintf(line, sizeof line, "Week %d · %s", (w - FIRST_WEEK) / 7 + 1, nm); ui_text(line, bx.x + 7, bx.y + 12, 9, UI_GOLD, AL_LEFT);
    shortDay(w, a, sizeof a); shortDay(w + 6, c, sizeof c); snprintf(line, sizeof line, "%s to %s", a, c);
    ui_text(line, bx.x + bx.w - 7, bx.y + 12, 8, UI_DIM, AL_RIGHT);
    fmtNum(best, a); snprintf(line, sizeof line, best ? "Your best: %s m" : "Not ridden yet", a);
    ui_text(line, bx.x + 7, bx.y + 24, 8, best ? UI_CYAN : 0xa59cc0ffu, AL_LEFT);
  }
  if (n > 5) { char pg[32]; snprintf(pg, sizeof pg, "%d / %d", archSel + 1, n); ui_text(pg, 306, 228, 8, UI_DIM, AL_RIGHT); }
  ui_text("A ride · B back", 14, 228, 8, UI_DIM, AL_LEFT);
}
static void bottomPlay(int touchY) {                              // touchY: where the stylus is, or -1
  ui_panel();
  char b[24]; fmtNum(st == ST_PLAY ? scoreOf() : g.dist, b);
  ui_text(trackName, 12, 18, 9, UI_BONE, AL_LEFT);
  ui_text("START pause", 308, 18, 8, UI_DIM, AL_RIGHT);
  cv_font(FONT_DISP); cv_text(b, 161, 52, 26, H(a87400), AL_CENTER, BL_ALPHA); cv_text(b, 160, 51, 26, UI_GOLD, AL_CENTER, BL_ALPHA);
  bool held = touchY >= 0;
  cv_fill(held ? rgba(255, 210, 31, .12) : rgba(255, 255, 255, .05)); cv_rect(8, 62, 304, 170);
  cv_stroke_rect(8.5, 62.5, 303, 169, held ? rgba(255, 210, 31, .6) : 0x2a1a44ffu, 1);
  if (held) { cv_fill(UI_GOLD); cv_rect(8, clampd(touchY, 62, 232) - 2, 304, 4); }
  else {
    double up[6] = { 160, 80, 150, 92, 170, 92 }, dn[6] = { 160, 214, 150, 202, 170, 202 };
    cv_fill(0x4a3a70ffu); cv_poly(up, 3); cv_poly(dn, 3);
    ui_text("Drag up and down to steer", 160, 150, 9, UI_SUB, AL_CENTER);
  }
}
enum { O_AGAIN, O_MENU, O_CODE };
static void bottomOver(void) {
  ui_panel();
  char b[24], line[96]; fmtNum(lastScore, b);
  ui_heading("MY HIP!", 14, 40, 26, AL_LEFT);
  cv_font(FONT_DISP); cv_text(b, 306, 40, 28, UI_GOLD, AL_RIGHT, BL_ALPHA);
  ui_wrap(causeTxt, 14, 52, 9, UI_SUB, 292);
  fmtNum(prevBest, b);
  if (mode == MODE_OLD && newBest) snprintf(line, sizeof line, prevBest ? "A new best on this old map." : "Your first score on this old map.");
  else if (mode == MODE_OLD) snprintf(line, sizeof line, "Your best here: %s", b);
  else if (newBest) snprintf(line, sizeof line, "A new best. Best I've seen since '73.");
  else snprintf(line, sizeof line, mode == MODE_DAILY ? "Best today: %s" : "Best this week: %s", b);
  ui_text(line, 14, 124, 9, UI_GOLD, AL_LEFT);
  if (nUnlocked) {
    const Ach *a = &ACHS[unlocked[0]];
    snprintf(line, sizeof line, nUnlocked > 1 ? "Unlocked %s (%s) and %d more" : "Unlocked %s (%s)", item_name(a->rw[0].slot, a->rw[0].item), a->name, nUnlocked - 1);
    ui_text(line, 14, 110, 8, UI_PINK, AL_LEFT);
  }
  if (T - overAt > 0.6) {
    ui_button((Box){ 14, 142, 186, 38 }, "ONE MORE LAP", BTN_RED, 13);
    ui_button((Box){ 212, 142, 94, 38 }, "MENU", BTN_ALT, 12);
    ui_button((Box){ 212, 188, 94, 18 }, "Replay code", BTN_MINI, 8);
    if (sync_linked()) ui_text(sync_status(), 14, 196, 8, UI_CYAN, AL_LEFT);
    snprintf(line, sizeof line, "A or %s ride again · B menu", keyName(binds[BIND_GO]));
    ui_text(line, 14, 228, 8, UI_DIM, AL_LEFT);
  }
}


/* ---------- the garage: the look on the bike, and the achievements that unlock it ---------- */
typedef struct { int stat; const char *name, *unit; } Track;
static const Track TRACKS[] = {                                   // one row per stat, grouped as the web's garage groups them
  { ST_TOTAL_M, "Total distance", " m" }, { ST_BEST_M, "Longest ride", " m" }, { ST_CLEAN_M, "Clean ride", " m" },
  { ST_RUNS, "Rides finished", "" }, { ST_RIDES_500, "Rides of 500 m or more", "" }, { ST_WEEKS_PLAYED, "Weeks ridden", "" }, { ST_NIGHT_RUNS, "Night rides", "" },
  { ST_WHEELIE_S, "Wheelie time", " s" }, { ST_RUN_WHEELIE, "Wheelie in one run", " s" }, { ST_SAND_S, "Time in the dirt", " s" },
  { ST_GAPS, "Gaps passed", "" }, { ST_RUN_GAPS, "Gaps in one run", "" }, { ST_RAILS, "Barricades passed", "" }, { ST_RUN_RAILS, "Barricades in one run", "" },
  { ST_SWERVES, "Swerves", "" }, { ST_CRASH_RAIL, "Wall hits", "" }, { ST_CRASH_BEHIND, "Slid off the back", "" }, { ST_CRASH_AHEAD, "Shot off the front", "" },
  { ST_ENDS_52, "Runs ending in 52", "" }, { ST_WEEKS_5K, "Weekly maps over 5,000", "" }, { ST_PBS, "New bests", "" }, { ST_OLD_3K, "3,000 on an old map", "" },
  { ST_DAILY_RUNS, "Daily Dash runs", "" }, { ST_DAILY_DAYS, "Daily Dash days", "" }, { ST_DAILY_STREAK, "Daily Dash streak", " days" },
  { ST_DAILY_2K_DAYS, "Daily Dash days over 2,000", "" }, { ST_DAILY_1K, "Daily Dash runs over 1,000", "" }, { ST_DAILY_BEST, "Daily Dash best", "" },
  { ST_HALLOWEEN_RUNS, "Halloween rides", "" }, { ST_HALLOWEEN_3K, "3,000 on a Halloween map", "" },
  { ST_CHRISTMAS_RUNS, "Christmas rides", "" }, { ST_CHRISTMAS_3K, "3,000 on a Christmas map", "" } };
#define NTRACK (int)(sizeof TRACKS / sizeof TRACKS[0])
static int trackTiers(int t, int *out) {                          // the track's achievements, easiest first
  int n = 0;
  for (int a = 0; a < NACH; a++) if (ACHS[a].stat == TRACKS[t].stat) {
    int j = n++; while (j > 0 && ACHS[out[j - 1]].goal > ACHS[a].goal) { out[j] = out[j - 1]; j--; } out[j] = a;
  }
  return n;
}
static void statStr(int t, double v, char *out) { char b[24]; fmtNum((int)v, b); snprintf(out, 32, "%s%s", b, TRACKS[t].unit); }
enum { G_BACK, G_LOOK, G_ACH };
static void garageTabs(void) {
  cv_fill(H(150c26)); cv_rect(0, 0, 320, 240);
  ui_heading("GARAGE", 14, 30, 22, AL_LEFT);
  ui_button((Box){ 236, 10, 70, 24 }, "BACK", BTN_RED, 10);
  ui_button((Box){ 124, 12, 50, 20 }, "Look", BTN_MINI, 8);
  ui_button((Box){ 178, 12, 52, 20 }, "Goals", BTN_MINI, 8);
  cv_fill(UI_CYAN); cv_rect(garTab ? 178 : 124, 33, garTab ? 52 : 50, 2);
}
static void bottomGarage(void) {
  garageTabs();
  char line[96], a[32];
  if (garTab == 0) {
    for (int sl = 0; sl < NSLOT; sl++) {
      double y = 42 + sl * 19; Box bx = { 10, y, 300, 18 };
      int id = ui_hot(bx); bool f = sl == garSel; (void)id;
      if (f) { cv_fill(rgba(34, 230, 216, .10)); cv_rect(bx.x, bx.y, bx.w, bx.h); }
      ui_text(SLOT_NAMES[sl], 16, y + 12, 9, f ? UI_BONE : 0xcfc9e0ffu, AL_LEFT);
      bool more = slot_owned(sl) > 0;
      ui_text("‹", 94, y + 12, 10, more ? UI_PINK : UI_DIM, AL_CENTER); ui_text("›", 254, y + 12, 10, more ? UI_PINK : UI_DIM, AL_CENTER);
      ui_text(item_name(sl, look_get(sl)), 174, y + 12, 9, *look_get(sl) ? UI_GOLD : UI_SUB, AL_CENTER);
      snprintf(line, sizeof line, "%d/%d", slot_owned(sl), slot_total(sl)); ui_text(line, 304, y + 12, 8, UI_DIM, AL_RIGHT);
    }
    snprintf(line, sizeof line, "%d of %d achievements · left / right changes", ach_count(), NACH);
    ui_text(line, 14, 228, 8, UI_DIM, AL_LEFT);
    return;
  }
  if (garSel >= NTRACK) garSel = NTRACK - 1;
  if (garSel < garTop) garTop = garSel;
  if (garSel > garTop + 4) garTop = garSel - 4;
  for (int r = 0; r < 5 && garTop + r < NTRACK; r++) {
    int t = garTop + r, tiers[16], n = trackTiers(t, tiers), done = 0; double v = stats[TRACKS[t].stat];
    for (int i = 0; i < n; i++) done += ach_done(tiers[i]);
    Box bx = { 10, 42 + r * 30, 300, 28 }; ui_hot(bx); bool f = t == garSel;
    cv_fill(rgba(255, 255, 255, f ? .08 : .04)); cv_rect(bx.x, bx.y, bx.w, bx.h);
    cv_stroke_rect(bx.x + .5, bx.y + .5, bx.w - 1, bx.h - 1, f ? UI_CYAN : done == n ? 0x6a5a20ffu : 0x2a1a44ffu, 1);
    ui_text(TRACKS[t].name, 16, bx.y + 11, 9, UI_BONE, AL_LEFT);
    snprintf(line, sizeof line, "%d/%d", done, n); ui_text(line, 304, bx.y + 11, 8, done == n ? UI_GOLD : UI_DIM, AL_RIGHT);
    int next = -1; for (int i = 0; i < n; i++) if (!ach_done(tiers[i])) { next = tiers[i]; break; }
    double goal = next >= 0 ? ACHS[next].goal : ACHS[tiers[n - 1]].goal, k = clampd(v / goal, 0, 1);
    cv_fill(0x241838ffu); cv_rect(16, bx.y + 18, 150, 4); cv_fill(next >= 0 ? UI_PINK : UI_GOLD); cv_rect(16, bx.y + 18, 150 * k, 4);
    statStr(t, v, a); char gs[32]; statStr(t, goal, gs);
    snprintf(line, sizeof line, next >= 0 ? "%s / %s" : "%s · all done", a, gs); ui_text(line, 304, bx.y + 23, 8, UI_SUB, AL_RIGHT);
  }
  snprintf(line, sizeof line, "%d of %d achievements · the top screen lists each goal", ach_count(), NACH);
  ui_text(line, 14, 228, 8, UI_DIM, AL_LEFT);
}
static void topGarage(void) {                                      // the bike up close, or the selected goal's tiers
  char line[128];
  cv_fill(rgba(10, 6, 18, .78)); cv_rect(0, 0, LW, LH);
  if (garTab == 0) {
    ui_kicker("THE GARAGE", 16, 26, UI_CYAN);
    ui_heading(SLOT_NAMES[garSel], 16, 62, 26, AL_LEFT);
    ui_text(item_name(garSel, look_get(garSel)), 16, 86, 11, UI_GOLD, AL_LEFT);
    if (*look_get(garSel)) { fmtNum((int)cos_meters(garSel), line); strcat(line, " m ridden in it"); ui_text(line, 16, 104, 9, UI_SUB, AL_LEFT); }
    else ui_text("The Old Man's own.", 16, 104, 9, UI_SUB, AL_LEFT);
    if (slot_owned(garSel) == 0) ui_wrap("Nothing unlocked here yet. The Goals tab says what unlocks what.", 16, 124, 9, UI_DIM, 190);
    cv_fill(rgba(255, 210, 31, .10)); cv_rect(200, 244, 280, 4);
    look_preview(310, 244, 2.3);
    return;
  }
  int tiers[16], n = trackTiers(garSel, tiers);
  ui_kicker("GOALS", 16, 26, UI_CYAN);
  ui_heading(TRACKS[garSel].name, 16, 58, 22, AL_LEFT);
  int first = 0; while (first < n && ach_done(tiers[first])) first++;
  first = first - 2 < 0 ? 0 : first - 2; if (first > n - 7) first = n - 7 < 0 ? 0 : n - 7;   // seven tiers fit: keep the next one in view
  for (int i = first; i < n && i < first + 7; i++) {
    const Ach *a = &ACHS[tiers[i]]; bool d = ach_done(tiers[i]); double y = 82 + (i - first) * 25;
    ui_check(18, y, d);
    snprintf(line, sizeof line, "%s · %s", a->name, item_name(a->rw[0].slot, a->rw[0].item));
    if (a->nrw > 1) { char m[24]; snprintf(m, sizeof m, " +%d more", a->nrw - 1); strcat(line, m); }
    ui_text(line, 34, y + 8, 10, d ? UI_GOLD : UI_BONE, AL_LEFT);
    ui_text(a->desc, 34, y + 19, 8, UI_SUB, AL_LEFT);
  }
  if (n > 7) { char m[32]; snprintf(m, sizeof m, "%d-%d of %d", first + 1, first + 7 < n ? first + 7 : n, n); ui_text(m, 464, 26, 8, UI_DIM, AL_RIGHT); }
}
// Garage, SELECT: shows every item on its own for a moment and writes what drawing it costs to bench.txt, to find the ones
// that make an Old 3DS lag. "frame" is the CPU time of a garage frame with the animation frames already drawn; "build" is
// the one-off cost of drawing the bike sprite (all NPH animation frames for animated items). B stops it.
static void cosmeticBench(void) {
  FILE *f = fopen(SAVE_DIR "/bench.txt", "w"); if (!f) return;
  bool n3ds = false; APT_CheckNew3DS(&n3ds);
  fprintf(f, "Cosmetic bench, %s. ms of CPU per garage frame (avg / max), then the one-off sprite build.\n", n3ds ? "New 3DS" : "Old 3DS");
  char was[NSLOT][16]; for (int s = 0; s < NSLOT; s++) snprintf(was[s], sizeof was[s], "%s", look_get(s));
  int total = 0, done = 0; for (int s = 0; s < NSLOT; s++) total += look_count(s) + 1;
  for (int s = 0; s < NSLOT; s++) for (int i = -1; i < look_count(s); i++) {
    for (int k = 0; k < NSLOT; k++) look_set(k, "");
    const char *id = look_item(s, i); look_set(s, id); done++;
    double sum = 0, mx = 0, build = 0; int n = 0;
    for (int fr = 0; fr < 30; fr++) {
      hidScanInput(); if (hidKeysDown() & KEY_B) goto stop;
      T += 1 / 60.0;
      gfx_frame_begin(); u64 a = svcGetSystemTick();
      look_frame(); if (fr == 0) look_prebuild(); u64 b = svcGetSystemTick();
      render_game(NULL);
      gfx_top_begin();
      gfx_bottom_begin(H(0a0612));
      char line[64]; snprintf(line, sizeof line, "%s: %s", SLOT_NAMES[s], *id ? item_name(s, id) : "original");
      ui_text("COSMETIC BENCH", 160, 90, 12, UI_GOLD, AL_CENTER); ui_text(line, 160, 116, 10, UI_BONE, AL_CENTER);
      snprintf(line, sizeof line, "%d of %d. B stops.", done, total); ui_text(line, 160, 140, 9, UI_SUB, AL_CENTER);
      gfx_frame_end(); u64 c = svcGetSystemTick();
      double ms = (c - a) / (SYSCLOCK_ARM11 / 1000.0);
      if (fr == 0) build = (b - a) / (SYSCLOCK_ARM11 / 1000.0);
      else if (fr >= 6) { sum += ms; n++; if (ms > mx) mx = ms; }
    }
    fprintf(f, "%-8s %-11s frame %5.1f / %5.1f   build %5.1f\n", SLOT_IDS[s], *id ? id : "-", sum / n, mx, build);
  }
  fprintf(f, "done\n");
stop:
  fclose(f);
  for (int s = 0; s < NSLOT; s++) look_set(s, was[s]);
}
static void openGarage(void) { backTo = UI_TITLE; screen = UI_GARAGE; garageNew = false; ui_reset_focus(); }


/* ---------- the replay code: the last run as a long code and a QR code, for Discord's IMPORT when there's no Wi-Fi ---------- */
#include "qrcodegen.h"
static char *codeTxt; static uint8_t *qr; static char codeFile[96];
static void openCode(void) {
  free(codeTxt); codeTxt = rec_code(); codeFile[0] = 0;
  if (!qr) qr = malloc(qrcodegen_BUFFER_LEN_MAX);
  static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
  if (codeTxt && qr && !qrcodegen_encodeText(codeTxt, tmp, qr, qrcodegen_Ecc_LOW, 1, 40, qrcodegen_Mask_AUTO, true)) qr[0] = 0;
  if (codeTxt) {                                                   // and a copy on the SD card, to copy off by hand
    mkdir("sdmc:/3ds/oldmanburn/codes", 0777);
    snprintf(codeFile, sizeof codeFile, "sdmc:/3ds/oldmanburn/codes/%d-%d.txt", rec.seed, rec.score);
    FILE *f = fopen(codeFile, "w"); if (f) { fputs(codeTxt, f); fputc('\n', f); fclose(f); } else codeFile[0] = 0;
  }
  backTo = screen; screen = UI_CODE; ui_reset_focus();
}
static void bottomCode(void) {
  cv_fill(H(150c26)); cv_rect(0, 0, 320, 240);
  int n = codeTxt && qr && qr[0] ? qrcodegen_getSize(qr) : 0;
  if (!n) { ui_wrap("That run is too long for a code. It still uploads when you're linked to Discord.", 14, 40, 9, UI_SUB, 290); }
  else {
    int m = 236 / (n + 4); if (m < 1) m = 1;                       // whole pixels per module, with a quiet zone of two
    int w = (n + 4) * m, x0 = (320 - w) / 2, y0 = (240 - w) / 2;
    cv_fill(0xffffffffu); cv_rect(x0, y0, w, w);
    cv_fill(0x000000ffu);
    for (int y = 0; y < n; y++) for (int x = 0; x < n; ) {          // dark runs along each row, one rect each
      if (!qrcodegen_getModule(qr, x, y)) { x++; continue; }
      int x1 = x; while (x1 < n && qrcodegen_getModule(qr, x1, y)) x1++;
      cv_rect(x0 + (x + 2) * m, y0 + (y + 2) * m, (x1 - x) * m, m); x = x1;
    }
  }
  ui_button((Box){ 4, 4, 50, 18 }, "Back", BTN_MINI, 8);
}
static void topCode(void) {
  cv_fill(rgba(10, 6, 18, .85)); cv_rect(0, 0, LW, LH);
  char line[160];
  ui_kicker("REPLAY CODE", 16, 26, UI_CYAN);
  ui_heading("SEND IT TO DISCORD", 16, 60, 24, AL_LEFT);
  ui_wrap("Scan the code on the bottom screen with your phone's camera and copy the text. In Discord, open 3DS RIDES, "
          "press IMPORT and paste it in. The replay shows up with your other rides.", 16, 74, 10, UI_BONE, 380);
  if (codeTxt) { snprintf(line, sizeof line, "%d characters. Also saved as %s", (int)strlen(codeTxt), codeFile[0] ? codeFile + 5 : "(couldn't save)"); ui_wrap(line, 16, 160, 9, UI_SUB, 380); }
  if (sync_linked()) ui_wrap("Linked to Discord: rides upload by themselves, so you only need this without Wi-Fi.", 16, 200, 9, UI_CYAN, 380);
  ui_text("B back", 16, 258, 9, UI_DIM, AL_LEFT);
}

/* ---------- settings: a panel like the web's, with sliders for steering ---------- */
enum { S_MUSIC, S_SFX, S_DPAD, S_TOUCH, S_CLOCK, S_GO, S_UP, S_DOWN, S_RESET, S_DISCORD, S_CLOSE, NS };
static int setHot[NS];
static const Box SL_DPAD = { 168, 0, 104, 14 }, SL_TOUCH = { 168, 0, 104, 14 };
static double rowY(int i) { return 26 + i * 16.5 + (i >= S_CLOCK ? 3 : 0) + (i >= S_GO ? 3 : 0) + (i >= S_DISCORD ? 3 : 0); }
static void bottomSettings(void) {
  cv_fill(rgba(10, 6, 18, .97)); cv_rect(0, 0, 320, 240);
  cv_stroke_rect(4.5, 4.5, 311, 231, UI_PINK, 1);
  ui_kicker("SETTINGS", 14, 18, UI_PINK);
  char v[48];
  for (int i = 0; i < S_CLOSE; i++) {
    double y = rowY(i);
    setHot[i] = ui_hot((Box){ 10, y - 1, 300, 17 });
    bool f = ui_is_focus(setHot[i]);
    if (f) { cv_fill(rgba(34, 230, 216, .08)); cv_rect(10, y - 1, 300, 17); }
    unsigned col = f ? UI_BONE : 0xcfc9e0ffu;
    double ty = y + 11;
    switch (i) {
      case S_MUSIC: ui_check(16, y + 3, optMusic); ui_text("Music", 34, ty, 9, col, AL_LEFT); break;
      case S_SFX: ui_check(16, y + 3, optSfx); ui_text("Sound effects (engine and crashes)", 34, ty, 9, col, AL_LEFT); break;
      case S_DPAD: case S_TOUCH: {
        int pct = i == S_DPAD ? dpadPct : touchPct;
        ui_text(i == S_DPAD ? "Steering speed" : "Drag sensitivity", 16, ty, 9, col, AL_LEFT);
        Box sb = i == S_DPAD ? SL_DPAD : SL_TOUCH; sb.y = y + 1;
        ui_slider(sb, (pct - 50) / 150.0, f);
        snprintf(v, sizeof v, "%d.%dx", pct / 100, pct / 10 % 10); ui_text(v, 306, ty, 9, UI_GOLD, AL_RIGHT);
        break; }
      case S_CLOCK:
        ui_text("Clock vs Pacific time", 16, ty, 9, col, AL_LEFT);
        snprintf(v, sizeof v, "‹ %s%d h ›", tzOff >= 0 ? "+" : "", tzOff); ui_text(v, 306, ty, 9, UI_GOLD, AL_RIGHT); break;
      case S_GO: case S_UP: case S_DOWN: {
        const char *nm[3] = { "Go", "Steer up", "Steer down" };
        ui_text(nm[i - S_GO], 16, ty, 9, col, AL_LEFT);
        ui_text(capturing == i - S_GO ? "press a button..." : keyName(binds[i - S_GO]), 306, ty, 9, capturing == i - S_GO ? UI_PINK : UI_CYAN, AL_RIGHT);
        break; }
      case S_RESET: ui_text("Reset controls", 16, ty, 9, col, AL_LEFT); ui_text("L · D-pad", 306, ty, 9, UI_DIM, AL_RIGHT); break;
      case S_DISCORD:
        ui_text("Discord", 16, ty, 9, col, AL_LEFT);
        if (sync_linked()) { snprintf(v, sizeof v, "Linked: %.20s", sync_name()); ui_text(v, 306, ty, 9, UI_CYAN, AL_RIGHT); }
        else ui_text("Link to Discord", 306, ty, 9, UI_PINK, AL_RIGHT);
        break;
    }
  }
  setHot[S_CLOSE] = ui_button((Box){ 14, 210, 58, 20 }, "Close", BTN_MINI, 8);
  int fi = -1; for (int i = 0; i < NS; i++) if (ui_is_focus(setHot[i])) fi = i;
  if (fi != S_DISCORD) unlinkAsk = false;
  const char *help = fi == S_DPAD ? "How fast the bike moves while you hold up or down."
    : fi == S_TOUCH ? "How far the bike moves for each bit you drag."
    : fi == S_CLOCK ? (sync_linked() ? "Linked to Discord: the maps follow the server's clock." : "Hours your clock is ahead of Pacific, so maps change when Discord's do.")
    : fi == S_DISCORD ? (sync_linked() ? (unlinkAsk ? "Press A again to unlink this 3DS from Discord." : sync_status()) : "In Discord: Settings, LINK A 3DS. Then A here and type its code.")
    : fi >= S_GO && fi <= S_DOWN ? (capturing >= 0 ? "Press the button to use. START cancels." : "A, then press a button. Dragging and the Circle Pad always steer.")
    : "Left / right or drag to change · B closes";
  ui_wrap(help, 80, 210, 8, UI_SUB, 228);
}

/* ---------- what the buttons do ---------- */
static void openSettings(void) { backTo = screen; screen = UI_SETTINGS; capturing = -1; ui_reset_focus(); }
static void toggleSound(void) { bool on = !(optMusic || optSfx); optMusic = optSfx = on; audio_set(optMusic, optSfx); save(); }
static void ridePicked(int m) { enterMode(m); startRun(); }
static void settingsAdjust(int row, int d) {
  if (row == S_MUSIC) optMusic = !optMusic;
  else if (row == S_SFX) optSfx = !optSfx;
  else if (row == S_DPAD) dpadPct = (int)clampd(dpadPct + d * 10, 50, 200);
  else if (row == S_TOUCH) touchPct = (int)clampd(touchPct + d * 10, 50, 200);
  else if (row == S_CLOCK) { tzOff += d; if (tzOff > 24) tzOff = -24; if (tzOff < -24) tzOff = 24; int m = mode; clockMaps(); enterMode(m); }
  audio_set(optMusic, optSfx);
}

int main(void) {
  osSetSpeedupEnable(true);                                      // New 3DS: the faster clock and the extra cache
  gfx_init();
  srand((unsigned)osGetTime());
  load();
  ach_load(); ach_bests(bests, nbests);
  sync_init();
  draw_init();
  gfx_build(draw_sprites);
  audio_init(); audio_set(optMusic, optSfx);
  perf_init(); int audioBufs = 0;
  clockMaps(); enterMode(MODE_WEEK);
  touchPosition lastTouch = { 0 }; bool touching = false;
  const double dt = 1.0 / 60;
  while (aptMainLoop()) {
    perf_frame((st == ST_PLAY || st == ST_CRASH) && !paused, audioBufs);
    hidScanInput();
    u32 kd = hidKeysDown(), kh = hidKeysHeld();
    touchPosition tp; hidTouchRead(&tp);
    Input in = { 0 }; int touchY = -1;
    if (!(kh & KEY_TOUCH)) touching = false;
    bool menus = st == ST_TITLE || screen == UI_SETTINGS || screen == UI_CODE;
    if (screen == UI_CODE) {
      if (kd & (KEY_B | KEY_START)) { screen = backTo; ui_reset_focus(); }
      kd &= ~(KEY_A | KEY_B | KEY_START | KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT);
    } else if (screen == UI_SETTINGS && capturing >= 0) {               // waiting for the button to put a control on
      if (kd & (KEY_START | KEY_SELECT)) capturing = -1;
      else for (int i = 0; i < NKEYS; i++) if (kd & KEYS[i].key) { binds[capturing] = KEYS[i].key; capturing = -1; save(); break; }
      kd = 0;
    } else if (screen == UI_SETTINGS) {
      if (kd & (KEY_SELECT | KEY_B)) { screen = backTo; save(); ui_reset_focus(); kd = 0; }
      int fi = -1; for (int i = 0; i < NS; i++) if (ui_is_focus(setHot[i])) fi = i;
      int d = (kd & KEY_RIGHT) ? 1 : (kd & KEY_LEFT) ? -1 : 0;
      if (d && fi >= S_DPAD && fi <= S_CLOCK) { settingsAdjust(fi, d); kd &= ~(KEY_LEFT | KEY_RIGHT); }
      kd &= ~(KEY_LEFT | KEY_RIGHT);
      if ((kh & KEY_TOUCH) && tp.px >= SL_DPAD.x - 6 && tp.px <= SL_DPAD.x + SL_DPAD.w + 6) {   // dragging a slider
        for (int r = S_DPAD; r <= S_TOUCH; r++) if (tp.py >= rowY(r) - 2 && tp.py < rowY(r) + 17) {
          int pct = (int)(50 + clampd((tp.px - SL_DPAD.x) / SL_DPAD.w, 0, 1) * 150 + 5) / 10 * 10;
          if (r == S_DPAD) dpadPct = pct; else touchPct = pct;
          ui_set_focus(setHot[r]);
        }
      }
    } else if (st == ST_TITLE && screen == UI_GARAGE) {
      if (kd & KEY_B) { screen = UI_TITLE; ui_reset_focus(); kd = 0; }
      if (kd & (KEY_L | KEY_R)) { garTab ^= 1; garSel = garTop = 0; }
      if (kd & KEY_SELECT) { cosmeticBench(); kd = 0; }
      int n = garTab ? NTRACK : NSLOT;
      if (kd & (KEY_DOWN | KEY_UP)) { garSel += (kd & KEY_DOWN) ? 1 : -1; if (garSel < 0) garSel = 0; if (garSel >= n) garSel = n - 1; }
      if (garTab == 0 && (kd & (KEY_LEFT | KEY_RIGHT))) ach_wear(garSel, (kd & KEY_RIGHT) ? 1 : -1);
      kd &= ~(KEY_DOWN | KEY_UP | KEY_LEFT | KEY_RIGHT | KEY_A);  // the rows move by themselves; buttons are tapped
    } else if (st == ST_TITLE && screen == UI_ARCHIVE) {
      int n = nOldWeeks();
      if (kd & KEY_B) { screen = UI_TITLE; enterMode(MODE_WEEK); ui_reset_focus(); kd = 0; }
      if (kd & KEY_SELECT) openSettings();
      if (n && (kd & (KEY_DOWN | KEY_UP | KEY_LEFT | KEY_RIGHT))) {   // the list moves by itself; BACK is a tap or B
        int ns = archSel + ((kd & (KEY_DOWN | KEY_RIGHT)) ? 1 : -1); if (ns < 0) ns = 0; if (ns >= n) ns = n - 1;
        if (ns != archSel) { archSel = ns; oldWeek = archWeek(archSel); enterMode(MODE_OLD); }
        kd &= ~(KEY_DOWN | KEY_UP | KEY_LEFT | KEY_RIGHT);
      }
      if (kd & KEY_A) { if (n) ridePicked(MODE_OLD); kd = 0; }
    } else if (st == ST_TITLE) {
      if (kd & KEY_SELECT) openSettings();
      if (kd & KEY_R) ridePicked(MODE_DAILY);
    } else if (st == ST_PLAY) {
      if (kd & KEY_START) paused = !paused;
      if (paused && (kd & KEY_SELECT)) { perf_ride_end(scoreOf()); enterMode(mode); screen = UI_TITLE; ui_reset_focus(); }
      else if (!paused) {
        if (kd & KEY_SELECT) { paused = true; }
        in.thr = (kh & binds[BIND_GO]) != 0;
        double steer = ((kh & binds[BIND_UP]) ? -1 : 0) + ((kh & binds[BIND_DOWN]) ? 1 : 0);
        circlePosition cp; hidCircleRead(&cp);
        if (!steer && abs(cp.dy) > 18) steer = clampd(-cp.dy / 140.0, -1, 1);
        in.steer = steer * dpadPct / 100.0;
#ifdef DEVTEST
        roadAt(g.cam + g.bx); in.steer = clampd((RC - g.y) / 10, -1, 1);   // autopilot: keep to the middle of the road
#endif
        if (kh & KEY_TOUCH) {                                      // drag the stylus up and down: the bike follows it
          if (touching) in.drag = (tp.py - lastTouch.py) * (LH / 240.0) * 1.25 * touchPct / 100.0;
          touching = true; lastTouch = tp; touchY = tp.py;
        }
      }
    } else if (st == ST_OVER) {
      if (T - overAt > 0.6) {
        if (kd & binds[BIND_GO] && !(binds[BIND_GO] & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_A))) startRun();
        else if (kd & KEY_B) { enterMode(mode == MODE_OLD ? MODE_WEEK : mode); screen = UI_TITLE; ui_reset_focus(); }
        menus = true;
      }
    }
    sync_tick(menus || st == ST_OVER);
    { int w, d; if (st == ST_TITLE && sync_clock(&w, &d) && (w != liveWeek || d != liveDay)) { clockMaps(); enterMode(mode == MODE_OLD ? MODE_WEEK : mode); } }
    bool riding = st == ST_PLAY && !paused;
    update(dt, &in);
    if (riding && st == ST_PLAY) { ach_run_tick(dt); look_trail(dt); rec_tick(); }
    perf_mark(PF_SIM);
    audioBufs = audio_tick(); perf_mark(PF_AUDIO);
#ifdef SHOWCASE                                                    // test builds: hold the title scene at the garage for the banner
    if (st == ST_TITLE) { if (SEASON) { SEASON = 0; look_apply(); } g.cam = SHOWCASE; }
#endif

    gfx_frame_begin(); perf_mark(PF_WAIT);
    look_frame();
    char hint[48]; snprintf(hint, sizeof hint, "HOLD %s, SONNY. KEEP UP.", keyName(binds[BIND_GO]));
    for (char *c = hint; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    draw_best_m = bestOf(curSeed);
    render_game(st == ST_PLAY && g.t < 5 ? hint : NULL);
    gfx_top_begin();
    topHud();
    gfx_bottom_begin(H(0a0612));
    ui_begin();
    int shown = screen == UI_SETTINGS || screen == UI_CODE ? screen : st == ST_TITLE ? screen : -1;
    if (shown == UI_SETTINGS) bottomSettings();
    else if (shown == UI_ARCHIVE) bottomArchive();
    else if (shown == UI_GARAGE) bottomGarage();
    else if (shown == UI_CODE) bottomCode();
    else if (shown == UI_TITLE) bottomTitle();
    else if (st == ST_OVER) bottomOver();
    else bottomPlay(touchY);
    if (menus) ui_input(kd, tp.px, tp.py, (kd & KEY_TOUCH) != 0);
    if (kh & KEY_Y && !menus) {                                  // hold Y while riding: how busy the CPU and GPU are
      char pf[64]; snprintf(pf, sizeof pf, "CPU %.0f%%  GPU %.0f%%", C3D_GetProcessingTime() * 6.0f, C3D_GetDrawingTime() * 6.0f);
      cv_font(FONT_MONO); cv_fill(rgba(0, 0, 0, .7)); cv_rect(0, 0, 150, 16); cv_text(pf, 4, 12, 9, H(7dff9c), AL_LEFT, BL_ALPHA);
    }
    gfx_frame_end(); perf_mark(PF_DRAW);

    int p = menus ? ui_pressed() : -1;                           // the buttons act after the frame they were drawn in
    if (p >= 0) {
      if (shown == UI_SETTINGS) {
        int row = -1; for (int i = 0; i < NS; i++) if (setHot[i] == p) row = i;
        bool tap = (kd & KEY_TOUCH) != 0;
        if (row == S_CLOSE) { screen = backTo; save(); ui_reset_focus(); }
        else if (row == S_MUSIC || row == S_SFX) settingsAdjust(row, 1);
        else if (row == S_CLOCK && tap) settingsAdjust(row, tp.px >= 260 ? 1 : -1);
        else if (row == S_CLOCK) settingsAdjust(row, 1);
        else if (row >= S_GO && row <= S_DOWN) capturing = row - S_GO;
        else if (row == S_RESET) { memcpy(binds, BIND_DEFAULT, sizeof binds); save(); }
        else if (row == S_DISCORD) { if (!sync_linked()) sync_link_prompt(); else if (unlinkAsk) { sync_unlink(); unlinkAsk = false; } else unlinkAsk = true; }
      } else if (shown == UI_TITLE) {
        if (p == T_RIDE) ridePicked(MODE_WEEK);
        else if (p == T_DAILY) ridePicked(MODE_DAILY);
        else if (p == T_OLD) { screen = UI_ARCHIVE; archSel = 0; archTop = 0; ui_reset_focus(); if (nOldWeeks()) { oldWeek = archWeek(0); enterMode(MODE_OLD); } }
        else if (p == T_GARAGE) openGarage();
        else if (p == T_SETTINGS) openSettings();
        else if (p == T_SOUND) toggleSound();
      } else if (shown == UI_CODE) {
        if (p == 0) { screen = backTo; ui_reset_focus(); }
      } else if (shown == UI_GARAGE) {
        if (p == G_BACK) { screen = UI_TITLE; ui_reset_focus(); }
        else if (p == G_LOOK || p == G_ACH) { garTab = p == G_ACH; garSel = garTop = 0; }
        else if (garTab == 0) {                                  // a slot row: the left half goes back, the right half on
          int sl = p - 3; garSel = sl;
          if ((kd & KEY_TOUCH) && tp.px < 140) ach_wear(sl, -1); else if (kd & KEY_TOUCH) ach_wear(sl, 1);
        } else garSel = garTop + (p - 3);
      } else if (shown == UI_ARCHIVE) {
        if (p == 0) { screen = UI_TITLE; enterMode(MODE_WEEK); ui_reset_focus(); }
        else { int i = archTop + (p - 1); if (i < nOldWeeks()) { archSel = i; oldWeek = archWeek(i); ridePicked(MODE_OLD); } }
      } else if (st == ST_OVER) {
        if (p == O_CODE) openCode();
        else if (p == O_AGAIN) startRun(); else if (p == O_MENU) { enterMode(mode == MODE_OLD ? MODE_WEEK : mode); screen = UI_TITLE; ui_reset_focus(); }
      }
    }
  }
  save();
  sync_exit();
  audio_exit();
  gfx_exit();
  return 0;
}

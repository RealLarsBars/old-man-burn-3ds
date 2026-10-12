// Old Man Burn for the Nintendo 3DS: the weekly map, the Daily Dash and the old maps, offline.
// No coins, pot or leaderboards: your best on each map is kept on the SD card.
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
#include <citro3d.h>

#define H(x) RGB(0x##x)
enum { MODE_WEEK, MODE_DAILY, MODE_OLD };
static int mode = MODE_WEEK, liveWeek, liveDay, oldWeek, curSeed;
static char trackName[64];
static bool optMusic = true, optSfx = true; static int tzOff = 0;   // hours the console clock is ahead of Pacific time
static int dpadPct = 100, touchPct = 100;                       // steering sensitivity, percent (Settings sliders)
enum { UI_TITLE, UI_ARCHIVE, UI_SETTINGS };
static int screen = UI_TITLE, backTo = UI_TITLE;                // which menu the bottom screen shows (rides use st)
static int capturing = -1;                                       // the control (BIND_*) waiting for a button press
static int archSel, archTop;                                     // the old-maps list: highlighted week and scroll

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
typedef struct { int seed, best; } Best;
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
void on_crash(void) { audio_crash(); }
void on_over(void) {
  overAt = T; lastScore = g.dist; prevBest = bestOf(curSeed);
  newBest = setBest(curSeed, g.dist);
  perf_ride_end(g.dist);
  causeTxt = CAUSES[g.cause][(int)(frand() * 3)];
}
static void startRun(void) {
  if (st == ST_OVER) { reseed(curSeed, mode == MODE_DAILY || curSeed >= V2_FROM); }
  newRun(); st = ST_PLAY; paused = false;
  perf_ride_start(trackName, curSeed);
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

static void topHud(void) {
#ifdef SHOWCASE
  if (st == ST_TITLE) return;
#endif
  char b[128], u[128];
  if (st == ST_TITLE) {                                          // the title column of the web game, over the road
    cv_fill(rgba(10, 6, 18, .72)); cv_rect(0, 0, 230, 240);
    double gs[2] = { 0, 1 }; col_t gc[2] = { rgba(10, 6, 18, .72), rgba(10, 6, 18, 0) };
    cv_grad(230, 0, 110, 240, false, 230, 340, 2, gs, gc);
    if (screen == UI_ARCHIVE && nOldWeeks()) { snprintf(b, sizeof b, "Old map, week %d: %s", (archWeek(archSel) - FIRST_WEEK) / 7 + 1, trackName); }
    else snprintf(b, sizeof b, "%s: %s", modeLabel(), trackName);
    upper(b, u, sizeof u); ui_kicker(u, 16, 26, UI_CYAN);
    ui_heading("OLD MAN", 16, 70, 38, AL_LEFT); ui_heading("BURN", 16, 108, 38, AL_LEFT);
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
enum { T_RIDE, T_DAILY, T_OLD, T_SETTINGS, T_SOUND };
static void bottomTitle(void) {
  ui_panel();
  char b[24], line[96];
  fmtNum(bestOf(liveWeek), b); snprintf(line, sizeof line, bestOf(liveWeek) ? "Your best this week: %s m" : "No ride on this week's map yet", b);
  ui_text(line, 14, 20, 9, UI_GOLD, AL_LEFT);
  ui_button((Box){ 14, 32, 290, 44 }, "RIDE", BTN_RED, 20);
  ui_button((Box){ 14, 88, 290, 34 }, "DAILY DASH · TODAY'S MAP", BTN_CYAN, 11);
  ui_button((Box){ 14, 134, 290, 30 }, "OLD MAPS", BTN_ALT, 11);
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
enum { O_AGAIN, O_MENU };
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
  if (T - overAt > 0.6) {
    ui_button((Box){ 14, 142, 186, 38 }, "ONE MORE LAP", BTN_RED, 13);
    ui_button((Box){ 212, 142, 94, 38 }, "MENU", BTN_ALT, 12);
    snprintf(line, sizeof line, "A or %s ride again · B menu", keyName(binds[BIND_GO]));
    ui_text(line, 14, 204, 8, UI_DIM, AL_LEFT);
  }
}

/* ---------- settings: a panel like the web's, with sliders for steering ---------- */
enum { S_MUSIC, S_SFX, S_DPAD, S_TOUCH, S_CLOCK, S_GO, S_UP, S_DOWN, S_RESET, S_CLOSE, NS };
static int setHot[NS];
static const Box SL_DPAD = { 168, 0, 104, 14 }, SL_TOUCH = { 168, 0, 104, 14 };
static double rowY(int i) { return 28 + i * 18 + (i >= S_CLOCK ? 4 : 0) + (i >= S_GO ? 4 : 0); }
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
    }
  }
  setHot[S_CLOSE] = ui_button((Box){ 14, 210, 58, 20 }, "Close", BTN_MINI, 8);
  int fi = -1; for (int i = 0; i < NS; i++) if (ui_is_focus(setHot[i])) fi = i;
  const char *help = fi == S_DPAD ? "How fast the bike moves while you hold up or down."
    : fi == S_TOUCH ? "How far the bike moves for each bit you drag."
    : fi == S_CLOCK ? "Hours your clock is ahead of Pacific, so maps change when Discord's do."
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
    bool menus = st == ST_TITLE || screen == UI_SETTINGS;
    if (screen == UI_SETTINGS && capturing >= 0) {               // waiting for the button to put a control on
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
    update(dt, &in); perf_mark(PF_SIM);
    audioBufs = audio_tick(); perf_mark(PF_AUDIO);
#ifdef SHOWCASE                                                    // test builds: hold the title scene at the garage for the banner
    if (st == ST_TITLE) { if (SEASON) { SEASON = 0; look_apply(); } g.cam = SHOWCASE; }
#endif

    gfx_frame_begin(); perf_mark(PF_WAIT);
    char hint[48]; snprintf(hint, sizeof hint, "HOLD %s, SONNY. KEEP UP.", keyName(binds[BIND_GO]));
    for (char *c = hint; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    render_game(st == ST_PLAY && g.t < 5 ? hint : NULL);
    gfx_top_begin();
    topHud();
    gfx_bottom_begin(H(0a0612));
    ui_begin();
    int shown = screen == UI_SETTINGS ? UI_SETTINGS : st == ST_TITLE ? screen : -1;
    if (shown == UI_SETTINGS) bottomSettings();
    else if (shown == UI_ARCHIVE) bottomArchive();
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
      } else if (shown == UI_TITLE) {
        if (p == T_RIDE) ridePicked(MODE_WEEK);
        else if (p == T_DAILY) ridePicked(MODE_DAILY);
        else if (p == T_OLD) { screen = UI_ARCHIVE; archSel = 0; archTop = 0; ui_reset_focus(); if (nOldWeeks()) { oldWeek = archWeek(0); enterMode(MODE_OLD); } }
        else if (p == T_SETTINGS) openSettings();
        else if (p == T_SOUND) toggleSound();
      } else if (shown == UI_ARCHIVE) {
        if (p == 0) { screen = UI_TITLE; enterMode(MODE_WEEK); ui_reset_focus(); }
        else { int i = archTop + (p - 1); if (i < nOldWeeks()) { archSel = i; oldWeek = archWeek(i); ridePicked(MODE_OLD); } }
      } else if (st == ST_OVER) {
        if (p == O_AGAIN) startRun(); else if (p == O_MENU) { enterMode(mode == MODE_OLD ? MODE_WEEK : mode); screen = UI_TITLE; ui_reset_focus(); }
      }
    }
  }
  save();
  audio_exit();
  gfx_exit();
  return 0;
}

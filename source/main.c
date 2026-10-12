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
#include <citro3d.h>

#define H(x) RGB(0x##x)
enum { MODE_WEEK, MODE_DAILY, MODE_OLD };
static int mode = MODE_WEEK, liveWeek, liveDay, oldWeek, curSeed;
static char trackName[64];
static bool optMusic = true, optSfx = true; static int tzOff = 0;   // hours the console clock is ahead of Pacific time
static bool settingsOpen = false;
static int settingsSel = 0, capturing = -1;                     // capturing: the control row waiting for a button press
#define NSET 7

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
  fprintf(f, "music %d\nsfx %d\ntz %d\ngo %d\nup %d\ndown %d\n", optMusic, optSfx, tzOff, (int)binds[0], (int)binds[1], (int)binds[2]);
  for (int i = 0; i < nbests; i++) fprintf(f, "best %d %d\n", bests[i].seed, bests[i].best);
  fclose(f);
}
static void load(void) {
  FILE *f = fopen(SAVE_FILE, "r"); if (!f) return;
  char k[16]; int a, b;
  while (fscanf(f, "%15s %d", k, &a) == 2) {
    if (!strcmp(k, "best") && fscanf(f, "%d", &b) == 1) { if (nbests < 512) bests[nbests++] = (Best){ a, b }; }
    else if (!strcmp(k, "music")) optMusic = a; else if (!strcmp(k, "sfx")) optSfx = a; else if (!strcmp(k, "tz")) tzOff = a;
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
  causeTxt = CAUSES[g.cause][(int)(frand() * 3)];
}
static void startRun(void) {
  if (st == ST_OVER) { reseed(curSeed, mode == MODE_DAILY || curSeed >= V2_FROM); }
  newRun(); st = ST_PLAY; paused = false;
#ifdef DEVTEST                                                     // test builds: start far down the road, ride through everything
  simGod = true; g.cam = DEVTEST * M_PX; roadAt(g.cam + g.bx); g.y = RC; g.v = g.P = pace(DEVTEST);
#endif
}

/* ---------- drawing the screens ---------- */
static void fmtNum(int v, char *out) {                          // 12,345
  char tmp[16]; snprintf(tmp, sizeof tmp, "%d", v); int n = strlen(tmp), o = 0;
  for (int i = 0; i < n; i++) { out[o++] = tmp[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ','; }
  out[o] = 0;
}
static void topHud(void) {
#ifdef SHOWCASE
  if (st == ST_TITLE) return;
#endif
  if (st == ST_TITLE) {
    cv_save(); cv_translate(14, 0);
    cv_text("OLD MAN", 2, 44, 34, H(ffd21f), AL_LEFT, BL_ALPHA); cv_text("OLD MAN", 0, 42, 34, H(ff2d2d), AL_LEFT, BL_ALPHA);
    cv_text("BURN", 2, 80, 34, H(ffd21f), AL_LEFT, BL_ALPHA); cv_text("BURN", 0, 78, 34, H(ff2d2d), AL_LEFT, BL_ALPHA);
    cv_restore();
    return;
  }
  char b[24]; int s = st == ST_PLAY ? scoreOf() : g.dist;
  fmtNum(s, b);
  cv_text(b, 362, 36, 30, H(a87400), AL_RIGHT, BL_ALPHA); cv_text(b, 360, 34, 30, H(ffd21f), AL_RIGHT, BL_ALPHA);
  const char *v[4] = { "ジ", "イ", "サ", "ン" }; for (int i = 0; i < 4; i++) cv_text(v[i], 380, 12 + i * 9, 8, H(c2243a), AL_CENTER, BL_ALPHA);
  int best = bestOf(curSeed); fmtNum(best, b);
  cv_text("TRACK BEST", 200, 10, 6, rgba(255, 236, 220, .55), AL_CENTER, BL_ALPHA);
  cv_text(b, 200, 26, 14, rgba(255, 236, 220, .55), AL_CENTER, BL_ALPHA);
}
static void panelBg(void) {
  double s[3] = { 0, 0.5, 1 }; col_t c[3] = { rgba(255, 45, 45, .22), rgba(255, 45, 45, .08), rgba(255, 45, 45, 0) };
  cv_grad(0, 0, 320, 240, true, 0, 240, 3, s, c);
}
static void button(double x, double y, double w, double h, const char *label, col_t bg, bool on) {
  cv_fill(H(ffd21f)); cv_rect(x + 3, y + 3, w, h);
  cv_fill(on ? bg : H(3b2a5c)); cv_rect(x, y, w, h);
  cv_text(label, x + w / 2, y + h / 2, 13, on ? H(0a0612) : H(b8a9d6), AL_CENTER, BL_MIDDLE);
}
typedef struct { int x, y, w, h; } Box;
static const Box TABS[3] = { { 8, 30, 96, 22 }, { 112, 30, 96, 22 }, { 216, 30, 96, 22 } };
static const Box RIDE = { 96, 150, 128, 34 };
static bool hit(const Box *b, int px, int py) { return px >= b->x && px < b->x + b->w && py >= b->y && py < b->y + b->h; }
static const char *rulesOf(void) {
  if (mode == MODE_DAILY) return "Gauntlets and rests rules";
  return curSeed < V2_FROM ? "Original rules: bigger bike, no swerves" : curSeed < SW4_FROM ? "Swerves rules" : "Gauntlets and rests rules";
}
static void bottomTitle(void) {
  panelBg();
  const char *tl[3] = { "THIS WEEK", "DAILY DASH", "OLD MAPS" };
  cv_text("L / R  pick a map", 160, 20, 9, H(7d6d9c), AL_CENTER, BL_ALPHA);
  for (int i = 0; i < 3; i++) button(TABS[i].x, TABS[i].y, TABS[i].w, TABS[i].h, tl[i], i == MODE_DAILY ? H(22e6d8) : H(ff2d2d), mode == i);
  char line[96], a[16], b2[16];
  if (mode == MODE_OLD) {
    shortDay(oldWeek, a, sizeof a); shortDay(oldWeek + 6, b2, sizeof b2);
    snprintf(line, sizeof line, "<  WEEK %d · %s to %s  >", (oldWeek - FIRST_WEEK) / 7 + 1, a, b2);
  } else snprintf(line, sizeof line, mode == MODE_DAILY ? "DAILY DASH · TODAY'S TRACK" : "THIS WEEK'S TRACK");
  cv_text(line, 160, 74, 10, H(22e6d8), AL_CENTER, BL_ALPHA);
  cv_text(trackName, 160, 98, 20, H(f1ecf7), AL_CENTER, BL_ALPHA);
  cv_text(rulesOf(), 160, 114, 9, H(b8a9d6), AL_CENTER, BL_ALPHA);
  int best = bestOf(curSeed); fmtNum(best, a);
  snprintf(line, sizeof line, best ? "Your best here: %s" : "No score on this map yet", a);
  cv_text(line, 160, 134, 11, H(ffd21f), AL_CENTER, BL_ALPHA);
  button(RIDE.x, RIDE.y, RIDE.w, RIDE.h, "\uE000  RIDE", H(ff2d2d), true);
  snprintf(line, sizeof line, "Hold %s to go · steer with %s / %s or drag", keyName(binds[BIND_GO]), keyName(binds[BIND_UP]), keyName(binds[BIND_DOWN]));
  cv_text(line, 160, 210, cv_text_width(line, 9) > 304 ? 8 : 9, H(b8a9d6), AL_CENTER, BL_ALPHA);
  cv_text("START pause · SELECT settings and controls", 160, 226, 9, H(7d6d9c), AL_CENTER, BL_ALPHA);
}
static void bottomPlay(int touchY) {                              // touchY: where the stylus is, or -1
  panelBg();
  char b[24]; fmtNum(st == ST_PLAY ? scoreOf() : g.dist, b);
  cv_text(trackName, 12, 18, 11, H(f1ecf7), AL_LEFT, BL_ALPHA);
  cv_text("START pause", 308, 18, 9, H(7d6d9c), AL_RIGHT, BL_ALPHA);
  cv_text(b, 160, 52, 30, H(ffd21f), AL_CENTER, BL_ALPHA);
  bool held = touchY >= 0;
  cv_fill(held ? rgba(255, 210, 31, .12) : rgba(255, 255, 255, .06)); cv_rect(8, 62, 304, 170);
  cv_stroke_rect(8, 62, 304, 170, held ? rgba(255, 210, 31, .6) : rgba(255, 255, 255, .15), 2);
  if (held) { cv_fill(H(ffd21f)); cv_rect(8, clampd(touchY, 62, 232) - 2, 304, 4); }
  else {
    double up[6] = { 160, 80, 148, 94, 172, 94 }, dn[6] = { 160, 214, 148, 200, 172, 200 };
    cv_fill(H(7d6d9c)); cv_poly(up, 3); cv_poly(dn, 3);
    cv_text("Drag up and down to steer", 160, 151, 11, H(b8a9d6), AL_CENTER, BL_MIDDLE);
  }
}
static void bottomOver(void) {
  panelBg();
  cv_text("MY HIP!", 162, 50, 34, H(ffd21f), AL_CENTER, BL_ALPHA); cv_text("MY HIP!", 160, 48, 34, H(ff2d2d), AL_CENTER, BL_ALPHA);
  char b[24], line[96]; fmtNum(lastScore, b);
  cv_text(b, 160, 92, 34, H(ffd21f), AL_CENTER, BL_ALPHA);
  cv_text_wrap(causeTxt, 20, 104, 10, H(f1ecf7), 280);
  fmtNum(prevBest, b);
  if (mode == MODE_OLD && newBest) snprintf(line, sizeof line, prevBest ? "A new best on this old map." : "Your first score on this old map.");
  else if (mode == MODE_OLD) snprintf(line, sizeof line, "Your best here: %s", b);
  else if (newBest) snprintf(line, sizeof line, "A new best. Best I've seen since '73.");
  else snprintf(line, sizeof line, mode == MODE_DAILY ? "Best today: %s" : "Best this week: %s", b);
  cv_text(line, 160, 160, 11, H(ffd21f), AL_CENTER, BL_ALPHA);
  if (T - overAt > 0.6) { button(RIDE.x - 50, 180, 128, 30, "\uE000  AGAIN", H(ff2d2d), true); button(RIDE.x + 92, 180, 80, 30, "\uE001  MENU", H(22e6d8), true); }
}
static void bottomSettings(void) {
  cv_fill(H(0a0612)); cv_rect(0, 0, 320, 240);
  cv_text("SETTINGS", 160, 34, 20, H(ff2d2d), AL_CENTER, BL_ALPHA);
  char l[NSET][80];
  snprintf(l[0], 80, "Music: %s", optMusic ? "on" : "off");
  snprintf(l[1], 80, "Sound effects: %s", optSfx ? "on" : "off");
  snprintf(l[2], 80, "Clock vs Pacific time: %s%d h", tzOff >= 0 ? "+" : "", tzOff);
  const char *bl[3] = { "Go", "Steer up", "Steer down" };
  for (int i = 0; i < 3; i++) snprintf(l[3 + i], 80, "%s: %s", bl[i], capturing == 3 + i ? "press a button..." : keyName(binds[i]));
  snprintf(l[6], 80, "Reset controls");
  for (int i = 0; i < NSET; i++) {
    double y = 64 + i * 19 + (i >= 3 ? 8 : 0);
    cv_text(l[i], 40, y, 12, settingsSel == i ? H(ffd21f) : H(f1ecf7), AL_LEFT, BL_ALPHA);
    if (settingsSel == i) cv_text(">", 26, y, 12, H(ffd21f), AL_LEFT, BL_ALPHA);
  }
  const char *help = settingsSel == 2
    ? "The maps change Sunday midnight and noon daily, Pacific time. Set how far your clock is ahead of Pacific."
    : settingsSel >= 3 && settingsSel <= 5 ? (capturing >= 0 ? "Press the button to use. START cancels." : "\uE000 then press the button to use. Dragging on the touch screen and the Circle Pad always steer too.")
    : settingsSel == 6 ? "Go on L, steer on the D-pad." : "";
  cv_text_wrap(help, 20, 200, 9, H(b8a9d6), 280);
  cv_text("\uE000 change · SELECT / \uE001 close", 160, 232, 9, H(7d6d9c), AL_CENTER, BL_ALPHA);
}

int main(void) {
  osSetSpeedupEnable(true);                                      // New 3DS: the faster clock and the extra cache
  gfx_init();
  srand((unsigned)osGetTime());
  load();
  draw_init();
  gfx_build(draw_sprites);
  audio_init(); audio_set(optMusic, optSfx);
  clockMaps(); enterMode(MODE_WEEK);
  touchPosition lastTouch = { 0 }; bool touching = false;
  const double dt = 1.0 / 60;
  while (aptMainLoop()) {
    hidScanInput();
    u32 kd = hidKeysDown(), kh = hidKeysHeld();
    touchPosition tp; hidTouchRead(&tp);
    Input in = { 0 }; int touchY = -1;
    if (!(kh & KEY_TOUCH)) touching = false;
    if (settingsOpen && capturing >= 0) {                        // waiting for the button to put a control on
      if (kd & (KEY_START | KEY_SELECT)) capturing = -1;
      else for (int i = 0; i < NKEYS; i++) if (kd & KEYS[i].key) { binds[capturing - 3] = KEYS[i].key; capturing = -1; save(); break; }
    } else if (settingsOpen) {
      if (kd & (KEY_SELECT | KEY_B)) { settingsOpen = false; save(); }
      if (kd & KEY_DOWN) settingsSel = (settingsSel + 1) % NSET;
      if (kd & KEY_UP) settingsSel = (settingsSel + NSET - 1) % NSET;
      int d = (kd & (KEY_RIGHT | KEY_A)) ? 1 : (kd & KEY_LEFT) ? -1 : 0;
      if (settingsSel >= 3 && settingsSel <= 5) { if (kd & KEY_A) capturing = settingsSel; }
      else if (settingsSel == 6) { if (kd & KEY_A) { memcpy(binds, BIND_DEFAULT, sizeof binds); save(); } }
      else if (d) {
        if (settingsSel == 0) optMusic = !optMusic; else if (settingsSel == 1) optSfx = !optSfx;
        else { tzOff += d; if (tzOff > 24) tzOff = -24; if (tzOff < -24) tzOff = 24; int m = mode; clockMaps(); enterMode(m); }
        audio_set(optMusic, optSfx);
      }
    } else if (st == ST_TITLE) {
      if (kd & KEY_SELECT) { settingsOpen = true; settingsSel = 0; capturing = -1; }
      if (kd & KEY_R) enterMode((mode + 1) % 3);
      if (kd & KEY_L) enterMode((mode + 2) % 3);
      if (mode == MODE_OLD && (kd & (KEY_LEFT | KEY_RIGHT))) {
        int w = oldWeek + ((kd & KEY_RIGHT) ? 7 : -7);
        if (w >= FIRST_WEEK && w < liveWeek) { oldWeek = w; enterMode(MODE_OLD); }
      }
      if (kd & KEY_TOUCH) { for (int i = 0; i < 3; i++) if (hit(&TABS[i], tp.px, tp.py)) enterMode(i);
        if (mode == MODE_OLD && tp.py >= 62 && tp.py < 80) { int w = oldWeek + (tp.px >= 160 ? 7 : -7); if (w >= FIRST_WEEK && w < liveWeek) { oldWeek = w; enterMode(MODE_OLD); } }
        if (hit(&RIDE, tp.px, tp.py)) startRun(); }
      if (kd & KEY_A) startRun();
    } else if (st == ST_PLAY) {
      if (kd & KEY_START) paused = !paused;
      if (paused && (kd & KEY_SELECT)) { enterMode(mode); }
      else if (!paused) {
        if (kd & KEY_SELECT) { paused = true; }
        in.thr = (kh & binds[BIND_GO]) != 0;
        double steer = ((kh & binds[BIND_UP]) ? -1 : 0) + ((kh & binds[BIND_DOWN]) ? 1 : 0);
        circlePosition cp; hidCircleRead(&cp);
        if (!steer && abs(cp.dy) > 18) steer = clampd(-cp.dy / 140.0, -1, 1);
        in.steer = steer;
#ifdef DEVTEST
        roadAt(g.cam + g.bx); in.steer = clampd((RC - g.y) / 10, -1, 1);   // autopilot: keep to the middle of the road
#endif
        if (kh & KEY_TOUCH) {                                      // drag the stylus up and down: the bike follows it
          if (touching) in.drag = (tp.py - lastTouch.py) * (LH / 240.0) * 1.25;
          touching = true; lastTouch = tp; touchY = tp.py;
        }
      }
    } else if (st == ST_OVER) {
      if (T - overAt > 0.6) {
        if (kd & (KEY_A | binds[BIND_GO])) startRun();
        else if (kd & KEY_B) enterMode(mode);
        else if (kd & KEY_TOUCH) { if (tp.py >= 180 && tp.py < 212) { if (tp.px < RIDE.x + 85) startRun(); else enterMode(mode); } }
      }
    }
    update(dt, &in);
    audio_tick();
#ifdef SHOWCASE                                                    // test builds: hold the title scene at the garage for the banner
    if (st == ST_TITLE) { if (SEASON) { SEASON = 0; look_apply(); } g.cam = SHOWCASE; }
#endif

    gfx_frame_begin();
    char hint[48]; snprintf(hint, sizeof hint, "HOLD %s, SONNY. KEEP UP.", keyName(binds[BIND_GO]));
    for (char *c = hint; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    render_game(st == ST_PLAY && g.t < 5 ? hint : NULL);
    gfx_top_begin();
    topHud();
    gfx_bottom_begin(H(0a0612));
    if (st == ST_TITLE) bottomTitle(); else if (st == ST_OVER) bottomOver(); else bottomPlay(touchY);
    if (settingsOpen) bottomSettings();
    if (kh & KEY_Y) {                                            // hold Y: how busy the CPU and GPU are
      char pf[64]; snprintf(pf, sizeof pf, "CPU %.0f%%  GPU %.0f%%", C3D_GetProcessingTime() * 6.0f, C3D_GetDrawingTime() * 6.0f);
      cv_fill(rgba(0, 0, 0, .7)); cv_rect(0, 0, 150, 16); cv_text(pf, 4, 12, 10, H(7dff9c), AL_LEFT, BL_ALPHA);
    }
    gfx_frame_end();
  }
  save();
  audio_exit();
  gfx_exit();
  return 0;
}

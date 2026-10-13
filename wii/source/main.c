// Old Man Burn, the Wii arcade edition: one road a week for the tavern, a top 10 with three-letter initials, played on
// the TV. Offline: the road is the arcade's own (not the Discord week's map), and the scores live on the SD card.
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include "sim.h"
#include "gfx.h"
#include "gfx_wii.h"
#include "draw.h"
#include "look.h"
#include "audio.h"
#include "ui.h"
#include "keys.h"

#define H(x) RGB(0x##x)
#define ARCADE_BASE (DAILY_BASE + 600000)                        // arcade seeds: past every Discord seed, with the newest road rules
#define NTOP 10
#define IDLE_BACK 25.0                                           // seconds before the game-over screen goes back to the title

/* ---------- the save: settings and every week's top 10 ---------- */
typedef struct { int seed; char ini[4]; int score; } Score;
static Score scores[1024]; static int nscores;
static bool optMusic = true, optSfx = true; static int steerPct = 100, reroll;
static char venue[40] = "ABBEY TAVERN";
static char saveDir[160] = "sd:/apps/oldmanburn", saveFile[200];

static void mkdirs(const char *path) {                           // mkdir -p
  char p[200]; snprintf(p, sizeof p, "%s", path);
  for (char *c = strchr(p, '/'); c; c = strchr(c + 1, '/')) { if (c > p && c[-1] != ':') { *c = 0; mkdir(p, 0777); *c = '/'; } }
  mkdir(p, 0777);
}
static void save(void) {
  mkdirs(saveDir);
  FILE *f = fopen(saveFile, "w"); if (!f) return;
  fprintf(f, "music %d\nsfx %d\nsteer %d\nreroll %d\nvenue %s\n", optMusic, optSfx, steerPct, reroll, venue);
  for (int i = 0; i < nscores; i++) {
    char in[4]; snprintf(in, sizeof in, "%s", scores[i].ini); for (char *c = in; *c; c++) if (*c == ' ') *c = '_';
    fprintf(f, "score %d %s %d\n", scores[i].seed, in, scores[i].score);
  }
  fclose(f);
}
static void load(void) {
  FILE *f = fopen(saveFile, "r"); if (!f) return;
  char line[160];
  while (fgets(line, sizeof line, f)) {
    char k[16], ini[8]; int a, b;
    line[strcspn(line, "\r\n")] = 0;
    if (sscanf(line, "score %d %3s %d", &a, ini, &b) == 3 && nscores < 1024) {
      scores[nscores].seed = a; snprintf(scores[nscores].ini, 4, "%.3s", ini); scores[nscores].score = b;
      for (char *c = scores[nscores].ini; *c; c++) if (*c == '_') *c = ' ';   // spaces are saved as _
      nscores++;
    }
    else if (!strncmp(line, "venue ", 6)) snprintf(venue, sizeof venue, "%.39s", line + 6);
    else if (sscanf(line, "%15s %d", k, &a) == 2) {
      if (!strcmp(k, "music")) optMusic = a; else if (!strcmp(k, "sfx")) optSfx = a;
      else if (!strcmp(k, "steer")) steerPct = a < 50 ? 50 : a > 200 ? 200 : a; else if (!strcmp(k, "reroll")) reroll = a < 0 ? 0 : a;
    }
  }
  fclose(f);
}
static const Score **boardRows(int seed, int *n) {               // the week's top 10, best first (all-time when seed is 0)
  static const Score *top[NTOP]; int k = 0;
  for (int i = 0; i < nscores; i++) {
    const Score *s = &scores[i]; if (seed && s->seed != seed) continue;
    if (k == NTOP && s->score <= top[NTOP - 1]->score) continue;
    int j = k < NTOP ? k++ : NTOP - 1;
    while (j > 0 && top[j - 1]->score < s->score) { top[j] = top[j - 1]; j--; }
    top[j] = s;
  }
  *n = k; return top;
}
static int rankFor(int seed, int score) {                        // where a score would land on the week's board (1-based), or 0
  if (score <= 0) return 0;
  int n; const Score **r = boardRows(seed, &n);
  for (int i = 0; i < n; i++) if (score > r[i]->score) return i + 1;
  return n < NTOP ? n + 1 : 0;
}
static Score *addScore(int seed, const char *ini, int score) {
  if (nscores == 1024) {                                         // full: drop the lowest score of the oldest week
    int lo = 0; for (int i = 1; i < nscores; i++) if (scores[i].seed < scores[lo].seed || (scores[i].seed == scores[lo].seed && scores[i].score < scores[lo].score)) lo = i;
    scores[lo] = scores[--nscores];
  }
  Score *s = &scores[nscores++]; s->seed = seed; snprintf(s->ini, 4, "%s", ini); s->score = score;
  int n; const Score **r = boardRows(seed, &n);                  // keep only the week's top 10
  for (int i = 0; i < nscores; i++) if (scores[i].seed == seed) {
    bool kept = false; for (int k = 0; k < n; k++) if (r[k] == &scores[i]) kept = true;
    if (!kept) { scores[i] = scores[--nscores]; i--; }
  }
  for (int i = 0; i < nscores; i++) if (scores[i].seed == seed && scores[i].score == score && !strcmp(scores[i].ini, ini)) s = &scores[i];
  save();
  return s;
}

/* ---------- this week's road ---------- */
static int today, weekStart, curSeed; static char trackName[64];
static void pickMap(void) {
  today = (int)(time(NULL) / 86400);                              // the Wii's clock (local time)
  weekStart = today - ((today + 4) % 7);                         // weeks start on Sunday (1970-01-01 was a Thursday)
  curSeed = ARCADE_BASE + weekStart + reroll * 7000;             // a new road every week; the operator can roll another
  reseed(curSeed, true);
  SEASON = seasonOf(today);                                       // Halloween and Christmas looks follow the calendar
  look_apply();
  nameOf(curSeed, SEASON, trackName, sizeof trackName);
  st = ST_TITLE; paused = false;
}

/* ---------- controllers: any of the four Wii Remotes (sideways, or with a Nunchuk or Classic Controller) or GameCube pads ---------- */
static u32 held, down, lastHeld; static double stickY;            // stickY: -1 up .. 1 down
static void readPads(void) {
  WPAD_ScanPads(); PAD_ScanPads();
  u32 h = 0; double sy = 0;
  for (int c = 0; c < 4; c++) {
    WPADData *d = WPAD_Data(c); u32 b = WPAD_ButtonsHeld(c);
    if (d && d->err == WPAD_ERR_NONE) {
      int exp = d->exp.type;
      if (exp == WPAD_EXP_NUNCHUK) {                              // held upright: A or B or Z to go, stick or D-pad to steer
        if (b & WPAD_BUTTON_A) h |= KEY_A | KEY_GO;
        if (b & (WPAD_BUTTON_B | WPAD_NUNCHUK_BUTTON_Z)) h |= KEY_GO;
        if (b & WPAD_NUNCHUK_BUTTON_C) h |= KEY_B;
        if (b & WPAD_BUTTON_UP) h |= KEY_UP; if (b & WPAD_BUTTON_DOWN) h |= KEY_DOWN;
        if (b & WPAD_BUTTON_LEFT) h |= KEY_LEFT; if (b & WPAD_BUTTON_RIGHT) h |= KEY_RIGHT;
        const joystick_t *js = &d->exp.nunchuk.js;
        if (js->mag > 0.25) sy = -js->mag * cos(js->ang * M_PI / 180);
      } else if (exp == WPAD_EXP_CLASSIC) {
        if (b & WPAD_CLASSIC_BUTTON_A) h |= KEY_A | KEY_GO;
        if (b & (WPAD_CLASSIC_BUTTON_FULL_R | WPAD_CLASSIC_BUTTON_ZR)) h |= KEY_GO;
        if (b & WPAD_CLASSIC_BUTTON_B) h |= KEY_B;
        if (b & WPAD_CLASSIC_BUTTON_UP) h |= KEY_UP; if (b & WPAD_CLASSIC_BUTTON_DOWN) h |= KEY_DOWN;
        if (b & WPAD_CLASSIC_BUTTON_LEFT) h |= KEY_LEFT; if (b & WPAD_CLASSIC_BUTTON_RIGHT) h |= KEY_RIGHT;
        if (b & WPAD_CLASSIC_BUTTON_PLUS) h |= KEY_START; if (b & WPAD_CLASSIC_BUTTON_MINUS) h |= KEY_SELECT;
        if (b & WPAD_CLASSIC_BUTTON_HOME) h |= KEY_HOME;
        const joystick_t *js = &d->exp.classic.ljs;
        if (js->mag > 0.25) sy = -js->mag * cos(js->ang * M_PI / 180);
      } else {                                                    // sideways, like an NES pad: 2 goes, the D-pad steers
        if (b & WPAD_BUTTON_2) h |= KEY_A | KEY_GO;
        if (b & WPAD_BUTTON_1) h |= KEY_B;
        if (b & WPAD_BUTTON_A) h |= KEY_A;
        if (b & WPAD_BUTTON_B) h |= KEY_GO;
        if (b & WPAD_BUTTON_RIGHT) h |= KEY_UP; if (b & WPAD_BUTTON_LEFT) h |= KEY_DOWN;
        if (b & WPAD_BUTTON_UP) h |= KEY_LEFT; if (b & WPAD_BUTTON_DOWN) h |= KEY_RIGHT;
      }
      if (b & WPAD_BUTTON_PLUS) h |= KEY_START; if (b & WPAD_BUTTON_MINUS) h |= KEY_SELECT;
      if (b & WPAD_BUTTON_HOME) h |= KEY_HOME;
    }
    u32 p = PAD_ButtonsHeld(c);                                   // GameCube: A or R to go, the stick or D-pad to steer
    if (p & PAD_BUTTON_A) h |= KEY_A | KEY_GO;
    if (p & PAD_TRIGGER_R) h |= KEY_GO;
    if (p & PAD_BUTTON_B) h |= KEY_B;
    if (p & PAD_BUTTON_START) h |= KEY_START;
    if (p & PAD_TRIGGER_Z) h |= KEY_SELECT;
    if (p & PAD_BUTTON_UP) h |= KEY_UP; if (p & PAD_BUTTON_DOWN) h |= KEY_DOWN;
    if (p & PAD_BUTTON_LEFT) h |= KEY_LEFT; if (p & PAD_BUTTON_RIGHT) h |= KEY_RIGHT;
    int y = PAD_StickY(c); if (abs(y) > 18) sy = -y / 90.0;
  }
#ifdef AUTOPLAY
  extern u32 autoplay(u32 h, double *sy); h = autoplay(h, &sy);
#endif
  down = h & ~lastHeld; lastHeld = held = h; stickY = clampd(sy, -1, 1);
}

/* ---------- screens ---------- */
enum { SC_TITLE, SC_PLAY, SC_ENTRY, SC_OVER, SC_SETTINGS };
static int screen = SC_TITLE;
static double overAt, idleAt, holdAt = -1;
static int lastScore, rank; static const char *causeTxt = "";
static char ini[4] = "AAA"; static int iniPos; static const Score *mine;
static const char LETTERS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.!? ";
static const char *CAUSES[4][3] = { { "", "", "" },
  { "Slid off the back. Back in '73 we kept up with the Torino. Stay on the road and mind the throttle.", "Too slow. I once waited so long at the DMV they changed the carpet. Keep on the throttle.", "You nodded off, didn't you. Naps are for the fountain, not the highway." },
  { "Shot off the front. Slow down, you'll spill your thermos.", "Too fast. In 1964 we had a speed limit and a sense of shame.", "Went right past the screen. Wasn't that the Woolworth's?" },
  { "Hit the guardrail. They never had those on Route 9 in '66.", "Guardrail. Solid steel. Like my hip.", "You hit the barricade. Marge would have swerved." } };

static void kicker(const char *s, double x, double y, unsigned col) { cv_font(FONT_MONO); cv_text(s, x, y, 11, col, AL_LEFT, BL_ALPHA); }   // ui_kicker, sized for a TV
static void fmtNum(int v, char *out) {
  char tmp[16]; snprintf(tmp, sizeof tmp, "%d", v); int n = strlen(tmp), o = 0;
  for (int i = 0; i < n; i++) { out[o++] = tmp[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ','; }
  out[o] = 0;
}
static void upper(const char *s, char *out, int n) { int i = 0; for (; s[i] && i < n - 1; i++) out[i] = s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]; out[i] = 0; }
static int topScore(void) { int n; const Score **r = boardRows(curSeed, &n); return n ? r[0]->score : 0; }

void on_crash(void) { audio_crash(); }
void on_over(void) {
  lastScore = g.dist; overAt = idleAt = T;
  causeTxt = CAUSES[g.cause][(int)(frand() * 3)];
  rank = rankFor(curSeed, lastScore); mine = NULL;
  if (rank) { screen = SC_ENTRY; memcpy(ini, "AAA", 4); iniPos = 0; } else screen = SC_OVER;
}
static void startRun(void) {
  if (st == ST_OVER || st == ST_CRASH) reseed(curSeed, true), SEASON = seasonOf(today);
  newRun(); st = ST_PLAY; paused = false; screen = SC_PLAY;
}
static void toTitle(void) { pickMap(); screen = SC_TITLE; ui_reset_focus(); }

// the leaderboard panel; hl: the row to light up
static void drawBoard(double x, double y, double w, const char *title, int seed, const Score *hl) {
  char b[64], sc[24];
  cv_fill(rgba(10, 6, 18, .78)); cv_rect(x, y, w, 38 + NTOP * 26);
  cv_stroke_rect(x + .5, y + .5, w - 1, 37 + NTOP * 26, UI_LINE, 1);
  kicker(title, x + 14, y + 22, UI_PINK);
  int n; const Score **r = boardRows(seed, &n);
  for (int i = 0; i < NTOP; i++) {
    double ry = y + 52 + i * 26;
    bool me = i < n && r[i] == hl;
    if (me) { cv_fill(rgba(255, 210, 31, .16)); cv_rect(x + 6, ry - 18, w - 12, 24); }
    snprintf(b, sizeof b, "%d", i + 1); ui_text(b, x + 34, ry, 14, i == 0 ? UI_GOLD : UI_DIM, AL_RIGHT);
    if (i < n) {
      cv_font(FONT_DISP); cv_text(r[i]->ini, x + 50, ry, 15, me ? UI_GOLD : UI_BONE, AL_LEFT, BL_ALPHA);
      fmtNum(r[i]->score, sc); snprintf(b, sizeof b, "%s m", sc); ui_text(b, x + w - 14, ry, 14, me || i == 0 ? UI_GOLD : UI_BONE, AL_RIGHT);
    } else ui_text("---", x + 50, ry, 14, 0x4a3874ffu, AL_LEFT);
  }
}
static void titleScreen(void) {
  char b[128], u[128];
  cv_fill(rgba(10, 6, 18, .74)); cv_rect(0, 0, 330, UI_H);
  double gs[2] = { 0, 1 }; col_t gc[2] = { rgba(10, 6, 18, .74), rgba(10, 6, 18, 0) };
  cv_grad(330, 0, 60, UI_H, false, 330, 390, 2, gs, gc);
  upper(venue, u, sizeof u); kicker(u, 22, 34, UI_CYAN);
  snprintf(b, sizeof b, "This week's road: %s", trackName); upper(b, u, sizeof u); kicker(u, 22, 52, UI_GOLD);
  ui_heading("OLD MAN", 22, 112, 50, AL_LEFT); ui_heading("BURN", 22, 166, 50, AL_LEFT);
  ui_wrap("Born in 1952 and still riding. Ease off the throttle and you slide off the back. Hold it too long and you shoot "
          "off the front. A new road every week. Top ten get their initials up.", 22, 184, 12, UI_BONE, 290);
  if (fmod(T, 1.2) < 0.85) { cv_font(FONT_DISP); cv_text("PRESS 2 TO RIDE", 23, 315, 22, UI_GOLD, AL_LEFT, BL_ALPHA); cv_text("PRESS 2 TO RIDE", 22, 314, 22, UI_RED, AL_LEFT, BL_ALPHA); }
  ui_text("Wii Remote sideways: hold 2 to go, the D-pad steers.", 22, 340, 10, UI_SUB, AL_LEFT);
  ui_text("Nunchuk: hold B or Z, stick steers. GameCube: hold A or R.", 22, 354, 10, UI_SUB, AL_LEFT);
  bool allTime = fmod(T, 16) >= 8;                                // the week's board, then the all-time one
  drawBoard(388, 30, 232, allTime ? "ALL-TIME TOP 10" : "THIS WEEK'S TOP 10", allTime ? 0 : curSeed, NULL);
}
static void playHud(void) {
  char b[64], sc[24]; int s = st == ST_PLAY ? scoreOf() : g.dist;
  fmtNum(s, b);
  cv_font(FONT_DISP); cv_text(b, 619, 51, 40, H(a87400), AL_RIGHT, BL_ALPHA); cv_text(b, 616, 48, 40, H(ffd21f), AL_RIGHT, BL_ALPHA);
  int n; const Score **r = boardRows(curSeed, &n);
  if (n) {
    fmtNum(r[0]->score, sc); snprintf(b, sizeof b, "%s %s", r[0]->ini, sc);
    ui_text("TOP THIS WEEK", 320, 16, 9, rgba(255, 236, 220, .6), AL_CENTER);
    cv_font(FONT_DISP); cv_text(b, 320, 38, 18, s > r[0]->score ? H(ffd21f) : rgba(255, 236, 220, .6), AL_CENTER, BL_ALPHA);
  }
  ui_text(trackName, 14, 20, 10, rgba(255, 236, 220, .55), AL_LEFT);
}
static void panel(void) {                                        // the dark card over the whole picture, red light from the top
  cv_fill(rgba(10, 6, 18, .86)); cv_rect(0, 0, UI_W, UI_H);
  double s[3] = { 0, 0.5, 1 }; col_t c[3] = { rgba(255, 45, 45, .20), rgba(255, 45, 45, .07), rgba(255, 45, 45, 0) };
  cv_grad(0, 0, UI_W, UI_H, true, 0, UI_H, 3, s, c);
}
static void entryScreen(void) {
  char b[64], sc[24];
  panel();
  ui_heading("NEW HIGH SCORE!", 320, 70, 40, AL_CENTER);
  fmtNum(lastScore, sc); snprintf(b, sizeof b, "%s m · #%d this week", sc, rank);
  ui_text(b, 320, 104, 16, UI_GOLD, AL_CENTER);
  ui_text("ENTER YOUR INITIALS", 320, 140, 12, UI_CYAN, AL_CENTER);
  for (int i = 0; i < 3; i++) {
    double x = 320 + (i - 1) * 84 - 34, y = 158; bool f = i == iniPos;
    cv_fill(f ? rgba(255, 210, 31, .14) : rgba(255, 255, 255, .05)); cv_rect(x, y, 68, 84);
    cv_stroke_rect(x + .5, y + .5, 67, 83, f ? UI_GOLD : UI_LINE, f ? 2 : 1);
    char c[2] = { ini[i], 0 }; if (c[0] == ' ') c[0] = '_';
    cv_font(FONT_DISP); cv_text(c, x + 34, y + 62, 46, f ? UI_GOLD : UI_BONE, AL_CENTER, BL_ALPHA);
    if (f && fmod(T, 0.8) < 0.55) {
      double up[6] = { x + 34, y - 14, x + 26, y - 5, x + 42, y - 5 }, dn[6] = { x + 34, y + 98, x + 26, y + 89, x + 42, y + 89 };
      cv_fill(UI_PINK); cv_poly(up, 3); cv_poly(dn, 3);
    }
  }
  ui_text("Up / down picks a letter · 2 next · 1 back", 320, 296, 12, UI_SUB, AL_CENTER);
}
static void overScreen(void) {
  char b[96], sc[24];
  panel();
  ui_heading("MY HIP!", 30, 70, 44, AL_LEFT);
  fmtNum(lastScore, sc); snprintf(b, sizeof b, "%s m", sc);
  cv_font(FONT_DISP); cv_text(b, 30, 112, 28, UI_GOLD, AL_LEFT, BL_ALPHA);
  ui_wrap(causeTxt, 30, 128, 12, UI_SUB, 330);
  if (mine) { char in[4]; snprintf(in, sizeof in, "%s", mine->ini); for (int i = 2; i >= 0 && in[i] == ' '; i--) in[i] = 0;
    snprintf(b, sizeof b, "%s is #%d on this week's board.", in, rank); }
  else { int top = topScore(), n; boardRows(curSeed, &n); fmtNum(n == NTOP ? boardRows(curSeed, &n)[NTOP - 1]->score : 0, sc);
    snprintf(b, sizeof b, n == NTOP ? "The top ten starts at %s m. Have another go." : "Have another go.", sc); (void)top; }
  ui_text(b, 30, 210, 12, UI_GOLD, AL_LEFT);
  if (T - overAt > 0.8) {
    if (fmod(T, 1.2) < 0.85) { cv_font(FONT_DISP); cv_text("PRESS 2 TO RIDE AGAIN", 31, 291, 20, UI_GOLD, AL_LEFT, BL_ALPHA); cv_text("PRESS 2 TO RIDE AGAIN", 30, 290, 20, UI_RED, AL_LEFT, BL_ALPHA); }
    ui_text("1: back to the title", 30, 316, 11, UI_DIM, AL_LEFT);
  }
  drawBoard(388, 30, 232, "THIS WEEK'S TOP 10", curSeed, mine);
}

/* ---------- the operator's settings: hold - (or Z, or HOME) on the title screen ---------- */
enum { S_MUSIC, S_SFX, S_STEER, S_NEWMAP, S_CLEAR, S_EXIT, S_CLOSE, NS };
static int setSel, confirmRow = -1;
static void settingsScreen(void) {
  char v[64];
  cv_fill(rgba(10, 6, 18, .95)); cv_rect(0, 0, UI_W, UI_H);
  cv_stroke_rect(100.5, 30.5, 439, 299, UI_PINK, 1);
  kicker("SETTINGS · FOR WHOEVER RUNS THE TAVERN NIGHT", 120, 56, UI_PINK);
  const char *names[NS] = { "Music", "Sound effects", "Steering speed", "Roll a new road for this week", "Clear this week's top 10", "Exit to the Homebrew Channel", "Close" };
  for (int i = 0; i < NS; i++) {
    double y = 92 + i * 30; bool f = i == setSel;
    if (f) { cv_fill(rgba(34, 230, 216, .10)); cv_rect(110, y - 18, 420, 26); }
    ui_text(names[i], 124, y, 13, f ? UI_BONE : 0xcfc9e0ffu, AL_LEFT);
    if (i == S_MUSIC || i == S_SFX) ui_check(500, y - 11, i == S_MUSIC ? optMusic : optSfx);
    if (i == S_STEER) { ui_slider((Box){ 380, y - 14, 100, 14 }, (steerPct - 50) / 150.0, f); snprintf(v, sizeof v, "%d.%dx", steerPct / 100, steerPct / 10 % 10); ui_text(v, 516, y, 12, UI_GOLD, AL_RIGHT); }
    if (i == confirmRow) ui_text("press 2 again", 516, y, 12, UI_PINK, AL_RIGHT);
  }
  ui_text("Up / down picks · left / right changes · 2 does it · 1 closes", 120, 314, 10, UI_SUB, AL_LEFT);
}
static void settingsInput(void) {
  if (down & (KEY_B | KEY_SELECT | KEY_HOME)) { screen = SC_TITLE; save(); confirmRow = -1; return; }
  if (down & KEY_UP) { setSel = (setSel + NS - 1) % NS; confirmRow = -1; }
  if (down & KEY_DOWN) { setSel = (setSel + 1) % NS; confirmRow = -1; }
  int d = (down & KEY_RIGHT) ? 1 : (down & KEY_LEFT) ? -1 : 0;
  if (d && setSel == S_STEER) steerPct = (int)clampd(steerPct + d * 10, 50, 200);
  if (!(down & KEY_A) && !(d && setSel <= S_SFX)) return;
  switch (setSel) {
    case S_MUSIC: optMusic = !optMusic; break;
    case S_SFX: optSfx = !optSfx; break;
    case S_NEWMAP: if (confirmRow != S_NEWMAP) { confirmRow = S_NEWMAP; return; } reroll++; pickMap(); break;
    case S_CLEAR: if (confirmRow != S_CLEAR) { confirmRow = S_CLEAR; return; }
      for (int i = 0; i < nscores; i++) if (scores[i].seed == curSeed) { scores[i] = scores[--nscores]; i--; } break;
    case S_EXIT: if (confirmRow != S_EXIT) { confirmRow = S_EXIT; return; } save(); audio_exit(); gfx_exit(); exit(0);
    case S_CLOSE: screen = SC_TITLE; break;
  }
  confirmRow = -1; audio_set(optMusic, optSfx); save();
}

static void findSaveDir(int argc, char **argv) {                   // next to boot.dol: sd:/apps/oldmanburn, or the USB drive's
  if (argc > 0 && argv[0] && strchr(argv[0], '/')) {
    snprintf(saveDir, sizeof saveDir, "%s", argv[0]);
    *strrchr(saveDir, '/') = 0;
  }
  snprintf(saveFile, sizeof saveFile, "%s/scores.txt", saveDir);
}

#ifdef AUTOPLAY
#include "autoplay.h"
#endif

#ifdef AUTOPLAY
static void mark(const char *m) { SYS_Report("MARK %s\n", m); }
#else
#define mark(m)
#endif
static volatile int powerOff, resetPressed;                       // the console's buttons: power turns it off, reset goes back to the HBC
static void onPower(void) { powerOff = 1; }
static void onRemotePower(s32 chan) { (void)chan; powerOff = 1; }
static void onReset(u32 irq, void *ctx) { (void)irq; (void)ctx; resetPressed = 1; }
int main(int argc, char **argv) {
  mark("main");
  gfx_init(); mark("gfx");
  WPAD_Init(); PAD_Init();
  SYS_SetPowerCallback(onPower); SYS_SetResetCallback(onReset); WPAD_SetPowerButtonCallback(onRemotePower);
  WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
  fatInitDefault();
  findSaveDir(argc, argv);
  srand((unsigned)time(NULL));
  mark("start"); load(); mark("loaded");
  draw_init(); mark("draw_init");
  gfx_build(draw_sprites); mark("sprites");
  audio_init(); audio_set(optMusic, optSfx); mark("audio");
  pickMap(); mark("map");
  const double dt = 1.0 / gfx_hz();
  for (;;) {
    if (powerOff || resetPressed) { save(); audio_exit(); gfx_exit(); if (powerOff) SYS_ResetSystem(SYS_POWEROFF, 0, 0); exit(0); }
    readPads();
    Input in = { 0 };
    if (screen == SC_SETTINGS) settingsInput();
    else if (screen == SC_TITLE) {
      if (held & (KEY_SELECT | KEY_HOME)) { if (holdAt < 0) holdAt = T; if (T - holdAt > 1.5) { screen = SC_SETTINGS; setSel = 0; confirmRow = -1; holdAt = -1; } }
      else holdAt = -1;
      if (down & (KEY_A | KEY_GO)) { pickMap(); startRun(); }   // a new week may have started since the title came up
    } else if (screen == SC_PLAY) {
      if (st == ST_PLAY) {
        if (down & KEY_START) paused = !paused;
        if (paused && (down & KEY_B)) toTitle();
        else if (!paused) {
          in.thr = (held & KEY_GO) != 0;
          double steer = ((held & KEY_UP) ? -1 : 0) + ((held & KEY_DOWN) ? 1 : 0);
          if (!steer) steer = stickY;
          in.steer = steer * steerPct / 100.0;
        }
      }
    } else if (screen == SC_ENTRY) {
      if (down) idleAt = T;
      if (T - overAt > 0.8) {
        int n = (int)strlen(LETTERS), k = (int)(strchr(LETTERS, ini[iniPos]) - LETTERS);
        if (down & KEY_UP) ini[iniPos] = LETTERS[(k + n - 1) % n];
        if (down & KEY_DOWN) ini[iniPos] = LETTERS[(k + 1) % n];
        if (down & (KEY_B | KEY_LEFT)) { if (iniPos > 0) iniPos--; }
        if ((down & (KEY_A | KEY_RIGHT)) || T - idleAt > 30) {
          if (iniPos < 2 && T - idleAt <= 30) iniPos++;
          else { mine = addScore(curSeed, ini, lastScore); screen = SC_OVER; overAt = idleAt = T; }
        }
      }
    } else if (screen == SC_OVER) {
      if (down) idleAt = T;
      if (T - overAt > 0.8) {
        if (down & (KEY_A | KEY_GO)) startRun();
        else if ((down & KEY_B) || T - idleAt > IDLE_BACK) toTitle();
      }
    }
    update(dt, &in);
    audio_tick();

    gfx_frame_begin();
    look_frame();
    render_game(st == ST_PLAY && g.t < 5 ? "HOLD 2, SONNY. KEEP UP." : NULL);
    gfx_top_begin();
    ui_begin();
    if (screen == SC_SETTINGS) settingsScreen();
    else if (screen == SC_TITLE) titleScreen();
    else if (screen == SC_ENTRY) entryScreen();
    else if (screen == SC_OVER) overScreen();
    else playHud();
    gfx_frame_end();
#ifdef AUTOPLAY
    autoplay_frame();
#endif
  }
}

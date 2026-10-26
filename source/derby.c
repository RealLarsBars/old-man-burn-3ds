// The Old Man Derby screen (see derby.h), ported from the web game's Race tab (web/derby.js). The server decides
// everything: the card, the odds, the result and the path every horse takes. This only shows it, so the race here is
// the one Discord watches at 3pm, played from the stored path and the server's clock.
#include "derby.h"
#pragma GCC diagnostic ignored "-Wformat-truncation"   // names are cut to fit on purpose
#include "derby_art.h"
#include "sync.h"
#include "ui.h"
#include "gfx.h"
#include "look.h"
#include "audio.h"
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define H(x) RGB(0x##x)
#define OUTRO 5                 // seconds after the race view ends before the results come up
enum { RQ_STATE, RQ_BET };

typedef struct { int n, d, purse; char who[96]; char form[40]; int runs, wins; bool hasRec; } Extra;
typedef struct {
  bool ok; int id; char theme[16]; s64 closes, start; int nh; DaHorse h[DA_NH]; Extra x[DA_NH];
  int oldMan, bettors; bool raced, oldManWon; int order[DA_NH];
  int mine, mineBoost, payout; bool paid;          // mine: -1 no pick; payout: -1 not known yet
} Race;
typedef struct { char name[32]; col_t silk; int wins, places, runs; } StableRow;

static bool isOpen, have, asking, betting; static u64 nextAsk; static char err[96];
static bool on; static int boostMax = 5, roundBonus = 5, keepCoins = 1, coins = -1;
static s64 off;                                   // the server's clock minus the console's (osGetTime: ms since 1900)
static Race card, last; static s64 opens; static StableRow stable[16]; static int nstable;
static int sel, boost, seenCard = -1; static char msg[160]; static u64 msgAt;
static double tt, parade;
static struct { bool on, replay, results; Race race; s64 t0; DaShow v; DaShowOut o; bool bell, won, photo; double hoofAt; } sh;
static int idBack, idRow[DA_NH], idMinus, idPlus, idPick, idWatch, idRetry, idAgain, idCard;

static s64 now(void) { return (s64)osGetTime() + off; }
static const char *nth(int n) { static char b[8]; snprintf(b, sizeof b, "%d%s", n, n == 1 ? "st" : n == 2 ? "nd" : n == 3 ? "rd" : "th"); return b; }
static const char *coinsStr(int n) { static char b[2][24]; static int k; k ^= 1; snprintf(b[k], sizeof b[k], "%d coin%s", n, n == 1 ? "" : "s"); return b[k]; }
static void say(const char *m) { snprintf(msg, sizeof msg, "%s", m); msgAt = osGetTime(); }
static void ask(void) {
  if (asking) return;
  if (sync_derby_send(RQ_STATE, "GET", "/api/3ds/derby", NULL)) asking = true;
  else if (!have) snprintf(err, sizeof err, sync_linked() ? "No connection. Is Wi-Fi on?" : "Link this 3DS to Discord (Settings, Discord) to play the Derby.");
  nextAsk = osGetTime() + 30000;                  // the card refreshes every 30 seconds
}

/* ---------- reading the server's answer ---------- */
static const char *js(json_t *o, const char *k) { const char *s = json_string_value(json_object_get(o, k)); return s ? s : ""; }
static int ji(json_t *o, const char *k, int d) { json_t *v = json_object_get(o, k); return json_is_integer(v) ? (int)json_integer_value(v) : d; }
static s64 jms(json_t *o, const char *k) { json_t *v = json_object_get(o, k); return json_is_integer(v) ? (s64)json_integer_value(v) : 0; }
static col_t hexc(const char *s) { return *s == '#' ? RGB(strtoul(s + 1, NULL, 16)) : H(e0242e); }
static int horseIndex(const Race *r, const char *id) { for (int i = 0; i < r->nh; i++) if (!strcmp(r->h[i].id, id)) return i; return -1; }
static void readRace(json_t *o, Race *r, bool raced) {
  memset(r, 0, sizeof *r); r->mine = -1; r->payout = -1; r->oldMan = -1;
  if (!json_is_object(o)) return;
  r->id = ji(o, "id", 0); r->closes = jms(o, "closes_ms"); r->start = jms(o, "start_ms"); r->bettors = ji(o, "bettors", 0);
  json_t *hs = json_object_get(o, "horses"); size_t i; json_t *v;
  const char *me = sync_my_id();
  json_array_foreach(hs, i, v) {
    if (r->nh == DA_NH) break;
    DaHorse *h = &r->h[r->nh]; Extra *x = &r->x[r->nh]; r->nh++;
    snprintf(h->id, sizeof h->id, "%s", js(v, "id")); snprintf(h->name, sizeof h->name, "%s", js(v, "name"));
    snprintf(h->odds, sizeof h->odds, "%s", js(v, "odds_text")); h->num = ji(v, "num", r->nh);
    json_t *lk = json_object_get(v, "look"), *silks = json_object_get(lk, "silks");
    snprintf(h->look.coat, sizeof h->look.coat, "%s", js(lk, "coat")); snprintf(h->look.mane, sizeof h->look.mane, "%s", js(lk, "mane"));
    snprintf(h->look.marks, sizeof h->look.marks, "%s", js(lk, "marks")); snprintf(h->look.pattern, sizeof h->look.pattern, "%s", js(lk, "pattern"));
    h->look.silk1 = hexc(json_string_value(json_array_get(silks, 0)) ?: "#e0242e"); h->look.silk2 = hexc(json_string_value(json_array_get(silks, 1)) ?: "#ffffff");
    json_t *od = json_object_get(v, "odds");
    x->n = (int)json_integer_value(json_array_get(od, 0)); x->d = (int)json_integer_value(json_array_get(od, 1)); if (x->d <= 0) x->d = 1;
    x->purse = ji(v, "purse", 0);
    // who picked it: "Pat, Sam, you +4"
    json_t *bk = json_object_get(v, "backers"); size_t bi; json_t *b; int nb = (int)json_array_size(bk), shown = 0;
    json_array_foreach(bk, bi, b) {
      if (shown == 3) { char more[12]; snprintf(more, sizeof more, " +%d", nb - 3); strncat(x->who, more, sizeof x->who - strlen(x->who) - 1); break; }
      const char *bid = js(b, "id"), *nm = me[0] && !strcmp(bid, me) ? "you" : js(b, "name");
      if (shown) strncat(x->who, ", ", sizeof x->who - strlen(x->who) - 1);
      strncat(x->who, nm, sizeof x->who - strlen(x->who) - 1); shown++;
    }
    json_t *fm = json_object_get(v, "form"); size_t fi; json_t *f;
    json_array_foreach(fm, fi, f) {
      if (fi >= 5) break;
      strncat(x->form, fi ? ", " : (json_array_size(fm) == 1 ? "last race: " : "last races: "), sizeof x->form - strlen(x->form) - 1);
      strncat(x->form, nth((int)json_integer_value(f)), sizeof x->form - strlen(x->form) - 1);
    }
    json_t *rec = json_object_get(v, "record");
    if (json_is_object(rec)) { x->hasRec = true; x->runs = ji(rec, "runs", 0); x->wins = ji(rec, "wins", 0); }
  }
  r->oldMan = horseIndex(r, js(o, "old_man"));
  snprintf(r->theme, sizeof r->theme, "%s", js(o, "theme"));
  json_t *mine = json_object_get(o, "mine");
  if (json_is_object(mine)) {
    r->mine = horseIndex(r, js(mine, "horse")); r->mineBoost = ji(mine, "boost", 0);
    r->payout = ji(mine, "payout", -1); r->paid = json_is_true(json_object_get(mine, "paid"));
  }
  if (raced) {
    json_t *ord = json_object_get(o, "order"), *path = json_object_get(o, "path");
    for (int k = 0; k < r->nh; k++) r->order[k] = k;
    json_array_foreach(ord, i, v) if ((int)i < r->nh) { int k = horseIndex(r, json_string_value(v) ?: ""); if (k >= 0) r->order[i] = k; }
    for (int k = 0; k < r->nh; k++) {
      json_t *p = json_object_get(path, r->h[k].id), *a = json_object_get(p, "a");
      r->h[k].T = json_number_value(json_object_get(p, "T")); if (r->h[k].T <= 0) r->h[k].T = 60;
      for (int q = 0; q < DA_NA; q++) r->h[k].a[q] = json_number_value(json_array_get(a, q));
    }
    r->oldManWon = json_is_true(json_object_get(o, "old_man_won"));
    r->raced = r->nh > 1;
  }
  // the jockeys: your pick's wears your own garage outfit; the rest are dressed the same way on every screen
  for (int k = 0; k < r->nh; k++) {
    DaRider *rd = &r->h[k].rider;
    if (k == r->mine) { snprintf(rd->coat, sizeof rd->coat, "%s", look_get(SL_COAT)); snprintf(rd->cap, sizeof rd->cap, "%s", look_get(SL_CAP)); }
    else da_outfit(r->id, r->h[k].num, rd);
  }
  r->ok = r->nh > 0;
}
static void applyState(json_t *j) {
  on = json_is_true(json_object_get(j, "on"));
  s64 n = jms(j, "now_ms"); if (n) off = n - (s64)osGetTime();
  boostMax = ji(j, "boost_max", 5); roundBonus = ji(j, "round_bonus", 5); keepCoins = ji(j, "keep_coins", 1);
  coins = ji(j, "coins", -1);
  readRace(json_object_get(j, "race"), &card, false);
  readRace(json_object_get(j, "last"), &last, true);
  opens = jms(j, "opens_ms");
  nstable = 0; size_t i; json_t *v;
  json_array_foreach(json_object_get(j, "stable"), i, v) {
    if (nstable == 16) break;
    StableRow *s = &stable[nstable++];
    snprintf(s->name, sizeof s->name, "%s", js(v, "name"));
    s->silk = hexc(json_string_value(json_array_get(json_object_get(json_object_get(v, "look"), "silks"), 0)) ?: "#e0242e");
    s->wins = ji(v, "wins", 0); s->places = ji(v, "places", 0); s->runs = ji(v, "runs", 0);
  }
  if (card.ok && card.id != seenCard) {            // a new card: start on your pick, or the favourite
    seenCard = card.id; sel = card.mine >= 0 ? card.mine : 0; boost = card.mine >= 0 ? card.mineBoost : 0;
  }
  if (sel >= card.nh) sel = 0;
  have = true; err[0] = 0;
}
void derby_reply(int what, int status, json_t *j) {
  bool ok = status >= 200 && status < 300 && j;
  const char *detail = json_string_value(json_object_get(j, "detail"));
  if (what == RQ_STATE) {
    asking = false;
    if (ok) applyState(j);
    else if (!have) snprintf(err, sizeof err, "%s", status == 0 ? "No connection. Is Wi-Fi on?" : detail ? detail : "The track is closed right now.");
    if (!ok) nextAsk = osGetTime() + 10000;
  } else if (what == RQ_BET) {
    betting = false;
    if (ok) {
      applyState(j);
      if (card.mine >= 0) {
        char b[160]; const DaHorse *h = &card.h[card.mine];
        if (card.mineBoost) snprintf(b, sizeof b, "You're on #%d %s with a %d-coin boost. The Old Man wishes you luck. Not too much luck.", h->num, h->name, card.mineBoost);
        else snprintf(b, sizeof b, "You're on #%d %s. The Old Man wishes you luck. Not too much luck.", h->num, h->name);
        sel = card.mine; boost = card.mineBoost; say(b);
      }
    } else say(status == 0 ? "No connection. Is Wi-Fi on?" : detail ? detail : "The window is closed. Try again.");
  }
}

/* ---------- the card ---------- */
static int winFor(const Race *r, int i, int b) {   // what a win would pay, as the server works it out (Derby.payout)
  const Extra *x = &r->x[i];
  return x->purse + b * x->n / x->d + b + (i == r->oldMan ? roundBonus : 0);
}
static int maxBoost(void) {
  int placed = card.mine >= 0 ? card.mineBoost : 0;
  return coins < 0 ? boostMax : (int)clampd(coins + placed - keepCoins, 0, boostMax);
}
static void place(void) {
  if (!card.ok || betting || sel < 0 || sel >= card.nh) return;
  if (card.mine == sel && card.mineBoost == boost) return;
  char body[96]; snprintf(body, sizeof body, "{\"horse\":\"%s\",\"boost\":%d}", card.h[sel].id, boost);
  if (sync_derby_send(RQ_BET, "POST", "/api/3ds/derby/bet", body)) { betting = true; say("Taking your pick to the window..."); }
  else say("No connection. Is Wi-Fi on?");
}

/* ---------- the race ---------- */
static void boardOf(const Race *r, DaBoardRow *b, int hot) {
  for (int i = 0; i < r->nh; i++) b[i] = (DaBoardRow){ r->h[i].num, r->h[i].name, r->h[i].odds, i == hot };
}
static void soundStart(void) {   // the bugler's call to the post, a short fanfare on a brassy square wave
  static const float CALL[6][3] = { { 67, 0, .16f }, { 72, .18f, .16f }, { 76, .36f, .16f }, { 79, .54f, .34f }, { 76, .92f, .16f }, { 79, 1.1f, .5f } };
  for (int i = 0; i < 6; i++) {
    audio_fx_tone(CALL[i][1], 440 * powf(2, (CALL[i][0] - 69) / 12), CALL[i][2], AW_SQUARE, 0.09f, 0);
    audio_fx_tone(CALL[i][1], 440 * powf(2, (CALL[i][0] - 57) / 12), CALL[i][2], AW_TRI, 0.06f, 0);
  }
}
static void sound(const DaShowOut *o, double dt) {
  if (o->rs >= 0 && !sh.bell) {                   // the starting bell
    sh.bell = true;
    for (int i = 0; i < 8; i++) { audio_fx_tone(i * 0.09f, 1568, 0.12f, AW_SQUARE, 0.05f, 0); audio_fx_tone(i * 0.09f, 2350, 0.08f, AW_SINE, 0.05f, 0); }
  }
  // hooves: a rolling drum of low thumps while the field is running
  if (o->rs >= 0 && o->rs < 70 && (o->lead < 1.03 || !o->won)) {
    sh.hoofAt -= dt;
    while (sh.hoofAt <= 0) { audio_fx_noise(frand() * 0.02f, 0.07f, AF_LOWPASS, 260 + frand() * 120, 0.22f + frand() * 0.12f); sh.hoofAt += 0.06 + frand() * 0.05; }
  }
  if (o->won && !sh.won) { sh.won = true; audio_fx_noise(0, 2.5f, AF_BANDPASS, 1200, 0.25f); }   // the roar at the line
  if (o->photo && !sh.photo) { sh.photo = true; audio_fx_noise(0, 0.05f, AF_HIGHPASS, 4000, 0.4f); audio_fx_tone(0.04f, 2400, 0.25f, AW_SINE, 0.08f, 1200); }
  if (!sh.results) audio_crowd(o->rs < 0 ? 0.05f : o->won ? 0.16f : o->lead > 0.82 ? 0.14f : o->lead > 0.6 ? 0.08f : 0.05f);
}
static int skipped = -1;                          // the live race you left: it doesn't start again by itself
static void startShow(const Race *r, bool replay) {
  // a live race's clock starts when the server drew it (3pm); everyone sees the gate for a few seconds, then the off
  Race race = *r;                                 // r may be the show's own race (WATCH AGAIN)
  memset(&sh, 0, sizeof sh);
  sh.on = true; sh.replay = replay; sh.race = race; sh.t0 = replay ? (s64)osGetTime() : race.start;
  da_show_start(&sh.v, sh.race.h, sh.race.nh, sh.race.mine, sh.race.oldMan, sh.race.order[0], sh.race.order[1]);
  soundStart();
}
static void stopShow(void) { if (!sh.replay) skipped = sh.race.id; sh.on = false; audio_crowd(0); ask(); }
#ifndef DERBY_REPLAY_SPEED
#define DERBY_REPLAY_SPEED 1      // a test build can play replays faster, to check a whole race in the emulator
#endif
static double showClock(void) { return sh.replay ? ((s64)osGetTime() - sh.t0) / 1000.0 * DERBY_REPLAY_SPEED : (now() - sh.t0) / 1000.0; }
static s64 raceEnd(const Race *r) {               // when a live race's show is over (and the results have had their moment)
  double mx = 0; for (int i = 0; i < r->nh; i++) mx = fmax(mx, r->h[i].T);
  return r->start + (s64)((mx + OUTRO + DA_INTRO) * 1000);
}

/* ---------- open, close, the clock ---------- */
void derby_open(void) {
  isOpen = true; tt = 0; parade = 0; msg[0] = 0; err[0] = 0; sh.on = false;
  ask();
}
bool derby_is_open(void) { return isOpen; }
void derby_close(void) { isOpen = false; sh.on = false; audio_crowd(0); }
void derby_tick(double dt) {
  if (!isOpen) return;
  tt += dt;
  u64 t = osGetTime(); s64 n = now();
  if (!sh.on && have && last.ok && last.raced && last.id != skipped && n >= last.start && n < raceEnd(&last)) startShow(&last, false);   // the race on right now, live
  if (!asking && have && !sh.on) {
    if (card.ok && n >= card.closes && t >= nextAsk - 28500) ask();              // 3pm: fetch the race the server has just drawn
    else if (!card.ok && opens && n >= opens && t >= nextAsk - 28500) ask();     // 4pm: tomorrow's card is out
  }
  if (!asking && t >= nextAsk && !sh.on && !betting) ask();
  if (sh.on && !sh.results && showClock() > da_show_end(&sh.v) + OUTRO) { sh.results = true; audio_crowd(0.02f); }
}
void derby_keys(u32 *kd) {
  u32 k = *kd;
  if (k & KEY_B) { if (sh.on) stopShow(); else derby_close(); }
  else if (sh.on) {}
  else if (card.ok && on) {
    if (k & (KEY_UP | KEY_DOWN)) { sel = (int)clampd(sel + ((k & KEY_DOWN) ? 1 : -1), 0, card.nh - 1); }
    if (k & KEY_LEFT) boost = boost > 0 ? boost - 1 : 0;
    if (k & KEY_RIGHT) boost = boost < maxBoost() ? boost + 1 : boost;
  }
  if ((k & KEY_Y) && !sh.on && last.ok && last.raced) startShow(&last, true);
  *kd &= ~(KEY_B | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_Y | KEY_L | KEY_R | KEY_START | KEY_SELECT);
}

/* ---------- the top screen ---------- */
static void tvBox(const char *a, const char *b) {   // the TV graphic in the corner: two lines of the pixel font
  int w = (int)fmax(da_textw(a, 1), da_textw(b, 1)) + 10;
  cv_fill(rgba(20, 16, 31, .82)); cv_rect(4, 4, w, 20);
  da_text(a, 9, 8, H(ffd21f), 1); da_text(b, 9, 15, H(f6f4fa), 1);
}
static void drawParade(double dt) {
  const Race *r = card.ok ? &card : last.ok ? &last : NULL;
  parade += dt * 120;
  DaScene s = { .t = tt, .dt = dt, .camX = parade, .finish = DA_RACE_LEN, .cheer = false };
  static char tagBuf[16];
  if (r) {
    s.nr = r->nh; s.nb = r->nh;
    for (int i = 0; i < r->nh; i++) {
      const DaHorse *h = &r->h[i]; double x = parade + 60 + i * 34 + sin(tt * 0.4 + i) * 6;
      bool mine = r == &card && i == sel;
      s.board[i] = (DaBoardRow){ h->num, h->name, h->odds, mine };
      if (mine) snprintf(tagBuf, sizeof tagBuf, "YOU #%d", h->num);
      s.r[i] = (DaRunner){ .x = x - parade, .y = DA_LANE0 + (h->num - 1) * DA_LANE, .phase = fmod(x / 52 + i * 0.3, 1), .h = i,
                           .mine = mine, .tag = mine ? tagBuf : NULL };
    }
  }
  da_scene(&s);
  char a[48], b[48]; s64 n = now();
  if (!have) { tvBox("OLD MAN DERBY", err[0] ? "OFF THE AIR" : "TUNING IN..."); return; }
  if (!on) { tvBox("OLD MAN DERBY", "THE TRACK OPENS SOON"); return; }
  if (card.ok) {
    s64 left = card.closes - n; int sec = (int)(left > 0 ? left / 1000 : 0);
    snprintf(a, sizeof a, "TODAY'S CARD - OFF AT 3PM");
    if (left > 0) snprintf(b, sizeof b, "%d:%02d:%02d TO GO", sec / 3600, sec / 60 % 60, sec % 60); else snprintf(b, sizeof b, "THE GATES ARE OPEN");
  } else {
    s64 left = opens - n; int sec = (int)(left > 0 ? left / 1000 : 0);
    snprintf(a, sizeof a, "TOMORROW'S CARD AT 4PM");
    if (left > 0) snprintf(b, sizeof b, "%d:%02d TO GO", sec / 60, sec % 60); else snprintf(b, sizeof b, "COMING OUT NOW...");
  }
  tvBox(a, b);
}
void derby_top(double dt) {
  const Race *r = sh.on ? &sh.race : card.ok ? &card : last.ok ? &last : NULL;
  da_theme(r ? r->theme : "", r ? r->id : 0);
  if (r) { DaBoardRow b[DA_NH]; boardOf(r, b, -1); da_set(r->h, r->nh, b, r->nh); }
  else da_set(NULL, 0, NULL, 0);
  da_build();
  if (sh.on) { sh.o = da_show_draw(&sh.v, showClock(), dt); sound(&sh.o, dt); }
  else drawParade(dt);
}

/* ---------- the bottom screen ---------- */
static void swatch(double x, double y, double sz, const DaHorse *h) {
  char n[4]; snprintf(n, sizeof n, "%d", h->num);
  cv_fill(h->look.silk1); cv_rect(x, y, sz, sz);
  cv_stroke_rect(x + 0.5, y + 0.5, sz - 1, sz - 1, rgba(0, 0, 0, .35), 1);
  cv_font(FONT_MONO); cv_text(n, x + sz / 2, y + sz / 2 + 0.5, sz * 0.62, h->look.silk2, AL_CENTER, BL_MIDDLE);
}
static void header(const char *title) {
  cv_fill(H(150c26)); cv_rect(0, 0, 320, 240);
  ui_heading(title, 12, 26, 20, AL_LEFT);
  idBack = ui_button((Box){ 250, 8, 62, 22 }, "BACK", BTN_RED, 10);
}
static void bottomCard(void) {
  header("DERBY");
  char b[200], hh[12] = ""; s64 left = card.closes - now(); int sec = (int)(left > 0 ? left / 1000 : 0);
  if (sec >= 3600) snprintf(hh, sizeof hh, "%d:", sec / 3600);
  if (left > 0) snprintf(b, sizeof b, "Off at 3pm · %s%02d:%02d to go · %d picked", hh, sec / 60 % 60, sec % 60, card.bettors);
  else snprintf(b, sizeof b, "The gates are open!");
  ui_text(b, 12, 40, 8, UI_CYAN, AL_LEFT);
  for (int i = 0; i < card.nh; i++) {
    const DaHorse *h = &card.h[i]; const Extra *x = &card.x[i];
    Box bx = { 8, 44 + i * 20, 304, 19 }; idRow[i] = ui_hot(bx);
    bool f = i == sel, mine = i == card.mine;
    if (f || mine) { cv_fill(f ? rgba(34, 230, 216, .12) : rgba(255, 210, 31, .10)); cv_rect(bx.x, bx.y, bx.w, bx.h); }
    if (f) cv_stroke_rect(bx.x + .5, bx.y + .5, bx.w - 1, bx.h - 1, UI_CYAN, 1);
    swatch(13, bx.y + 2, 15, h);
    ui_text(h->name, 34, bx.y + 9, 9, mine ? UI_GOLD : UI_BONE, AL_LEFT);
    cv_font(FONT_MONO); double tx = 38 + cv_text_width(h->name, 9);
    if (i == card.oldMan) { ui_text("OLD MAN'S PICK", tx, bx.y + 8, 6, UI_PINK, AL_LEFT); cv_font(FONT_MONO); tx += cv_text_width("OLD MAN'S PICK", 6) + 4; }
    if (mine) ui_text("YOUR PICK", tx, bx.y + 8, 6, UI_GOLD, AL_LEFT);
    char sub[200] = "";
    if (x->hasRec) { if (x->runs) snprintf(sub, sizeof sub, "%d win%s in %d", x->wins, x->wins == 1 ? "" : "s", x->runs); else snprintf(sub, sizeof sub, "first start"); }
    if (x->form[0]) { strncat(sub, sub[0] ? " · " : "", sizeof sub - strlen(sub) - 1); strncat(sub, x->form, sizeof sub - strlen(sub) - 1); }
    if (x->who[0]) { strncat(sub, sub[0] ? " · picked by " : "picked by ", sizeof sub - strlen(sub) - 1); strncat(sub, x->who, sizeof sub - strlen(sub) - 1); }
    ui_text(sub, 34, bx.y + 16.5, 6.5, UI_DIM, AL_LEFT);
    cv_font(FONT_DISP); cv_text(h->odds, 306, bx.y + 10, 10, UI_GOLD, AL_RIGHT, BL_MIDDLE);
  }
  // the boost
  int mb = maxBoost(); if (boost > mb) boost = mb;
  ui_text("BOOST", 12, 179, 8, UI_SUB, AL_LEFT);
  idMinus = ui_button((Box){ 52, 167, 22, 17 }, "-", BTN_MINI, 10);
  snprintf(b, sizeof b, "%d", boost); cv_font(FONT_DISP); cv_text(b, 90, 176, 12, boost ? UI_GOLD : UI_BONE, AL_CENTER, BL_MIDDLE);
  idPlus = ui_button((Box){ 106, 167, 22, 17 }, "+", BTN_MINI, 10);
  ui_text(boost ? "coins of your own" : "free pick", 134, 179, 7, UI_DIM, AL_LEFT);
  if (coins >= 0) { snprintf(b, sizeof b, "You have %s", coinsStr(coins)); ui_text(b, 308, 179, 8, UI_SUB, AL_RIGHT); }
  // what a win would pay
  if (msg[0] && osGetTime() - msgAt < 6000) ui_wrap(msg, 12, 188, 7.5, UI_PINK, 296);
  else {
    const DaHorse *h = &card.h[sel];
    snprintf(b, sizeof b, "If %s wins, you get to give the Old Man %s.%s", h->name, coinsStr(winFor(&card, sel, boost)),
             boost ? "" : " Losing costs you nothing.");
    if (boost) { char c[48]; snprintf(c, sizeof c, " If it loses, the house keeps your %s.", coinsStr(boost)); strncat(b, c, sizeof b - strlen(b) - 1); }
    ui_wrap(b, 12, 188, 7.5, UI_BONE, 296);
  }
  bool same = card.mine == sel && card.mineBoost == boost;
  const char *lbl = betting ? "..." : card.mine < 0 ? "PICK IT" : same ? "YOU'RE IN" : card.mine == sel ? "CHANGE BOOST" : "CHANGE PICK";
  idPick = ui_button((Box){ 8, 213, last.ok && last.raced ? 150 : 302, 22 }, lbl, same ? BTN_ALT : BTN_RED, 10);
  if (last.ok && last.raced) idWatch = ui_button((Box){ 166, 213, 144, 22 }, "Y: WATCH LAST RACE", BTN_ALT, 8);
  ui_set_focus(idPick);
}
static void bottomStable(void) {               // the hour after the race: the whole stable's record, best first
  header("DERBY");
  char b[120]; s64 left = opens - now(); int sec = (int)(left > 0 ? left / 1000 : 0);
  if (left > 0) snprintf(b, sizeof b, "Betting on tomorrow opens at 4pm · %d:%02d to go", sec / 60, sec % 60);
  else snprintf(b, sizeof b, "Tomorrow's card is coming out...");
  ui_text(b, 12, 40, 8, UI_CYAN, AL_LEFT);
  ui_text("HORSE", 26, 52, 6, UI_DIM, AL_LEFT); ui_text("W", 236, 52, 6, UI_DIM, AL_RIGHT); ui_text("TOP 3", 272, 52, 6, UI_DIM, AL_RIGHT); ui_text("RAN", 306, 52, 6, UI_DIM, AL_RIGHT);
  for (int i = 0; i < nstable; i++) {
    const StableRow *s = &stable[i]; double y = 62 + i * 9.5;
    cv_fill(s->silk); cv_rect(12, y - 6, 7, 7);
    ui_text(s->name, 26, y, 7, i ? UI_BONE : UI_GOLD, AL_LEFT);
    snprintf(b, sizeof b, "%d", s->wins); ui_text(b, 236, y, 7, UI_GOLD, AL_RIGHT);
    snprintf(b, sizeof b, "%d", s->places); ui_text(b, 272, y, 7, UI_BONE, AL_RIGHT);
    snprintf(b, sizeof b, "%d", s->runs); ui_text(b, 306, y, 7, UI_SUB, AL_RIGHT);
  }
  if (!nstable) ui_wrap("Tomorrow's card comes out at 4pm. Watch the race back and size up the stable.", 12, 60, 9, UI_SUB, 296);
  if (last.ok && last.raced) { idWatch = ui_button((Box){ 8, 216, 304, 20 }, "Y: WATCH TODAY'S RACE", BTN_GOLD, 10); ui_set_focus(idWatch); }
}
static void bottomShow(void) {
  const Race *r = &sh.race; char b[160];
  cv_fill(H(150c26)); cv_rect(0, 0, 320, 240);
  ui_kicker(sh.replay ? "THE TAPE · OLD MAN DERBY" : "LIVE · OLD MAN DERBY · 3PM", 12, 16, sh.replay ? UI_SUB : UI_PINK);
  if (r->mine >= 0) {
    const DaHorse *h = &r->h[r->mine]; int pos = 0; for (int i = 0; i < r->nh; i++) if (sh.o.order[i] == r->mine) pos = i + 1;
    swatch(12, 24, 20, h);
    ui_text(h->name, 38, 35, 11, UI_GOLD, AL_LEFT);
    snprintf(b, sizeof b, sh.o.rs < 0 ? "Your pick, in the gate" : "Your pick, running %s", nth(pos)); ui_text(b, 38, 46, 8, UI_SUB, AL_LEFT);
  } else ui_text("You had no pick in this one.", 12, 40, 9, UI_SUB, AL_LEFT);
  for (int i = 0; i < r->nh; i++) {
    int k = sh.o.order[i]; const DaHorse *h = &r->h[k]; double y = 60 + i * 23;
    if (k == r->mine) { cv_fill(rgba(255, 210, 31, .12)); cv_rect(8, y, 304, 21); }
    snprintf(b, sizeof b, "%d", i + 1); cv_font(FONT_DISP); cv_text(b, 22, y + 11, 12, i ? UI_DIM : UI_GOLD, AL_CENTER, BL_MIDDLE);
    swatch(36, y + 3, 15, h);
    ui_text(h->name, 58, y + 14, 9, k == r->mine ? UI_GOLD : UI_BONE, AL_LEFT);
    if (k == r->oldMan) { cv_font(FONT_MONO); ui_text("OLD MAN", 62 + cv_text_width(h->name, 9), y + 13, 6, UI_PINK, AL_LEFT); }
    ui_text(h->odds, 306, y + 14, 9, UI_GOLD, AL_RIGHT);
  }
  ui_text("B skips to the card", 160, 234, 7, UI_DIM, AL_CENTER);
}
static void bottomResults(void) {
  const Race *r = &sh.race; char b[200];
  cv_fill(H(150c26)); cv_rect(0, 0, 320, 240);
  ui_heading("RESULT", 12, 24, 18, AL_LEFT);
  for (int i = 0; i < r->nh; i++) {
    int k = r->order[i]; const DaHorse *h = &r->h[k]; double y = 30 + i * 17;
    if (k == r->mine) { cv_fill(rgba(255, 210, 31, .12)); cv_rect(8, y, 304, 16); }
    snprintf(b, sizeof b, "%d", i + 1); ui_text(b, 16, y + 11, 9, i ? UI_DIM : UI_GOLD, AL_LEFT);
    swatch(28, y + 2, 12, h);
    ui_text(h->name, 46, y + 11, 8, k == r->mine ? UI_GOLD : UI_BONE, AL_LEFT);
    ui_text(h->odds, 196, y + 11, 8, UI_GOLD, AL_RIGHT);
    ui_text(r->x[k].who, 204, y + 11, 6, UI_SUB, AL_LEFT);
  }
  b[0] = 0;
  const DaHorse *w = &r->h[r->order[0]];
  if (r->mine >= 0) {
    int place = 0; for (int i = 0; i < r->nh; i++) if (r->order[i] == r->mine) place = i + 1;
    if (place == 1) snprintf(b, sizeof b, "%s came in! You get to give the Old Man %s.", w->name, coinsStr(r->payout >= 0 ? r->payout : winFor(r, r->mine, r->mineBoost)));
    else snprintf(b, sizeof b, "%s came in %s.%s%s%s", r->h[r->mine].name, nth(place), r->mineBoost ? " The house keeps your " : " No harm done. It was free.",
                  r->mineBoost ? coinsStr(r->mineBoost) : "", r->mineBoost ? "." : "");
  } else if (!sh.replay) snprintf(b, sizeof b, "You had no pick in this one. Tomorrow's card comes out at 4pm.");
  if (b[0]) ui_wrap(b, 12, 136, 9, UI_GOLD, 296);
  if (r->oldMan >= 0) {
    const DaHorse *om = &r->h[r->oldMan]; int place = 0; for (int i = 0; i < r->nh; i++) if (r->order[i] == r->oldMan) place = i + 1;
    if (r->oldManWon) snprintf(b, sizeof b, "The Old Man's %s won at %s! He's buying a round: %s for everyone who rode with him.", om->name, om->odds, coinsStr(roundBonus));
    else snprintf(b, sizeof b, "The Old Man's %s finished %s. He says the track was wet.", om->name, nth(place));
    ui_wrap(b, 12, 170, 8, UI_SUB, 296);
  }
  idAgain = ui_button((Box){ 8, 212, 150, 22 }, "WATCH AGAIN", BTN_ALT, 10);
  idCard = ui_button((Box){ 166, 212, 146, 22 }, "BACK TO THE CARD", BTN_RED, 9);
  ui_set_focus(idCard);
}
void derby_bottom(void) {
  idBack = idPick = idWatch = idRetry = idAgain = idCard = idMinus = idPlus = -1;
  for (int i = 0; i < DA_NH; i++) idRow[i] = -1;
  if (sh.on) { if (sh.results) bottomResults(); else bottomShow(); return; }
  if (!have) {
    header("DERBY");
    ui_wrap(err[0] ? err : "Calling the track...", 12, 50, 10, err[0] ? UI_PINK : UI_SUB, 296);
    if (err[0]) { idRetry = ui_button((Box){ 8, 212, 304, 22 }, "TRY AGAIN", BTN_CYAN, 10); ui_set_focus(idRetry); }
    return;
  }
  if (!on) { header("DERBY"); ui_wrap("The track opens soon. Check back, sonny.", 12, 50, 10, UI_SUB, 296); ui_set_focus(idBack); return; }
  if (card.ok) bottomCard(); else bottomStable();
}
void derby_pressed(int p) {
  if (p < 0) return;
  if (p == idBack) derby_close();
  else if (p == idRetry) { err[0] = 0; ask(); }
  else if (p == idPick) place();
  else if (p == idWatch && last.ok && last.raced) startShow(&last, true);
  else if (p == idMinus) boost = boost > 0 ? boost - 1 : 0;
  else if (p == idPlus) boost = boost < maxBoost() ? boost + 1 : boost;
  else if (p == idAgain) startShow(&sh.race, true);
  else if (p == idCard) stopShow();
  else for (int i = 0; i < DA_NH; i++) if (p == idRow[i]) sel = i;
}

#include "sync.h"
#pragma GCC diagnostic ignored "-Wformat-truncation"   // names and paths are cut to fit on purpose
#include "net.h"
#include "ach.h"
#include "replay.h"
#include "sim.h"
#include "derby.h"
#include <3ds.h>
#include <jansson.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DIR_ "sdmc:/3ds/oldmanburn"
#define LINK_FILE DIR_ "/link.txt"
#define OUTBOX DIR_ "/outbox"
#define BOARD_FILE DIR_ "/board.json"
enum { K_STATE = 1, K_LINK, K_RUN, K_LOADOUT, K_WATCH, K_DAILY, K_DERBY };   // K_DERBY + n: the Derby's own requests (derby.c)
static char token[80], name[64], auth[96], status[64] = "Not linked to Discord";
static bool linked, haveClock, loadoutDirty, sending, derbyOn;
static int sWeek, sDay; static u64 lastState, nextRetry; static char sending_file[128];
bool ach_server[NACH];                                            // achievements the Discord profile has unlocked (ach.c reads it)

static void setStatus(const char *s) { snprintf(status, sizeof status, "%s", s); }
static void saveLink(void) {
  mkdir("sdmc:/3ds", 0777); mkdir(DIR_, 0777);
  FILE *f = fopen(LINK_FILE, "w"); if (!f) return;
  if (linked) fprintf(f, "token %s\nname %s\n", token, name);
  if (strcmp(net_server(), "https://oldmanburn-production.up.railway.app")) fprintf(f, "server %s\n", net_server());
  fclose(f);
}
static void loadLink(void) {
  FILE *f = fopen(LINK_FILE, "r"); if (!f) return;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "token ", 6)) { snprintf(token, sizeof token, "%s", line + 6); linked = token[0] != 0; }
    else if (!strncmp(line, "name ", 5)) snprintf(name, sizeof name, "%s", line + 5);
    else if (!strncmp(line, "server ", 7)) net_set_server(line + 7);   // for testing against another server
  }
  fclose(f);
  snprintf(auth, sizeof auth, "Device %s", token);
}
static void loadBoards(void);
static void askState(void) { if (linked && net_send(K_STATE, "GET", "/api/3ds/state", NULL, auth)) { lastState = osGetTime(); setStatus("Syncing with Discord..."); } }

void sync_init(void) {
  net_init(); loadLink(); if (linked) loadBoards();
  if (linked) { setStatus("Linked, connecting..."); askState(); }
}
void sync_exit(void) { net_exit(); }
bool sync_linked(void) { return linked; }
const char *sync_name(void) { return name; }
const char *sync_status(void) { return status; }
bool sync_clock(int *week, int *day) {
  if (!haveClock) return false;
  *week = sWeek; *day = sDay; return true;
}

/* ---------- the profile from Discord ---------- */
static void applyProfile(json_t *p) {
  json_t *un = json_object_get(p, "unlocked");
  if (json_is_array(un)) {
    memset(ach_server, 0, sizeof ach_server);
    size_t i; json_t *v;
    json_array_foreach(un, i, v) { const char *id = json_string_value(v); if (id) for (int a = 0; a < NACH; a++) if (!strcmp(ACHS[a].id, id)) ach_server[a] = true; }
  }
  json_t *lo = json_object_get(p, "loadout");
  if (json_is_object(lo) && !loadoutDirty) {                       // the look follows you, unless you just changed it here
    for (int s = 0; s < NSLOT; s++) {
      const char *id = json_string_value(json_object_get(lo, SLOT_IDS[s]));
      look_set(s, id && look_known(s, id) ? id : "");
    }
    ach_save();
  }
}
/* ---------- the leaderboards from Discord (kept on the SD card, so they still show offline) ---------- */
static BoardRow boards[2][BOARD_N]; static int nBoard[2] = { -1, -1 }, boardWeek[2], sPot; static char myId[24]; static bool dailyCanWatch; static int dailyTries = -1;
static void readBoard(int k, json_t *a, int id) {
  if (!json_is_array(a)) return;
  size_t i; json_t *v; nBoard[k] = 0; boardWeek[k] = id;
  json_array_foreach(a, i, v) {
    if (nBoard[k] >= BOARD_N) break;
    BoardRow *r = &boards[k][nBoard[k]++];
    const char *nm = json_string_value(json_object_get(v, "name")), *src = json_string_value(json_object_get(v, "src")),
               *uid = json_string_value(json_object_get(v, "user_id"));
    snprintf(r->name, sizeof r->name, "%.12s", nm ? nm : "Rider");   // 12 letters, like the Discord board
    r->score = json_integer_value(json_object_get(v, "score"));
    r->ds = src && !strcmp(src, "3ds"); r->me = uid && myId[0] && !strcmp(uid, myId);
    const char *rp = json_string_value(json_object_get(v, "replay"));
    snprintf(r->replay, sizeof r->replay, "%s", rp && strlen(rp) < sizeof r->replay ? rp : "");
  }
}
static void applyBoards(json_t *j) {
  const char *id = json_string_value(json_object_get(j, "id")); if (id) snprintf(myId, sizeof myId, "%s", id);
  json_t *w = json_object_get(j, "week"), *d = json_object_get(j, "day");
  if (!json_is_integer(w) || !json_is_integer(d)) return;
  readBoard(0, json_object_get(j, "board"), json_integer_value(w));
  readBoard(1, json_object_get(j, "daily_board"), json_integer_value(d));
  sPot = json_integer_value(json_object_get(j, "pot"));
  dailyCanWatch = json_is_true(json_object_get(j, "daily_can_watch"));
  json_t *dt = json_object_get(j, "daily_tries_left"); dailyTries = json_is_integer(dt) ? (int)json_integer_value(dt) : -1;
}
static void saveBoards(json_t *j) {
  json_t *b = json_object();
  const char *keys[] = { "id", "week", "day", "board", "daily_board", "pot", "daily_can_watch", "daily_tries_left" };
  for (int i = 0; i < 8; i++) { json_t *v = json_object_get(j, keys[i]); if (v) json_object_set(b, keys[i], v); }
  json_dump_file(b, BOARD_FILE, JSON_COMPACT); json_decref(b);
}
static void loadBoards(void) {
  json_error_t err; json_t *j = json_load_file(BOARD_FILE, 0, &err);
  if (j) { applyBoards(j); json_decref(j); }
}
int sync_board(bool daily, int id, const BoardRow **rows, int *pot) {
  int k = daily;
  if (!linked || nBoard[k] < 0 || boardWeek[k] != id) return -1;   // none yet, or a saved one from a week that's over
  *rows = boards[k]; *pot = sPot; return nBoard[k];
}
bool sync_daily_can_watch(void) { return dailyCanWatch; }
int sync_daily_tries(void) { return linked && nBoard[1] >= 0 && boardWeek[1] == sDay ? dailyTries : -1; }

/* ---------- starting a Daily Dash: POST /api/3ds/daily/start takes one of today's tries and gives its token ---------- */
static int dailyAsk; static bool dailyFlight; static char dailyMsg[96];   // 0 nothing asked, 1 waiting, 2 yes, -1 no; a request out
static char heldTok[72], rideTok[72]; static int heldDay = -1;          // a try taken but not ridden yet; the try this ride uses
static void dailyNo(const char *m) { dailyAsk = -1; snprintf(dailyMsg, sizeof dailyMsg, "%s", m); }
void sync_daily_start(void) {
  if (heldTok[0] && heldDay == sDay) { dailyAsk = 2; return; }    // taken earlier, and the wait for it was cancelled
  heldTok[0] = 0;
  if (!linked) { dailyNo("Link to Discord (Settings) to ride the Daily Dash"); return; }
  dailyAsk = 1;
  if (dailyFlight) return;                                         // the last ask is still out: its answer will do
  if (net_send(K_DAILY, "POST", "/api/3ds/daily/start", "{}", auth)) dailyFlight = true;
  else dailyNo("No Wi-Fi: the Daily Dash needs it to check your tries");
}
int sync_daily_poll(const char **msg) {
  int s = dailyAsk;
  if (s == 2) { snprintf(rideTok, sizeof rideTok, "%s", heldTok); heldTok[0] = 0; dailyAsk = 0; return 1; }
  if (s == -1) { *msg = dailyMsg; dailyAsk = 0; return -1; }
  return 0;
}
void sync_daily_cancel(void) { if (dailyAsk == 1) dailyAsk = 0; }

/* ---------- watching a run from the board: GET /api/3ds/board-replay/<id>, notes 10 a second like our own ---------- */
static int watchState; static Watched *watched; static char watchMsg[96], watchId[12];   // 0 nothing asked, 1 loading, 2 in, -1 failed
void sync_watch_free(Watched *w) { if (w) { free(w->f); free(w); } }
void sync_watch_cancel(void) { if (watchState == 2) sync_watch_free(watched); watched = NULL; watchState = 0; }
void sync_watch_fetch(const char *id) {
  sync_watch_cancel();
  char path[64]; snprintf(path, sizeof path, "/api/3ds/board-replay/%.11s", id);
  snprintf(watchId, sizeof watchId, "%.11s", id);
  if (linked && net_send(K_WATCH, "GET", path, NULL, auth)) watchState = 1;
  else { watchState = -1; snprintf(watchMsg, sizeof watchMsg, "No connection. Is Wi-Fi on?"); }
}
int sync_watch_poll(Watched **out, const char **msg) {
  int s = watchState;
  if (s == 2) { *out = watched; watched = NULL; watchState = 0; return 1; }
  if (s == -1) { *msg = watchMsg; watchState = 0; return -1; }
  return 0;
}
static const char *ENDS[4] = { "", "behind", "ahead", "rail" };
static Watched *readWatched(json_t *j) {
  json_t *fr = json_object_get(j, "frames");
  size_t n = json_array_size(fr); if (n < 2) return NULL;
  Watched *w = calloc(1, sizeof *w); if (!w) return NULL;
  w->f = malloc(n * sizeof *w->f); if (!w->f) { free(w); return NULL; }
  w->seed = json_integer_value(json_object_get(j, "seed")); w->score = json_integer_value(json_object_get(j, "score"));
  const char *nm = json_string_value(json_object_get(j, "name")), *src = json_string_value(json_object_get(j, "src")),
             *end = json_string_value(json_object_get(j, "end"));
  snprintf(w->name, sizeof w->name, "%.12s", nm ? nm : "Somebody");
  w->ds = src && !strcmp(src, "3ds");
  w->end = 3; for (int i = 1; i < 4; i++) if (end && !strcmp(end, ENDS[i])) w->end = i;   // CAUSE_*; no end: the rail, as on the web
  json_t *lo = json_object_get(j, "look");
  for (int s = 0; s < NSLOT; s++) {
    const char *id = json_string_value(json_object_get(lo, SLOT_IDS[s]));
    snprintf(w->look[s], sizeof w->look[s], "%s", id && look_known(s, id) ? id : "");
  }
  size_t i; json_t *v; double t0 = 0;
  json_array_foreach(fr, i, v) {                                  // time from 0 and only ever forward, like the web's frames3ds
    if (json_array_size(v) != 6) continue;
    float x[6]; for (int k = 0; k < 6; k++) x[k] = (float)json_number_value(json_array_get(v, k));
    if (!w->n) t0 = x[0];
    x[0] -= t0;
    if (w->n && x[0] <= w->f[w->n - 1].t) continue;
    w->f[w->n++] = (RecFrame){ x[0], x[1], x[2], x[3], x[4], x[5] };
  }
  if (w->n < 2) { sync_watch_free(w); return NULL; }
  return w;
}

static void applyState(json_t *j) {
  json_t *w = json_object_get(j, "week"), *d = json_object_get(j, "day");
  if (json_is_integer(w) && json_is_integer(d)) { sWeek = json_integer_value(w); sDay = json_integer_value(d); haveClock = true; }
  const char *nm = json_string_value(json_object_get(j, "name"));
  if (nm && strcmp(nm, name)) { snprintf(name, sizeof name, "%s", nm); saveLink(); }
  json_t *p = json_object_get(j, "profile"); if (json_is_object(p)) applyProfile(p);
  if (json_is_array(json_object_get(j, "board"))) { applyBoards(j); saveBoards(j); }
  derbyOn = json_is_true(json_object_get(j, "derby_on"));
}

/* ---------- the outbox: one JSON file per finished run, sent oldest first ---------- */
static bool nextOutbox(char *out, size_t n) {
  DIR *dir = opendir(OUTBOX); if (!dir) return false;
  struct dirent *e; char best[64] = "";
  while ((e = readdir(dir))) if (strstr(e->d_name, ".json") && (!best[0] || strcmp(e->d_name, best) < 0)) snprintf(best, sizeof best, "%s", e->d_name);
  closedir(dir);
  if (!best[0]) return false;
  snprintf(out, n, OUTBOX "/%s", best); return true;
}
static void sendOutbox(void) {
  if (!linked || sending || osGetTime() < nextRetry) return;
  char path[128]; if (!nextOutbox(path, sizeof path)) return;
  FILE *f = fopen(path, "rb"); if (!f) return;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *body = malloc(n + 1); if (!body) { fclose(f); return; }
  fread(body, 1, n, f); body[n] = 0; fclose(f);
  if (net_send(K_RUN, "POST", "/api/3ds/run", body, auth)) { sending = true; snprintf(sending_file, sizeof sending_file, "%s", path); setStatus("Uploading your ride..."); }
  free(body);
}
void sync_run(void) {
  if (!linked) return;
  json_t *j = json_object();
  json_object_set_new(j, "seed", json_integer(rec.seed));
  json_object_set_new(j, "mode", json_string(rec.mode == RM_DAILY ? "daily" : rec.mode == RM_OLD ? "old" : "week"));
  json_object_set_new(j, "score", json_integer(rec.score));
  json_object_set_new(j, "elapsed", json_real(rec.n ? rec.f[rec.n - 1].t : 0));
  if (rec.end > 0 && rec.end < 4) json_object_set_new(j, "end", json_string(ENDS[rec.end]));
  json_t *ex = json_object(); ach_extras(ex); json_object_set_new(j, "extras", ex);
  json_t *lo = json_object(); for (int s = 0; s < NSLOT; s++) if (rec.look[s][0]) json_object_set_new(lo, SLOT_IDS[s], json_string(rec.look[s]));
  json_object_set_new(j, "look", lo);
  json_t *fr = json_array();
  for (int i = 0; i < rec.n; i++) {
    const RecFrame *f = &rec.f[i]; json_t *a = json_array();
    json_array_append_new(a, json_real(floor(f->t * 1000 + .5) / 1000)); json_array_append_new(a, json_real(floor(f->cam * 100 + .5) / 100));
    json_array_append_new(a, json_real(floor(f->bx * 100 + .5) / 100)); json_array_append_new(a, json_real(floor(f->y * 100 + .5) / 100));
    json_array_append_new(a, json_real(floor(f->thr * 100 + .5) / 100)); json_array_append_new(a, json_real(floor(f->wheelie * 100 + .5) / 100));
    json_array_append_new(fr, a);
  }
  json_object_set_new(j, "frames", fr);
  if (rec.mode == RM_DAILY && rideTok[0]) json_object_set_new(j, "daily_token", json_string(rideTok));   // the try it used
  rideTok[0] = 0;
  mkdir(OUTBOX, 0777);
  char path[128]; snprintf(path, sizeof path, OUTBOX "/%016llx.json", (unsigned long long)osGetTime());
  json_dump_file(j, path, JSON_COMPACT | JSON_REAL_PRECISION(10));
  json_decref(j);
  nextRetry = 0; sendOutbox();
}

/* ---------- linking ---------- */
void sync_link_prompt(void) {
  SwkbdState kb; char code[8] = "";
  swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 6);
  swkbdSetHintText(&kb, "The 6-digit code from Discord");
  swkbdSetValidation(&kb, SWKBD_FIXEDLEN, 0, 0);
  if (swkbdInputText(&kb, code, sizeof code) != SWKBD_BUTTON_CONFIRM || strlen(code) != 6) return;
  char body[96]; snprintf(body, sizeof body, "{\"code\":\"%s\",\"device\":\"3DS\"}", code);
  if (net_send(K_LINK, "POST", "/api/3ds/link", body, NULL)) setStatus("Linking...");
  else setStatus("No connection. Is Wi-Fi on?");
}
void sync_unlink(void) {
  linked = false; token[0] = name[0] = 0; haveClock = false;
  memset(ach_server, 0, sizeof ach_server); saveLink(); setStatus("Not linked to Discord");
  nBoard[0] = nBoard[1] = -1; myId[0] = 0; dailyCanWatch = false; dailyTries = -1; remove(BOARD_FILE);
  heldTok[0] = 0; derbyOn = false;
}
void sync_loadout_changed(void) {
  if (!linked) return;
  json_t *j = json_object(), *lo = json_object();
  for (int s = 0; s < NSLOT; s++) if (*look_get(s)) json_object_set_new(lo, SLOT_IDS[s], json_string(look_get(s)));
  json_object_set_new(j, "loadout", lo);
  char *b = json_dumps(j, JSON_COMPACT); json_decref(j);
  loadoutDirty = true;
  if (b) { net_send(K_LOADOUT, "POST", "/api/3ds/loadout", b, auth); free(b); }
}

/* ---------- the Derby (derby.c): its requests go out with this console's link ---------- */
bool sync_derby_on(void) { return linked && derbyOn; }
const char *sync_my_id(void) { return myId; }
bool sync_derby_send(int what, const char *method, const char *path, const char *body) {
  return linked && net_send(K_DERBY + what, method, path, body, auth);
}

static const char *detail(json_t *j, const char *dflt) { const char *d = json_string_value(json_object_get(j, "detail")); return d ? d : dflt; }
void sync_tick(bool menus) {
  if (!menus) return;                                              // mid-ride, replies wait in the queue: handling one writes the SD card (80+ ms on an Old 3DS)
  NetResult r;
  while (net_poll(&r)) {
    json_error_t err; json_t *j = json_loads(r.body, 0, &err);
    bool ok = r.status >= 200 && r.status < 300;
    if (r.kind == K_LINK) {
      const char *t = json_string_value(json_object_get(j, "token")), *nm = json_string_value(json_object_get(j, "name"));
      if (ok && t) {
        snprintf(token, sizeof token, "%s", t); snprintf(name, sizeof name, "%s", nm ? nm : "Discord"); linked = true;
        snprintf(auth, sizeof auth, "Device %s", token); saveLink(); askState();
      } else setStatus(r.status ? detail(j, "That code didn't work. Get a new one in Discord.") : "No connection. Is Wi-Fi on?");
    } else if (r.kind == K_STATE) {
      if (ok && j) { applyState(j); setStatus("Synced with Discord"); sendOutbox(); }
      else if (r.status == 401) { sync_unlink(); setStatus("Unlinked by Discord. Link again in Settings."); }
      else setStatus("Offline: rides wait on the SD card");
    } else if (r.kind == K_RUN) {
      sending = false;
      if (ok) {
        remove(sending_file); setStatus("Sent to Discord: it's in 3DS RIDES");
        json_t *dd = json_object_get(j, "daily");                 // a Daily Dash ride that used one of today's tries
        if (json_is_object(dd)) {
          dailyTries = (int)json_integer_value(json_object_get(dd, "tries_left"));
          if (dailyTries <= 0) dailyCanWatch = true;
          char m[64]; snprintf(m, sizeof m, json_is_true(json_object_get(dd, "accepted")) ? "On today's Daily Dash board · %d %s left" : "Sent · %d %s left",
                               dailyTries, dailyTries == 1 ? "try" : "tries");
          setStatus(m);
        }
        lastState = 0;                                             // fetch the board again: this ride may be on it
        json_t *p = json_object_get(j, "profile"); if (json_is_object(p)) applyProfile(p);
        sendOutbox();
      } else if (r.status >= 400 && r.status < 500 && r.status != 401 && r.status != 429) {   // the server won't take it: don't retry forever
        remove(sending_file); setStatus("Discord didn't take that ride");
      } else { nextRetry = osGetTime() + 60000; setStatus("Offline: rides wait on the SD card"); }
    } else if (r.kind == K_WATCH) {
      const char *got = json_string_value(json_object_get(j, "id"));
      if (watchState == 1 && (!ok || (got && !strcmp(got, watchId)))) {   // still the run wanted (not cancelled, not an older ask)
        Watched *w = ok && j ? readWatched(j) : NULL;
        if (w) { watched = w; watchState = 2; }
        else {
          watchState = -1;
          snprintf(watchMsg, sizeof watchMsg, "%s", ok ? "That run has nothing to watch." : r.status == 0 ? "No connection. Is Wi-Fi on?"
                   : detail(j, "Couldn't load that run."));
        }
      }
    } else if (r.kind == K_DAILY) {
      dailyFlight = false;
      const char *t = json_string_value(json_object_get(j, "daily_token"));
      if (ok && t) {
        json_t *dd = json_object_get(j, "day");
        snprintf(heldTok, sizeof heldTok, "%s", t); heldDay = json_is_integer(dd) ? (int)json_integer_value(dd) : sDay;
        if (haveClock && heldDay != sDay) { sDay = heldDay; lastState = 0; }   // noon just passed: ride the server's new day
        dailyTries = (int)json_integer_value(json_object_get(j, "tries_left"));
        if (dailyAsk == 1) dailyAsk = 2;
      } else {
        if (r.status == 402) { dailyTries = 0; dailyCanWatch = true; }
        if (dailyAsk == 1) dailyNo(r.status == 0 ? "No Wi-Fi: the Daily Dash needs it to check your tries"
                                   : r.status == 402 ? "No Daily Dash tries left today. New map at noon Pacific"
                                   : detail(j, "Couldn't start the Daily Dash. Try again."));
      }
    } else if (r.kind == K_LOADOUT) {
      if (ok) loadoutDirty = false;
    } else if (r.kind >= K_DERBY) {
      if (r.status == 401) { sync_unlink(); setStatus("Unlinked by Discord. Link again in Settings."); }
      derby_reply(r.kind - K_DERBY, r.status, j);
    }
    if (j) json_decref(j);
    free(r.body);
  }
  if (linked && menus && !net_busy() && osGetTime() - lastState > 300000) askState();   // every five minutes on the menus
  if (linked && menus && !net_busy()) sendOutbox();
}

#include "sync.h"
#pragma GCC diagnostic ignored "-Wformat-truncation"   // names and paths are cut to fit on purpose
#include "net.h"
#include "ach.h"
#include "replay.h"
#include "sim.h"
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
enum { K_STATE = 1, K_LINK, K_RUN, K_LOADOUT };
static char token[80], name[64], auth[96], status[64] = "Not linked to Discord";
static bool linked, haveClock, loadoutDirty, sending;
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
static void askState(void) { if (linked && net_send(K_STATE, "GET", "/api/3ds/state", NULL, auth)) { lastState = osGetTime(); setStatus("Syncing with Discord..."); } }

void sync_init(void) {
  net_init(); loadLink();
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
static void applyState(json_t *j) {
  json_t *w = json_object_get(j, "week"), *d = json_object_get(j, "day");
  if (json_is_integer(w) && json_is_integer(d)) { sWeek = json_integer_value(w); sDay = json_integer_value(d); haveClock = true; }
  const char *nm = json_string_value(json_object_get(j, "name"));
  if (nm && strcmp(nm, name)) { snprintf(name, sizeof name, "%s", nm); saveLink(); }
  json_t *p = json_object_get(j, "profile"); if (json_is_object(p)) applyProfile(p);
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
static const char *ENDS[4] = { "", "behind", "ahead", "rail" };
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

static const char *detail(json_t *j, const char *dflt) { const char *d = json_string_value(json_object_get(j, "detail")); return d ? d : dflt; }
void sync_tick(bool menus) {
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
        json_t *p = json_object_get(j, "profile"); if (json_is_object(p)) applyProfile(p);
        sendOutbox();
      } else if (r.status >= 400 && r.status < 500 && r.status != 401 && r.status != 429) {   // the server won't take it: don't retry forever
        remove(sending_file); setStatus("Discord didn't take that ride");
      } else { nextRetry = osGetTime() + 60000; setStatus("Offline: rides wait on the SD card"); }
    } else if (r.kind == K_LOADOUT) {
      if (ok) loadoutDirty = false;
    }
    if (j) json_decref(j);
    free(r.body);
  }
  if (linked && menus && !net_busy() && osGetTime() - lastState > 300000) askState();   // every five minutes on the menus
  if (linked && menus && !net_busy()) sendOutbox();
}

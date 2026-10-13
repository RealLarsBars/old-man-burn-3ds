#include "ach.h"
#include "sim.h"
#include "sync.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <jansson.h>

#define PROFILE "sdmc:/3ds/oldmanburn/profile.txt"
double stats[NSTAT];
static double cosM[NITEMS];
int ach_new[NACH];
static bool doneBefore[NACH];

bool ach_done(int a) { return stats[ACHS[a].stat] >= ACHS[a].goal || ach_server[a]; }
int ach_count(void) { int n = 0; for (int a = 0; a < NACH; a++) n += ach_done(a); return n; }
int item_index(int slot, const char *id) { for (int i = 0; i < NITEMS; i++) if (ITEMS[i].slot == slot && !strcmp(ITEMS[i].id, id)) return i; return -1; }
const char *item_name(int slot, const char *id) { int i = item_index(slot, id); return i < 0 ? "Original" : ITEMS[i].name; }
bool item_owned(int slot, const char *id) {
  if (!*id) return true;
#ifdef UNLOCK_ALL                                                  // test builds: try every cosmetic
  return item_index(slot, id) >= 0;
#endif
  for (int a = 0; a < NACH; a++) if (ach_done(a)) for (int r = 0; r < ACHS[a].nrw; r++) if (ACHS[a].rw[r].slot == slot && !strcmp(ACHS[a].rw[r].item, id)) return true;
  return false;
}
int slot_total(int slot) { int n = 0; for (int i = 0; i < NITEMS; i++) n += ITEMS[i].slot == slot && look_known(slot, ITEMS[i].id); return n; }
int slot_owned(int slot) { int n = 0; for (int i = 0; i < NITEMS; i++) n += ITEMS[i].slot == slot && look_known(slot, ITEMS[i].id) && item_owned(slot, ITEMS[i].id); return n; }
void ach_wear(int slot, int dir) {                               // cycle through "" and the owned items, in the list's order
  int cur = item_index(slot, look_get(slot)), i = cur;
  for (int k = 0; k <= NITEMS; k++) {
    i += dir; if (i >= NITEMS) i = -1; if (i < -1) i = NITEMS - 1;
    if (i == -1) { look_set(slot, ""); break; }
    if (ITEMS[i].slot == slot && look_known(slot, ITEMS[i].id) && item_owned(slot, ITEMS[i].id)) { look_set(slot, ITEMS[i].id); break; }
  }
  ach_save(); sync_loadout_changed();
}
double cos_meters(int slot) { int i = item_index(slot, look_get(slot)); return i < 0 ? 0 : cosM[i]; }

void ach_save(void) {
  mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/oldmanburn", 0777);
  FILE *f = fopen(PROFILE, "w"); if (!f) return;
  for (int s = 0; s < NSTAT; s++) if (stats[s]) fprintf(f, "stat %s %.1f\n", STAT_IDS[s], stats[s]);
  for (int s = 0; s < NSLOT; s++) if (*look_get(s)) fprintf(f, "wear %s %s\n", SLOT_IDS[s], look_get(s));
  for (int i = 0; i < NITEMS; i++) if (cosM[i]) fprintf(f, "cos %s %s %.0f\n", SLOT_IDS[ITEMS[i].slot], ITEMS[i].id, cosM[i]);
  fclose(f);
}
static int slotOf(const char *s) { for (int i = 0; i < NSLOT; i++) if (!strcmp(SLOT_IDS[i], s)) return i; return -1; }
void ach_load(void) {
  FILE *f = fopen(PROFILE, "r");
  char k[16], a[32], b[32]; double v;
  if (f) {
    while (fscanf(f, "%15s", k) == 1) {
      if (!strcmp(k, "stat") && fscanf(f, "%31s %lf", a, &v) == 2) { for (int s = 0; s < NSTAT; s++) if (!strcmp(STAT_IDS[s], a)) stats[s] = v; }
      else if (!strcmp(k, "wear") && fscanf(f, "%31s %31s", a, b) == 2) { int s = slotOf(a); if (s >= 0 && look_known(s, b)) look_set(s, b); }
      else if (!strcmp(k, "cos") && fscanf(f, "%31s %31s %lf", a, b, &v) == 3) { int s = slotOf(a), i = s < 0 ? -1 : item_index(s, b); if (i >= 0) cosM[i] = v; }
      else { int c; while ((c = fgetc(f)) != '\n' && c != EOF) {} }
    }
    fclose(f);
  }
}
void ach_bests(const Best *bs, int n) {
  int weeks = 0, w5k = 0, days = 0, d2k = 0, hw = 0, xm = 0, streak = 0;
  static int dl[512]; int nd = 0;
  for (int i = 0; i < n; i++) {
    int s = bs[i].seed, b = bs[i].best; if (b <= 0) continue;
    if (s >= DAILY_BASE) { days++; d2k += b >= 2000; if (nd < 512) dl[nd++] = s - DAILY_BASE; }
    else { weeks++; w5k += b >= 5000; }
    int sn = seasonOf(s); hw |= sn == 1 && b >= 3000; xm |= sn == 2 && b >= 3000;
  }
  for (int i = 1; i < nd; i++) for (int j = i; j > 0 && dl[j] < dl[j - 1]; j--) { int t = dl[j]; dl[j] = dl[j - 1]; dl[j - 1] = t; }
  for (int i = 0, run = 0; i < nd; i++) { run = i && dl[i] == dl[i - 1] + 1 ? run + 1 : 1; if (run > streak) streak = run; }
  stats[ST_WEEKS_PLAYED] = weeks; stats[ST_WEEKS_5K] = w5k; stats[ST_DAILY_DAYS] = days; stats[ST_DAILY_2K_DAYS] = d2k;
  stats[ST_DAILY_STREAK] = streak; stats[ST_HALLOWEEN_3K] = hw; stats[ST_CHRISTMAS_3K] = xm;
}

/* ---------- counting a run, as trackRun() and localFinish() do on the web ---------- */
static struct { double wheelie, sand, clean; int gaps, rails, swerves, gi, ri, si, cause, dist; } ex;
void ach_run_start(void) {
  memset(&ex, 0, sizeof ex); ex.clean = -1;
  for (int a = 0; a < NACH; a++) doneBefore[a] = ach_done(a);
}
void ach_run_tick(double dt) {
  double wx = g.cam + g.bx;
  if (g.wheelie > 0.5) ex.wheelie += dt;
  if (!g.onRoad) { ex.sand += dt; if (ex.clean < 0) ex.clean = scoreOf(); }   // how far you got before the first touch of dirt
  while (ex.gi < gaps.n && gaps.a[ex.gi].x + gaps.a[ex.gi].len < wx) { ex.gaps++; ex.gi++; }
  while (ex.ri < nrails && rails[ex.ri].x + rails[ex.ri].len < wx) { ex.rails++; ex.ri++; }
  if (V2) { int s = swervesPassed(wx); if (s > ex.si) { ex.swerves += s - ex.si; ex.si = s; } }
}
static void maxTo(int s, double v) { if (v > stats[s]) stats[s] = v; }
int ach_run_end(int d, int cause, int seed, bool daily, bool oldMap, bool newBest, int season, const Best *b, int nb) {
  ex.cause = cause; ex.dist = d;
  if (d > 0) for (int s = 0; s < NSLOT; s++) { int i = item_index(s, look_get(s)); if (i >= 0) cosM[i] += d; }
  stats[ST_TOTAL_M] += d; maxTo(ST_BEST_M, d); stats[ST_RUNS]++;
  if (d >= 500) stats[ST_RIDES_500]++;
  if (newBest && d > 0) stats[ST_PBS]++;
  stats[ST_WHEELIE_S] += ex.wheelie; stats[ST_SAND_S] += ex.sand; stats[ST_GAPS] += ex.gaps; stats[ST_RAILS] += ex.rails; stats[ST_SWERVES] += ex.swerves;
  maxTo(ST_RUN_GAPS, ex.gaps); maxTo(ST_RUN_RAILS, ex.rails); maxTo(ST_RUN_WHEELIE, ex.wheelie); maxTo(ST_CLEAN_M, ex.clean < 0 ? d : ex.clean);
  if (d % 100 == 52) stats[ST_ENDS_52]++;
  if (cause == CAUSE_RAIL) stats[ST_CRASH_RAIL]++; else if (cause == CAUSE_BEHIND) stats[ST_CRASH_BEHIND]++; else if (cause == CAUSE_AHEAD) stats[ST_CRASH_AHEAD]++;
  if (daily) { stats[ST_DAILY_RUNS]++; maxTo(ST_DAILY_BEST, d); if (d >= 1000) stats[ST_DAILY_1K]++; }
  if (oldMap && d >= 3000) stats[ST_OLD_3K] = 1;
  if (season == 1) stats[ST_HALLOWEEN_RUNS]++; else if (season == 2) stats[ST_CHRISTMAS_RUNS]++;
  time_t now = time(NULL); struct tm *lt = localtime(&now); if (lt && lt->tm_hour < 4) stats[ST_NIGHT_RUNS]++;
  (void)seed; ach_bests(b, nb);
  ach_save();
  int n = 0; for (int a = 0; a < NACH; a++) if (ach_done(a) && !doneBefore[a]) { ach_new[n++] = a; doneBefore[a] = true; }
  return n;
}
void ach_extras(json_t *o) {
  static const char *C[4] = { "", "behind", "ahead", "rail" };
  json_object_set_new(o, "wheelie_s", json_integer((int)ex.wheelie)); json_object_set_new(o, "sand_s", json_integer((int)ex.sand));
  json_object_set_new(o, "gaps", json_integer(ex.gaps)); json_object_set_new(o, "rails", json_integer(ex.rails)); json_object_set_new(o, "swerves", json_integer(ex.swerves));
  json_object_set_new(o, "run_gaps", json_integer(ex.gaps)); json_object_set_new(o, "run_rails", json_integer(ex.rails)); json_object_set_new(o, "run_wheelie", json_integer((int)ex.wheelie));
  json_object_set_new(o, "clean_m", json_integer(ex.clean < 0 ? ex.dist : (int)ex.clean));
  if (ex.cause > 0 && ex.cause < 4) json_object_set_new(o, "cause", json_string(C[ex.cause]));
}

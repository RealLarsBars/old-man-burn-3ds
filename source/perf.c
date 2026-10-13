#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "perf.h"
#include "look.h"
#include "sim.h"
#include "audio.h"

#define LOG_FILE "sdmc:/3ds/oldmanburn/perf.txt"
#define LOG_OLD  "sdmc:/3ds/oldmanburn/perf-old.txt"
#define MAX_LOG  (256 * 1024)                 // past this the log moves to perf-old.txt and starts again
#define TICKS_MS (SYSCLOCK_ARM11 / 1000.0)
#define SLOW_MS  18.0                         // a frame longer than this missed the 60 Hz refresh
#define NWORST   8

typedef struct { double total, part[PF_N], cpu, gpu; int dist, st, parts, audioBufs; } Frame;
static u64 frameStart, lastMark;
static double partMs[PF_N];
static bool lastRiding;
static bool isNew3ds;
static char track[64]; static int seed, gapsAtStart;
static int frames, slow, dropped;
static double sumTotal, maxTotal, sumPart[PF_N], maxPart[PF_N], sumCpu, maxCpu, sumGpu, maxGpu;
static Frame worst[NWORST]; static int nworst;

void perf_init(void) {
  bool n = false; APT_CheckNew3DS(&n); isNew3ds = n;
  frameStart = lastMark = svcGetSystemTick();
}
void perf_mark(int part) { u64 t = svcGetSystemTick(); partMs[part] += (t - lastMark) / TICKS_MS; lastMark = t; }

static void keepWorst(const Frame *f) {
  int at;
  if (nworst < NWORST) at = nworst++;
  else if (worst[NWORST - 1].total < f->total) at = NWORST - 1;
  else return;
  worst[at] = *f;
  for (int i = at; i > 0 && worst[i].total > worst[i - 1].total; i--) { Frame t = worst[i]; worst[i] = worst[i - 1]; worst[i - 1] = t; }
}
void perf_frame(bool nowRiding, int audioBufs) {
  u64 t = svcGetSystemTick();
  if (lastRiding) {                            // the frame that just finished was part of a ride: count it
    Frame f = { (t - frameStart) / TICKS_MS, { 0 }, C3D_GetProcessingTime(), C3D_GetDrawingTime(), scoreOf(), st, g.nparts, audioBufs };
    memcpy(f.part, partMs, sizeof partMs);
    frames++; sumTotal += f.total; if (f.total > maxTotal) maxTotal = f.total;
    if (f.total > SLOW_MS) { slow++; dropped += (int)(f.total / 16.7); }
    for (int i = 0; i < PF_N; i++) { sumPart[i] += f.part[i]; if (f.part[i] > maxPart[i]) maxPart[i] = f.part[i]; }
    sumCpu += f.cpu; if (f.cpu > maxCpu) maxCpu = f.cpu; sumGpu += f.gpu; if (f.gpu > maxGpu) maxGpu = f.gpu;
    keepWorst(&f);
  }
  lastRiding = nowRiding;
  memset(partMs, 0, sizeof partMs);
  frameStart = lastMark = t;
}
void perf_ride_start(const char *name, int sd) {
  snprintf(track, sizeof track, "%s", name); seed = sd; gapsAtStart = audio_gaps();
  frames = slow = dropped = nworst = 0;
  sumTotal = maxTotal = sumCpu = maxCpu = sumGpu = maxGpu = 0;
  memset(sumPart, 0, sizeof sumPart); memset(maxPart, 0, sizeof maxPart);
}

static const char *ST_NAME[4] = { "title", "riding", "crash", "over" };
static const char *PART_NAME[PF_N] = { "game", "sound", "waiting", "drawing" };
void perf_ride_end(int score) {
  if (!frames) return;
  struct stat sb;
  if (stat(LOG_FILE, &sb) == 0 && sb.st_size > MAX_LOG) { remove(LOG_OLD); rename(LOG_FILE, LOG_OLD); }
  FILE *f = fopen(LOG_FILE, "a"); if (!f) return;
  long long ms = (long long)osGetTime() - 2208988800000LL; long long s = ms / 1000;   // console clock, 1900-based
  int day = (int)(s / 86400), hh = (int)(s % 86400 / 3600), mm = (int)(s % 3600 / 60);
  int z = day + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int mp = (5 * doy + 2) / 153, dd = doy - (153 * mp + 2) / 5 + 1, mo = mp < 10 ? mp + 3 : mp - 9, yy = yoe + era * 400 + (mo <= 2);
  char snd[24]; if (audio_core() >= 0) snprintf(snd, sizeof snd, "sound on core %d", audio_core()); else snprintf(snd, sizeof snd, "sound in main loop");
  fprintf(f, "== %04d-%02d-%02d %02d:%02d  %s  seed %d  %s  %s  score %d\n", yy, mo, dd, hh, mm, track, seed, isNew3ds ? "New 3DS" : "3DS", snd, score);
  fprintf(f, "look:"); int nl = 0; for (int s = 0; s < NSLOT; s++) if (*look_get(s)) { fprintf(f, " %s", look_get(s)); nl++; }
  fprintf(f, "%s\n", nl ? "" : " original");
  fprintf(f, "frames %d  slow %d (%.1f%%)  dropped %d  avg %.1f ms  worst %.1f ms  sound gaps %d\n", frames, slow, 100.0 * slow / frames, dropped,
    sumTotal / frames, maxTotal, audio_gaps() - gapsAtStart);
  fprintf(f, "avg/max ms:");
  for (int i = 0; i < PF_N; i++) fprintf(f, "  %s %.1f/%.1f", PART_NAME[i], sumPart[i] / frames, maxPart[i]);
  fprintf(f, "  cpu %.1f/%.1f  gpu %.1f/%.1f\n", sumCpu / frames, maxCpu, sumGpu / frames, maxGpu);
  if (slow) {
    fprintf(f, "slowest frames (ms):\n");
    for (int i = 0; i < nworst && worst[i].total > SLOW_MS; i++) {
      const Frame *w = &worst[i];
      fprintf(f, "  %.1f  game %.1f sound %.1f (%d buf) waiting %.1f drawing %.1f gpu %.1f  at %dm %s  particles %d\n", w->total,
        w->part[PF_SIM], w->part[PF_AUDIO], w->audioBufs, w->part[PF_WAIT], w->part[PF_DRAW], w->gpu, w->dist, ST_NAME[w->st & 3], w->parts);
    }
  }
  fputc('\n', f);
  fclose(f);
}

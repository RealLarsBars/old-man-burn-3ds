#include "update.h"
#include "net.h"
#include <3ds.h>
#include <curl/curl.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cacert_bin.h"

static Thread th; static LightEvent wake; static volatile bool quit, cancel, go, running;
static volatile int state = UP_IDLE, newer; static volatile u32 got, total;
static char failMsg[96], selfPath[128] = "sdmc:/3ds/OldManBurn.3dsx";
static bool cia, amOk;

int update_newer(void) { return newer; }
bool update_is_cia(void) { return cia; }
int update_state(float *progress, const char **msg) {
  if (progress) *progress = total ? (float)got / total : 0;
  if (msg) *msg = failMsg;
  return state;
}
static void fail(const char *m) { snprintf(failMsg, sizeof failMsg, "%s", m); state = UP_FAILED; }

static void setup(CURL *c, const char *path) {
  char url[256]; snprintf(url, sizeof url, "%s%s", net_server(), path);
  curl_easy_reset(c);
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "OldManBurn3DS/1");
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
  static struct curl_blob ca = { (void *)cacert_bin, 0, CURL_BLOB_NOCOPY }; ca.len = cacert_bin_size;
  curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
}

/* ---------- which build is out ---------- */
typedef struct { char p[512]; size_t n; } Small;
static size_t onSmall(char *d, size_t sz, size_t nm, void *u) {
  Small *s = u; size_t k = sz * nm; if (s->n + k >= sizeof s->p) return 0;
  memcpy(s->p + s->n, d, k); s->n += k; s->p[s->n] = 0; return k;
}
static void check(CURL *c) {
  Small s = { .n = 0 }; s.p[0] = 0;
  setup(c, "/api/3ds/update");
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onSmall);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
  if (curl_easy_perform(c) != CURLE_OK) return;                    // no Wi-Fi: just no offer this time
  json_error_t err; json_t *j = json_loads(s.p, 0, &err); if (!j) return;
  int b = (int)json_integer_value(json_object_get(j, "build"));
  total = (u32)json_integer_value(json_object_get(j, cia ? "cia" : "3dsx"));
  json_decref(j);
  if (b > BUILD_NUM && total) newer = b;
}

/* ---------- downloading it straight into the installer (.cia) or a new file next to the old one (.3dsx) ---------- */
typedef struct { Handle h; FILE *f; u64 off; } Sink;
static size_t onData(char *d, size_t sz, size_t nm, void *u) {
  Sink *s = u; size_t k = sz * nm;
  if (cancel || s->off + k > total) return 0;
  if (s->f) { if (fwrite(d, 1, k, s->f) != k) return 0; }
  else { u32 w = 0; if (R_FAILED(FSFILE_Write(s->h, &w, s->off, d, (u32)k, 0)) || w != k) return 0; }
  s->off += k; got = (u32)s->off; return k;
}
static void download(CURL *c) {
  got = 0; cancel = false;
  char part[140]; snprintf(part, sizeof part, "%s.part", selfPath);
  Sink s = { 0 };
  if (cia) {
    if (!amOk || R_FAILED(AM_StartCiaInstall(MEDIATYPE_SD, &s.h))) { fail("Couldn't start the installer."); return; }
  } else if (!(s.f = fopen(part, "wb"))) { fail("Couldn't write to the SD card."); return; }
  setup(c, cia ? "/api/3ds/update/OldManBurn.cia" : "/api/3ds/update/OldManBurn.3dsx");
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 600L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 256L);              // stalled for 30 s: give up rather than hang
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
  bool ok = curl_easy_perform(c) == CURLE_OK && s.off == total;
  if (cia) {
    if (ok && R_SUCCEEDED(AM_FinishCiaInstall(s.h))) { state = UP_DONE; return; }
    AM_CancelCIAInstall(s.h);                                      // the game stays exactly as it was
  } else {
    ok = fclose(s.f) == 0 && ok;
    if (ok) { remove(selfPath); ok = rename(part, selfPath) == 0; }
    if (ok) { state = UP_DONE; return; }
    remove(part);
  }
  fail(cancel ? "Update stopped. Nothing changed." : "The download didn't finish. Nothing changed; try again.");
}

static void loop(void *arg) {
  CURL *c = curl_easy_init();
  if (c) check(c);
  while (c && !quit) {
    LightEvent_Wait(&wake);
    if (quit) break;
    if (go) { go = false; download(c); }
  }
  if (c) curl_easy_cleanup(c);
}

void update_init(const char *self) {
  cia = !envIsHomebrew();
  if (self && strlen(self) < sizeof selfPath - 8 && strstr(self, ".3dsx")) snprintf(selfPath, sizeof selfPath, "%s", self);
  if (cia) amOk = R_SUCCEEDED(amInit());
  if (BUILD_NUM <= 0) return;                                      // local builds don't update themselves
  LightEvent_Init(&wake, RESET_ONESHOT);
  s32 prio = 0; svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  th = threadCreate(loop, NULL, 32 * 1024, prio + 2, -2, false);   // below the game, like the sync thread
  running = th != NULL;
}
void update_exit(void) {
  if (running) { quit = true; cancel = true; LightEvent_Signal(&wake); threadJoin(th, U64_MAX); threadFree(th); running = false; }
  if (amOk) { amExit(); amOk = false; }
}
void update_start(void) {
  if (!running || !newer || state == UP_DOWNLOADING || state == UP_DONE) return;
  failMsg[0] = 0; got = 0; state = UP_DOWNLOADING; go = true; LightEvent_Signal(&wake);
}
void update_cancel(void) { if (state == UP_DOWNLOADING) cancel = true; }
void update_restart(void) { if (cia && state == UP_DONE) aptSetChainloaderToSelf(); }

#include "net.h"
#include <3ds.h>
#include <curl/curl.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "cacert_bin.h"

#define SOC_SIZE 0x100000
#define QN 8
typedef struct { int kind; char method[8], path[96], auth[96]; char *body; } Job;
static Job jobs[QN]; static int jHead, jTail;                     // waiting requests
static NetResult done[QN]; static int dHead, dTail;               // finished ones
static LightLock lock; static LightEvent wake;
static Thread th; static volatile bool quit, running, working;
static u32 *socBuf; static bool socOk;
static char server[128] = "https://old-man-burn-production.up.railway.app";

void net_set_server(const char *url) { LightLock_Lock(&lock); strncpy(server, url, sizeof server - 1); LightLock_Unlock(&lock); }
const char *net_server(void) { return server; }

typedef struct { char *p; size_t n, cap; } Buf;
static size_t onData(char *d, size_t sz, size_t nm, void *u) {
  Buf *b = u; size_t k = sz * nm;
  if (b->n + k + 1 > 1 << 20) return 0;                            // a megabyte is far more than any answer
  if (b->n + k + 1 > b->cap) { b->cap = (b->n + k + 1) * 2; b->p = realloc(b->p, b->cap); }
  memcpy(b->p + b->n, d, k); b->n += k; b->p[b->n] = 0; return k;
}
static void runJob(CURL *c, Job *j, NetResult *r) {
  char url[256]; snprintf(url, sizeof url, "%s%s", server, j->path);
  Buf b = { 0 }; struct curl_slist *h = NULL; char ah[128];
  h = curl_slist_append(h, "Content-Type: application/json");
  if (j->auth[0]) { snprintf(ah, sizeof ah, "Authorization: %s", j->auth); h = curl_slist_append(h, ah); }
  curl_easy_reset(c);
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "OldManBurn3DS/1");
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  struct curl_blob ca = { (void *)cacert_bin, cacert_bin_size, CURL_BLOB_NOCOPY };
  curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
  if (j->body) { curl_easy_setopt(c, CURLOPT_POSTFIELDS, j->body); curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, j->method); }
  long code = 0;
  if (curl_easy_perform(c) == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  curl_slist_free_all(h);
  r->kind = j->kind; r->status = (int)code; r->body = b.p ? b.p : strdup("");
}
static void loop(void *arg) {
  CURL *c = curl_easy_init();
  while (!quit) {
    LightEvent_Wait(&wake);
    for (;;) {
      LightLock_Lock(&lock);
      if (jHead == jTail || quit) { working = false; LightLock_Unlock(&lock); break; }
      Job j = jobs[jHead]; jHead = (jHead + 1) % QN; working = true;
      LightLock_Unlock(&lock);
      NetResult r; runJob(c, &j, &r); free(j.body);
      LightLock_Lock(&lock);
      if ((dTail + 1) % QN != dHead) { done[dTail] = r; dTail = (dTail + 1) % QN; } else free(r.body);
      LightLock_Unlock(&lock);
    }
  }
  curl_easy_cleanup(c);
}
void net_init(void) {
  if (running) return;
  LightLock_Init(&lock); LightEvent_Init(&wake, RESET_ONESHOT);
  socBuf = memalign(0x1000, SOC_SIZE);
  socOk = socBuf && R_SUCCEEDED(socInit(socBuf, SOC_SIZE));
  if (!socOk) { free(socBuf); socBuf = NULL; return; }
  sslcInit(0);                                                     // mbedTLS takes its random numbers from the ssl service
  curl_global_init(CURL_GLOBAL_DEFAULT);
  s32 prio = 0; svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  quit = false;
  th = threadCreate(loop, NULL, 64 * 1024, prio + 2, -2, false);   // below the game's priority, so it only runs while the game waits for the screen
  running = th != NULL;
}
void net_exit(void) {
  if (running) { quit = true; LightEvent_Signal(&wake); threadJoin(th, U64_MAX); threadFree(th); running = false; curl_global_cleanup(); }
  if (socOk) { sslcExit(); socExit(); free(socBuf); socOk = false; }
}
bool net_send(int kind, const char *method, const char *path, const char *body, const char *auth) {
  if (!running) return false;
  LightLock_Lock(&lock);
  bool ok = (jTail + 1) % QN != jHead;
  if (ok) {
    Job *j = &jobs[jTail]; memset(j, 0, sizeof *j); j->kind = kind;
    strncpy(j->method, method, 7); strncpy(j->path, path, 95); if (auth) strncpy(j->auth, auth, 95);
    j->body = body ? strdup(body) : NULL;
    jTail = (jTail + 1) % QN;
  }
  LightLock_Unlock(&lock);
  if (ok) LightEvent_Signal(&wake);
  return ok;
}
bool net_poll(NetResult *out) {
  if (!running) return false;
  LightLock_Lock(&lock);
  bool ok = dHead != dTail; if (ok) { *out = done[dHead]; dHead = (dHead + 1) % QN; }
  LightLock_Unlock(&lock);
  return ok;
}
bool net_busy(void) { if (!running) return false; LightLock_Lock(&lock); bool b = working || jHead != jTail || dHead != dTail; LightLock_Unlock(&lock); return b; }

// The music and sound, as a tiny software synth on the DSP: the same notes and envelopes the web game schedules on
// Web Audio (the "Summer" storm every week, the Haunted Organ on Halloween maps, the engine and the crash).
#include "audio.h"
#include "sim.h"
#include <3ds.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define RATE 22050
#define BUF 1024
#define NBUF 4
#define MAXV 64

enum { W_SINE, W_SQUARE, W_SAW, W_TRI, W_P25, W_P12, W_NOISE };
enum { B_MUSIC, B_BASS, B_LEAD, B_SFX };
enum { F_NONE, F_LP, F_HP, F_BP };

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Biquad;
typedef struct {
  bool on; int wave, bus; int delay;                 // samples before it starts
  float ph, inc, incMul; int incLeft;               // frequency (as phase step) and its exponential ramp
  float g, peak, attMul, relMul; int att, hold, rel; int t;   // envelope: rise, hold, fall (samples)
  Biquad f; bool filt;
} Voice;

static bool ok, music = true, sfx = true;
static ndspWaveBuf wbuf[NBUF]; static s16 *pcm;
static Voice V[MAXV];
static float sinTab[1024];
static uint32_t nseed = 22222;
static Biquad bassF, engF; static float engPh, engFreq = 60, engGain = 0, engGainT = 0, engFreqT = 60;
static float *dly; static int dlyLen, dlyPos; static float dlyTime = 0.29f;
static float musicLvl = 0.55f, musicT = 0.55f;
static int stepN, stepLeft; static int lastSt = -1;

static void biquad(Biquad *q, int type, float f, float Q) {         // RBJ cookbook, as Web Audio's BiquadFilterNode
  float w = 2 * (float)M_PI * f / RATE, cs = cosf(w), al = sinf(w) / (2 * Q), a0 = 1 + al;
  float b0, b1, b2;
  if (type == F_LP) { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2; }
  else if (type == F_HP) { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; }
  else { b0 = al; b1 = 0; b2 = -al; }
  q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = -2 * cs / a0; q->a2 = (1 - al) / a0; q->z1 = q->z2 = 0;
}
static inline float bq(Biquad *q, float x) { float y = q->b0 * x + q->z1; q->z1 = q->b1 * x - q->a1 * y + q->z2; q->z2 = q->b2 * x - q->a2 * y; return y; }
static float mtof(float m) { return 440 * powf(2, (m - 69) / 12); }

static Voice *voice(int wave, int bus, float delay, float f, float fEnd, float dur, float gain, float att, float hold) {
  Voice *v = NULL; for (int i = 0; i < MAXV; i++) if (!V[i].on) { v = &V[i]; break; }
  if (!v) return NULL;
  memset(v, 0, sizeof *v); v->on = true; v->wave = wave; v->bus = bus; v->delay = (int)(delay * RATE);
  v->inc = f / RATE; v->incMul = 1;
  if (fEnd > 0) { v->incLeft = (int)(dur * RATE); v->incMul = powf(fEnd / f, 1.0f / (v->incLeft > 0 ? v->incLeft : 1)); }
  v->peak = gain; v->att = (int)(att * RATE); v->hold = (int)(hold * RATE); v->rel = (int)(dur * RATE) - v->hold; if (v->rel < 1) v->rel = 1;
  if (v->att > 0) { v->g = 0.0001f; v->attMul = powf(gain / 0.0001f, 1.0f / v->att); } else v->g = gain;
  v->relMul = powf(0.001f / gain, 1.0f / v->rel);
  return v;
}
// tone(): a set gain falling away exponentially, like the web game's tone()
static void tone(float delay, float f, float dur, int wave, float gain, int bus, float fEnd) { voice(wave, bus, delay, f, fEnd, dur, gain, 0, 0); }
static void chip(float f, float dur, int wave, float gain, int bus) { voice(wave, bus, 0, f, 0, dur, gain, 0.006f, dur * 0.6f); }
static void noise(float delay, float dur, int type, float f, float gain, int bus) {
  Voice *v = voice(W_NOISE, bus, delay, 1, 0, dur, gain, 0, 0); if (!v) return; v->filt = true; biquad(&v->f, type, f, 1);
}
static void organ(float m, float dur, float vol) {                 // a stack of pipes: square at pitch, thin pulse an octave up, sine an octave down
  voice(W_SQUARE, B_LEAD, 0, mtof(m), 0, dur, vol, 0.02f, dur * 0.85f);
  voice(W_P25, B_LEAD, 0, mtof(m) * 2, 0, dur, vol * 0.5f, 0.02f, dur * 0.85f);
  voice(W_SINE, B_LEAD, 0, mtof(m) * 0.5f, 0, dur, vol * 1.2f, 0.02f, dur * 0.85f);
}

/* ---------- the tracks: note for note what web/game.js plays ---------- */
static const int SUM_PROG[8][2] = { { 0, 0 }, { 0, 0 }, { 7, 1 }, { 7, 1 }, { 0, 0 }, { 5, 0 }, { 7, 1 }, { 0, 0 } };
static const bool SUM_STORM[8] = { 1, 0, 1, 0, 1, 1, 0, 1 };
static const int H_MINOR[7] = { 0, 2, 3, 5, 7, 8, 11 };
static int hmNote(int i) { int f = (int)floor(i / 7.0); return H_MINOR[((i % 7) + 7) % 7] + 12 * f; }
static int sumLead(int bar, int s) {
  int r = SUM_PROG[bar][0], third = r + (SUM_PROG[bar][1] ? 4 : 3);
  if (SUM_STORM[bar]) { int ns[4] = { r + 7, third, r, third }; return ns[s / 4]; }
  int d = r == 0 ? 0 : r == 7 ? 4 : 3;
  return s < 8 ? hmNote(d + s) : hmNote(d + 6 - (s - 8));
}
static float thrNow(void) { return (float)g.thr; }
static void playSummer(int n, float sd) {
  int s = n % 16, bar = (n / 16) % 8; bool roll = bar == 7;
  int r = SUM_PROG[bar][0], third = r + (SUM_PROG[bar][1] ? 4 : 3);
  if (s % 4 == 0) tone(0, 150, 0.12f, W_SINE, 0.95f, B_MUSIC, 42);
  if (s == 4 || s == 12 || (roll && s >= 12 && s % 2 == 0)) { for (int k = 0; k < 3; k++) noise(k * 0.011f, 0.09f, F_BP, 1900, 0.32f, B_MUSIC); tone(0, 210, 0.06f, W_TRI, 0.25f, B_MUSIC, 120); }
  if (s % 2 == 1) noise(0, 0.03f, F_HP, 9000, 0.08f, B_MUSIC);
  if (s % 4 == 2) noise(0, 0.09f, F_HP, 7000, 0.1f, B_MUSIC);
  if (s % 2 == 0) tone(0, mtof(43 + r + (s % 4 == 2 ? 12 : 0)), sd * 1.7f, W_SAW, 0.3f, B_BASS, 0);
  int chord[4] = { r, third, r + 7, r + 12 }; static const int AI[8] = { 0, 1, 2, 3, 2, 1, 2, 1 };
  chip(mtof(55 + chord[AI[s % 8]]), sd * 0.9f, W_P12, 0.03f, B_MUSIC);
  float v = 0.04f * (0.6f + 0.9f * thrNow()), f = mtof(67 + sumLead(bar, s));
  chip(f, sd * 0.95f, W_P25, v, B_LEAD); chip(f * 1.004f, sd * 0.95f, W_SQUARE, v * 0.35f, B_LEAD);
}
static const int MOTIF[11][2] = { { 81, 1 }, { 79, 1 }, { 81, 8 }, { 0, 2 }, { 79, 1 }, { 77, 1 }, { 76, 1 }, { 74, 1 }, { 73, 4 }, { 74, 8 }, { 0, 4 } };
static const int ORGAN_DIM[12] = { 49, 52, 55, 58, 61, 64, 67, 70, 73, 76, 79, 82 };
static void playOrgan(int n, float sd) {
  int s = n % 128, bar = s >> 4, b = s % 16; float lv = 0.7f + 0.5f * thrNow();
  if (bar < 4) { int at = 0; for (int i = 0; i < 11; i++) { if (at == s % 32 && MOTIF[i][0]) { float m = MOTIF[i][0] - (bar >= 2 ? 12 : 0); organ(m, MOTIF[i][1] * sd, 0.03f * lv); organ(m - 12, MOTIF[i][1] * sd, 0.017f * lv); } at += MOTIF[i][1]; } }
  else if (bar < 6) { int k = s - 64; if (k < 12) organ(ORGAN_DIM[k], (24 - k) * sd, 0.019f * lv); }
  else { int k = s - 96; if (k == 0) { const int ch[5] = { 50, 57, 62, 65, 69 }; for (int i = 0; i < 5; i++) organ(ch[i], 32 * sd, 0.019f * lv); }
    static const int FL[8] = { 62, 65, 69, 74, 77, 81, 86, 89 }; if (k % 2 == 0 && k < 16) chip(mtof(FL[k >> 1]), sd * 1.2f, W_P25, 0.035f, B_LEAD); }
  int root = bar == 4 || bar == 5 ? 37 : 38;                        // D, with C# under the diminished roll
  if (b % 2 == 0) tone(0, mtof(root + (b % 4 == 2 ? 12 : 0)), sd * 1.6f, W_SAW, 0.28f, B_BASS, 0);
  if (b % 4 == 0) tone(0, 150, 0.12f, W_SINE, 0.95f, B_MUSIC, 42);
  if (b == 4 || b == 12) { noise(0, 0.12f, F_BP, 1900, 0.32f, B_MUSIC); tone(0, 210, 0.06f, W_TRI, 0.22f, B_MUSIC, 120); }
  if (b % 2 == 1) noise(0, 0.03f, F_HP, 8500, 0.08f, B_MUSIC);
  static const int AR[4] = { 62, 65, 69, 74 }; if (b % 4 == 2) chip(mtof(AR[(b >> 2) % 4]), sd * 0.8f, W_P12, 0.025f, B_MUSIC);
}
static float bpm(void) { return SEASON == 1 ? 132 : 156; }

/* ---------- mixing ---------- */
static inline float osc(Voice *v) {
  float p = v->ph;
  switch (v->wave) {
    case W_SINE: return sinTab[(int)(p * 1024) & 1023];
    case W_SQUARE: return p < 0.5f ? 1 : -1;
    case W_SAW: return 2 * p - 1;
    case W_TRI: return p < 0.5f ? 4 * p - 1 : 3 - 4 * p;
    case W_P25: return p < 0.25f ? 1 : -0.333f;
    case W_P12: return p < 0.125f ? 1 : -0.143f;
    default: nseed = nseed * 1664525u + 1013904223u; return (int32_t)nseed / 2147483648.0f;
  }
}
static void fill(s16 *out, int n) {
  float sd = 60 / bpm() / 4; int stepLen = (int)(sd * RATE);
  bool playing = st != ST_TITLE;
  for (int i = 0; i < n; i++) {
    if (playing && stepLeft <= 0) { if (SEASON == 1) playOrgan(stepN, sd); else playSummer(stepN, sd); stepN++; stepLeft = stepLen; }
    stepLeft--;
    float mus = 0, bass = 0, lead = 0, fx = 0;
    for (int k = 0; k < MAXV; k++) {
      Voice *v = &V[k]; if (!v->on) continue;
      if (v->delay > 0) { v->delay--; continue; }
      float s = osc(v) * v->g; if (v->filt) s = bq(&v->f, s);
      v->ph += v->inc; if (v->ph >= 1) v->ph -= 1;
      if (v->incLeft > 0) { v->inc *= v->incMul; v->incLeft--; }
      if (v->t < v->att) v->g *= v->attMul; else if (v->t >= v->hold && v->t >= v->att) v->g *= v->relMul;
      if (++v->t > v->hold + v->rel + RATE / 30) v->on = false;
      if (v->bus == B_MUSIC) mus += s; else if (v->bus == B_BASS) bass += s; else if (v->bus == B_LEAD) lead += s; else fx += s;
    }
    // the engine: a sawtooth growl through a low-pass, pitched by speed and throttle
    engFreq += (engFreqT - engFreq) * 0.0009f; engGain += (engGainT - engGain) * 0.0006f;
    engPh += engFreq / RATE; if (engPh >= 1) engPh -= 1;
    fx += bq(&engF, 2 * engPh - 1) * engGain;
    // the lead's dotted-eighth echo
    int dl = (int)(dlyTime * RATE); if (dl >= dlyLen) dl = dlyLen - 1;
    float echo = dly[(dlyPos - dl + dlyLen) % dlyLen];
    dly[dlyPos] = lead * 0.42f + echo * 0.32f; dlyPos = (dlyPos + 1) % dlyLen;
    musicLvl += (musicT - musicLvl) * (1.0f / (0.1f * RATE));
    float m = (mus + bq(&bassF, bass) + lead + echo) * musicLvl * (music ? 1 : 0) + fx * 0.8f * (sfx ? 1 : 0);
    m *= 0.6f;
    int o = (int)(m * 40000); out[i] = o > 32767 ? 32767 : o < -32768 ? -32768 : o;
  }
}

void audio_init(void) {
  for (int i = 0; i < 1024; i++) sinTab[i] = sinf(i * 2 * (float)M_PI / 1024);
  biquad(&bassF, F_LP, 600, 1); biquad(&engF, F_LP, 420, 1);
  dlyLen = RATE; dly = calloc(dlyLen, sizeof(float));
  if (R_FAILED(ndspInit())) return;                               // no dspfirm.cdc on the SD card: play silently
  ok = true;
  ndspSetOutputMode(NDSP_OUTPUT_MONO);
  ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
  ndspChnSetRate(0, RATE);
  ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
  float mix[12] = { 1, 1 }; ndspChnSetMix(0, mix);
  pcm = linearAlloc(BUF * NBUF * sizeof(s16)); memset(pcm, 0, BUF * NBUF * sizeof(s16));
  memset(wbuf, 0, sizeof wbuf);
  for (int i = 0; i < NBUF; i++) { wbuf[i].data_vaddr = pcm + i * BUF; wbuf[i].nsamples = BUF; DSP_FlushDataCache(wbuf[i].data_pcm16, BUF * 2); ndspChnWaveBufAdd(0, &wbuf[i]); }
}
void audio_exit(void) { if (ok) { ndspExit(); linearFree(pcm); } }
void audio_set(bool m, bool s) { music = m; sfx = s; }
void audio_crash(void) {
  noise(0, 1.0f, F_LP, 1800, 0.9f, B_SFX); tone(0, 160, 0.7f, W_SINE, 1.0f, B_SFX, 28);
  tone(0, 1400, 0.35f, W_SAW, 0.18f, B_SFX, 200); noise(0.02f, 0.25f, F_HP, 3000, 0.35f, B_SFX); tone(0.12f, 90, 0.5f, W_TRI, 0.6f, B_SFX, 40);
  musicT = 0.15f;
}
void audio_tick(void) {
  if (st == ST_PLAY && lastSt != ST_PLAY) { musicT = 0.55f; stepN = 0; stepLeft = 0; }   // a fresh run starts the track from the top
  if (st == ST_TITLE && lastSt != ST_TITLE) { for (int i = 0; i < MAXV; i++) if (V[i].bus != B_SFX) V[i].on = false; }
  lastSt = st;
  engFreqT = 46 + (float)g.v * 0.18f + (float)g.thr * 22;
  engGainT = st == ST_PLAY && !paused ? 0.05f + 0.05f * (float)g.thr : 0;
  dlyTime = fminf(0.95f, 60 / bpm() / 4 * 3);
  if (!ok) return;
  for (int i = 0; i < NBUF; i++) if (wbuf[i].status == NDSP_WBUF_DONE) {
    fill(wbuf[i].data_pcm16, BUF); DSP_FlushDataCache(wbuf[i].data_pcm16, BUF * 2); ndspChnWaveBufAdd(0, &wbuf[i]);
  }
}

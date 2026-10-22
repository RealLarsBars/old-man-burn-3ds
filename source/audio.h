#pragma once
#include <stdbool.h>
void audio_init(void);
void audio_exit(void);
void audio_set(bool music, bool sfx);
void audio_volume(int pct);     // the Settings volume: 100 is normal
int audio_tick(void);            // once a frame: engine, music and the state they follow. Returns how many buffers it filled
void audio_crash(void);
int audio_core(void);
int audio_gaps(void);            // how many times the sound has run dry and cut out since the game started            // the CPU core the sound runs on, or -1 when the main loop makes it
// The Derby's sounds, as the web game's tone() and noise() (delay in seconds from now) and the crowd's level (0 = quiet)
enum { AW_SINE, AW_SQUARE, AW_SAW, AW_TRI };
enum { AF_LOWPASS = 1, AF_HIGHPASS, AF_BANDPASS };
void audio_fx_tone(float delay, float f, float dur, int wave, float gain, float fEnd);
void audio_fx_noise(float delay, float dur, int filter, float f, float gain);
void audio_crowd(float level);

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

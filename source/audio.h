#pragma once
#include <stdbool.h>
void audio_init(void);
void audio_exit(void);
void audio_set(bool music, bool sfx);
int audio_tick(void);            // once a frame: engine, music and the state they follow. Returns how many buffers it filled
void audio_crash(void);

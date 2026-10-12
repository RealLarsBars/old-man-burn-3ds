#pragma once
#include <stdbool.h>
void audio_init(void);
void audio_exit(void);
void audio_set(bool music, bool sfx);
void audio_tick(void);           // once a frame: engine, music and the state they follow
void audio_crash(void);

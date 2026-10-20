#pragma once
// Updating the game over Wi-Fi. At start it asks the game server for the newest build (the server hands out the 3DS
// repo's GitHub releases). If it's newer than this one, the menus offer it: the .cia build installs the new .cia over
// itself and restarts, the .3dsx build writes the new .3dsx over its own file. All of it runs on its own thread and
// only from the menus, never during a ride.
#include <stdbool.h>

#ifndef BUILD_NUM
#define BUILD_NUM 0                        // a local build: never offers updates (CI builds pass the release number)
#endif

enum { UP_IDLE, UP_CHECKING, UP_DOWNLOADING, UP_DONE, UP_FAILED };

void update_init(const char *self);         // after net_init (sync_init). self: argv[0] (the .3dsx's path), or NULL
void update_exit(void);
int update_newer(void);                     // the newer build that's out, or 0 (none, not checked yet or no Wi-Fi)
bool update_is_cia(void);                   // installed from a .cia (else it's the Homebrew Launcher's .3dsx)
void update_start(void);                    // download and install update_newer()
void update_cancel(void);                   // stop a download; the game stays as it was
int update_state(float *progress, const char **msg);   // UP_*; progress 0..1 while downloading; msg on UP_FAILED
void update_restart(void);                  // after UP_DONE: the .cia build is set to start again when the app closes

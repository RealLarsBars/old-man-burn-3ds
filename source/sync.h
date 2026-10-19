#pragma once
// Linking the console to a Discord account and keeping the two in step over Wi-Fi: the server's clock and map seeds,
// the profile (Discord unlocks count here too; the loadout follows you), weekly scores for the board (tagged 3DS, never
// for the pot) and every run's replay. Runs wait in an outbox on the SD card while there's no connection.
#include <stdbool.h>
#include "replay.h"

void sync_init(void);                      // after ach_load: reads the link from the SD card and asks for the state
void sync_exit(void);
void sync_tick(bool menus);                // every frame; menus = on a menu screen, where a refresh can't hitch a ride
                                           // (replies that finish mid-ride are only handled once it's over)
bool sync_linked(void);
const char *sync_name(void);               // the Discord name it's linked to
const char *sync_status(void);             // a short line for the menus ("Synced with Discord", "Offline", ...)
bool sync_clock(int *week, int *day);      // the live weekly seed and Daily Dash day from the server, once it has answered
void sync_link_prompt(void);               // the software keyboard for the 6-digit code from Discord, then links
void sync_unlink(void);
void sync_loadout_changed(void);           // the garage changed the look
void sync_run(void);                       // the run just recorded (replay.h) is over: queue it for upload
#define BOARD_N 10
typedef struct { char name[16]; int score; bool ds, me; char replay[12]; } BoardRow;   // replay: the id to watch it by, or ""
// The Discord top 10 for week `id` (or Daily Dash day `id`): how many rows, or -1 when there's no board for it yet
int sync_board(bool daily, int id, const BoardRow **rows, int *pot);
bool sync_daily_can_watch(void);
int sync_daily_tries(void);                // today's Daily Dash tries left (shared with Discord), or -1 when not known
// Starting a Daily Dash: it needs a linked 3DS on Wi-Fi, which takes one of today's tries from the server first (as
// Discord's Start does). sync_daily_poll() each frame until it's yes (1, ride now) or no (-1, msg says why).
void sync_daily_start(void);
int sync_daily_poll(const char **msg);
void sync_daily_cancel(void);              // stop waiting; a try that still comes in is kept for the next Daily Dash
// Watching a run from the board: ask for it, then sync_watch_poll() each frame until it's in (1) or failed (-1, msg says
// why). The run is malloc'd: free it with sync_watch_free().
typedef struct { int seed, score, end; bool ds; char name[16]; char look[NSLOT][24]; int n; RecFrame *f; } Watched;
void sync_watch_fetch(const char *id);
int sync_watch_poll(Watched **out, const char **msg);
void sync_watch_cancel(void);
void sync_watch_free(Watched *w);

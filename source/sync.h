#pragma once
// Linking the console to a Discord account and keeping the two in step over Wi-Fi: the server's clock and map seeds,
// the profile (Discord unlocks count here too; the loadout follows you), weekly scores for the board (tagged 3DS, never
// for the pot) and every run's replay. Runs wait in an outbox on the SD card while there's no connection.
#include <stdbool.h>

void sync_init(void);                      // after ach_load: reads the link from the SD card and asks for the state
void sync_exit(void);
void sync_tick(bool menus);                // every frame; menus = on a menu screen, where a refresh can't hitch a ride
bool sync_linked(void);
const char *sync_name(void);               // the Discord name it's linked to
const char *sync_status(void);             // a short line for the menus ("Synced with Discord", "Offline", ...)
bool sync_clock(int *week, int *day);      // the live weekly seed and Daily Dash day from the server, once it has answered
void sync_link_prompt(void);               // the software keyboard for the 6-digit code from Discord, then links
void sync_unlink(void);
void sync_loadout_changed(void);           // the garage changed the look
void sync_run(void);                       // the run just recorded (replay.h) is over: queue it for upload

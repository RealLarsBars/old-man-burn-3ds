#pragma once
// Achievements and the garage, offline: stats are counted on the console and kept on the SD card next to the save.
// The list and the rewards are the Discord version's (achdata.c, made by tools/achgen.py); goals that need coins or
// the leaderboards have offline ones instead.
#include <stdbool.h>
#include "look.h"
#include "achdata.h"

typedef struct { int slot; const char *id, *name; } AchItem;
typedef struct { int slot; const char *item; } AchReward;
typedef struct { const char *id, *name, *desc; int stat, goal, nrw; AchReward rw[MAX_REWARDS]; } Ach;
typedef struct { int seed, best; } Best;

extern const char *const SLOT_IDS[NSLOT], *const SLOT_NAMES[NSLOT], *const STAT_IDS[NSTAT];
extern const AchItem ITEMS[NITEMS];
extern const Ach ACHS[NACH];
extern double stats[NSTAT];

void ach_load(void);                       // the profile file, then puts the saved loadout on the bike
void ach_save(void);
void ach_bests(const Best *b, int n);      // works out the stats that follow from the map bests (weeks ridden, Daily Dash days...)
bool ach_done(int a);
int ach_count(void);
bool item_owned(int slot, const char *id);
int item_index(int slot, const char *id);  // into ITEMS, or -1 for the default look
const char *item_name(int slot, const char *id);
int slot_owned(int slot);                  // how many of the slot's items are unlocked
int slot_total(int slot);
void ach_wear(int slot, int dir);          // next / previous unlocked item in a slot ("" = the original)
double cos_meters(int slot);               // meters ridden wearing the current item in a slot

void ach_run_start(void);
void ach_run_tick(double dt);              // while riding: wheelies, dirt, gaps, barricades, swerves
// the run is over: counts it, saves, and returns how many achievements it unlocked (their indexes in ach_new)
int ach_run_end(int dist, int cause, int seed, bool daily, bool oldMap, bool newBest, int season, const Best *b, int nb);
extern int ach_new[NACH];
extern bool ach_server[NACH];                // unlocked on the linked Discord profile (sync.c)
struct json_t; void ach_extras(struct json_t *o);   // the last run's counts, as the server's run extras

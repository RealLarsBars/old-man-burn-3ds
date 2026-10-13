#pragma once
// Recording a run for its replay: where the bike was ten times a second, as the Discord version's spectating notes
// ([t, camera, bike x, bike y, throttle, wheelie]). It goes up over Wi-Fi, or out as a long code for when it can't.
#include <stdbool.h>
#include "look.h"

#define REC_STEP 0.1
#define REC_MAX 20000                      // 33 minutes
typedef struct { float t, cam, bx, y, thr, wheelie; } RecFrame;
typedef struct { int seed, mode, score, end; char look[NSLOT][24]; int n; RecFrame f[REC_MAX]; } Recording;
enum { RM_WEEK, RM_DAILY, RM_OLD };
extern Recording rec;

void rec_start(int seed, int mode);
void rec_tick(void);                       // every frame of the run
void rec_end(int score, int cause);        // cause: CAUSE_*
// the long code ("OMB1." + base64url of the zlib'd frames, 5 a second); malloc'd, or NULL
char *rec_code(void);

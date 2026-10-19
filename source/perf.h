#pragma once
// The performance log: every ride writes how long its frames took, and its slowest frames with what was going on, to
// sdmc:/3ds/oldmanburn/perf.txt, so a laggy run can be sent in and looked at.
#include <stdbool.h>
enum { PF_SIM, PF_AUDIO, PF_WAIT, PF_DRAW, PF_SYNC, PF_N };   // the parts of a frame that are timed (wait: for the refresh and the GPU;
                                                              // sync: handling Discord replies and their SD card writes)
void perf_init(void);
void perf_mark(int part);                     // call right after that part of the frame finishes (marks start at perf_frame)
void perf_frame(bool riding, int audioBufs);  // once a frame, at the top of the loop: closes out the previous frame
void perf_ride_start(const char *track, int seed);
void perf_ride_end(int score);                // writes the ride's block to the log

// The 3DS port's side of roadcheck.js: same map, same printout.
#include "../source/sim.h"
#include <stdio.h>
#include <stdlib.h>
void on_crash(void) {} void on_over(void) {} void dirt_colors(uint32_t o[4]) { (void)o; }
int main(int argc, char **argv) {
  int seed = atoi(argv[1]); double dist = argc > 2 ? atof(argv[2]) : 120000;
  reseed(seed, seed >= DAILY_BASE || seed >= V2_FROM);
  genTo(dist);
  for (int x = 0; x < dist - 3000; x += 37) { roadAt(x); const Span *gp = gapAt(x); double gx = gp ? gp->x : -1; printf("r %d %.6f %.6f %.6f\n", x, RC, RW, gx); }
  for (int i = 0; i < nrails; i++) if (rails[i].x < dist - 3000) { int s = railSide(i); printf("b %.6f %.6f %d\n", rails[i].x, rails[i].len, s); }
  for (int i = 0; i < edges.n; i++) if (edges.a[i].x < dist - 3000) printf("e %.6f %.6f\n", edges.a[i].x, edges.a[i].len);
  for (int y = 112; y < 266; y += 3) for (int x = 2000; x < dist - 3000; x += 211) printf("h %d %d %d%d\n", x, y, hitRail(x, y, y - 2) ? 1 : 0, hitEdge(x, y, y + 2) ? 1 : 0);
}

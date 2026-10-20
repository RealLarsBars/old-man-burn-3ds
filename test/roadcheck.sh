#!/bin/sh
# Builds maps with the 3DS port and with the road code in web/game.js and diffs them, for weekly maps under each set of
# rules (original, swerves, gauntlets, split roads, mixed gauntlets) and a few Daily Dash days. Needs gcc, node and the web game: a checkout of
# RealLarsBars/old-man-burn next to this repo, or GAME_JS=/path/to/web/game.js.
set -e
cd "$(dirname "$0")"
gcc -O2 -o /tmp/omb-roadcheck roadcheck.c ../source/sim.c -lm
for seed in 20730 20737 20744 20751 20758 20800 1020737 1020738 1020739 1020740 1020745 1020746 1020747 1020760; do
  node roadcheck.js $seed > /tmp/omb-js.txt
  /tmp/omb-roadcheck $seed > /tmp/omb-c.txt
  if cmp -s /tmp/omb-js.txt /tmp/omb-c.txt; then echo "seed $seed: same ($(wc -l < /tmp/omb-c.txt) lines)"; else echo "seed $seed: DIFFERENT"; diff /tmp/omb-js.txt /tmp/omb-c.txt | head; exit 1; fi
done

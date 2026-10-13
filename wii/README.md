# Old Man Burn for the Wii: the arcade edition

A Homebrew Channel version of Old Man Burn for game nights at the Abbey Tavern. It's offline and arcade style:

- **A new road every week.** The road changes on Sunday at midnight (by the Wii's clock) and is the arcade's own, not
  the Discord week's map. It's built with the newest road rules (the Daily Dash's gauntlets and rests). Halloween and
  Christmas get their seasonal look and music, going by the date.
- **A top 10 for the week, with three-letter initials.** A score that makes the week's top 10 asks for initials (up and
  down pick a letter, 2 moves on, 1 goes back). The title screen shows this week's top 10, then the all-time top 10.
- **No coins, garage, achievements or Daily Dash.** Everyone rides the Old Man's own bike.
- The game fills a 16:9 TV, or is letterboxed on a Wii set to 4:3. Music and sound are the same synth as the 3DS
  version.

## Installing

Copy `boot.dol`, `meta.xml` and `icon.png` into `apps/oldmanburn/` on the SD card (or a USB drive) and start Old Man
Burn from the Homebrew Channel. Scores and settings are saved next to it in `scores.txt`.

## Controls

| | Go (hold) | Steer | OK / back |
|---|---|---|---|
| Wii Remote held sideways | 2 (or B) | D-pad | 2 / 1 |
| Wii Remote + Nunchuk | B, Z or A | stick or D-pad | A / C |
| Classic Controller | A, R or ZR | left stick or D-pad | A / B |
| GameCube controller | A or R | stick or D-pad | A / B |

Any controller works, so whoever's up can just grab one. + (START) pauses; 1 (B) then goes back to the title.

## Settings (for whoever runs the night)

Hold − (or HOME, or Z on a GameCube controller) on the title screen for a second and a half. There:

- music and sound effects on or off, and the steering speed;
- **Roll a new road for this week**, if the week's road is a dud (it also starts a fresh top 10);
- **Clear this week's top 10**;
- **Exit to the Homebrew Channel**.

Each of the last three asks for a second press of 2. The name at the top of the title screen is the `venue` line in
`scores.txt`. The reset button goes back to the Homebrew Channel and the power button turns the Wii off, saving first.

## Building

Needs devkitPro's devkitPPC with libogc and the `ppc-freetype` portlib (FreeType draws the fonts):

    cd wii && make

It builds `boot.dol` from `wii/source` (the Wii's graphics layer, controllers and the arcade screens) plus the shared
code in `../source`: the road and physics (`sim.c`), the drawing (`draw.c`, `look.c`), the menu look (`ui.c`) and the
synth (`audio.c`). A road-rule change in `sim.c` reaches both consoles.

`make AUTOPLAY=1` builds `boot-autoplay.dol`, which plays itself through the title, a ride, the initials, the game-over
board and the settings, saves a screenshot of each to `apps/oldmanburn/shots/` and stops. `test/wii-shots.sh` runs it in
Dolphin and turns the screenshots into PNGs.

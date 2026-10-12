# Old Man Burn for the Nintendo 3DS

An offline homebrew port of [Old Man Burn](https://github.com/RealLarsBars/old-man-burn). It rides this week's map,
today's Daily Dash and every old weekly map, built from the same seeds with the same road rules, so the roads are the
ones Discord is riding. There are no coins, pot, leaderboards, garage or replays: your best on each map is kept on the
SD card.

- The top screen is the game. It's drawn at the web game's 480x270 and scaled to fit the 400x240 screen.
- The bottom screen has the map picker, your best, the score while you ride and the game-over card.
- Halloween and Christmas maps get their seasonal look, and Halloween maps play the Haunted Organ, as on the web.
- Music and sound are a small synth playing the same notes as the web game's Web Audio code.

## Installing

Either file works on a 3DS with custom firmware:

- **OldManBurn.cia**: install it with FBI (or any CIA installer) and it shows up on the HOME Menu.
- **OldManBurn.3dsx**: copy it to `sdmc:/3ds/` and start it from the Homebrew Launcher.

Sound needs the DSP firmware dump that most homebrew needs (`sdmc:/3ds/dspfirm.cdc`, made with DSP1). Without it the
game runs silently.

## Controls

| | |
|---|---|
| Go | hold L |
| Steer | D-pad up / down, the Circle Pad, or drag the stylus up and down on the bottom screen |
| Pick a map | L / R, or tap the tabs. On Old Maps, left / right picks the week |
| Pause | START. While paused, SELECT goes back to the menu |
| Settings | SELECT on the menu: music, sound effects, the clock and the controls |

Go, steer up and steer down can each be moved to any button in Settings (pick the line, press A, then press the
button). Dragging on the touch screen and the Circle Pad always steer as well.

## Reporting lag

Every ride adds a few lines to `sdmc:/3ds/oldmanburn/perf.txt`: how many frames missed the 60 Hz refresh, how long the
game, sound and drawing took on average and at worst, and the slowest frames with where on the road they happened. Send
that file along with a lag report. Holding Y shows the CPU and GPU load on the bottom screen while you play.

## The clock

The weekly map changes Sunday at midnight and the Daily Dash at noon, Pacific time, the same as the server. A 3DS only
knows its own local clock, so the game treats it as Pacific time. If your console isn't on Pacific time, set the
"Clock vs Pacific time" offset in Settings (for example +3 on Eastern time) so the maps change when Discord's do.

## Building

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `3ds-dev` group (devkitARM, libctru,
citro3d and citro2d).

```sh
make            # OldManBurn.3dsx
make cia        # OldManBurn.cia as well: needs bannertool and makerom on the PATH
```

`bannertool` (github.com/carstene1ns/3ds-bannertool) and `makerom` (github.com/3DSGuy/Project_CTR) both build with
CMake / make on Linux. `meta/` holds the icon, the HOME Menu banner and its sound, and the makerom settings.

## Keeping the maps in step with the web game

`source/sim.c` is a line-for-line port of the road, physics and collision code in the web game's `web/game.js`.
`test/roadcheck.sh` builds several maps with both (it lifts the road code straight out of `web/game.js` and runs it in
node) and checks that every road point, gap, barricade, guardrail and collision test comes out the same. It looks for a
checkout of old-man-burn next to this repo, or set `GAME_JS`:

```sh
./test/roadcheck.sh
GAME_JS=/path/to/old-man-burn/web/game.js ./test/roadcheck.sh
```

When the road rules change on the web (a new `SW5_FROM`, say), make the same change in `sim.c` and run it again.

## Layout

- `source/sim.c`: seeds, road generation, physics, collisions (no 3DS code, so the check above runs on a PC)
- `source/draw.c`: the game picture: backdrop, road, rails, decor, the garage, the Old Man and the crash
- `source/gfx.c`: a small slice of the canvas API on citro2d, so `draw.c` reads like the JavaScript
- `source/audio.c`: the music and sound
- `source/main.c`: the menus, input, saves and the clock
- `source/perf.c`: the performance log

# Old Man Burn for the Nintendo 3DS

A homebrew port of [Old Man Burn](https://github.com/RealLarsBars/old-man-burn). It rides this week's map, today's
Daily Dash and every old weekly map, built from the same seeds with the same road rules, so the roads are the ones
Discord is riding. It plays fully offline, with the garage and achievements kept on the SD card; linked to Discord over
Wi-Fi it shares your profile, puts weekly runs on the board and uploads replays. There are no coins or pot.

- The top screen is the game. It's drawn at the web game's 480x270 and scaled to fit the 400x240 screen.
- The bottom screen has the menus, your best, the score while you ride and the game-over card.
- Halloween, Thanksgiving week and Christmas maps get their seasonal look, and Halloween maps play the Haunted Organ, as on the web.
- From the Nov 1 2026 maps on, each map picks its landscape from desert, coast, forest, Yosemite (El Capitan, Half Dome, waterfalls, bears, and campgrounds about 8% of the ride), the Grand Canyon, a library, a neon city and a boardwalk pier in the same shuffled cycle as the web game (`landOf` in `source/sim.c`). It only changes the look, never the road. The library and neon backdrops are cached in the garage texture once you ride past the garage, so they cost the old 3DS little.
- Music and sound are a small synth playing the same notes as the web game's Web Audio code. It runs on the console's
  second CPU core, so it doesn't slow the game down.

## Installing

Download the newest build from [Releases](https://github.com/RealLarsBars/old-man-burn-3ds/releases/latest). Every change
to `main` builds both files and publishes a new release (`.github/workflows/release.yml`). Either file works on a 3DS
with custom firmware:

- **OldManBurn.cia**: install it with FBI (or any CIA installer) and it shows up on the HOME Menu.
- **OldManBurn.3dsx**: copy it to `sdmc:/3ds/` and start it from the Homebrew Launcher.

Sound needs the DSP firmware dump that most homebrew needs (`sdmc:/3ds/dspfirm.cdc`, made with DSP1). Without it the
game runs silently.

## Controls

| | |
|---|---|
| Go | hold L |
| Steer | D-pad up / down, the Circle Pad, or drag the stylus up and down on the bottom screen |
| Menus | tap a button, or move with the D-pad and press A. B goes back. R on the title screen rides the Daily Dash |
| Watch a board run | BOARD, then up / down picks the row and A watches it. L / R switches weekly and Daily Dash, B stops watching |
| Pick an old map | OLD MAPS, then up / down picks the week (the top screen shows it) and A rides it |
| Pause | there is none. Runs can't be paused: pressing HOME or closing the lid mid-ride ends the run and keeps the score |
| Settings | the Settings button or SELECT on the menu: music, sound effects, steering speed, drag sensitivity, the clock and the controls |

Go, steer up and steer down can each be moved to any button in Settings (pick the line, press A, then press the
button). Dragging on the touch screen and the Circle Pad always steer as well.

Two sliders in Settings tune the steering, each from 0.5x to 2.0x: "Steering speed" is how fast the bike moves while you
hold up or down (D-pad, Circle Pad or the remapped buttons), and "Drag sensitivity" is how far it moves for each bit
you drag the stylus.

## The garage and achievements

GARAGE on the title screen dresses the Old Man and his bike: paint, cardigan, hat, wheels, decal, gear, carrier, cane
and trail, the same 124 items as the Discord version, each unlocked by the same achievement. Up / down picks a slot,
left / right (or tapping either side) changes it, and the top screen shows the bike up close. The Goals tab (or L / R)
lists every achievement by what it counts, with your progress.

There are no coins or leaderboards offline, so the achievements that count them have offline goals instead: rides of
500 m or more for coins put in, 5,000 on a weekly map for a pot win, new bests for pot coins, 2,000 in a Daily Dash for
a Daily Dash win, and 3,000 on an old map or a holiday map for a top 3. Stats and the loadout are kept in
`sdmc:/3ds/oldmanburn/profile.txt`. `tools/achgen.py` rebuilds `source/achdata.c` from `tools/achievements.json`
(copy it over from the web game's `web/achievements.json` when that changes).

## Linking to Discord

Settings > Discord links the console to your Discord account: in the Discord game press LINK A 3DS in Settings, then
type its 6-digit code on the 3DS. After that, whenever the console has Wi-Fi:

- the maps follow the server's clock (no need for the clock offset),
- the profile is shared: achievements unlocked on Discord unlock their items here too, and the loadout follows you,
- weekly runs go on the weekly board with a 3DS tag (they never count for the pot or the runner-up refund),
- every run's replay uploads and shows up under 3DS RIDES in the Discord game (Daily Dash ones never get posted to chat).
- the title screen shows the Discord leaderboard (the same top 10 as the Discord boards, with this week's pot, or
  today's Daily Dash board after a Daily Dash ride). It refreshes every five minutes on the menus and after each upload,
  and the last one is kept in `sdmc:/3ds/oldmanburn/board.json` so it still shows offline until its week or day is over.
- BOARD on the title screen shows the whole top 10, weekly or Daily Dash (L / R or the tabs switch). Pick a row with
  up / down and press A (or tap it) to watch that run: it plays back on the map with the rider's look, and B stops it.
  The runs come from the server (`GET /api/3ds/board-replay/<id>`, thinned to 10 notes a second). As in Discord, today's
  Daily Dash runs can only be watched once you've used all of today's Daily Dash tries in Discord (your own any time),
  and runs ridden before the board kept them have nothing to watch.

Runs wait in `sdmc:/3ds/oldmanburn/outbox/` until they're sent. The link is kept in `sdmc:/3ds/oldmanburn/link.txt`
(a `server https://...` line there points the game at another server, for testing). HTTPS goes through curl and
mbedTLS from devkitPro's portlibs (`3ds-curl`, `3ds-mbedtls`, `3ds-jansson`, `3ds-zlib`), with the root certificates in
`data/cacert.bin`.

## Replay codes

Without Wi-Fi, "Replay code" on the game over screen turns the run into a long text code, shows it as a QR code on the
bottom screen and saves it in `sdmc:/3ds/oldmanburn/codes/`. Scan it with a phone and paste it into IMPORT under 3DS
RIDES in Discord. The code is `OMB1.` plus base64url (no padding) of zlib-compressed bytes, little-endian:

```
u8 version (1), u8 mode (0 week, 1 Daily Dash, 2 old map), u32 seed, u32 score, u8 end (0 none, 1 behind, 2 ahead, 3 rail)
u8 nlook, then per item: u8 slot (paint, coat, cap, rims, decal, gear, carrier, cane, trail), u8 length, the item id
u16 nframes, 0.2 s apart
frame 0:   i32 camera*4, u16 bike x*4, u16 bike y*4, u8 flags
frame i>0: i16 camera change (px, rounded), u8 bike x/2, u8 bike y-15, u8 flags
flags: bit 7 throttle, bits 0-6 wheelie*127
```

## Reporting lag

Every ride adds a few lines to `sdmc:/3ds/oldmanburn/perf.txt`: how many frames missed the 60 Hz refresh, how long the
game, sound and drawing took on average and at worst, how often the sound ran dry, and the slowest frames with where on
the road they happened. Send that file along with a lag report. Holding Y shows the CPU and GPU load on the bottom
screen while you play.

To time every cosmetic, open the garage and press SELECT. The bench shows each item on its own for half a second and
writes `sdmc:/3ds/oldmanburn/bench.txt`: the CPU time of a frame with it on (average and worst), and the one-off cost of
drawing the bike sprite. Animated items draw ten frames of it once, then loop them. B stops it early. Emulator timings
mean nothing here, so run it on the console.

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
- `source/main.c`: the screens, input, saves and the clock
- `source/look.c`: the cosmetics, drawn as the web game draws them; static parts are cached in the bike sprite
- `source/ach.c`: achievements, stats and the loadout, saved to the SD card
- `source/ui.c`: the menu look of the web game: buttons with an offset shadow, panels, checkboxes, sliders, D-pad focus
- `fonts/`: DM Mono and Dela Gothic One (SIL Open Font License), the web game's fonts. `fonts/build.sh` turns them into
  the `data/*.bin` system fonts the game embeds
- `source/perf.c`: the performance log
- `source/net.c`, `source/sync.c`: HTTPS on a background thread, and linking / syncing with the Discord game
- `source/replay.c`: recording runs and the replay codes (`source/qrcodegen.c` is Project Nayuki's QR encoder, MIT)

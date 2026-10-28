# Colin McRae Rally 2.0 – 3 and 4 player split screen mod

**Download**: the ready-to-play zip is in
[Releases](https://github.com/Dimax0987/OpenCMR2/releases). Extract it over
a PC installation of the game and run `opencmr2.exe`.
**Versione italiana**: [LEGGIMI-mod-4-giocatori.md](LEGGIMI-mod-4-giocatori.md).

This repository is a fork of [OpenCMR2](https://github.com/pablocpas/OpenCMR2),
the native port of the game, with the mod on the `Coop-Multiplayer` branch;
every change is marked `// PORT:` in the `game/` folder. OpenCMR2's own README,
with the build instructions, is in [docs/README-OpenCMR2.md](docs/README-OpenCMR2.md).
The license is GPL v3, as OpenCMR2's.

This mod takes the game from 2 to 4 simultaneous players on one screen, in
every mode, each with their own profile, car and controller. It is built on
top of OpenCMR2, the modern port of the game
(github.com/pablocpas/OpenCMR2), which already provides widescreen
resolutions, high frame rates and modern gamepad support.

## Contents of the zip

| File | What it is |
| --- | --- |
| `opencmr2.exe` | the game with the mod |
| `SDL3.dll`, `msvcp140.dll`, `vcruntime140*.dll` | libraries the exe needs |
| `README-mod-4-players.md` | this document |
| `LEGGIMI-mod-4-giocatori.md` | the same in Italian |
| `OpenCMR2-4giocatori-NOTE.md` | short technical notes (Italian) |
| `OpenCMR2-4giocatori-src/split-screen-4-giocatori.patch` | every change to the OpenCMR2 source |

## Installation

1. You need a PC installation of Colin McRae Rally 2.0 (the folders `Game`,
   `FrontEnd`, `Sounds`, `CountrySpecific`, `SetupRep`).
2. Extract the zip into the game folder, overwriting `opencmr2.exe` and
   `SDL3.dll` if OpenCMR2 was already there.
3. Run `opencmr2.exe`. On first launch it asks for the game folder if it
   cannot find it by itself. Settings, saves and logs live in
   `%APPDATA%\OpenCMR2\` (`opencmr2.ini`, `opencmr2.log`).

To go back to the original, put back the `opencmr2.exe` and `SDL3.dll` from
the official OpenCMR2 release.

## Before playing: assign the controllers

**Options → Controllers** lists "Player 1 Controller" ... "Player 4
Controller". Give each player their pad: with the default assignment the
first pad belongs to player 3, so do this before starting. The assignment
is saved in `Configuration\Controller.rcf`.

- Player 1: keyboard (arrow keys) or their pad.
- Player 2: numeric keypad or their pad.
- Players 3 and 4: pad only (the choice cycles through the available
  joysticks).

Remapping controls: the control pages (list, device, key redefinition,
calibration) can be navigated with player 1's pad and with the pad being
configured. To change a command, select the entry with the pad and press
the new button or move the new axis. While calibrating an axis the pad
buttons confirm or cancel and the directions move the markers, except when
the axis being calibrated is the stick itself: use the keyboard there.

## The modes and how they work

### Arcade → Single Race

The **Number of Players** page offers 1, 2, 3 or 4 players. With 2 or more
players the advanced race settings page (race type, laps, ...) has the
entry **Number Of CPU Drivers**: from 0 up to 6 − players (the AI tables
hold six cars in total), and below it **Catch Up** as in the original
2-player game. Everyone starts together from the grid, sees and hits each
other.

### Arcade → Championship

After the difficulty the **Number of Players** page appears (1-4). In the
original game this page existed but was not wired up, and the game went
straight to single player. With 2-4 players, 6 − players CPU drivers race
too and count for the championship points. The standings and score screens
include every human player.

### Rally → Single Rally, Championship, Time Trial

Choosing 3 or 4 players brings up the **Split Screen Mode / Alternate
Mode** page, which previously appeared only with 2. It has three entries:

- **Split Screen Mode** ("ghost", as in the original 2-player game):
  everyone on the split screen, simultaneous start from the same point; the
  players do not see or touch each other.
- **Alternate Mode**: one player at a time, as always.
- **Split Screen - Contact** (new): the players see and hit each other and
  start on a 2+2 grid: players 1 and 2 side by side on the line, 3 and 4
  nine metres behind. Simultaneous start.

Each player picks a profile (or guest), car and transmission as usual;
everyone's times go into the rally or championship standings as in the
2-player mode. The Contact/ghost choice is not saved in the championship:
reloading it returns to ghost mode. A championship saved with 3-4 players
reloads with the same number of players.

### Four-way split screen

With 3 or 4 players the screen is split into four quadrants (with 3 players
the fourth stays empty): 1 top left, 2 top right, 3 bottom left, 4 bottom
right. In each quadrant the HUD is laid out as on a full screen (times and
positions top left and top right, gear counter, speedometer, co-driver
notes) at half size; it is adjusted as a percentage with `hud_scale_quad`
in the `[video]` section of `opencmr2.ini` (100 = half size, 120 = a bit
bigger).

### Per-player menus

Each player's menu pages respond only to their own device: profile, car and
transmission choice, car setup before the stage, and the in-race pause
menu. Player 1 can no longer drive the other players' choices. If a
player's pad is not connected, their page answers the keyboard. To let the
keyboard also drive the pages of players 2-4: `menu_keyboard_shared = 1`
in the `[input]` section of `opencmr2.ini`.

### Everything unlocked in multiplayer

With 2 or more players every car (bonus cars included), every country and
stage, the Expert class and every difficulty are available in every mode,
as with the "all cars" and "all tracks" cheats but without their effect on
records. Single player is unchanged. Turn it off with
`multiplayer_unlock_all = 0` in the `[game]` section of `opencmr2.ini`.

## Options added to `opencmr2.ini`

| Section | Option | Default | Effect |
| --- | --- | --- | --- |
| `[video]` | `hud_scale_quad` | 100 | HUD size in the quadrants, as a percentage of half size |
| `[input]` | `menu_keyboard_shared` | 0 | 1: the keyboard also drives the menus of players 2-4 |
| `[game]` | `multiplayer_unlock_all` | 1 | 0: the normal unlocks apply in multiplayer |

## The work done (for anyone reading the patch)

The original game was written for at most 2 players on screen: many tables
had two slots and many checks said "== 2". The patch
(`split-screen-4-giocatori.patch`, every change marked `// PORT:`):

- **Menus**: Number of Players page with 1-4 in arcade; Number of Players
  page wired up in the arcade championship; Split/Alternate page also with
  3-4 players, with the third entry "Split Screen - Contact"; "Number Of
  CPU Drivers" entry in the advanced arcade race settings; four entries in
  Options → Controllers.
- **Views**: four game views instead of two (detail level tables, node
  visibility masks, trackside cameras, per-car camera state). Before this,
  players 3 and 4 could not see the cars, and changing camera or crossing
  the finish line crashed the game.
- **Starts**: 2+2 grid along the route direction in Contact mode; same
  point in ghost mode.
- **HUD**: in four quadrants each view is laid out as a full screen scaled
  to a quarter, with smaller fonts; gear counter and speedometer also work
  for players 3 and 4; split time and record tables extended to 4 cars.
- **Input**: controller slots 3 and 4 saved in `Controller.rcf` (the file
  is 4 bytes longer, still readable by the original); each player drives
  their own menus from their own device; control remapping from the pad;
  force feedback and gear markers for 4 slots.
- **Race logic**: stage finish counters, record messages, co-driver call
  and "wrong way" resets for 4 cars; end-of-race fades for every view; CPU
  drivers selectable in arcade.
- **Unlocks**: with 2+ players the "all cars" and "all tracks" flags read
  as set without touching the records.
- **Performance**: the relief the original applies to the 2-way split
  screen (it hides bushes, poles and minor trackside details) also applies
  with 3 and 4 views; without it there were frame drops at the race start.

Every change was tested with the project's headless probe (15 races with 1
to 4 players in every mode) and on screen with four pads.

## Known limits

- The interior (cockpit) view is available only for players 1 and 2.
- Keyboard keys assigned in the pad configuration of players 3-4 are not
  read during the race: use pad buttons and axes for them.
- With 3 players the fourth quadrant stays black.
- If frames still drop, lower `car_detail` in the `[video]` section of
  `opencmr2.ini`.

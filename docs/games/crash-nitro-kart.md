# Crash Nitro Kart

| | |
|---|---|
| Releases | `BCNE` USA · `BCNP` Europe · `BCNJ` Japan (Crash Bandicoot Bakusou! Nitro Cart) |
| Dumps | `BCNE` cartridge `e6d58aa0` (tested) · `BCNP` cartridge `03925772` (tested) · `BCNJ` cartridge `70f8291f` (tested) |
| Profile | `src/profiles.c`: the `BCNE` entry (with `BCNP`; `BCNJ` a variant) |
| Tests | `tests/regress/crash-nitro-kart.json` (`tools/regress.py crash-nitro-kart`) |
| Status | in testing (branch `game/crash-nitro-kart`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list crash-nitro-kart`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

The second screen follows [Mario Kart: Super Circuit](mario-kart-super-circuit.md)'s: the race HUD moves there, with the course map twice as big over the course's own scenery, and what you watch while you drive (the item you hold and your place) stays on the game screen.

## Rules

**CNK-1. Logo.** The logo is the title's tyre with the lettering, the flame and ™, without the swirl behind it, taken from power-on (Japan: its own title, the tyre and its Japanese lettering, without the space behind). Only the logo's outline is stored in GBA DS (`src/logo_masks.c`); its pixels come from the game.

**CNK-2. HUD moved, not copied.** While driving (a race of any kind, Time Trial, the Adventure hub: whenever the game draws its course map in the bottom-right corner), each frame is drawn twice: the game screen without the HUD and the second screen with only the HUD, so nothing behind the HUD is lost. It starts once the game has faded the course in (the HUD is up a few frames before that), so the scenery behind the map (CNK-4) isn't taken half faded. The HUD is the game's top layer (BG0) and its priority-0 sprites in the HUD's places: the place numbers by the faces, the wumpa icon, the hub's trophy and key, the racers' dots on the map. The item you hold, the countdown lights (also as they fly off at GO) and everything over the track stay on the game screen.

**CNK-3. Driving: game screen.** The game screen keeps, where the game draws them: the item you hold (top centre), your place (bottom left), anything the HUD draws in the middle of the screen (the turn signs), and on the hub the name of the place you're driving towards (bottom centre).

**CNK-4. Driving: second screen.** The second screen has the time, the wumpa count and the lap along the top (on the hub: the trophies and keys), the four leading racers' faces with their places down the left (not in Time Trial or on the hub, which have none), and the course map with every racer's dot, twice as big, in the middle, over a dimmed picture of the course's sky and scenery taken as the race starts.

**CNK-5. Results.** After the finish, RACE RANKINGS (the faces, names and times) is on the second screen, on the course's scenery, while the karts drive on on the game screen, until A takes the game on.

**CNK-6. Pause and menus.** The pause menu stays on the game screen, with the last HUD on the second screen until the race is back. The camera shot after RACE RANKINGS, TRY AGAIN?, the title, the menus and the story's scenes show the logo.

**CNK-7. One screen.** With `layout = single` the HUD stays where the game draws it.

## How it's told apart

All three releases draw the race HUD on BG0 (BG0CNT 0x1800, its map at 0x0600C000) in display mode 1 (DISPCNT 0x1741), and so do the end-of-race menus, the pause and the hub; its tiles are handed out as each screen is built, so no tile number is fixed. What the map holds tells the screens apart:

- driving (CNK-2): the course map is an 8x8 block of map cells, rows 12-19, columns 22-29, filled whether or not the course's shape reaches them: cell (16, 26) not blank;
- results (CNK-5): the course map's cells blank, the faces' column filled: cell (3, 0) not blank;
- TRY AGAIN? and pause: both blank (pause: the game takes its HUD off BG0).

The fade-in is 0x03002A5B (0x03002A6B in Japan): 0xB0 to 0xF0 as a race fades in through its palettes, 0xFF from then on (held at 0xFF through a whole race, items and hits included).

The pause is the player's byte at 0x030045A1 (USA, Europe; 0x030045D1 in Japan, whose player block is 0x30 higher), 1 from START until the HUD is back after RESUME.

Other addresses found (for tests and later rules): the player's lap count 0x0300448C (laps done, 0-3), the auto-pilot byte 0x0300458E (1: the game drives; from the game's CodeBreaker auto-pilot code, used to drive test races to their finish), the wumpa count 0x03004584; Japan 0x30 higher. The decompilation (RayCarrot/CrashNitroKart-GBA-Decomp) names the classes (HUD, PauseMenu, RankingsMenu...) but no RAM.

## Accepted (not listed as a known issue)

- As a race fades in, the game puts its HUD up about 8 frames (an eighth of a second) before the fade ends: the HUD shows on the game screen over the dark picture for those frames, and the second screen keeps the logo, before the HUD moves over. Moving it sooner would take the scenery behind the map half faded (CNK-2). To check on the device.

## Not covered yet

- Adventure's events other than its races: the boss races, the CNK challenges (C, N and K to collect), the relic races (time boxes), the crystal arenas, and the gem cups. They're expected to draw the same race HUD (the map in its corner), but none has been tried; the crystal arenas and relic races may have HUD pieces of their own.
- Time Trial's results and the cup races' standings between races: not tried; without the faces' column or the map they'd show the logo.
- The hub's HUD (CNK-2 to CNK-4) is from one moment in Velo's Citadel; the other worlds' hubs are expected to be alike. Its sky is the plain backdrop colour, not a layer, so behind its map the second screen is mostly dark, with a dimmed strip of the wall. *By hand: the hub's name of the place ahead (bottom centre) staying on the game screen; it wasn't up in any saved moment.*
- Link play needs a link cable: not tried.
- The Crash Superpack (USA, `B8AE`, with Crash Bandicoot 2: N-Tranced) is another game code: not supported.

## Exceptions

- The course map drawn twice as big (as Mario Kart's and the F-Zero games'): a GBA pixel per 2x2 of the second screen's GBA screen, so still the game's own picture at its own resolution.

## For the merge

Changelog line (under the open version's What's new):

- Crash Nitro Kart (USA, Europe and Japan) is supported: while driving, the race HUD moves to the second screen (the time, wumpa fruit and lap along the top, the leaders' faces down the side, and the course map twice as big over the course's scenery), and the item you hold and your place stay on the game screen; after the finish, the race rankings; the logo in the menus.

`wiki/Supported-games.md`, the table row and the section:

| Crash Nitro Kart | `BCNE` `BCNP` `BCNJ` | the race HUD: time, wumpa fruit, lap, the leaders and the course map; the race rankings |

### Crash Nitro Kart

The USA, European and Japanese releases.

While driving (races, Time Trial and the Adventure hub), the game's HUD moves to the second screen, as in Mario Kart: Super Circuit: the time, wumpa fruit and lap along the top (on the hub, your trophies and keys), the four leaders' faces with their places down the left, and the course map with every racer's dot, twice as big, in the middle, over a dimmed picture of the course's sky and scenery. The item you're holding and your place stay on the game screen, where the game draws them, as do the turn signs and the countdown. After the finish, the race rankings are on the second screen while the karts drive on. Pausing keeps the last HUD on the second screen; TRY AGAIN, the title and the menus show the logo.

Known issues: Adventure's boss races, CNK challenges, relic races and crystal arenas, Time Trial's results and link play haven't been tried yet.

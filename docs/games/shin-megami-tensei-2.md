# Shin Megami Tensei II

| | |
|---|---|
| Releases | `A5TJ` Japan (the only release; played in English with a port of the Super Famicom game's fan translation, as a patch or a patched ROM) |
| Dumps | `A5TJ` cartridge `af40cc99` (tested) |
| Mods | `A5TJ` English (port of the Super Famicom translation) `7e4b4978` (tested) |
| Profile | `src/profiles.c`: the `A5TJ` entry (`smt2_compose`: Shin Megami Tensei's `smt_*` with this game's addresses) |
| Tests | `tests/regress/shin-megami-tensei-2.json` (`tools/regress.py shin-megami-tensei-2`) |
| Status | supported (merged into dev Nov 26, 2026, for v1.6.4) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list shin-megami-tensei-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game came out only in Japan (the 2003 port of the Super Famicom game, a year after the first game's). Most players play it in English with a port of the Super Famicom game's fan translation to the GBA ROM: an xdelta patch for the Japanese ROM that makes a 16 MB ROM (GEN-9). It runs on [Shin Megami Tensei](shin-megami-tensei.md)'s engine: the same first-person view, the same automap screen pixel for pixel, the game's state at other addresses. So its second screen is the first game's, rule for rule (SMT-1 to SMT-8), from the same code.

## Why this screen

As in the first game: it is a dungeon crawler, and what a player keeps opening is the map (L), the game's own automap, which takes the place of the first-person view while it's open. So the second screen is that map, opened on the hidden copy and taken as it is, with the compass and the floor's name; the game screen keeps the view, the party and the moon. Everything you act on (the command bar, fights, conversations, the rooms' menus) stays on the game screen. Chosen to be the same as the first game's.

## Rules

**SMT2-1. Logo.** Whenever there's no map to show (SMT2-6), the second screen shows the logo: the title's lettering, 真・女神転生II, red on its own, without the pattern and the city behind it, PRESS ANY BUTTON or the copyright line. (GEN-1.)

**SMT2-2. The map.** While walking about in the first-person view, the second screen is the game's automap (L), as the game draws it: the explored floor, the party's marker (a green arrow showing which way you face), the map markers you set (SMT2-8) and the compass at the top right, with the floor's name in the box under it. The party's portraits and bars at the bottom of the game's screen aren't part of it (the game screen has them: nothing is on both, GEN-2). The map is at the game's own size (GEN-5), filling the second screen's width; its height is SMT2-3's. Unlike the first game, the automap is there from the first dungeon on (the Virtual trainer, reached from the training room where a new game starts).

**SMT2-3. The map, taller than the game's window of it (exception).** As SMT-8: the game's automap shows a window of 88 rows of the floor and the second screen is 160 rows tall, so the window is made taller from the game's own scrolling (UP and DOWN while it's open, 4 steps of 8 rows each way on the hidden copy), 145 rows in all, with the compass's box at the top right and the floor's name box under the map (15 rows). Past the pieces the game's scrolling gives, and where a floor is small (the Virtual trainer is a room of 4 by 3), the game's own blue grid and the grey bars at its sides go on as the game draws them. Nothing is drawn that the game doesn't draw. The scroll arrows the game puts beside its window when a floor is taller than it (▲ and ▼ under the compass, new in this game) are left out: they are prompts for the game's own window (the second screen has none), and on the taller map they came out twice and in the wrong places.

**SMT2-4. It follows you.** As SMT-3: a step, a turn on the spot or a change of floor takes a new map as soon as it has held for 3 frames (GEN-6); the arrow turns with you, and a map is never half drawn.

**SMT2-5. Fights, menus, talk and rooms.** Where the first-person view is covered or taken over (a fight, the command bar, the START menu, the map itself open on the game screen, a conversation) the second screen keeps the last map, as in SMT-4. So do the picture rooms between dungeons, where the game shows a room and a menu instead of the first-person view (Okamoto's training room with its Save, Training and Go out, the Virtual Battler's room, the save screen, a night's sleep): the player is still where the last map was taken. (GEN-3.)

**SMT2-6. Title and the rest.** On the title and its menu, the file lists and before the first map of a session (a new game opens in the training room, a picture room), the second screen shows the logo (SMT2-1), and the map comes at the first capture once you walk about.

**SMT2-7. Game screen untouched.** Nothing leaves the game screen: the view, the compass letter, the moon, the MAG counter, the party's panels, the command bar and the messages stay where the game draws them. (GEN-2.)

**SMT2-8. Map markers.** The automap's markers (new in this game: A in the map, a slot, a place, one of five kinds: NORMAL, EVENT, PRIZE, BOSS, OTHER) are drawn on the map by the game, so they show on the second screen with it. The marker list, the cursor and the kinds' menu stay on the game screen (while the map is open there, the second screen keeps the last map, SMT2-5).

**SMT2-9. The English translation.** On the English patch (as a patched ROM, or with the xdelta in the Patches menu, GEN-9), SMT2-1 to SMT2-8 hold as they are: the same addresses and screens, the floor names in English ("Virtual Space", "Valhalla-Downtown"; the patch leaves the Virtual Battler's first level as ヴァーチャル空間). The logo is the same as the Japanese game's: the patch keeps the title's lettering. The patch leaves the marker menu's kinds help and prompt in Japanese (text the Super Famicom game didn't have). GBA DS knows the patch and its result by their checksums: the game list says "Romhack: English (port of the Super Famicom translation)" under the logo for the patched ROM, and the same with "(patch)" for the Japanese ROM with the xdelta beside it or switched on in the Patches menu (GEN-8).

## Exceptions

SMT2-3 is SMT-8's exception, for the same reason (to fill the second screen instead of leaving it part empty), with the same cost: the hidden copy runs about 300 frames per capture instead of 70.

## Not covered yet

- **Past Valhalla's streets.** Played: the training room, the Virtual trainer, Valhalla-Downtown (the town's first street, in the first-person view with the automap: Okamoto's gym, a bar and the public Virtual Battler on it), the Virtual Battler's first level and Virtual Space, where the hero's true name is asked; also by hand on the desktop build, where it plays as the first game does. Go out is refused until the hero has levelled up in the trainer (level 7 in English and 9 in Japanese here, after a night's sleep). Stairs, the overworld, the Center and later dungeons are untried, and so are the shops and bars.
- **A floor taller than the game's window.** The town's map scrolls (UP and DOWN, so SMT2-3's pieces are taken: `smt2-*-town`), but its explored part fits in the game's own window of 88 rows, so the map from above or below the window hasn't been seen yet.
- **Conversations inside a dungeon, shops and the name entry** aren't covered by a case (the Virtual Battler's room and its talk are, `smt2-en-battler`).
- **A step onto a place that starts a conversation** (the Virtual trainer's exit, where Okamoto asks; the Virtual Battler's door) keeps the map from before that step: the game is in a conversation as soon as the step ends, so no capture runs there (`smt2-en-room`'s first shot).

## Facts found so far (for sessions)

| What | Japan (and the English patch, byte for byte) |
|---|---|
| Mode (`0x03004550`) | 0 title, 0x01 walking, 0x04 a step or a door, 0x0A command bar, 0x10 map, 0x15 fight, 0x16 a fight's end, 0x19 picture rooms, conversations and other events |
| Position | x `0x03004562`, y `0x03004564` (bytes; `0x03004566` / `0x03004568` follow them), facing `0x03004552` (0 N, 1 E, 2 S, 3 W): the first game's, 0xE0 further on |
| Area | `0x03004614` (0 the training room and Valhalla's streets, 1 the Virtual trainer, 4 Virtual Space, 8 the Virtual Battler's first level) |
| Automap scroll | `0x0200DFCC` (the first game's `0x0200BFCC` code, 0x2000 further on; moves in Valhalla's streets, the first map that scrolls) |
| Title | the lettering, PRESS ANY BUTTON and the copyright line are sprites; the lettering's lowest row (転) is above row 80 |
| Boot | the Atlus logo and the opening for about 2 000 frames; START twice for the title. L opens the map, B closes it, A the command bar; A in the map opens the markers |
| New game | starts in the training room (Okamoto, mode 0x19): Training is the Virtual trainer; its exit is facing east where it starts (Okamoto asks; once the question is written out, YES is chosen). Go out leads to Valhalla-Downtown once the hero has levelled up (see Not covered yet) |
| Valhalla-Downtown | the public Virtual Battler is west of (3,3); Virtual Battle, Level 1 (20 Macca) is A through its menus' defaults |


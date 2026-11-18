# Shin Megami Tensei

| | |
|---|---|
| Releases | `AAUJ` Japan (the only release; played in English with the iOS version's translation ported by Gymzatan, a mod of it: with or without its font hack) |
| Dumps | `AAUJ` cartridge `b857c3c5` (tested) |
| Mods | `AAUJ` English v1.3b iOS Localized (Gymzatan) `f74db49f` (tested), English v1.3b iOS Localized with the font hack (Gymzatan) `2050b471` (tested) |
| Profile | `src/profiles.c`: the `AAUJ` entry (`smt_*`) |
| Tests | `tests/regress/shin-megami-tensei.json` (`tools/regress.py shin-megami-tensei`) |
| Status | working on the branch `game/shin-megami-tensei`, in test (not merged) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list shin-megami-tensei`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

The game came out only in Japan (the 2003 port of the Super Famicom game). Most players play it in English with Gymzatan's translation, which brings the iOS version's English text to the Japanese ROM (a 16 MB ROM, or a patch for the Japanese one, GEN-9), with or without a font hack that changes how the text is drawn. The rules hold for all of them (SMT-7).

## Why this screen

Asked what the game would have put on a second GBA screen: it is a dungeon crawler, and what a player keeps opening is the map (L), the game's own automap, which takes the place of the first-person view while it's open. So the second screen is that map, opened on the hidden copy and taken as it is, with the compass and the floor's name; the game screen keeps the view, the party and the moon. Everything you act on (the command bar, battles, conversations, the shops) stays on the game screen.

## Rules

**SMT-1. Logo.** Whenever there's no map to show (SMT-5, or a place where the game's map doesn't open), the second screen shows the logo (the title's lettering, 真・女神転生, red on its own, without the pattern behind it or the copyright line). (GEN-1.)

**SMT-2. The map.** While walking about in the first-person view (a dungeon, a building, a town street), the second screen is the game's automap (L), as the game draws it: the explored floor, the party's marker (a green arrow showing which way you face) and the compass at the top right, with the floor's name in the box under it. The party's portraits and bars at the bottom of the game's screen aren't part of it (the game screen has them: nothing is on both, GEN-2). It's the 240x160 picture's top part, at the game's own size (GEN-5), centred on the second screen.

**SMT-3. It follows you.** A step, a turn on the spot or a change of floor takes a new map as soon as it has held for 3 frames (GEN-6); the arrow turns with you.

**SMT-4. Fights, menus and talk.** Where the first-person view is covered or taken over (a fight, the command bar's COMP, MAGIC, ITEM, EQUIP, STATUS and CHANGE, the START menu, a conversation, a shop), the second screen keeps the last map, as the player is still in the same place. (GEN-3.)

**SMT-5. Title and the rest.** On the title, the file lists, the name entry and while the game isn't in a place you can walk about, the second screen shows the logo (SMT-1), and the map comes back at the first capture once you walk about again.

**SMT-6. Game screen untouched.** Nothing leaves the game screen: the view, the compass letter, the moon, the MAG counter, the party's panels, the command bar and the messages stay where the game draws them. (GEN-2.)

**SMT-7. The English translation and its font hack.** On Gymzatan's English v1.3b (iOS Localized, as a patched ROM or with its patches on the Japanese ROM, GEN-9) and on the same with the font hack, SMT-1 to SMT-6 hold as they are: the same addresses and screens, the floor names in English. The logo is the one the title shows. Known issue: GBA DS keeps one logo per game (made from whichever of them is started first), so with both the Japanese game and a translation on the card, they all show that one's title.

## Not covered yet

- **Areas without an automap.** The map isn't tried before the game gives you the Automap app (the first dungeon of a new game), nor in towns or on the overworld, if the game has any. Where L does nothing the last map stays; it isn't switched to the logo.
- **Floor changes** are followed by the position bytes and the area byte, but only walked in the three save files' floors; stairs and teleporters elsewhere haven't been tried.
- **Conversations, shops and name entry** aren't covered by a case; they're expected to keep the last map (SMT-4), with the name entry on the logo (SMT-5).
- **The logo** is made from whichever of the Japanese ROM and the translation you start first (the lettering differs), and both then show that one.
- **Fights on the translation** have no case (the Japanese fight's steps led to a different encounter there); the fight is on the game screen only in either.

## Facts found so far (for sessions)

| What | Japan (and the translation, byte for byte) |
|---|---|
| Mode (`0x03004470`) | 0 title and file lists, 0x01 walking, 0x0A command bar and START menu, 0x10 map, 0x15 fight, 0x19 name entry or other events (0x02 was seen in the new-game intro: unconfirmed) |
| Position | x `0x03004482`, y `0x03004484` (bytes; `0x03004486` / `0x03004488` follow them), facing `0x03004472` (0 N, 1 E, 2 S, 3 W) |
| Area | `0x030044EC` / `0x03004C92` (differs per save: 0x23, 0x11, 0x1D) |
| Save | SRAM, 32 KB (the owner's SharkPort file, `tools/sharkport.py`, no `--magic`) |
| Boot | the Atlus logo and the opening play for about 2 000 frames; START skips them. L opens the map, B closes it, A the command bar, START the system menu |

## For the merge

Changelog line (under the open version):

- **Shin Megami Tensei** (Japan), and Gymzatan's English translation (v1.3b iOS Localized, with or without the font hack), as patched ROMs or with the patches in the Patches menu: while you walk a dungeon the second screen shows the game's own automap (the floor, the compass and the floor's name), following each step and turn, and it stays up through fights, the command bar and menus; the title and file lists show the game's logo. Tried on the three floors of the test save; the first dungeon of a new game, towns and floor changes haven't been tried yet. The logo is made from whichever of the Japanese game and the translation you start first, and both then show that one.

Wiki section for `Supported-games.md`:

> **Shin Megami Tensei** (Japan; Gymzatan's English translation v1.3b, with or without its font hack). The second screen shows the game's automap, which you open with L, kept up to date as you walk and turn. The party panels at the bottom of the map screen are left off, so nothing is on both screens. Fights, menus and conversations keep the last map; the title and file lists show the logo. Known issues: not tried in a new game's first dungeon or on floor changes outside the test save; one logo is kept for the Japanese game and the translation.

Also for the merge: `docs/patches.md` already has the two patches; run `tools/make_dumps.py` after merging `dev` if a Releases row changed, and the README's game count goes up by one.

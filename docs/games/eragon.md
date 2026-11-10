# Eragon

| | |
|---|---|
| Releases | `BENE` USA · `BENP` Europe (English, French, German, Spanish, Italian; a language screen before the title; the same addresses) |
| Dumps | `BENE` cartridge `af8af2e8` (tested) · `BENP` cartridge `bb05a63f` (tested) |
| Profile | `src/profiles.c`: the `BENE` / `BENP` entry (`er_compose`) |
| Tests | `tests/regress/eragon.json` (`tools/regress.py eragon`) |
| Status | testing (branch `game/eragon`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list eragon`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**ER-1. Towns and dungeons: the Party screen.** Walking about where L opens no map (a town, a house, a cave, a castle), the second screen shows the Party screen (START, Party), the whole screen at the game screen's size, as the game draws it: each member's portrait, name, HP and XP bars, focus and level, and the party's gold. It's opened on the hidden copy at every refresh, so it stays up to date without the player opening it. With the START menu or the Party screen open in the game, the same.

**ER-2. Every card lit.** The game dims every member's card but the chosen one (the first, as the screen opens), so the second screen shows each card as the game draws it when that member is chosen (DOWN on the hidden copy to each in turn): all of them at full brightness, nobody singled out. An empty slot (fewer than three in the party) stays as the game draws it.

**ER-3. The overworld: its map.** Wherever L opens the map (the overworld between towns, and wild places such as the Hadarac desert), the second screen shows that map, the whole screen as the game draws it: the region with its place names, and Eragon's figure where the party is. Opened on the hidden copy at every refresh (L there tells whether the place has a map); with the map open in the game, the same.

**ER-4. Last one stays.** Where neither can be opened (a conversation, a scene, a door's fade, the debug menu), the last picture stays; before the first, the logo.

**ER-5. Battles: the party's HP.** In a battle, whenever the game shows the party's name plates with their HP bars along the top of the screen (as each attack plays out, the enemies' too), they go to the second screen instead, at its top as the game draws them, the logo below (GEN-1), their bars going down as they are hit. While they're not up (choosing a command, a conversation, the victory's XP windows) the last picture of them stays; before they first show in a battle, the logo. The map or Party screen from before is dropped, and taken again once walking about resumes. *Until Nov 9, 2026: the logo for the whole battle.*

**ER-6. Title.** On the title screen (and the European release's language screen before it) the second screen shows the logo, and no captures are tried there.

**ER-7. Logo.** The logo is the title's carved "eragon" alone, cut out of the stone it's carved into along the owner's outline of the word (the film's logo), one pixel wider so its carved rims come too; no ™, sparkles, PRESS START or copyright lines. *Until Nov 9, 2026: the word on its strip of stone, with the ™.*

**ER-8. Game screen.** Nothing leaves the game screen but the battle plates (ER-5); the combo track that takes their place while a command is chosen stays.

**ER-9. Quick to follow.** Going from the overworld into a town or back out, and leaving a battle, a capture starts at once (GEN-6) instead of waiting for the next refresh.

## Facts found so far (for sessions)

The same addresses in both releases.

| What | Where / value |
|---|---|
| The screen | `0x03005660` (byte): 1 in the field (walking, or a scripted walk), 3 the title, 4 the debug menu, 6 a conversation (in the field or a battle), 7 the European language screen, 13 (0x0D) the START menu, 14 (0x0E) the Party screen, 16 (0x10) a battle, 22 (0x16) a battle's victory, 26 (0x1A) the overworld map; 0x8x / 0x9x for a moment between |
| The kind of place | `0x03002330` (byte, outside battles): 12 (0x0C) the overworld, 10 (0x0A) a town or dungeon, but the Hadarac desert, which has a map, reads 10 too; 0 the title; in a battle it runs through the battle's own steps. Only used to follow the player (ER-9): pressing L tells where there's a map (the screen byte reads 0x9A at once, the map's way in; in a town it stays 1) |
| A battle | `0x0200CAF1`: 1 from a battle's start to its end, 0 elsewhere (2 seen once just after a debug jump, before its battle; 5 or 6 in the debug menu's battles) |
| Timing | The START menu takes A ~50 frames after the screen byte reads 13 (its eye keeps moving: no still picture tells when). After leaving the menu the field takes no START or L for ~120 frames, ~140 after the map. A press must be held a few frames (2 is missed) |
| Party cards | 56 rows apart (0-55, 56-111, 112-159); the chosen one undimmed |
| Battle plates | BG3 (BGxCNT 0x1C08), its top three rows of tiles: the map entry at `0x0600E002` is 0x0212 (the first plate's corner) exactly while the plates are up, 0 with the combo track; the red bars are sprites at row 11, priority 0. (`0x0200BF71`, `0x030023AC` follow them only in some battles.) The debug menu's Combat runs battles with `0x0200CAF1` at 5 or 6, so a battle is taken as it being anything but 0 |
| Debug menu | CodeBreaker `730051C4 0200` / `83005660 0004` (TCRF): write 4 to the screen byte in the field. Jump to Story reaches every chapter (the harness: `poke 0x03005660 2 4`) |

## For the merge

- Changelog: "Eragon: the Party screen on the second screen in towns and dungeons, every member's card lit, and the overworld map between them; in battles, the party's HP bars."
- Wiki (Supported games): **Eragon** (USA, Europe). In towns and dungeons the second screen shows the Party screen: everyone's HP, XP, focus and level, and the party's gold, every card lit. On the overworld it shows the game's map with the party's place on it. In battles the party's name plates and HP bars move there from the top of the game screen. On the title it shows the logo; in conversations and scenes the last picture stays.

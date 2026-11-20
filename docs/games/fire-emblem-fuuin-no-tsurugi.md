# Fire Emblem: Fuuin no Tsurugi (The Binding Blade)

| | |
|---|---|
| Releases | `AFEJ` Japan (the only release; played in English with the FE6 Localization fan translation, a mod of it) |
| Dumps | `AFEJ` cartridge `d38763e1` (tested), Virtual Console `f8af3359` |
| Mods | `AFEJ` English v1.2.1 (FE6 Localization fan translation) `197de10b` (tested) |
| Profile | `src/profiles.c`: the `AFEJ` entry (Fire Emblem's `fe_virt` / `fe_compose`; its addresses in `feRegions`) |
| Tests | `tests/regress/fire-emblem-fuuin-no-tsurugi.json` (`tools/regress.py fire-emblem-fuuin-no-tsurugi`) |
| Toggle | `pages`: a unit's three pages (FE6-2) · GEN-14 |
| Status | supported (merged into dev Nov 16, 2026, for v1.5.4) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list fire-emblem-fuuin-no-tsurugi`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-12).

The game came out only in Japan. Most players play it in English with the FE6 Localization fan translation (a UPS patch for the Japanese ROM, the English title The Binding Blade): the rules hold for it as for the Japanese game (FE6-6).

It runs on the engine of the later Fire Emblem games, and GBA DS treats it the same way (their rules: [fire-emblem.md](fire-emblem.md), FE-1 to FE-10, and [fire-emblem-sacred-stones.md](fire-emblem-sacred-stones.md)); each rule here matches one there.

## Why these screens

Asked what the game would have put on a second GBA screen: what a player keeps opening on the map is a unit's status screen (R), to check a unit's stats, items and weapon levels, theirs or an enemy's, before moving. It's the game's own screen, opened on the hidden copy for the unit under the cursor and taken as it is. Everything the player acts on (the unit, terrain and goal windows, menus, the battle forecast, conversations, the phase banners) stays on the game screen, as in the other Fire Emblem games.

## Rules

**FE6-1. Logo.** Whenever the second screen has no unit's status to show (FE6-2), it shows the logo (the title's: the sword, FIRE EMBLEM, the lettering with its shadow and the subtitle's ribbon, without Press start or the copyright line): on the title and menus, the preparations before a chapter, in story scenes (FE6-3), in fights and on the other armies' turns (FE6-4), and on the map with the cursor on no unit or with a unit selected. (FE-1.)

**FE6-2. Unit status.** With the cursor free on a unit, yours or another army's, the second screen is that unit's status screen (R), as the game draws it, filling the screen, turning through its three pages every 5 seconds (from the page the game last showed); taken again only when the cursor or a unit changes. Once a unit is selected (moving it, its menu, the forecast) the logo comes back until the cursor is free again. (FE-3.)

**FE6-3. Story scenes.** Story scenes before, during and after the battle (a picture behind the talk, or the world map) stay on the game screen, and the second screen shows the logo. (FE-7.)

**FE6-4. Fights and other turns.** In a fight and on the other armies' turns (their phase banner too), the second screen shows the logo. (FE-9.)

**FE6-5. Game screen untouched.** Nothing leaves the game screen: the unit, terrain and goal windows, menus, the battle forecast, conversations and the phase banners stay where the game draws them. (FE-10.)

**FE6-6. The English translation.** On the FE6 Localization fan translation (v1.2.1, as a patched ROM or with its UPS patch on the Japanese ROM, GEN-9) FE6-1 to FE6-5 hold as they are: the same addresses and screens, the status screen in English. Its logo is the translation's own title (FIRE EMBLEM in English, The Binding Blade on the ribbon, ™ for ®), taken the same way. Known issue: GBA DS keeps one logo per game, made from whichever of the two is started first, so with both the Japanese game and the translation on the card, both show that one's title.

## Not covered yet

Tried from the owner's save (GameShark, "17515" on GameFAQs: the end of chapter 22, the final chapter and chapter 23 saved): the end of chapter 22, the preparations and chapter 23's first turns (a unit's status, ours and an enemy's, the cursor on no unit, a unit moving, its menu, fights in both phases, the enemy phase and its banner, a story scene during the battle), in Japanese and, from the same save, in English. Not tried yet: the early chapters, fog of war, the link arena (needs a link cable), the trial maps (Extras), and the older English patch in docs/patches.md (Dark Twilkitri's IPS).

## Facts found so far (for sessions)

| What | Japan (and the translation) |
|---|---|
| Chapter data (gPlaySt; 0 off the map) | `0x0202AA48` (chapter number +0xE, whose turn +0xF) |
| Process slots (64 of 0x6C) | `0x02023CC4` |
| Map cursor (gBmSt + 0x14) | `0x0202AA1C` |
| Units (0x48 bytes each, x and y at +0x0E / +0x0F; 62 + 50 + 20 + 5) | `0x0202AB78` |
| Player phase script | `0x085C56A8` |
| Event engine | `0x085C40A4` |
| Battle scene | `0x085CB528` |
| Phase banner | `0x085C5AA0` |
| Status screen | `0x08677680` |
| Player phase positions | cursor free `0x70`, a unit's menu `0xC8`, a fight `0xE8` (Fire Emblem: `0x70`, `0xD0`, `0xF0`) |

gPlaySt and the units were mapped from Fire Emblem (Japan)'s by `tools/romxref.py`; the process slots found by their free list (it follows them in RAM); the scripts by matching the [fireemblem6j decompilation](https://github.com/FireEmblemUniverse/fireemblem6j)'s (ProcScr_PlayerPhase, ProcScr_Event, ProcScr_EkrBattle, ProcScr_PhaseIntro, ProcScr_StatScreen) against the ROM by their command sequences, and checked running. The unit struct is laid out a little differently from the later games' (x and y 2 bytes earlier), so FeRegion has `unitXY` and `nUnits`. The fan translation keeps every one of these. Boot: no health and safety screen; START skips the intro to the title. The save is SRAM (32 KB); the owner's GameShark file converts with `tools/sharkport.py` (it pads to 64 KB: the fixture is the first 32 KB).


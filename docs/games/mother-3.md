# MOTHER 3

| | |
|---|---|
| Releases | `A3UJ` Japan (the only release; played in English with the fan translation, a mod of it) |
| Dumps | `A3UJ` cartridge `42ac9cb9` (tested), Virtual Console `c704a567` |
| Mods | `A3UJ` English v1.3a (fan translation) `e73b0f7d` (tested), English v1.3 (fan translation) `0e013b7b` (tested), English v1.3 (fan translation; its published UPS) `8a3bc5a8` (tested), English v1.3a + HQ audio mix (MusicTheorist) `789605cd` (tested), English v1.3 + HQ audio mix (MusicTheorist) `57f94dcf` (tested), English v1.3 + HQ audio mix (MusicTheorist; its UPS) `91ac31cb` (tested), HQ audio mix (MusicTheorist) `add15282` (tested), HQ audio mix alternative (MusicTheorist) `b7faa37d` (tested) |
| Profile | `src/profiles.c`: the `A3UJ` entry (`m3_compose`) |
| Tests | `tests/regress/mother-3.json` (`tools/regress.py mother-3`) |
| Status | being added (branch `game/mother-3`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list mother-3`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-9).

MOTHER 3 came out only in Japan. Most players play it in English with the fan translation (Chewy, Jeffman, Tomato and others; a UPS patch for the Japanese ROM), often with MusicTheorist's HQ audio mix: the rules hold for those as for the Japanese game (M3-8, M3-9).

## Why these screens

Asked what the game would have put on a second GBA screen: the two things a player keeps opening while walking about are the area map (R, where the area has one) and the party's Status (START, Status). Both are the game's own screens, opened on the hidden copy and taken as they are, with their button prompts left out. The HP and PP meters in a battle stay on the game screen: they roll as the party takes damage, and acting before they reach zero is the game, so they're something to react to, not to glance at.

## Rules

**M3-1. Logo.** On the title, the file screens and before the first picture, the second screen shows the logo: the title's MOTHER 3 lettering with its ®, without NEW GAME, LOAD, SOUND PLAYER or the copyright lines (BG3 of the title, over black, taken from power-on past the health warning and, in the translations, their title card). The translations keep the Japanese title, so the logo is the same in every version.

**M3-2. The area map.** Walking about where R opens a map of the area (Tazmily and the Nowhere Islands, Mt. Oriander, New Pork City's highway...), the second screen shows that map, opened on the hidden copy: the whole screen as the game draws it, where the game opens it, with its marker of where the party is (the blinking red dot, the circle on Mt. Oriander) shown, and without the white scroll arrows at its top and bottom edges (prompts to scroll it, which the second screen can't). Entering another area, its map (or the Status, M3-3) is taken at once; otherwise again at every refresh, so the marker follows the party.

**M3-3. The Status where there's no map.** Walking about where R opens nothing (indoors, the Empire Porky Building...), the second screen shows the Status screen (START, Status, A): the party's portraits with the one shown framed, the DP on hand and the stats window, as the game draws them, but without the L and R badges and their orange arrows at either end of the portraits (prompts to turn the page) and without the help window at the bottom ("Press the A Button for skill info.", a prompt): the menu's brown background there, as in the gap above it. It turns through the party, one every 5 seconds (R on the hidden copy), back to the first.

**M3-4. A menu or the map open in the game.** With START's menu, one of its screens or the area map open in the game, the second screen shows what it would walking about there (M3-2, M3-3): the hidden copy closes them first (B).

**M3-5. Last picture stays.** In a conversation, a scene, between rooms, and anywhere else START and R open nothing, the last picture stays; before the first, the logo.

**M3-6. Title and file screens.** On the title, the file screens and before them (the area number 0), the second screen shows the logo and no captures are tried there.

**M3-7. Game screen untouched.** Nothing leaves the game screen.

**M3-8. The English translation.** On the fan translation (v1.3 and v1.3a, as a patched ROM or with its UPS patch on the Japanese ROM, GEN-9) M3-1 to M3-7 hold as they are: the same addresses, screens and sprites, the Status in English ("Status", "Level", "Offense"...), its help window ("Press the A Button for skill info.") left out the same way.

**M3-9. The HQ audio mix.** MusicTheorist's HQ audio mix (only the music changes) keeps M3-1 to M3-8: on the Japanese ROM (its two versions, "Default" and "Alt.") and on the English translation. Its patches are UPS files made for one exact ROM each (GEN-9: a UPS has to be first and can't follow another patch): the Japanese ones for the Japanese ROM, the English one for one build of the English v1.3 ROM (`0e013b7b`, not the result of the published v1.3 UPS, `8a3bc5a8`, nor v1.3a). So on a Japanese ROM GBA DS can play the translation or the Japanese HQ mix from the Patches menu, but not the translation and the HQ mix together: for that, the ROM already patched with both (the patch makers' own) is needed, or the v1.3 ROM it was made for with the English HQ patch.

## Not covered yet

- **Battles.** No battle state yet (the owner's saves are all before the last battle, and the areas reached with them have no enemies left). In a battle the hidden copy's START and R open nothing, so the last picture stays (M3-5). The plan, once a battle state shows how to tell a battle: the logo there (the Status's HP and PP would be stale while the meters roll), and the pictures taken again after it.
- Areas reached only later in the story or earlier in it (the save files are all in Chapter 8); the cases reach Tazmily, Mt. Oriander and New Pork City's highway through the game's own debug menu (see "How the states were made").

## How the states were made

The owner's saves (GameShark and a `.sav`, `saves/A3UJ-*.sav` in the fixtures) are all in Chapter 8, in the Empire Porky Building. Other areas were reached with the game's unused debug menu (The Cutting Room Floor, "Mother 3/Debug Menu"): on a scratch copy of the ROM, the three calls of the room set-up at 0x080075FE, 0x08007960 and 0x08007BEA go to a stub in free space at 0x0821EFE0 that runs it and then the debug menu's set-up at 0x08039C04; with M3_MODE set to 0x0F after a room loads, the menu's map number (M3_MAP) is set, START warps there, M3_MODE 0x0F and SELECT close the menu, and the state is saved again on the unchanged ROM (the same bytes in the English v1.3 and v1.3a ROMs). The fixtures' states were all saved on the ROM their cases use.

## For the merge

- Changelog (under the open version, What's new): "MOTHER 3 (Japan, and its English fan translation v1.3 / v1.3a, with or without the HQ audio mix): walking about, the second screen shows the area's map where R opens one, and the party's Status screen, a page each, elsewhere. In battles the last picture stays for now."
- `wiki/Supported-games.md`, table: MOTHER 3 | Japan | Romhacks tested: English translation v1.3, v1.3a; HQ audio mix.
- `wiki/Supported-games.md`, section:

  ### MOTHER 3

  Japan only; the English fan translation (v1.3 or v1.3a, as a patched ROM or with its UPS patch in the Patches menu) and MusicTheorist's HQ audio mix work the same.

  - Walking about where the R button opens a map of the area, the second screen shows that map, with the party's marker, without its scroll arrows.
  - Elsewhere it shows the Status screen, turning through the party every 5 seconds, without its L / R badges and its help line.
  - In a conversation or a scene, the last picture stays; on the title, the logo.
  - Known issues: in battles the last picture stays (the logo is planned). The translation and the HQ audio mix can't both be switched on as patches (each is a UPS made for one exact ROM): use a ROM patched with both.

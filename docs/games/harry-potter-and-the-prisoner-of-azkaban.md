# Harry Potter and the Prisoner of Azkaban (Harry Potter to Azkaban no Shuujin in Japan)

| | |
|---|---|
| Releases | `BHTE` USA and Europe (one release in seven languages: English, French, German, Spanish, Italian, Dutch, Danish) · `BHTJ` Japan (Harry Potter to Azkaban no Shuujin) |
| Dumps | `BHTE` cartridge `c0bccaad` (tested) · `BHTJ` cartridge `42e590d6` (tested) |
| Profile | `src/profiles.c`: the `BHTE` entry (`hpa_compose`); variant `BHTJ` (its screen byte, its logo) |
| Tests | `tests/regress/harry-potter-and-the-prisoner-of-azkaban.json` (`tools/regress.py harry-potter-and-the-prisoner-of-azkaban`) |
| Status | supported (new in v1.5) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list harry-potter-and-the-prisoner-of-azkaban`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

How it fits the [philosophy](../../wiki/Philosophy.md): the game is an RPG with no map. What the player looks things up for in START's menu is the party's state (level, experience, Stamina and Magic) on its Status/Equip screen, and what to do next: the task line at the bottom of the menu itself ("Talk to Cornelius Fudge", "Find the Rat Tonic"). With a second screen, both are always in view (principle 2: information otherwise found in a pause menu). Both are the game's own screens, opened on the hidden copy (principles 3 and 4); two of the game's windows placed together, the summary cut from its screen without the heading bar above it (allowed small changes: cropping, two windows together). The wand cursor is left out (principle 2). In battles, the game screen already shows the Stamina and Magic of whoever's turn it is, and the menu can't be opened there, so the second screen is idle rather than showing numbers from before the battle.

## Rules

**HPA-1. The party and the task.** Walking about (where START opens the menu), the second screen shows, from the top:
- the summary from START's Status/Equip screen, as the game draws it at the game's size: each one in the party (only Harry at first), their figure, and their box with Level, Exp, Next, SP and MP. Its rows 25-148 are used (the figures' tops to the boxes' bottoms), so the screen's heading bar ("Status/Equip") isn't there. The wand cursor and its sparkles are left out (the screen drawn without its sprites);
- right below it, to the screen's bottom edge, the task box from the bottom of START's menu ("Talk to Cornelius Fudge"), as the game draws it with the parchment at its ends. The posts at its corners lose their outermost row of pixels (rows 122-157 are used), so the two fit the screen's 160 rows exactly.

Both are opened on the hidden copy (START, the task box, then Status/Equip) about once a second, and at once when the screen changes (back from a menu, a conversation or a battle) or Harry's SP or MP changes, so they stay up to date without the player opening the menu. In the other languages and in Japan, the same, in their own words.

**HPA-2. With the menu open.** With START's menu, or one of its screens (Status/Equip and its pages, Items, Folios, Save Game, Connectivity, Help), open in the game, the second screen shows the same as HPA-1, opened afresh from there (B back to the menu on the hidden copy).

**HPA-3. Last one stays.** In a conversation or a scene (where START opens nothing), the second screen keeps the last picture of HPA-1; before the first one, the logo.

**HPA-4. Battles.** From a battle's start through its results screen ("You are victorious!"), the second screen shows the logo, and the picture from walking about is dropped: after the battle it's taken again as soon as START's menu can be opened (until then, the logo), so Stamina and Magic from before the battle are never shown.

**HPA-5. Title, files, elsewhere.** On the title screen, the language screen and the file screens (New Game, Load Game), and anywhere else that isn't walking about, a conversation or scene, the menu or a battle (the story's picture pages as a new game starts), the second screen shows the logo, any picture from before is dropped, and no captures are tried there.

**HPA-6. Logo.** The logo is the title's lettering, HARRY POTTER AND THE PRISONER OF AZKABAN with its ™ (BG2, without the parchment, the copyright lines or PRESS START); in Japan the title's ハリー・ポッター with とアズカバンの囚人 below it.

**HPA-7. Game screen untouched.** Nothing leaves the game screen: the spell and portrait at its top left stay (the spell is what B casts, and L and R change it).

## How it was checked

Ron and Hermione in the party: the game's debug menu (the screen byte 0x03003F18 set to 0x19, as tcrf.net describes) has a Party Member Select; Robed Ron (8) and Robed Hermione (7) were put in the followers' places there (it rewrites the three characters' stat blocks, at 0x030024F4, 0x0300253C and 0x03002584, 0x48 bytes apart, so the owner's save's were put back), then the screen byte set to 8 (walking about). With two, Harry's figure and box go to the left and Ron's to the right; with three, Hermione, Harry and Ron side by side. Either way the summary keeps rows 25-148, so HPA-1's layout holds. Its Map Select put the three in the Defense Against the Dark Arts Classroom, where they walked into a battle (HPA-4).

## Not covered yet

- Ron and Hermione joined by the debug menu, not by the story: a party they joined in play is taken to look the same.
- The mini-games in the story (Buckbeak's Hippogriff Glide, the Riddikulus Boggart Challenge, Wizard Cracker Pop-it) and the story's picture pages: what the screen byte reads there isn't known. Where it's none of walking about, a conversation or the menu, the second screen shows the logo (HPA-5); where START opens nothing, the last picture stays (HPA-3).
- Battles in Japan: the battle's screen byte is taken to read as in the USA and Europe release (only walking about, the menu and the title were tried there).

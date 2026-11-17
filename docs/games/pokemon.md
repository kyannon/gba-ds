# Pokemon FireRed, LeafGreen, Ruby, Sapphire and Emerald

| | |
|---|---|
| Releases | `BPRE` FireRed USA, Europe · `BPGE` LeafGreen USA, Europe · `BPRJ` FireRed Japan · `BPGJ` LeafGreen Japan · `BPRD` Feuerrote Edition · `BPGD` Blattgrüne Edition · `BPRF` Version Rouge Feu · `BPGF` Version Vert Feuille · `BPRI` Versione Rosso Fuoco · `BPGI` Versione Verde Foglia · `BPRS` Edición Rojo Fuego · `BPGS` Edición Verde Hoja · `AXVE` Ruby USA, Europe · `AXPE` Sapphire USA, Europe · `AXVJ` Ruby Japan · `AXPJ` Sapphire Japan · `AXVD` Rubin-Edition · `AXPD` Saphir-Edition · `AXVF` Version Rubis · `AXPF` Version Saphir · `AXVI` Versione Rubino · `AXPI` Versione Zaffiro · `AXVS` Edición Rubí · `AXPS` Edición Zafiro · `BPEE` Emerald USA, Europe · `BPEJ` Emerald Japan · `BPED` Smaragd-Edition · `BPEF` Version Émeraude · `BPEI` Versione Smeraldo · `BPES` Edición Esmeralda |
| Dumps | `BPRE` cartridge `dd88761c` (tested), Rev 1 `84ee4776` (tested), Rev 10, Switch `31554c22` · `BPGE` cartridge `d69c96cc` (tested), Rev 1 `daffecec` (tested), Rev 10, Switch `49ddf652` · `BPRJ` cartridge `3b2056e9` (tested), Rev 1 `bb640df7` · `BPGJ` cartridge `0a48556b` (tested), Rev 1 `8da4601a` · `BPRD` cartridge `1a81eedf` (tested) · `BPGD` cartridge `d12f1fdd` (tested) · `BPRF` cartridge `5dc668f6` (tested) · `BPGF` cartridge `ba3285e3` (tested) · `BPRI` cartridge `73a72167` (tested) · `BPGI` cartridge `16974506` (tested) · `BPRS` cartridge `9f08064e` (tested) · `BPGS` cartridge `2ca11d59` (tested) · `AXVE` cartridge `f0815ee7` (tested), Rev 1 `61641576` (tested), Rev 2 `aeac73e6` (tested) · `AXPE` cartridge `554dedc4` (tested), Rev 1 `bafedae5` (tested), Rev 2 `9cc4410e` (tested) · `AXVJ` cartridge `cee9471a` (tested), Pokemon Box `606c9adb`, Rev 1 `b448a337` · `AXPJ` cartridge `fd1eeb78` (tested), Pokemon Box `9eaea813`, Rev 1 `01bd60e3` · `AXVD` cartridge `15e1e280` (tested), Rev 1 `cae89464` (tested) · `AXPD` cartridge `6fcd7a98` (tested), Rev 1 `b0c40c7c` (tested) · `AXVF` cartridge `690fd310`, Rev 1 `9f981f72` (tested) · `AXPF` cartridge `3581a05f` (tested), Rev 1 `2ae49146` (tested) · `AXVI` cartridge `c18231a9` (tested), Rev 1 `9305e793` (tested) · `AXPI` cartridge `0c7992a9` (tested), Rev 1 `5efe4493` (tested) · `AXVS` cartridge `eb0729cf` (tested), Rev 1 `b980fff5` (tested) · `AXPS` cartridge `a04f5f0b` (tested), Rev 1 `f2c88931` (tested) · `BPEE` cartridge `1f1c08fb` (tested) · `BPEJ` cartridge `4881f3f8` (tested) · `BPED` cartridge `34c9df89` (tested) · `BPEF` cartridge `a3fdccb1` (tested) · `BPEI` cartridge `a0aec80a` (tested) · `BPES` cartridge `8c4d3108` (tested) |
| Mods | `BPRE` Radical Red 4.1 `fba55dd8` (tested), Unbound 2.1.1.1 `4b3d4957`, Gaia `b9465c5a`, Saiph `32b2929c`, Fuligin `8abbc1e1` · `AXVE` Light Platinum `41d07817` · `BPEE` Flora Sky `7b6e6fb9`, Glazed 9.1.0 `fda9453b`, Theta Emerald EX `6ce0ea9f` |
| Profile | `src/profiles.c`: the "Pokémon (preliminary)" section (`pk_pages`, one profile entry per engine and language group; the title logos' recipes `PK_LOGO_*` and `pkLogos`) |
| Tests | `tests/regress/pokemon.json` (`tools/regress.py pokemon`) |
| Status | in progress on the branch `game/pokemon` (a side build for the owner's RG DS Plus; not merged) |

What GBA DS does with these games. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list pokemon`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

The five games share one engine (FireRed and LeafGreen's, Ruby and Sapphire's, Emerald's) and every release in every language is handled the same way; the rules hold for all 30 release codes above. Romhacks built on them (PK-7) are the reason the profile finds things on the screen and in the Pokémon's own data rather than at addresses.

## Why these screens

Asked what a Pokémon game would have put on a second GBA screen: the two screens a player keeps opening START for are the **Party** (who is hurt, asleep, poisoned, how much HP is left) and the **map** (where am I, where is that town). Both are screens the game already draws. They're opened on the hidden copy and shown as the game draws them, so nothing is rebuilt or invented, and the game screen is never covered. There is no trainer card, no Pokédex and no summary: nothing that would need a cursor or a choice (the Party is shown with no Pokémon lit, and the game's own prompt and CANCEL button are left out).

## Rules

**PK-1. Logo.** Whenever the second screen has no Party to show (PK-2), it shows the logo: on the title and the menu, in a new game before the first Pokémon, in the intro and in scenes and in a fight that the game's START menu can't be opened in, on the game's own title logo (the lettering and its version name, without PRESS START and the copyright line), taken from this ROM's own title screen. A romhack has its own (PK-7).

**PK-2. Party.** With Pokémon in the party and the START menu open to the player, the second screen shows the game's Party screen, filling the screen, with all six slots as the game draws them (HP bars, levels, status, held items), the cursor on no Pokémon and without the "Choose a Pokémon." box or CANCEL. It's taken again when anything about the party changes (a Pokémon's HP, status, level or species), within a second or two of the change.

**PK-3. Map.** After the Party it shows the game's region map: in FireRed and LeafGreen the Town Map (a key item the player gets early on), in Ruby, Sapphire and Emerald the Pokénav's region map (once the player has the Pokénav), as the game draws them with the player's place marked, without the button prompts. Before the player has it, only the Party is shown. The map is taken again about every 15 seconds, so the marker can lag behind a player who has just walked into a new town (known issue).

**PK-4. Turning pages.** With both, the second screen turns from one to the other every 5 seconds, the way Fire Emblem's status screen turns through its pages (FE-3).

**PK-5. Game screen untouched.** The START menu, the Bag, scenes and fights stay on the game screen where the game draws them; nothing the second screen shows is taken from it, and opening the Party or the map on the hidden copy changes nothing the player sees or hears. (GEN-2.)

**PK-6. Menus that aren't the original's.** The Bag and the Pokénav are found by trying each row of the START menu and telling the screen that opens by the game's display registers, not by their place in the list; a menu without SAVE or EXIT (Radical Red, Rogue), with extra entries, or that is a bar of icons along the bottom (Unbound) works the same. A START menu that can't be found, or a Bag with no Town Map, leaves the logo (PK-1) or just the Party (PK-3).

**PK-7. Romhacks, as separate ROMs and as patches.** A romhack built on the original ROM (Radical Red, Unbound, Gaia, Saiph, Fuligin, Light Platinum, Flora Sky, Glazed, Theta Emerald EX are the ones checked, in the Mods row) shows the Party and the map as the game it's built on does (PK-2 to PK-4), as a ROM of its own or as a patch on the original ROM (GEN-9). A separate ROM has a logo of its own, taken from its own title screen and kept for that ROM file (so hacks of the same game don't share one); a patched game is the original ROM file and shows the original's logo, and none is taken while a patch is on. Known issues: Super MarioMon and Emerald Rogue EX are rebuilt from source (nothing like Emerald's ROM, so only a ROM of their own); the Party and the map haven't been checked on them. Gaia's Party screen has black vertical bars of its own.

## Not covered yet

- A battle's party screen (START isn't available in a fight, so the logo stays; the game's party screen in a fight could be taken another way).
- Real saves: every state here is a new game with a party and flags written into it, not a played save.
- Super MarioMon and Emerald Rogue EX (PK-7).
- The Japanese Rev 1 releases, the Pokémon Box and debug dumps, the Switch Online dumps and any Korean release (no fixture).
- The map's refresh (PK-3) only after a few captures.

## For the merge

Changelog line (under the open version): "Pokémon FireRed, LeafGreen, Ruby, Sapphire and Emerald, in every language, and many romhacks of them: the second screen shows the game's Party and its map, turning every five seconds, and the game's title logo."

Wiki (`wiki/Supported-games.md`): a section for the five games with PK-1 to PK-7 in plain words and the known issues above, and the nine romhacks checked in the "Romhacks tested" column.

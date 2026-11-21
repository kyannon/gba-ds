# Running the Pokémon prototype on every release

Scratch scripts (paths assume `/home/claude/work/all` and the host build; edit the top of each). Order:

1. Unzip the fixtures (`../gba-ds-tests/roms/{AXV,AXP,BPE,BPR,BPG}*.zip`) to `CODE[+revN].gba` in one folder.
2. `intro2.py ROM...` makes a new-game state `ROM/s_new.ss` by pressing START / A through the intro, then probes (START opens a menu with a white panel) and repeats until the player has control. Ruby / Sapphire / Emerald start in the moving van: `intro3.py` (walk out first, for ROMs that stay in the van) and `intro4.py` (advance Mom's lines in batches of five A, probing between, so it stops the moment the player has control: more A presses re-start her lines) finish the ones `intro2.py` leaves stuck.
3. `xref2.py ROM...` maps the party and save block 1 addresses from the English ROM (`tools/romxref.py`, slow: about two minutes per ROM), one json per ROM in `xr/`.
4. `runone.py ROM` / `driver.py` poke a six-Pokémon party, the Pokédex / Pokémon (/ Pokénav) flags and, in FireRed / LeafGreen, a Town Map into the state (offsets are the same in every release: count, flags, key items; only the party and save block 1 addresses move), save `s_p.ss`, and run the harness `map` capture (the prototype's pages `pg.png`, `pg_f01.png`).
5. `mksheet.py OUT.png CODE...` puts the pages of several ROMs side by side to look at.
6. `full.py ROM...` (names without .gba): the five variants and a run of the real app per ROM, results in `full_results.json`; keeps four small thumbnails of the bottom screen per ROM (`keep_*.png`).

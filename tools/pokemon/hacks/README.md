# Running the Pokémon prototype on romhacks

Scratch scripts (paths assume `/home/claude/work/hacks`, the ROMs copied there as `CODE+name.gba`, the matrix
scripts' `mkparty.py` in `/home/claude/work`). See `docs/pokemon-ideas.md`, "Romhacks".

- `adv.py`, `st.py`: get a new-game state (`s_new.ss`) through a hack's own intro (`st.py ROM IN OUT "script;..."`).
  The probe in `matrix/intro2.py` is stricter here: it needs the START panel's region not to be white already.
- `hackrun.py ROM`: pokes a party, the Pokédex / Pokémon flags and a Town Map into `s_new`, saves `s_p`, runs the
  harness `map` (the prototype's pages `pg.png`, `pg_f01.png`) for the base game's addresses. Radical Red, Unbound
  and Gaia keep the key items pocket elsewhere: `bagfind.py` finds the pocket table (5 `{ptr, capacity}` pairs, at
  0x0203988C in all of them) and the Town Map goes into its second entry.
- `partyfind.py`, `sbfind.py`, `cand*.py`: for a hack built from source (Super MarioMon, Rogue EX) the party and
  save block addresses move; the first two read the ROM's literal pools for candidates, the others try them. The
  flags weren't found this way (see the notes in the doc).
- `title.py`, `lay.py`, `lg.py`, `lgall.py`: the title screens' timelines, their layers (harness `layershot`), and
  `logo` extraction for one ROM or for all.
- `mkups.py SRC DST OUT`, `pt.py`: a UPS patch from a base ROM to a hack, and a check that the app's own patcher
  (`src/patch.c`) gives the same ROM bytes.

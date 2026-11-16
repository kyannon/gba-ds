# Pokémon on GBA DS: ideas and a first prototype (branch `pokemon-preliminary`)

Investigated with the USA/Europe base ROMs, all rev 0: FireRed (BPRE, CRC32 dd88761c), LeafGreen (BPGE, d69c96cc), Ruby (AXVE, f0815ee7), Sapphire (AXPE, 554dedc4) and Emerald (BPEE, 1f1c08fb). Nothing here is a rule yet: it's what the games allow, what a prototype did, and what to decide.

## The position: what GBA DS can be that a companion app isn't

PokeDaisy-style companions read the game's memory and draw their own screens, so they win on breadth (type charts, encounter lists, guides) and lose on upkeep: every game and every romhack needs its own tables of addresses, species, moves and maps (the archived `archive/pokedaisy-poc` shows the size: ~10,000 lines of core, and "ROM hacks aren't supported"). That's the road the Philosophy closes (principles 3 and 4). GBA DS's version of the same idea is narrower and sturdier:

- the second screen is **the game's own screens**, opened on the hidden copy: pixels the game drew, in the game's own look, in any hack that kept them;
- **no per-hack data**: what it needs to know is learned from the screen (a menu panel, which screen is up) or found in RAM by what a Pokémon *is*, not where a ROM keeps it;
- the player's game is never touched, nothing to set up.

So the aim isn't to out-feature a companion; it's "the DS-era second screen this game never had", for every Gen 3 ROM at once.

## What the prototype does

`src/profiles.c`, "Pokémon (preliminary)": three profile entries (FireRed/LeafGreen, Ruby/Sapphire, Emerald; the USA and Europe codes of each), one `composePages` function and one `virt`. On the hidden copy, every capture:

1. presses START and waits for the menu's white panel (a pixel test: lots of white at the right, picture changed);
2. puts the cursor on the first entry. **The START menu remembers its cursor** in both engines (checked), so the hidden copy, which starts from the player's moment, finds it anywhere. DOWN until a press moves the cursor from the last entry to the first: only two bands of pixels change in the panel, adjacent normally, far apart on a wrap. No geometry, no cursor sprite, so FireRed's arrow and Sapphire's red box are the same to it;
3. moves by counting: POKéMON is the first or second entry (second is tried first, the screen that comes up is checked, the first tried if not: works with or without a Pokédex); BAG (FireRed/LeafGreen) and POKéNAV (Ruby/Sapphire/Emerald) are the 5th from the end in all five games;
4. recognises the screen by the display registers (DISPCNT, BG0-3CNT). They're the engine's, so one table per **engine** (FireRed/LeafGreen, Ruby/Sapphire, Emerald), not per hack;
5. takes the picture: the Party with the cursor sent to CANCEL (nothing lit) and the message box and CANCEL button filled with the background beside them (button prompts out, as Philosophy 2 allows); and the region map with its button prompts filled out (FireRed/LeafGreen: "+MOVE" and the exit button; Ruby/Sapphire/Emerald: "A ZOOM B CANCEL"); the player's own marker and the location name stay (information).

**The map, by engine.** Ruby/Sapphire/Emerald: POKéNAV, whose first entry is the region map. FireRed/LeafGreen: the Town Map is a *key item* whose row depends on what the player owns, so the copy goes BAG, all the way left, once right (the Key Items pocket), cursor to the top, then down a row at a time pressing A, A (USE) until the Town Map's registers come up. The copy is loaded afresh for every try, so what another item does (the Bicycle, the Itemfinder) is gone. The row that worked is tried first next time; no Town Map in the first 10 rows means no map page, retried every fifth capture. The map screen's registers come up while it is still fading in from white, so the picture is taken ~110 frames after them.

Pages turn every 5 s (Fire Emblem's precedent): Party, Map. The map is kept between captures (it costs a menu's worth of frames, more in FireRed/LeafGreen) and retaken every fifth capture (~15 s: where the player is changes slowly); only the Party is retaken each time. A Trainer Card page was tried in the first version and dropped at the owner's request: the second screen is Party and Map only.

**When to look again** (`pk_virt`, used as the `room` probe, so GEN-6 applies): the party is *found*, once, by scanning EWRAM and IWRAM for 100-byte records whose checksum is right (the Gen 3 Pokémon structure: personality and trainer ID xor the 48 data bytes, summed). It found FireRed's party at 0x02024284 (EWRAM) and Sapphire's at 0x03004360 (IWRAM) with no table, and the same code would find a hack's, wherever it moved it. A hash of the records' HP, status and level makes the second screen follow a Potion, a faint, a poison tick (seen on Sapphire's second screen as the player walked) within about half a second.

### Tried on states made by poking a party into a new game (the saves are not the player's; nothing here came from a real playthrough)

| Case | FireRed | LeafGreen | Ruby | Sapphire | Emerald |
|---|---|---|---|---|---|
| normal | ok | ok | ok | ok | ok |
| START cursor left mid-list / on EXIT | ok | ok | ok | ok | ok |
| no Pokédex (POKéMON first) | ok | ok | ok | ok | ok |
| one Pokémon | ok | ok | ok | ok | ok |
| no Pokénav / no Town Map | no map page (expected) | not tried | not tried | no map page (expected) | not tried |
| Town Map in the 2nd Key Items row (Bicycle first) | not tried | ok | n/a | n/a | n/a |
| in the app: heal / poison tick updates the bottom | ok | ok | ok | ok | ok |

Gen 3's save blocks are allocated differently per game: Emerald moves them around in RAM (its pointer is read from 0x03005D8C), which only mattered for making test states; the capture itself uses no addresses except the party scan.

Cost: a capture is ~250 frames for the Party alone (about 0.2 s on this machine's core); the map adds ~220 (Ruby/Sapphire/Emerald) or ~600 per Key Items row tried (FireRed/LeafGreen: the first time only, then the row is remembered). START does nothing in battles, conversations, menus: "map button did nothing", the last picture stays (GEN-3).

## Ideas, in the order I'd do them

1. **Party** (done in the prototype). The game's most-opened menu, the one a handheld player opens to check HP. Works for every hack that keeps the START menu and the party screen's look.
2. **Region map** (done, all five games; see "The map, by engine"). Still to decide: refreshing the player's marker when the player changes map rather than every fifth capture (no hack-agnostic signal for a map change is known yet; the live screen's movement isn't one).
3. ~~Trainer Card~~: tried, dropped (owner's call: Party and Map only).
4. **Battle**: during the player's turn, the battle menu's POKéMON screen on the second screen (the whole party's HP while choosing a move or a switch). Not tried: needs a battle state (I could not reach one without playing past the intro). Same machinery, a different menu path (FIGHT/BAG/POKéMON/RUN, cursor remembered too).
5. **Summary / moves of the lead Pokémon** (PP): START, POKéMON, A, Summary, pages. One more step down the same path.
6. Pokédex area map for the foe, PC box view: later, if the above hold up.

Left out on purpose, because they need rebuilding from the game's data (principles 3 and 4): type charts, move or encounter tables, guides, damage calculators, IV/EV readouts.

## Romhacks

What carries over: the START menu structure, the party screen and the maps (hacks that restyle them still share their engine's registers), and the party's structure (Gen 3 hacks, including CFRU/expansion ones, keep the 100-byte record). What doesn't, and the response: a hack with a menu of another order (the 4th/5th-from-end counts and the "first or second" try fail the screen check: the capture fails, the last picture stays; "show less"), one with a different party screen (signature mismatch, same). Emerald's Party is FireRed's screen (same registers); the Pokénav's are its own. Not tested on any hack: I had none. `tools/romhack.py` can run the prototype's cases on a patch the owner sends; Radical Red, Unbound and an Emerald-based hack would answer most of it.

## What to check on the device

(The prototype's code is in the branch; it is not a release and has no test zip yet.) On the RG DS Plus, with a test zip: the Party appears below within ~1 s of loading a game from a save; walking with a poisoned Pokémon updates it; opening START yourself on the top screen doesn't disturb the bottom (the hidden copy fails quietly); the pages turn every 5 s; how heavy it feels: the capture runs about once a second (map_interval).

## Open questions for you

- (Answered: Party and Map only, no Trainer Card.)
- Party half the time (as built) or the map only when the player asks for it somehow? Philosophy 5 rules out touch, so a timer is the only switch there is.
- The Japanese, German, French, Italian and Spanish releases are left out of `.codes` (not measured; the START menu's order is the same but the Pokénav/Bag screens may differ). Say if any matter.

## Tools made

`harness` has a `pokes ADDR HEX` command; `tools/pokemon/` builds Gen 3 Pokémon and finds them in RAM dumps (addresses of every game's party, flags and key items in its README).

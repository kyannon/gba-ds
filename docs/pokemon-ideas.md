# Pokémon on GBA DS: ideas and a first prototype (branch `pokemon-preliminary`)

Investigated with the USA FireRed (BPRE, rev 0, CRC32 dd88761c) and Sapphire (AXPE, rev 0, 554dedc4). Nothing here is a rule yet: it's what the games allow, what a prototype did, and what to decide.

## The position: what GBA DS can be that a companion app isn't

PokeDaisy-style companions read the game's memory and draw their own screens, so they win on breadth (type charts, encounter lists, guides) and lose on upkeep: every game and every romhack needs its own tables of addresses, species, moves and maps (the archived `archive/pokedaisy-poc` shows the size: ~10,000 lines of core, and "ROM hacks aren't supported"). That's the road the Philosophy closes (principles 3 and 4). GBA DS's version of the same idea is narrower and sturdier:

- the second screen is **the game's own screens**, opened on the hidden copy: pixels the game drew, in the game's own look, in any hack that kept them;
- **no per-hack data**: what it needs to know is learned from the screen (a menu panel, which screen is up) or found in RAM by what a Pokémon *is*, not where a ROM keeps it;
- the player's game is never touched, nothing to set up.

So the aim isn't to out-feature a companion; it's "the DS-era second screen this game never had", for every Gen 3 ROM at once.

## What the prototype does

`src/profiles.c`, "Pokémon (preliminary)" (BPRE and AXPE only; one `composePages` function and one `virt`). On the hidden copy, every capture:

1. presses START and waits for the menu's white panel (a pixel test: lots of white at the right, picture changed);
2. puts the cursor on the first entry. **The START menu remembers its cursor** in both engines (checked), so the hidden copy, which starts from the player's moment, finds it anywhere. DOWN until a press moves the cursor from the last entry to the first: only two bands of pixels change in the panel, adjacent normally, far apart on a wrap. No geometry, no cursor sprite, so FireRed's arrow and Sapphire's red box are the same to it;
3. moves by counting: POKéMON is the first or second entry (second is tried first, the screen that comes up is checked, the first tried if not: works with or without a Pokédex); the Trainer Card (the name) and BAG / POKéNAV are the 4th and 5th from the end in both games;
4. recognises the screen by the display registers (DISPCNT, BG0-3CNT). They're the engine's, so one table per **engine** (FireRed/LeafGreen, Ruby/Sapphire, Emerald), not per hack;
5. takes the picture: the Party with the cursor sent to CANCEL (nothing lit) and the message box and CANCEL button filled with the background beside them (button prompts out, as Philosophy 2 allows); the Trainer Card as is; in Sapphire the Pokénav's Hoenn map with its button prompts filled out; the player's own marker and the location name stay (information).

Pages turn every 5 s (Fire Emblem's precedent): Party, Card, Party, Map. Card and map are kept between captures (they cost a menu's worth of frames); only the Party is retaken each time.

**When to look again** (`pk_virt`, used as the `room` probe, so GEN-6 applies): the party is *found*, once, by scanning EWRAM and IWRAM for 100-byte records whose checksum is right (the Gen 3 Pokémon structure: personality and trainer ID xor the 48 data bytes, summed). It found FireRed's party at 0x02024284 (EWRAM) and Sapphire's at 0x03004360 (IWRAM) with no table, and the same code would find a hack's, wherever it moved it. A hash of the records' HP, status and level makes the second screen follow a Potion, a faint, a poison tick (seen on Sapphire's second screen as the player walked) within about half a second.

### Tried on states made by poking a party into a new game (the saves are not the player's; nothing here came from a real playthrough)

| Case | FireRed | Sapphire |
|---|---|---|
| normal | ok | ok |
| START cursor left mid-list / on EXIT | ok | ok |
| no Pokédex (POKéMON first) | ok | ok |
| one Pokémon | ok | ok |
| no Pokénav / no Town Map | no map page (expected) | no map page (expected) |

Cost: a capture is ~250 frames for the Party alone (about 0.2 s on this machine's core), ~500-950 with the card and map. START does nothing in battles, conversations, menus: "map button did nothing", the last picture stays (GEN-3).

## Ideas, in the order I'd do them

1. **Party** (done in the prototype). The game's most-opened menu, the one a handheld player opens to check HP. Works for every hack that keeps the START menu and the party screen's look.
2. **Region map**. Sapphire/Ruby/Emerald: Pokénav, first entry (done). FireRed/LeafGreen: the Town Map is a *key item*, reached by BAG, RIGHT, then its row in the list, A (USE). Tried by hand (works: the Kanto map with the red-cap marker). Not in the prototype because the item's row depends on what the player owns. Options: (a) the SELECT shortcut: if the player registers the Town Map to SELECT, the hidden copy presses SELECT; (b) find the row by trying each key item and recognising the map's registers (a few screens' worth of frames, then cached). (a) is nearly free, (b) is generic.
3. **Trainer Card** (done): badges, money, play time as the idle page. Small, static, the badges are what players look for.
4. **Battle**: during the player's turn, the battle menu's POKéMON screen on the second screen (the whole party's HP while choosing a move or a switch). Not tried: needs a battle state (I could not reach one without playing past the intro). Same machinery, a different menu path (FIGHT/BAG/POKéMON/RUN, cursor remembered too).
5. **Summary / moves of the lead Pokémon** (PP): START, POKéMON, A, Summary, pages. One more step down the same path.
6. Pokédex area map for the foe, PC box view: later, if the above hold up.

Left out on purpose, because they need rebuilding from the game's data (principles 3 and 4): type charts, move or encounter tables, guides, damage calculators, IV/EV readouts.

## Romhacks

What carries over: the START menu structure, the party and card screens (hacks that restyle them still share their engine's registers), and the party's structure (Gen 3 hacks, including CFRU/expansion ones, keep the 100-byte record). What doesn't, and the response: a hack with a menu of another order (the 4th/5th-from-end counts and the "first or second" try fail the screen check: the capture fails, the last picture stays; "show less"), one with a different party screen (signature mismatch, same), Emerald-family signatures aren't measured (no ROM here). Not tested on any hack: I had none. `tools/romhack.py` can run the prototype's cases on a patch the owner sends; Radical Red, Unbound and an Emerald-based hack would answer most of it.

## What to check on the device

(The prototype's code is in the branch; it is not a release and has no test zip yet.) On the RG DS Plus, with a test zip: the Party appears below within ~1 s of loading a game from a save; walking with a poisoned Pokémon updates it; opening START yourself on the top screen doesn't disturb the bottom (the hidden copy fails quietly); the pages turn every 5 s; how heavy it feels: the capture runs about once a second (map_interval).

## Open questions for you

- Party always up, or rotating (Party half the time as built)? Philosophy 2 allows a timer; a Party that's always there is simpler.
- Is the Pokénav/Town Map page worth the extra capture cost and the FireRed Town Map problem, or leave it as Party + Trainer Card?
- LeafGreen, Ruby, Emerald and the other regions: same families, untested here, so left out of `.codes`.

## Tools made

`harness` has a `pokes ADDR HEX` command; `tools/pokemon/` builds Gen 3 Pokémon and finds them in RAM dumps.

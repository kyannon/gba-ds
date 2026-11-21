# Pokémon experiments (branch pokemon-preliminary)

- `gen3.py`: builds valid Gen 3 party members (100 bytes, encrypted, checksummed) and finds them in a RAM dump.
- `mkparty.py`: a sample party; `python3 tools/pokemon/mkparty.py` prints it as hex for the harness `pokes ADDR HEX` command.

Making a test state without playing: start a new game with the harness (tap A / START through the intro), then
`pokes <gPlayerParty> <hex>`, `poke <party count> 1 6` and set the "has Pokémon / Pokédex" flags in the save block.
FireRed (USA): party 0x02024284, count 0x02024029, save block 1 pointer 0x03005008, flags at +0xEE0 (0x828 Pokémon, 0x829 Pokédex).
Sapphire (USA): party 0x03004360, count 0x03004350, save block 1 0x02025734, flags at +0x1220 (system flags from 0x800: bit 0 Pokémon, 1 Pokédex, 2 Pokénav).
LeafGreen (USA): same as FireRed's but the save block 1 pointer (0x03005008) read 0x02025540 in a new game: flags 0x02026525 (value 3: Pokémon, Pokédex); key items at SB1+0x3B8 = 0x020258F8, an item is `u16 id, u16 quantity ^ (SB2 +0xF20 key, low 16 bits)`; Town Map is id 0x169.
Ruby (USA): same addresses as Sapphire (party 0x03004360, count 0x03004350, flags at 0x02025734+0x1320).
Emerald (USA): party 0x020244EC, count 0x020244E9. Save block 1 moves (pointer at 0x03005D8C: read it for each state); flags at SB1+0x1270, the system flags from 0x860 = SB1+0x137C: bit 0 Pokémon, 1 Pokédex, 2 Pokénav.
Making a state for these without playing: the harness, `tap START` through the title, `tap A` through the intro (Ruby/Sapphire/Emerald start in the moving van: walk right then down to leave it, then `tap A` through Mom's lines until the player is in the house), then `pokes` / `poke` as above.

- `saves.py`: real saves as test material: `info` (what a save holds), `clean` (an .sps / .gsv / 64 KB save as a 128 KB .sav with both slots valid), `frlg-jp` (an international FireRed / LeafGreen save the Japanese ROMs accept) and `state` (boots a ROM with a save to the overworld and saves a state). What carries across releases: "Real saves" in docs/games/pokemon.md.

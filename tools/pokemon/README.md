# Pokémon experiments (branch pokemon-preliminary)

- `gen3.py`: builds valid Gen 3 party members (100 bytes, encrypted, checksummed) and finds them in a RAM dump.
- `mkparty.py`: a sample party; `python3 tools/pokemon/mkparty.py` prints it as hex for the harness `pokes ADDR HEX` command.

Making a test state without playing: start a new game with the harness (tap A / START through the intro), then
`pokes <gPlayerParty> <hex>`, `poke <party count> 1 6` and set the "has Pokémon / Pokédex" flags in the save block.
FireRed (USA): party 0x02024284, count 0x02024029, save block 1 pointer 0x03005008, flags at +0xEE0 (0x828 Pokémon, 0x829 Pokédex).
Sapphire (USA): party 0x03004360, count 0x03004350, save block 1 0x02025734, flags at +0x1220 (system flags from 0x800: bit 0 Pokémon, 1 Pokédex, 2 Pokénav).

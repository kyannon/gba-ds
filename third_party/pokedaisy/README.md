# PokeDaisy's companion core (proof of concept)

`core/` is the portable C core of [PokeDaisy](https://github.com/lidor30/pokedaisy)'s
Nintendo 3DS port (its `3ds/core`), copied unchanged from commit 2839fd6. It reads a
running Pokémon FireRed / LeafGreen / Emerald and draws the companion (party,
bag, battle, map, guide, Pokédex, trainer card) into a 320x240 canvas;
`src/companion.c` hosts it on GBA DS's second screen.

Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy
(GPL-3.0 with an attribution term: see NOTICE).

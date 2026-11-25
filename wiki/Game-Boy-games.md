GBA DS also plays Game Boy and Game Boy Color games, the way a Game Boy Advance itself did: **on one screen, with the other screen idle**. There are no second-screen features for these games (yet). This page is what is different about them.

## Getting the games in

Put `.gb` and `.gbc` files (or a `.zip` / `.7z` of one) in your GBA folder, or in a `gb` and a `gbc` folder beside it (`/storage/roms/gb`, `/storage/roms/gbc`). They show in the game list with **Games shown** set to **All**, marked "Single screen only", and start from there. Starting them from EmulationStation, BananaOS's list or an Android frontend isn't set up yet: use GBA DS's own list. Saves, save states, rewind, cheats and the pause menu work as for a GBA game.

## How they look

By default the game's picture (160×144) is shown at the GBA's own pixel size, in the middle of the 240×160 screen with black around it, in the colours a Game Boy Color gave it (a game made for the plain Game Boy gets the palette the Color's boot screen gave it, grey if it had none). The second screen is plain dark grey.

The pause menu's **Display** page (and Settings > Display) has a group of **Game Boy** rows for these games only:

- **Game Boy size**: **GBA frame** (the default, as above) or **Game Boy screen**, where the 160×144 picture is the whole screen. This is an exception to the [[Philosophy]]'s first principle, there because it's how people play these games.
- **Game Boy scale** (with the Game Boy screen): **Pixel Perfect** (whole-number scale: 5× on the RG DS Plus, 3× on the RG DS), **Fit** (as large as it goes in its own 10:9 shape) or **Stretch** (the whole screen).
- **Game Boy palette**: for games that play in monochrome: **Game's own** (the default: the colours the game was made with, which for a game that supports the Super Game Boy, such as Castlevania Legends, are the ones it gave the Super Game Boy, without the border), **DMG** (the first Game Boy's greens), **Pocket**, **Light**, **Grayscale** or **Super Game Boy** (the same as Game's own for those games). Not for games made for the Game Boy Color alone, whose colours are their own.
- **Dual-mode games**: games made for both a Game Boy and a Game Boy Color (Pokémon Gold and Silver, for example) play **In colour** by default; **In monochrome** plays them as on a plain Game Boy, with the palette above. Only for those games.
- **Game Boy border**: **None** (the default) or **Super Game Boy**, for games that support the Super Game Boy (Castlevania Legends, Donkey Kong (1994) and the other "SGB Enhanced" games): the game's own border around its picture, as a Super Game Boy showed it. The picture is then 256×224 and fills the screen at the Game Boy scale above, whatever the Game Boy size says; the game draws its border a moment after it starts, so the border is black until then (a game that never sends one keeps a black border). The colours are the Super Game Boy's, so the palette row is greyed out while the border is on.
- **Game Boy color correction**: **As GBA** (follows Display > Color correction), **Off**, **Game Boy Color** (a Color's washed, mixed colours) or **Game Boy Advance** (a GBA's screen).
- **Game Boy ghosting**: **As GBA** (follows LCD ghosting), **Off**, **Light** (each frame half and half with the one before), **Smart** (only things that flicker are blended, so scrolling stays sharp) or **Strong** (slow trails, like the first Game Boy's screen).
- **Game Boy shader**: **As GBA** (follows Shader and its strength), **Off**, **LCD Grid**, **Scanlines**, **Soft Cells** or **Dot matrix** (the grid plus the faint shadow a Game Boy's screen put behind dark things).

A row that can't apply to the game you're playing is greyed out and says why.

**Palette, dual-mode and border changes count from the next start**, so when you close the menu after changing one, GBA DS asks: **Restart now** starts the game over from the top, or **Discard changes** puts the setting back. A resume point made with other colours isn't resumed (the game starts over and says so).

## Known issues

- The shades of DMG, Pocket and Light, the Game Boy Color's colour matrix and the dot matrix's look are first guesses and may change.
- A Super Game Boy game runs about 2 % slower than on a real Super Game Boy (GBA DS keeps every Game Boy game at the GBA's speed), and the Super Game Boy's own sound effects and controller features aren't played. No rumble, Game Boy Camera or link cable. No RetroAchievements for these games, and RetroArch's shared save states aren't checked.
- mGBA's Game Boy core is mGBA's: a timing-sensitive game may show problems that belong to it.

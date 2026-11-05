# Golden Sun: The Lost Age (Ougon no Taiyou: Ushinawareshi Toki in Japan)

| | |
|---|---|
| Releases | `AGFE` USA, Europe (English) · `AGFD` Germany · `AGFF` France · `AGFI` Italy · `AGFS` Spain · `AGFJ` Japan (each tried walking about and in a battle) |
| Dumps | `AGFE` cartridge `606a1c4d` (tested), Virtual Console `726bb764` · `AGFD` cartridge `d981d889` (tested) · `AGFF` cartridge `1090bd33` (tested) · `AGFI` cartridge `a3e49743` (tested) · `AGFS` cartridge `ee38ba94` (tested) · `AGFJ` cartridge `830b795f` (tested), Virtual Console `8c9ec3da` |
| Profile | `src/profiles.c`: the `AGFE` entry (gs2_compose; the releases' addresses in gs2Addr); variants `AGFS` (as `AGFE`), `AGFJ` (the one choosing 0x200 lower) |
| Tests | `tests/regress/golden-sun-2.json` (`tools/regress.py golden-sun-2`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list golden-sun-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6). The rules are [Golden Sun](golden-sun.md)'s, the game's menus being the same; what The Lost Age adds is a party of up to eight (four fighting, the others "in the back"), so the Status screens turn through more members.

## Rules

**GS2-1. Status screen.** Walking about (where SELECT opens the menu), the second screen shows the Status screen (SELECT, the last icon, Status, then A), the whole screen at the game screen's size, as the game draws it (the hand and the party row included), but for the box at the top right with its L-R: Rearrange / A: Details prompt, left out (black there, as between the game's windows), and the green arrows at the party row's right edge, which say it scrolls (more than four in the party), left out too (the window's own colour there; until the first test build they showed). The windows' corner pixels, which the game leaves see-through so the map behind shows in them, are black, as between the windows (until the second test build they showed the map). It's opened on the hidden copy at every map refresh (straight after a battle too), so it stays up to date without the player opening it. With the Status screen, or another of the menu's screens (Psynergy, Djinn, Item), open in the game, the same: the Status, opened afresh.

**GS2-2. Every party member in turn.** The Status screen turns through the whole party in the game's order (RIGHT on the hidden copy, as many as there are, up to eight, back to the first), one every 5 seconds, members in the back included, and keeps turning at that pace through each refresh.

**GS2-3. Last one stays.** Walking about, where the menu can't be opened (a conversation, a scene, the START menu), the last Status pictures stay; before the first, the logo.

**GS2-4. Title.** On the title screen, the file menu, the naming screens, the opening's story text and the credits (the game on no map), the second screen shows the logo, and no captures are tried there.

**GS2-5. Logo.** The logo is the title screen's emblem (the one behind PRESS START: GOLDEN SUN, 黄金の太陽 in Japan, with its dial), cut out of the picture of the lighthouse it's painted on: the gold and its dark outline, nothing of the sky or the mountains. The subtitle (The Lost Age, Die Vergessene Epoche, 失われし時代...) and the ™ (® in Japan) are left out in every release, as are a few loose specks of the dial's ornament (in the German, French and Spanish releases, whose subtitles cut into it, the dial and ornament are filled out from the USA / Europe and Italian ones, the same emblem: only pixels of the dial's colours left of the lettering, until the sixth test build they lost more of it): drawn thin over the sky, they couldn't be cut out cleanly (until the fifth test build the subtitle and ™ were kept, thin and broken up). The game only shows that title with a save, so the hidden copy the logo is taken from (never the game played) has its save check answer "one save", from power-on: START through the opening, the title 120 frames later. (Until the fourth test build, the small emblem a game with no save shows before naming Felix, whose dial and subtitle are drawn dark.)

**GS2-6. Game screen untouched.** Nothing leaves the game screen.

**GS2-7. A battle, choosing.** In a battle, while the player chooses what to do (the party menu: Fight, Switch, Flee, Status; a character's menu; picking a target), the second screen shows the battle's own Status screen of the one choosing (the party's first in the party menu), as the game draws it but for its "Use the L & R Buttons to switch between characters." line and the hand pointing at the character's name, left out (the window's own colour there; until the first test build the hand showed), and the window's four corner pixels, see-through to the battle behind, black (until the second test build they showed it): opened on the hidden copy (back to the party menu, Status, A, R to that character: as many as the character's place in the party), and again as soon as the next character's turn to choose comes, the first turn too. With the battle's Status open in the game, the party's first. Until that picture is taken, the logo: never the Status from walking about.

**GS2-8. A battle playing out.** In a battle, while it plays out (from its start, with the enemies appearing, through the party's and the enemies' moves and its messages, to the experience and coins at its end), the second screen shows the logo, and the pictures from walking about are dropped: after the battle they're taken again.

## Not covered yet

- At sea on Piers's ship: its states are past the test states' story (they're made from the game's first steps). If SELECT's menu works the same there, so does the second screen; a state at sea would settle it.
- Switch's menu (the party menu's second icon) open in the game during a battle: the hidden copy backs out of it to the party menu, as from a character's menu; not tried.

## Facts about the game

For sessions working on the profile (the USA / Europe release's addresses, also the German, French, Italian and Spanish ones'; Japan's in gs2Addr):

- `0x02000420` (half) the map: 0 on the title, the naming screens, the opening's story text and the credits; 2 the world map; 510 in a battle. `0x02000422` the entrance it was entered by. Setting both while a doorway's fade runs (about 44 frames into walking through it) warps there: map 2, entrance 0 is the world map near Venus Lighthouse, where battles come within a few seconds' walk.
- `0x02000040` (byte) who's in the party, a bit per character: 0 Isaac, 1 Garet, 2 Ivan, 3 Mia, 4 Felix, 5 Jenna, 6 Sheba, 7 Piers (their records 0x14C bytes apart from `0x02000520`, name first). `0x02000458` the party's order (8 bytes, character numbers; the first four fight). A new game has all eight records filled (Isaac's party at level 28, as with no Golden Sun data passed on), so setting the bits and the order gives any party.
- Walking about: `0x0203015A` 1 while SELECT's menu or one of its screens is up; `0x020036E0` 1 while one of its screens (Psynergy, Djinn, Item, Status) is.
- In a battle: `0x02030012` 5 while the player chooses; `0x02035EFC` the character choosing (a character number; 0xFF in the party menu and on the Status); `0x03001382` the x of the party menu's highlight (the first sprite in the game's copy of the sprite table: 49 Fight, 73 Switch, 97 Flee, 121 Status; with one in the party there's no Switch and Fight is at 73; the cursor itself is in a window whose place in memory changes); `0x02035E64` 1 while the battle's Status is up.
- In Japan, the one choosing and the battle's Status flag are 0x200 lower (`0x02035CFC`, `0x02035C64`); the rest as above.
- The title screen (its emblem over a picture of the lighthouse, BG2, one 8-bit picture) only comes with a save: the boot code (copied to `0x02008000`) calls the save count through the veneer at `0x080382F0` (every release; the function behind it moves) and goes to naming Felix with none. Made to answer 1 there, a game with no save plays the opening and the title. The masks: `tools/make_logo_masks.py` (`gs2`), from that frame (`GBADS_LOGO_DEBUG=1`, the logo's `.fg.png`).
- Battle messages wait for A (the default message speed).
- A new game with no save goes from the power-on emblem straight to naming Felix, then "Transfer data from Golden Sun?" (No: RIGHT, A), naming Isaac, the story text (START skips it, A doesn't) and some 10 minutes of A through the scene at the top of Venus Lighthouse, where Jenna can first walk (map 5, entrance 9). A battery save (any release's: the releases share the save's layout) continues on the title's file menu with A.
- How the test states were made: the party set by its bits and order, battles by walking on the world map, the world map reached by holding the map at 2 while walking through a doorway (the room loaded is then the world map), the other releases from a USA save made with all eight in the party.

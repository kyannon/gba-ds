# Mario & Luigi: Superstar Saga (Mario & Luigi RPG in Japan)

| | |
|---|---|
| Releases | `A88E` USA · `A88P` Europe (English, French, German, Spanish, Italian) · `A88J` Japan (Mario & Luigi RPG) |
| Profile | `src/profiles.c`: the `A88E` entry (`ml_compose`); variants `A88P` (its language menu), `A88J` |
| Tests | `tests/regress/mario-and-luigi-superstar-saga.json` (`tools/regress.py mario-and-luigi-superstar-saga`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list mario-and-luigi-superstar-saga`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**ML-1. Logo.** On the title and in battles (ML-3), the second screen shows the logo: MARIO & LUIGI and SUPERSTAR SAGA (in Japan マリオ&ルイージRPG), without the sky, PRESS START, the suitcase or the copyright line: no sky blue at the lettering's edge, no specks of the background around its shadow, no stray dots in its corners.

**ML-2. Bros. Info, always.** Walking about, the second screen is the Bros. Info screen (SELECT's menu, its third icon), as the game draws it, filling the screen, once it has settled (its opening and the brothers' turn over) and only if it is Bros. Info (its frame checked; never the field or a menu): the page of the brother you're controlling, the one in front (START swaps them; the page follows within a moment). It's opened in the background on a silent copy of the game about once a second; your game is never paused. (Until v1.4.5's later test builds, Mario's and Luigi's pages in turn, 5 seconds each.)

**ML-3. Elsewhere.** In a battle, the second screen shows the logo (Bros. Info again once you're walking about after it). In scenes, conversations and the game's own menus it keeps the last picture. Nothing leaves the game screen.

**ML-4. One screen.** With one screen, the game plays as it is.

## Not covered yet

Tried in the USA release from the owner's reports (the title, walking about at the start of the story, Bros. Info open, a conversation). One report showed the field on the second screen, from before the report's 10 seconds: not reproduced; the frame check keeps anything but Bros. Info off. Conversations, later areas (more equipment, badges) and Europe and Japan beyond their titles aren't tried yet: their screen and menu bytes are taken to be at the USA's addresses (both read the USA's title value at their titles).

## Facts found so far (for sessions)

| What | Where / value (USA) |
|---|---|
| Screen | `0x02000C99`: 1 walking about, 0 SELECT's menu, 2 Bros. Info, 3 the title |
| Menu icon | `0x030040A9`: 0 Items, 1 Equipment, 2 Bros. Info |
| Game state | `0x02000008`: 8 walking about (its menus, Bros. Info, the star into a battle too), 3 in a battle, 2 the title |
| Brother in front | `0x02004F4C`: 0 Mario, 1 Luigi (START swaps); Bros. Info always opens on Mario's page, R turns to Luigi's |
| Display | walking about DISPCNT `0x1F40` (here), the menu `0x1D40`, Bros. Info `0x7F40`, the title `0x1741` |
| Title | BG1 the sky, BG2 MARIO & LUIGI, sprites SUPERSTAR SAGA, PRESS START and the suitcase; START past the intro by frame 600. Europe: a language menu first (A at frame 700) |

# Pokémon Pinball: Ruby & Sapphire

| | |
|---|---|
| Releases | `BPPE` USA · `BPPP` Europe (English, French, German, Spanish, Italian) · `BPPJ` Japan |
| Dumps | `BPPE` cartridge `b992a3c0` (tested) · `BPPP` cartridge `a53ac14b` (tested) · `BPPJ` cartridge `8af1ec73` (tested), Rev 1 `925a5684` (tested) |
| Profile | `src/profiles.c`: the `BPPE` entry (logo-only, with `tall`: the table over both screens, game.c `update_tall`); `BPPJ` is a `variants[]` row of it (`pp_jpn`: the state pointer, its limit and the four camera stores) |
| Tests | `tests/regress/pokemon-pinball.json` (`tools/regress.py pokemon-pinball`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list pokemon-pinball`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

Unlike the other games, both screens show the table itself, live: two more copies of the game, run in step with yours (the same buttons every frame), draw the rows your game's camera doesn't (game.c `twin_*`).

## Rules

**PP-1. The table over both screens, live, scrolling smoothly.** On Ruby's and Sapphire's tables the two screens show 320 rows of the table in one piece, as if on one tall screen, and the view only ever scrolls: it never jumps. Your game itself is never changed (its camera neither: the game treats things off its screen differently, and holding its camera up changed how catch mode played). While your game's camera is in the table's upper part (rows 0 to 112), the top screen shows your game as it is and the bottom screen the 160 rows below it, drawn by a copy. Further down, the view stops at rows 112 to 432 (the whole lower table): the top screen shows rows 112 to 271 and the bottom screen rows 272 to 431, each drawn by a copy at that fixed place, with what your game puts on its screen laid over the top screen (PP-10). So the ball is on the top screen except when the table is at its lowest. (Until v1.3.2's later test builds, your game moved between the screens as the ball went down and up, and the whole table jumped by a screen each time.)

**PP-2.** *Retired in v1.3.2's later test builds:* the rows outside the camera kept from what the game drew, dimmed (replaced by PP-1's live view).

**PP-3. Score bar.** The score bar (the score, the Pokémon caught, the coins and the balls) is always at the bottom of the bottom screen: moved there from your game's picture.

**PP-4. Title and menus.** On the title, the menus and the field select, the second screen shows the logo (the title's, without its white background or PRESS START).

**PP-5. Swapped screens.** With the screens swapped, the table's upper rows stay on the top screen and the lower ones on the bottom screen, as without.

**PP-6. One screen.** With one screen, the game plays as it is. *By hand: with `layout = single` on the device. (checked on the device for v1.4.2)*

**PP-7.** *Retired in v1.3.2's later test builds:* events shown on the game screen as the game draws them (replaced by PP-10).

**PP-8.** *Retired in v1.3.2's later test builds:* the flippers drawn over the kept rows (PP-1's copy draws them).

**PP-9. In step.** Every frame each copy is taken from your game again (its state from the frame before last, run that frame and this one at the copy's own place), so everything that happens in your game happens on both screens: a bumper hit uncovering a square of catch mode's portrait shows on whichever screen the portrait is on, as do eggs hatching, the Pokémon you're catching and the like. The copies never drift from your game, and nothing is ever retaken or held. The top screen's copy only runs while its rows are shown (the lower view). The copies run on two more threads, side by side with your game. During the table's opening sweep (the view sliding down the whole table) the copies aren't running: the top screen shows the game as it is (with its score bar), the second screen the logo; they're back two frames after it ends. Nothing else stops them: on Sapphire, the ball spat back out of the Wailmer's mouth keeps both screens live, the view holding still over the lower table while the game's camera follows the ball down. (Until v1.3.2's later test builds, that moment turned the copies off for half a second, and they waited half a second after the sweep.) `pinball_live = 0` in `gba-ds.cfg` turns the copies off: the game plays on one screen with the logo on the other. States saved by v1.3.2's earlier test builds (which lowered the camera limit) are put right as the table comes up. (Until v1.3.2's later test builds, each copy ran on its own and was only taken from your game again when the balls' positions differed: a copy could miss what the game counts only on its own screen, such as the portrait's bumper hits.) When the game's view starts or stops moving (the ball let go from a trap mid-table), a copy's first frame is run again at the place it really went, so the rows where the screens meet are always the table's. (Until v1.5.3, such a frame left a few rows there wrong.) *By hand: speed on the device (up to three cores' emulation, with two frames of each copy every frame). (checked on the device for v1.4.2: 60 fps)*

**PP-10. What's fixed to the screen.** What the game puts on the screen rather than on the table (the catch timer, banners such as BALL SAVE) shows as the game places it on its screen: your game's own picture on the top screen in the table's upper part; in the lower part, the game's BG0 above the score bar (the catch timer) laid over the top screen, and its banners (sprites, such as BALL SAVE and Latios/Latias flying across) on the screen the ball is on as they come up, at their place on the game's screen, staying on that screen until they're gone (so BALL SAVE, the ball being down at the flippers, is on the bottom screen over the flippers, as in the game). Messages, pauses and the Mart: PP-11. The copies leave it out: BG0, the sprites the game draws at the same place on the screen whatever the camera, and sprites that only show on a copy because the hardware wraps them round from your game's screen (a message's letters). The copies' table sprites (Pokémon, flippers, the ball) are all kept, wherever they are. The screen darkening behind a message is on both screens. (Until v1.3.2's later test builds, the copy left out any sprite that matched one of your game's, wherever it was: Pokémon often went missing on the copy's screen. Until v1.5.3, the banners were always laid over the top screen, mid-table, while the ball was on the bottom one. Until v1.5.4, a banner flying across the catch timer (Latios flying up to the top right during BALL SAVE) took the part of the timer under it to the bottom screen: the timer is the game's BG0, which stays whole on the top screen, so only the banner's own pixels go.)

**PP-11. Paused, the Mart, messages.** While the game dims its screen (paused, the Pokémon Mart, CATCH 'EM MODE and the other mode banners) or shows a message box (a Pokémon caught), the bottom screen shows your game's whole screen as it is, the score bar included, so the message or menu and the table under it stay together; the top screen shows the 160 rows of the table just above it, live and dimmed alike, so the two screens still read as one table. When the game's screen is near the table's top (no 160 rows above it), the top screen shows the logo. (The owner's State Saver note "Pause screen". Until v1.5.3, the top screen went on showing the table's rows as in PP-1, which at such moments were mostly the same rows as the bottom screen's, or a picture held from before.)

**PP-12. Bonus stages.** In a bonus stage (Dusclops, Kecleon, Kyogre, Groudon, Rayquaza, Spheal: their own tables), the game plays on one screen, with the logo on the other; back on Ruby's or Sapphire's table, both screens again. (The copies only run on those two tables, whose camera limit they know. The bonus tables are only 27 to 43 rows taller than a screen (about 187 rows; Spheal's about 203), so a second screen would be mostly empty. The owner's choice for now, unless a player reports it.)

**PP-13. Events.** The egg hatch (the hatched Pokémon wandering, then caught), an evolution (the EVOLUTION banners, the evolved Pokémon shown), Travel (its confirmation and the travel scene) and Jirachi's catch mode (Jirachi flying about, then caught) play on both screens like the rest of the table (PP-1), in step (PP-9); when the game dims its screen for them, PP-11. (Their test states are built by `tools/pinball_events.py`, which steers the game there through its own code: docs/PINBALL.md.)

**PP-14. The Japanese release.** Pokémon Pinball: Ruby & Sapphire in Japan (`BPPJ`: the original cartridge and its Rev 1) plays as the USA release does: PP-1 to PP-13 hold for it, the table over both screens, the score bar, the logo on the title and menus (the katakana one), and the bonus stages on one screen. Only where the game keeps its state and where its code moves the camera differ (docs/PINBALL.md, "Japanese release"; Rev 1's code sits elsewhere again, its RAM is the original's). If a ROM's camera code isn't where the profile expects it (a hack, say), the game plays on one screen with the logo on the other, as with `pinball_live = 0`, rather than with a broken second screen. (Added for pull request #58, from shigehiro, who found the original's addresses and checked it on a device.)

**PP-15. The European release.** Pokémon Pinball: Ruby & Sapphire in Europe (`BPPP`, English, French, German, Spanish, Italian) plays as the USA release does: PP-1 to PP-13 hold, in whichever language is chosen. The logo is the title's, found after the language menu that comes first (English is chosen for it, so a player's own language choice in the game isn't touched). Its state, camera code, scroll copies and one table pointer are at their own places (docs/PINBALL.md, "European release"; the tests run it in English).

## Exceptions

PP-1 (the table over both screens, live, from two more copies of the game) is an exception to the philosophy's GBA screen (a GBA has one 240x160 screen) and cost rules: the owner's request, and the first game to have it (Sonic Pinball Party's SPP-2 and Muppet Pinball Mayhem's MPM-2 follow it), with `pinball_live = 0` as the way back (the game on one screen, the logo on the other).


## Not covered yet

Nothing the owner wants changed. Events (evolution, hatching, the bonus-stage entrances) were checked on the device for v1.4.2 (USA). The catch animation's outline (the Pokémon flashing red as it's caught) shows on your game's screen but not always on a copy's (cosmetic). The game always runs with the frame buffer on (Settings' Frame Buffer is shown as always on here): without it the device doesn't reach full speed.

The Japanese and European releases are tested with states made from the ROMs alone (`tools/pinball_events.py`: the title, field select, the tables at the top and bottom, pause, the start of catch mode, the events and the bonus stages; Rev 1 with a smaller set). The USA's owner-saved moments have no twins yet there: the Wailmer spitting the ball out, the portrait's reveal, the catch loop, the ball saver's banner and the long seam run (PP-9, PP-10, PP-11 on those releases). Their rules are the same code; a player's report would say if one differed. Not checked on a device: PP-14 and PP-15 (by the owner, if the games are to hand).

## For the merge

- Changelog line (under the open version): "Pokémon Pinball: Ruby & Sapphire works on the Japanese releases (the original and Rev 1) and the European one too, with the table over both screens like the USA one. Thanks to shigehiro, who found where the Japanese game's camera is."
- Wiki, Supported games (table row): codes `BPPE` `BPPP` `BPPJ`; the section's text unchanged.
- README: the game count doesn't change (no new game).
- `tools/make_dumps.py` has been run (the three fixtures' dumps are marked tested).

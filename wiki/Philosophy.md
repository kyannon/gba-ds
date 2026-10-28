What GBA DS is meant to be, and the principles behind what it does and doesn't do. If you've asked for a feature and the answer was no, or wondered why a game's second screen looks the way it does, this is usually why.

## 1. It's a Game Boy Advance with two screens.

GBA DS emulates a device with two 240×160 screens, neither of them touchscreens, as if Nintendo had sold this as a variant of, or an upgrade to, the GBA itself. All the other rules follow from this one.

What this means in practice:

- **The second screen is a GBA screen.** It's a 240×160 picture, scaled up exactly like the game screen, so a pixel is the same size on both screens. Whatever goes there is drawn at the game's own size. Before v1.4, some games' second screens were larger pictures, which made everything on them look smaller than the game; that was a departure from this rule.
- **Ask what the game would have done.** When deciding what goes on the second screen, the question is: if this game had been made for a two-screen GBA, what would its developers have put there? Usually the answer is something the game already has: the map, a status screen, a goal list, the battle intel.
- **The game itself isn't changed.** The game you're playing runs as it would on a GBA. Its second screen is filled from a hidden second copy of the game, which opens menus there without ever pausing your game, sending it input or changing its memory. The only changes to the live game screen are taking something off it that now has its own screen (a HUD moved to the second screen, a map the game also draws in a corner).
- **The emulator's own screens are separate.** The game list, pause menu, settings and update screens belong to GBA DS, not to the imagined console. They have their own look and use the touchscreen (rule 5).

## 2. Function over form.

The second screen gives the player information or context they would otherwise have to find in a pause menu or some other way. GBA DS puts that first, ahead of making anything look prettier or better than it did in the original game.

What this means in practice:

- **Useful beats pretty.** A status screen with its last rows cut off but showing who acts next (Final Fantasy Tactics Advance's turn order) is better than a nicer-looking screen with less in it, or a slower one.
- **Show what matters right now.** The second screen follows what the player is doing: the unit under the cursor in Fire Emblem, the level Mario is standing on. Keeping up quickly is part of the function.
- **When there's nothing useful, say so plainly.** If there's nothing live to show (a title screen, a cutscene, a menu), the second screen shows the game's logo at half strength so it reads as idle, rather than something stale or invented. If a capture can't be confirmed as correct, the last good picture stays up. Better nothing new than something wrong.
- **No enhancement for its own sake.** No smoothing, recolouring, upscaled art or animated transitions. Display options (scaling, LCD grid, overlays) are the player's choice, set in the pause menu, and are applied the same way to both screens.

## 3. Keep it simple.

GBA games use their layers and limited processing power in complex, clever ways. GBA DS shouldn't spend a lot of processing power untangling all of that, as a full decompilation and recompilation project might. Logic and actions should be simple to test, and should cover as many cases as possible in as few steps as possible.

What this means in practice:

- **Use what the game already does.** The usual approach is to open the game's own menu on the hidden copy and take a picture of it. That's simple, works in every situation the menu can be opened, and gets the game's own drawing for free. Reading a few memory addresses to know when to do it (a "screen is open" flag, the cursor's unit, the room number) is fine. Rebuilding a screen from the game's data is not.
- **Fewer, broader checks.** One check that holds everywhere (the menu's open flag plus a few fixed pixels) is better than many special cases. When a game has an odd situation (a tutorial that blocks the menu, a scene played on the battle map), it's often fine to show the logo there and write it down as a known issue, instead of adding a workaround.
- **Everything has to be testable.** Each game's behaviour is written down as a list of plain rules, and checked against saved moments from real play every time something changes. If a behaviour can't be described simply and checked that way, it's probably too complicated.
- **Know the cost.** The device has to run the game, the hidden copy and the drawing at full speed. Pokémon Pinball's tall view (two extra copies of the game running every frame) is the most expensive thing GBA DS does, and is about as far as this rule bends.

## 4. Use the original game's aesthetic.

Don't create original UI elements from scratch. Everything drawn on either screen comes directly from the original game's own screens (its pause and status menus, its map, its HUD), with as little changed as possible. Small changes are fine where they make sense, such as removing button prompts that no longer apply.

What this means in practice:

- **Taken from the game, not drawn for it.** Logos are taken from each game's own title screen. The map surrounds are built from the game's own graphics. Status screens are the game's own, captured from the hidden copy. GBA DS ships no art for any game.
- **Allowed changes are small and mechanical.** These include:
  - hiding sprites that don't belong (a cursor, a button prompt, the player's sprite behind a menu);
  - cropping to the part that matters;
  - moving a block of the game's own HUD into a free spot (Super Mario Advance 2's level name above its status bar);
  - placing two of the game's own windows together when both help (Final Fantasy Tactics Advance's Laws above its turn order);
  - turning through a status screen's pages on a timer (Fire Emblem's three pages, every 5 seconds).
- **Not allowed:** text in GBA DS's font on the second screen, new icons, panels or frames, redrawn or restyled game art, or a layout so rearranged that it no longer reads as the game's own screen.
- **The exception that proves the rule:** the logo is shown on dark grey rather than black, and at half strength when idle. That's GBA DS signalling where the screen is and that it's idle, kept to a minimum.

## 5. No in-game touch.

Touch is only for things the emulator does, such as the pause menu. GBA DS doesn't add touch controls to GBA games.

What this means in practice:

- **Touch belongs to GBA DS.** Tapping opens the pause menu; the game list, settings, the pause menu, the cheats list and the second screen's Controls button (in the games that have one) are all touch controls for GBA DS itself.
- **Nothing on the second screen needs a tap.** Everything the game puts there is to look at. Choosing a unit by tapping it, tapping a map to move there, or any other touch input sent to the game is out of scope, however natural it might feel on a DS.
- **Buttons do what they did on a GBA.** Remapping is limited to layouts players already know (rule 6).

## 6. Rules are written to be broken.

GBA DS already makes exceptions to the rules above: for example, the four-face-button layouts in the Tony Hawk's Pro Skater games and the SNES layout in Super Mario World. Above all, GBA DS is a fun exercise in making these games more fun to play on a dual-screen handheld. It also has to stay controlled enough that development doesn't get away from its one developer and tester.

What this means in practice:

- **An exception has to earn its place.** It should make the game clearly more fun or easier to play on the device, and be something the player would recognise (the four-button layouts mirror the console versions of those games). Being technically possible is not enough.
- **Every exception is deliberate.** It's written down with the game's other rules and tested like everything else, so it isn't "fixed" later by mistake. Current exceptions include:
  - the four-button and SNES layouts;
  - Pokémon Pinball's table spread over both screens as one tall view;
  - Mario Kart's race screen course map, drawn larger than the game's;
  - Super Mario Advance 2's level name moved above its status bar.
- **Optional where it can be.** Where an exception changes how the game plays or looks, there's a way back to the original: GBA is the default control layout, and `pinball_live = 0` turns off Pinball's tall view.
- **Scope is a feature.** Every change has to be built, tested on the real device and maintained by one person. A smaller, solid feature beats a bigger, fragile one, and that's often the honest answer to a feature request: not "never", but "not at the cost of everything else working".

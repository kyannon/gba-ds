# WarioWare: Twisted! (Mawaru: Made in Wario in Japan)

| | |
|---|---|
| Releases | `RZWE` USA · `RZWJ` Japan (Europe: no dump to test yet, not handled) |
| Dumps | `RZWE` cartridge `cb4e844b` (tested) · `RZWJ` cartridge `e69964f1` (tested) |
| Profile | `src/profiles.c`: the `RZWE` entry; variant `RZWJ` (its title comes sooner: no health and safety screen). The game's own code is `src/ww_twisted.inc` (the gyro sensor, the dial, the wheel, the buttons and the slider), included by `src/game.c`; `emu.c` has the cartridge's gyro input (`emu_set_gyro`) |
| Tests | `tests/regress/warioware-twisted.json` (`tools/regress.py warioware-twisted`) |
| Toggle | `none`: the second screen has no pages or views to turn. The contextual toggle's hotkey is the Touch! / Gyro! switch's too (WWT-3), whether the Contextual toggle setting is on or not · GEN-14 |
| Status | merged into `dev` for v1.6.2; tried on the RG DS Plus (Touch mode, the buttons, the slider, the motor); the device's gyro sensor not found on BananaOS yet |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list warioware-twisted`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules), except where the exceptions below say otherwise.

WarioWare: Twisted! is played by turning the console: its cartridge has a gyro sensor, and nearly every microgame is steered by how far, and how fast, the GBA is turned about the axis through its screen. This game is different from the others in GBA DS: its second screen has a job of its own. Its two modes are named here, and in the game's screens, **Gyro mode** and **Touch mode**.

- **Gyro mode** (how every game starts): the device's own gyro sensor is the cartridge's, so the game plays as it did on a GBA. The second screen shows the logo, and a **Touch!** button.
- **Touch mode**: the touchscreen is a dial. A finger dragged left or right turns an emulated gyro, and the second screen shows the game's main menu wheel turning with it. A **Gyro!** button takes you back, with a **Pause** button in the opposite corner; the sensitivity slider is in the quick bar.

## Rules

**WWT-1. Logo.** The logo (GEN-1) is the title's lettering: in the USA, WarioWare with its bomb and the vortex, and TWISTED!, without PRESS START! or the copyright line; in Japan, まわる (Mawaru) and メイドインワリオ with its TM, without PRESS START! or the copyright line.

**WWT-2. Gyro mode.** Every start of the game is in Gyro mode, and so is every load of a state: the mode is never kept in a state or a setting. The second screen is the logo at full strength on white, in every part of the game (the title, the menus, the microgames), and it tilts with the device: the gyro sensor's turning (WWT-6) is added up into an angle, in the direction the game turns (the same way as the Touch mode wheel, WWT-4), and the angle eases back to level when the device is still. Whatever the touchscreen is touched for (WWT-8), the game gets only the device's gyro sensor (WWT-6), never the dial's.

**WWT-3. The Touch! button.** In Gyro mode the second screen has a **Touch!** button, along the top of the picture (so the bottom edge stays free for the quick bar's swipe up), in the look of the contextual toggle's button (GEN-14: a raised key in the console's own style, partly see-through, drawn at the second screen's own pixel size) but larger, with the word on it. It is where the contextual toggle's button goes: left, centre or right by the toggle_button setting (left when that is off). A tap on it, or the contextual toggle's hotkey (**HOME + A**, changeable under Controls > Hotkeys: `hotkey_toggle`), goes to Touch mode. The hotkey works whether the Contextual toggle setting is on or off, in this game; it does nothing with the second screen off (Map: Off) or with one screen. Only the touchscreen takes a tap on the button; with the second screen on the other screen, the hotkey is the way.

**WWT-4. Touch mode: the wheel.** In Touch mode the second screen shows the game's own main menu, from its title's "Modes" wheel, with everything that is text or a prompt left out: the Modes heading, the four mode tabs (Story, Spinner, Souvenirs, Options), the pointing hand, the arrows, the R prompt and the scrolling line of text at the bottom. What is left is the wheel itself, in the game's own colours: its rings, its checkered disc and the pink console. They are taken from the game's menu by a hidden copy of the game when the game starts. The wheel turns about its centre, to the dial's angle (WWT-5): clockwise for a tilt to the right.
- The rings are as the game draws them (they look the same however far they turn); the checkered disc and the console turn; beyond the picture's edges, the rings carry on and the rest is the menu's white.
- Until the wheel has been taken (a few seconds after the game starts, the device's speed deciding), Touch mode shows the logo and the Gyro! button and slider as before; the dial works at once.

**WWT-5. The dial.** In Touch mode the touchscreen is the dial; a touch anywhere on it, except on the Gyro! and Pause buttons (along the top) and the strip along the bottom edge where a swipe up brings the quick bar, grabs it.
- **Dragging.** The dial's angle follows how far the finger has gone from where it touched down, left (negative, a tilt to the left) or right (positive), across the screen: a drag of half the screen's width is 90° at 100% of the slider's scale, in proportion to the slider (WWT-7: 5% to 25%), and a drag of a whole width is as far as it goes either way. Up and down do nothing. Dragging back brings the angle back.
- **Smoothing.** The angle moves to where the finger says in a few frames, not at once, and never faster than **the game can follow** (WWT-6): quick flicks are a little behind the finger.
- **Letting go.** When the finger lifts, the angle goes back to 0 by itself, at a steady 180° a second and slowing over its last few degrees, as a rotary phone's dial turns back. It can be grabbed again on the way.
- The wheel (WWT-4) and the game (WWT-6) both follow the same angle.

**WWT-6. What the game is given.** The cartridge's gyro reads a turning speed, and the game adds it up into an angle, so what the game gets is not the dial's angle but **how fast the dial's angle is changing**, frame by frame, scaled so that the game's own angle comes out the same as the dial's (the same at 59.7 frames a second whatever the screen's refresh). At rest it is the sensor's own zero. In Gyro mode it is the device's sensor, read about the axis through the screen (turning the device like a steering wheel, left or right), with its resting error taken off and a small dead zone; in Touch mode it is only the dial's (the sensor is not read). The game's pause, the pause menu and the quick bar give the game nothing.
- The sensor is found at start (Linux: an IIO gyro such as the RG DS's own; SDL's gyro sensors, Android's included; a controller's gyro) and the log says which, and what it reads. With none, Gyro mode gives the game its resting zero; Touch mode is how to play.

**WWT-7. Sensitivity.** The slider is in the quick bar (swipe up from the bottom edge), in the row where other games have Controls or Sun: **5% to 25%** in steps of 1, **15%** by default. It sets how much angle a drag gives (WWT-5; 100% would be 90 degrees for a drag of half the screen's width, so 25% is 22.5 degrees and 15% is 13.5; a drag of a whole width is as far as it goes), nothing else. It is kept, for every start, with the game's settings (`states/<game>.gyro`, with the sensor's scale and direction for the device test). It shows its percentage beside it. A drag on the slider moves it; a tap on its track moves the knob there.

**WWT-8. In Touch mode, the touchscreen is the dial's.** A tap or a drag on the touchscreen turns the dial and is not "a tap opens the pause menu". The pause menu and the quick bar are reached as always by their hotkeys and by swiping up from the bottom edge of the touchscreen (which brings the quick bar, and its settings button the menu); the quick bar's own buttons work as always. In Gyro mode a tap opens the pause menu as in every game. In Touch mode a **Pause** button sits in the top corner opposite the Gyro! button (top right with it at the left or centre) and opens the pause menu, since a tap on the picture is the dial's; the hotkey and the swipe up from the bottom edge work as before. Nothing the touchscreen does reaches the game except through the dial.

**WWT-9. The Gyro! button.** In Touch mode the **Gyro!** button is where the Touch! button was (WWT-3, along the top), the same size and look; a tap on it or the hotkey goes back to Gyro mode (WWT-2). The dial goes to 0 at once when it does (the game gets the sensor's from the next frame on), so a turn in progress isn't carried over.

**WWT-10. Exceptions.** This game is an exception to the Philosophy page's rules, deliberately and for good reason (the game cannot be played without turning something, and a handheld without a gyro, or one that can't be turned while it is open, has no other way):
- **No in-game touch (5):** the touchscreen sends the game a turning speed (WWT-5, WWT-6).
- **Nothing on the second screen to act on (2), only the game's own graphics (4):** the Touch! and Gyro! buttons and the slider are GBA DS's own, drawn like the contextual toggle's button, and the wheel is the game's menu rearranged, drawn turned.
- **A GBA with two non-touch screens (1):** the second screen is a dial in Touch mode.

**WWT-11. The motor.** The game asks for its cartridge motor all through the microgames. Here that is not passed on as it comes: whenever the game asks (a level above a twentieth), the motor gets **one pulse of 100 ms at 30% to 60% of the motor's strength** (60% at the most, whatever the game asks), then rests **150 ms** before the game's next ask can start another. Nothing the game does, nor how fast the device is turned, makes it longer, stronger or more often (at most 4 pulses a second, 40% of the time); the Rumble strength setting scales it as in every game, and a game that doesn't ask doesn't rumble. Why: on the RG DS Plus the device crashed in play with rumble on, never with it off, the gyro (and the dial) making the game ask for far more than the original's slow gyro ever did.

## How it was checked

The title and the main menu were reached in the harness from power-on: A pressed every 40 frames from frame 200 until the title (frame ~500 in the USA; the Japanese release has no health and safety screen and is about 100 frames sooner), then START every 40 frames from frame ~640 until the menu (frame ~800). The USA's title is BG0 (WarioWare, the vortex and the copyright line, which is rows 151 and below), BG1 (the rings behind), BG2 (TWISTED!) and sprites (PRESS START!); Japan's is BG0 (メイドインワリオ, ™ and the copyright box below row 138), BG2 (まわる) and sprites. The main menu is video mode 0: BG0 the scrolling text and its black bar (rows 144 to 159), BG1 the rings (and the R prompt, top right), BG2 the pink console, BG3 the checkered disc, sprites the heading, the tabs, the hand and the arrows. The wheel's centre is at (120, 112). The gyro's scale is mGBA's own (RetroArch's libretro core uses 5.5e8 to a radian a second, 4.58 counts of the cartridge's sample to a degree a second); no microgame was reached in the harness to measure it, so the device test tells whether a half-screen drag turns a microgame about 90 degrees (the dial's `scale` setting is the fix).

## Not covered yet

- Europe (`RZWP`): no dump to test; it is not in the profile.
- The gyro of the RG DS Plus (and of any other device) has not been read yet: the first device test says whether Gyro mode finds a sensor, and which way is left.
- The US title screen has no state (the harness reaches it only past the health and safety screen's timing); the logo case covers it.
- One-screen devices, and the second screen turned off, have no Touch mode.
- Bug reports do not record the gyro's values, so a report's keys play back inexactly in this game's gyro microgames.
- Rumble: the cartridge's own motor works as for every game (issue #83); not part of these rules.

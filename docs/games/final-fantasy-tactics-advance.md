# Final Fantasy Tactics Advance

| | |
|---|---|
| Releases | `AFXE` USA · `AFXP` Europe (language menu first) · `AFXJ` Japan |
| Profile | `src/profiles.c`: the `AFXE` entry; variants `AFXP`, `AFXJ` |
| Tests | `tests/regress/final-fantasy-tactics-advance.json` (`tools/regress.py final-fantasy-tactics-advance`) |
| Status | supported (second screen redone in v1.3.2) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list final-fantasy-tactics-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

The first version (v1.2-v1.3.1: the game's dialogue, menus and banners moved to the second screen over a grey still of the battle, with a ROM patch tagging the game's UI sprites; FT-1 to FT-6) is kept on the branch `archive/ffta-hud`.

**FT-1 to FT-6.** *Retired in v1.3.2* with that version (battle overlays moved, the still behind them, the game screen keeping the battle, the Status kept whole, the world map's overlays, the logo elsewhere).

**FT-7. Logo.** Outside battles and the world map (the title, menus, a town's Pub and Shop), during a scene on the battle map (above all once a battle is won: the camera turning to the last enemy defeated and the closing talk; any story talk during a battle too) and in a battle until its first picture, the second screen shows the logo (GEN-1). (Until v1.3.2's later test builds: a dark background with the logo small and dimmed in the middle, behind everything.)

**FT-8. Status.** In a battle, with the cursor on a unit (yours, an enemy, a guest) whose Status opens, the second screen is that unit's Status screen, as the game draws it but for the D-pad and arrow at the right of its page bar (the prompt to turn the page, left out since v1.4.2's later test builds), filling the screen, turning between its two pages (Equip, Abilities) every 5 seconds; also while a Status is open in the game. It's brought up on the hidden copy (A on the unit, Status). (Until v1.3.2's later test builds: at the top left, and the active unit's when the cursor was on no unit or on the judge.)

**FT-9. Turn order.** In a battle, with the cursor on no unit (or on one with no Status, such as the judge: A on the judge brings up the active unit's menu, and only then does the active unit's Status show), the second screen shows the turn order (the list L shows) and the Laws (FT-12) on the Status screen's angled checkerboard, filling the screen, both brought up the same way. The list is in two columns, its first half (01-04) at the left and the rest (05-08) at the right, each as wide as its longest name, under the list's LIST bar made wide enough for both (from the bar's own left part, middle and end; its bottom line the same); its blue box is see-through over the checkerboard as the game has it over the battle (its own blend), its colours running top to bottom once across the shorter box. The Laws are below it. Both are centred across, together centred down the screen. See Exceptions. (Until v1.4.8: the Laws at the top and the list below in one column on dark grey, its last rows off the bottom.) The checkerboard is taken from the first Status the hidden copy opens (one unit's, A on the list's first, if none has been yet); before it, dark grey.

**FT-10. Game screen untouched.** Nothing leaves the game screen: its menus, panels and dialogue stay as the game draws them.

**FT-11.** *Retired in v1.3.2's later test builds:* the Mission window at the second screen's top right.

**FT-12. Laws.** With the turn order (FT-9), the Laws window (SYSTEM > Laws: FORBIDDEN and RECOMMEND) is below the list, centred across, as the game draws it but without its empty fields (a law shown as a dash): an empty field isn't drawn, the checkerboard shows there, and a row with both fields empty is left out. With no law at all, the Laws aren't shown. (Until v1.4.8: at the top of the second screen, every field drawn.)

**FT-13. Kept while a unit moves or acts.** The second screen is taken again only with the battle map (or a Status) showing: when the cursor moves, and every fourth picture. While a unit picks where to move or what to hit, on the enemy's turn, and in the battle's scenes and banners, what it shows stays in place (a Status still turning its pages); only a scene (FT-7: winning the battle) or leaving the battle brings the logo back; after a scene in the middle of a battle, the picture is taken again. (Until v1.3.2's later test builds, the last picture stayed through the closing scene.)

**FT-14. Laws on the world map.** On the world map, the second screen shows the Laws window (L on the world map: FORBIDDEN and RECOMMEND) alone, as the game draws it, centred on dark grey: without the map behind it, its L Law sign or its pointer. (Until v1.4.2's later test builds: the whole screen, the map, sign and pointer included.) It's opened on the hidden copy when the world map comes up and again as soon as the clan arrives somewhere new, the place's own menu up or not (a town's menu, or a mission's at a place like Giza Plains: closed on the hidden copy first), and taken from the game's own screen when the Laws are open there. They're today's laws: they're taken only with the map's pointer on the clan or between places (pointed at another place, the window shows the laws on the day the clan would get there, lettered on blue and red; then the last picture stays). In a notice and while the clan travels, the last Laws picture stays; before the first, the logo; leaving the world map (a Pub, a battle) brings the logo (FT-7).

## Exceptions

FT-9's turn order in two columns is an exception to the philosophy's "use the original game's aesthetic" (a game's window rearranged, widened and drawn over another screen's background): the owner's request (Nov 4, 2026), under the general exception for information that can't otherwise be shown whole on one GBA screen. The game's list scrolls to show every unit; on a 240x160 screen under the Laws it was cut off at the bottom. Everything in it is the game's own: the list's lettering and box, its bar's own pieces, the Status screen's checkerboard and the list's own blend.

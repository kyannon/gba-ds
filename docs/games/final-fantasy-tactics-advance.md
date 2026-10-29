# Final Fantasy Tactics Advance

| | |
|---|---|
| Releases | `AFXE` USA · `AFXP` Europe (language menu first) · `AFXJ` Japan |
| Profile | `src/profiles.c`: the `AFXE` entry; variants `AFXP`, `AFXJ` |
| Tests | `tests/regress/final-fantasy-tactics-advance.json` (`tools/regress.py final-fantasy-tactics-advance`) |
| Status | preliminary (second screen redone in v1.3.2) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list final-fantasy-tactics-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

The first version (v1.2-v1.3.1: the game's dialogue, menus and banners moved to the second screen over a grey still of the battle, with a ROM patch tagging the game's UI sprites; FT-1 to FT-6) is kept on the branch `archive/ffta-hud`.

**FT-1 to FT-6.** *Retired in v1.3.2* with that version (battle overlays moved, the still behind them, the game screen keeping the battle, the Status kept whole, the world map's overlays, the logo elsewhere).

**FT-7. Logo.** Outside battles and the world map (the title, menus, a town's Pub and Shop), during a scene on the battle map (above all once a battle is won: the camera turning to the last enemy defeated and the closing talk; any story talk during a battle too) and in a battle until its first picture, the second screen shows the logo (GEN-1). (Until v1.3.2's later test builds: a dark background with the logo small and dimmed in the middle, behind everything.)

**FT-8. Status.** In a battle, with the cursor on a unit (yours, an enemy, a guest) whose Status opens, the second screen is that unit's Status screen, as the game draws it, filling the screen, turning between its two pages (Equip, Abilities) every 5 seconds; also while a Status is open in the game. It's brought up on the hidden copy (A on the unit, Status). (Until v1.3.2's later test builds: at the top left, and the active unit's when the cursor was on no unit or on the judge.)

**FT-9. Turn order.** In a battle, with the cursor on no unit (or on one with no Status, such as the judge: A on the judge brings up the active unit's menu, and only then does the active unit's Status show), the second screen shows the turn order (the list L shows) on dark grey, centred across, below the Laws (FT-12); brought up the same way. The list and the Laws are too tall to fit together: the Laws go at the top and the list's last rows off the bottom, so who acts next always shows.

**FT-10. Game screen untouched.** Nothing leaves the game screen: its menus, panels and dialogue stay as the game draws them.

**FT-11.** *Retired in v1.3.2's later test builds:* the Mission window at the second screen's top right.

**FT-12. Laws.** With the turn order (FT-9), the Laws window (SYSTEM > Laws: FORBIDDEN and RECOMMEND) is at the top of the second screen, centred across, the list right below it.

**FT-13. Kept while a unit moves or acts.** The second screen is taken again only with the battle map (or a Status) showing: when the cursor moves, and every fourth picture. While a unit picks where to move or what to hit, on the enemy's turn, and in the battle's scenes and banners, what it shows stays in place (a Status still turning its pages); only a scene (FT-7: winning the battle) or leaving the battle brings the logo back; after a scene in the middle of a battle, the picture is taken again. (Until v1.3.2's later test builds, the last picture stayed through the closing scene.)

**FT-14. Laws on the world map.** On the world map, the second screen shows the Laws window (L on the world map: FORBIDDEN and RECOMMEND) alone, as the game draws it, centred on dark grey: without the map behind it, its L Law sign or its pointer. (Until v1.4.2's later test builds: the whole screen, the map, sign and pointer included.) It's opened on the hidden copy when the world map comes up and again as soon as the clan arrives somewhere new, the place's own menu up or not (a town's menu, or a mission's at a place like Giza Plains: closed on the hidden copy first), and taken from the game's own screen when the Laws are open there. They're today's laws: they're taken only with the map's pointer on the clan or between places (pointed at another place, the window shows the laws on the day the clan would get there, lettered on blue and red; then the last picture stays). In a notice and while the clan travels, the last Laws picture stays; before the first, the logo; leaving the world map (a Pub, a battle) brings the logo (FT-7). *(USA and Europe; in Japan the world map keeps the logo: its addresses aren't known.)*

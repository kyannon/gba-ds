# Final Fantasy Tactics Advance

| | |
|---|---|
| Releases | `AFXE` USA · `AFXP` Europe (language menu first) · `AFXJ` Japan |
| Profile | `src/profiles.c`: the `AFXE` entry; variants `AFXP`, `AFXJ` |
| Tests | `tests/regress/final-fantasy-tactics-advance.json` (`tools/regress.py final-fantasy-tactics-advance`) |
| Status | preliminary (second screen redone in v1.3.2) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list final-fantasy-tactics-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

The first version (v1.2-v1.3.1: the game's dialogue, menus and banners moved to the second screen over a grey still of the battle, with a ROM patch tagging the game's UI sprites; FT-1 to FT-6) is kept on the branch `archive/ffta-hud`.

**FT-1 to FT-6.** *Retired in v1.3.2* with that version (battle overlays moved, the still behind them, the game screen keeping the battle, the Status kept whole, the world map's overlays, the logo elsewhere).

**FT-7. Background.** The second screen is always a dark background with the game's logo, small and dimmed, in the middle (the title, menus and the world map too).

**FT-8. Status.** In a battle the second screen shows, at the top left (the bottom left before v1.3.2's Mission and Laws), the Status screen (as the game draws it) of the unit under the cursor, or the active unit's when the cursor isn't on one, turning between its two pages (Equip, Abilities) every 5 seconds. It's brought up on the hidden copy (A on the unit, Status); where it can't be (the enemy's turn, a scene), the last one stays (FT-13).

**FT-9. Turn order.** In a battle the turn order (the list L shows) is at the bottom right, brought up the same way.

**FT-10. Game screen untouched.** Nothing leaves the game screen: its menus, panels and dialogue stay as the game draws them.

**FT-11. Mission.** In a battle the Mission window (Start, SYSTEM > Mission: the mission's name, what wins and loses it) is at the top right, brought up the same way.

**FT-12. Laws.** In a battle the Laws window (SYSTEM > Laws: FORBIDDEN and RECOMMEND) is at the bottom left, brought up the same way.

**FT-13. Kept while a unit moves or acts.** The second screen is taken again only with the battle map (or a Status) showing. While a unit picks where to move or what to hit, on the enemy's turn, and in the battle's scenes and banners, what it shows stays in place (the Status still turning its pages); only leaving the battle brings the logo back.

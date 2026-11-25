# GBA DS and RetroAchievements hardcore: compliance document

For the RetroAchievements team, answering the [Hardcore Compliance Requirements](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html) section by section. Written Nov 22, 2026 and brought up to date on Nov 24 for GBA DS v1.6.2 (the next release; the current one, v1.6.1.1, is the same except that fast forward still works in hardcore there, where hardcore is hidden); it should be updated whenever the code it describes changes. GBA DS is at https://github.com/kyannon/gba-ds (GPL-3.0). The maintainer is kyannon; RetroAchievements support was written with Claude (Anthropic) as the coding assistant, and the maintainer tried it on real hardware (section 4 says exactly what was and wasn't tried).

**Status:** hardcore is built but switched off for players: its menu item is hidden (`RA_HARDCORE_MENU 0` in `src/ra.h`) until RetroAchievements approves GBA DS. Casual mode has been available since v1.5.7; Rich Presence, leaderboards, the offline queue and the hardcore code are in v1.6.1.1; fast forward is switched off in hardcore from v1.6.2. Nothing here claims approval.

## 1. What GBA DS is

A player-facing emulator frontend for **dual-screen handhelds** (built for the Anbernic RG DS Plus on ROCKNIX; also BananaOS, Anbernic's stock Linux OS, and Android). It plays Game Boy Advance games on one screen and shows the game's own map or logo on the other. **All emulation is done by libmgba**, linked as a library and **unmodified** (mGBA commit `143adf5`, a development snapshot on the 0.11 line, MPL-2.0; the pinned commit is `MGBA_COMMIT` in `scripts/mgba-options.sh`). GBA DS adds: the pause menu and quick bar, the second screen (including a hidden second copy of the game, used only to draw maps; see 5), save states, RetroArch state compatibility, and the rcheevos integration (`src/ra.c`, rcheevos v12.5.0 `rc_client`, vendored unmodified apart from being trimmed).

Builds this application covers: the Linux zip for aarch64 (ROCKNIX, BananaOS and Anbernic's Linux, one binary) and the Android APK. **RetroAchievements applies to Game Boy Advance games only.** Since v1.6.1.1 GBA DS also plays Game Boy and Game Boy Color games, which have no RetroAchievements support at all (no login, no achievements, no hardcore). **Not covered:** those games and the web player, which has no RetroAchievements support.

## 2. Eligibility (section D)

- GBA DS's first public release was **v1.0 on Oct 13, 2026** (GitHub releases). Six months on its own is **Apr 13, 2027**. We aren't claiming otherwise.
- The emulation core is libmgba, whose first release (mGBA 0.1.0) was **Dec 13, 2014** ([mgba.io](https://mgba.io/2014/12/13/mgba-0.1.0)). RetroArch's mGBA core, which runs the same libmgba, is an approved route for Game Boy Advance on RetroAchievements.
- GBA DS is **not a fork of mGBA**: it links libmgba and adds a frontend. Whether the "parent project" wording covers a new frontend around a core RetroAchievements already trusts is a judgment for your team, so **we're asking, not asserting** (question 1 below). If the answer is no, we apply on or after Apr 13, 2027.

## 3. Requirements, section by section

### A. RetroAchievements features: met

| Requirement | How GBA DS meets it | Where |
| --- | --- | --- |
| Achievement list in the emulator | Pause menu → **Achievements**: grouped (recent, almost there, locked, unlocked), badges, points, descriptions, progress bars for measured ones, the totals, and the Rich Presence line. D-pad or touch to scroll. | `ra_list_draw` |
| Triggers, measured and trigger flags | All through rc_client (`rc_client_do_frame` each frame). Challenge indicators (badge at the bottom left while active), progress indicators (a few seconds after a measured value moves) and measured values are drawn on the second screen. | `draw_overlays`, `on_event` |
| Rich Presence | rc_client pings with it automatically; shown in the Achievements page. | `ra_list_draw` |
| Leaderboards | Started / failed / submitted / scoreboard rank popups and a running-value tracker (rc_client runs leaderboards only in hardcore). Can't be turned off. | `on_event` |
| Offline queueing | Every unlock and leaderboard entry rc_client sends is written to `states/ra/pending.txt` (mode 0600, no token, user and game kept) **before** the request goes out, removed when the server answers, and sent again at the next sign-in before the game's achievements load, rebuilt through rcheevos' own request builders so the validation hash is right (the time spent waiting is added). Entries older than 60 days are dropped. A game has to start online to get its achievements (no cached sets): a game started offline has none. | `pending_*` |
| Hit counts in save states (recommended) | Saved beside each state as `<state>.ra` and restored on load (casual). | `ra_state_saved` / `ra_state_loaded` |
| Save file formats (recommended) | Battery saves are mGBA's standard `.sav` (`.srm` on BananaOS, shared with its other GBA cores); states are mGBA's, or RetroArch's `RASTATE` where the player asks for sharing. | |

### B. Hardcore rules enforcement: met in code, enforcement path below

All of it hangs on `ra_hardcore()`, which is true while a game's achievements are loaded (or loading) with rc_client's hardcore flag on.

| Requirement | Enforcement | Where |
| --- | --- | --- |
| Cheats disabled | Cheats are removed from the core the moment hardcore is on and not applied again (including cheat files read from RetroArch `.cht` folders); the Cheats button is greyed out; cheat rows can't be toggled. They return when hardcore goes off. GBA DS has no other cheat mechanism (no memory writes from a menu). | `cheats_sync`, `toggle_cheat`, `item_enabled` |
| Rewind, slowdown, frame advance disabled | Rewind: not recorded and L2 does nothing in hardcore. **Fast forward** (2x to 5x, or unlimited; the hold button and the on/off hotkey) does nothing in hardcore either: we read "frame advance" as covering any speed-up, so it is off, and the pause menu says "Fast Forward: Off in hardcore". The setting itself is kept for casual play. GBA DS has no slow motion and no frame advance at all. | `canRewind`, `canFF` in `run_frame` |
| Save state loading always blocked | Every load, whichever way it's asked for (menu slot, quick state, quick bar, auto-load, RetroArch-shared state, `--load-state`, web), goes through one function, `state_load`, which refuses in hardcore. Load State is greyed out. Saving states is allowed. | `state_load` |
| Rich Presence and leaderboards can't be disabled | There is no setting to disable either. | |
| Resume drops to casual | A game that starts from a saved state (auto-load resume, a patch change, `--load-state`) or with an input script starts in casual mode, whatever the player chose, and says so on screen; the choice is kept for the next game. | `ra_start_mode` |
| Casual to hardcore resets | Switching on mid-session restarts the game (rc_client's `RC_CLIENT_EVENT_RESET`, and GBA DS restarts its core itself), then the start banner is shown again. Hardcore to casual also restarts, which is stricter than required. | `M_HARDCORE` handler |
| Prohibited tools | GBA DS has **no** memory viewer, debugger, scripting or input playback in the app or its menus. Two things to disclose: (1) the binary has a command-line testing option, `--input-script=FILE`, that plays scripted input; a game started with it is forced to casual mode (a scripted tap on the menu could still switch hardcore on afterwards; it isn't reachable from any menu, launcher or setting). (2) The development harness (frame stepping, memory peek and poke) is a **separate program and is not shipped** in the zip or the APK. | `src/main.c`, `game.c` |

Also in hardcore: **bug reports** are disabled (they load an older moment of the game into the core to write a file), and the **Super Mario Advance 2 SNES layout's camera-pan write is off** (see 5).

### C. Client identity

Each request sends `GBADS/<numeric version> (<Linux|Android>) mgba/<numeric libmgba version> rcheevos/<rc_client version>`, for example `GBADS/1.6.2 (Linux) mgba/0.11.0 rcheevos/12.5`. The name is unique to this project and stable; versions are numeric (dev builds send digits only); the OS segment is the platform only. GBA DS has never identified itself as another client. The extra `rcheevos/...` clause is what rc_client's `rc_client_get_user_agent_clause` returns; the page lists three segments, so question 2 asks whether it should stay.

### D. Eligibility: see 2.

### E. Defaults and UX

Casual is the default. Switching is one menu item on the Game page, next to Achievements (once the item is visible), and the mode is shown by the banner at the top of the second screen as every game starts (and again after a switch) and by the Achievements page.

### F. Transparency and legality

- **Monetization:** none. GBA DS is free, GPL-3.0, with no ads, purchases or paid features.
- **Licenses:** [Licenses](https://github.com/kyannon/gba-ds/wiki/Licenses) lists every component shipped with its license and upstream (GBA DS GPL-3.0; libmgba MPL-2.0 unmodified at a pinned commit with its source URL, plus its zlib, LZMA SDK and inih; SDL2 zlib; rcheevos MIT; libwebp BSD-3; rzstd MIT; stb libraries; Jost and Noto fonts, OFL). The zip carries the same list in `licenses/README.txt`; the APK carries the license texts. No non-commercial component is shipped.
- **Privacy policy:** [Privacy](https://github.com/kyannon/gba-ds/wiki/Privacy). The only network use is RetroAchievements and an update check against GitHub's release API (on by default, can be turned off). GBA DS runs no servers, collects no telemetry and keeps no data beyond the files on the player's device. It doesn't state data retention or server locations for RetroAchievements itself, which are yours (see question 5).

### G. Automatic rejection: checked against each item

Loading states in hardcore: blocked (one function). Rewind / slowdown / frame advance / fast forward: none available in hardcore. Gameplay-altering cheats: removed. Switching without a reset: always reset. Non-unique user agent: unique. Undisclosed user-agent history: none. Non-commercial cores with commercialization: none, no commercialization. Privacy policy: no placeholders; it doesn't claim things it can't know.

### H. Banned emulators: not applicable; GBA DS has no relation to any listed.

## 4. How it was tested

On a host build against a stand-in RetroAchievements server (`tools/ra_mock.py`: login, game lookup, achievements with Rich Presence and a leaderboard, award and leaderboard submission, and an outage switch), and on the maintainer's Anbernic RG DS Plus (casual: unlock popups, the Achievements page, offline replay; hardcore: toggling both ways with the restart and banner, refusal of state loads however they are asked for (slots, the quick state, auto-load, the quick bar), rewind, fast forward and cheats switched off, resume to casual). **Not tested against the real RetroAchievements server in hardcore**, since that can't count before approval. The regression suite does not cover RetroAchievements yet.

## 5. What GBA DS does to the running game (the audit)

The second screen's maps come from a **hidden second copy of the game** run on its own core from a copy of the live state; it never writes to the live game, its save or its achievements, and it's cancelled when it falls behind. Beyond that, these are the places where the live core is touched other than by the player's input:

1. **SNES-layout camera pan (Super Mario Advance 2).** When the player maps the SNES layout, the extra buttons write the game's own camera-pan variable (one byte), standing in for the game's L / R pan. **Disabled in hardcore.**
2. **Sprite snapshots** (e.g. Tony Hawk's Pro Skater 2's second screen, some HUD pictures). To draw one frame without some sprites, GBA DS saves the live state, loads it back with those sprites hidden in the *renderer* (and, for one case, one halfword written), runs one frame to draw it, then loads the saved state again, so the game is exactly as before and nothing reads memory in between (rc_client reads only at the end of its own frame call). **This is the item most like a state load, and it is still active in hardcore**; the player cannot load or choose anything, but a strict reading could object. We'd rather ask than guess (question 3); the fallback is to use the logo on the second screen in hardcore for those games.
3. **Bug reports:** load an older snapshot into the core to write it to a file. Disabled in hardcore.
4. **Pokémon Pinball's two-copy table view** runs its extra copies on separate cores; a one-off write on the live core repairs a camera limit stored in old test-build states, unreachable in hardcore (no state loads; resumes start casual).
5. **Map captures from a hidden copy** (Mega Man Battle Network 1 and 2, Baldur's Gate: Dark Alliance, Harvest Moon: Friends of Mineral Town, Advance Wars, Fire Emblem and most other games with a map on the second screen). The live game is only *read*: its state is saved (not loaded back) and loaded into a second core, which is then driven as a player would drive it: it presses the game's own map or menu button, waits for the screen to open, scrolls it (Harvest Moon's Farm Map is scrolled by that core's own scroll registers and stitched) and takes a picture; a few profiles also poke that core's memory to open the screen. Nothing goes back to the live core, its save file or its achievements, the copy has no achievement client, and it is thrown away or called off when the player moves on. Because it runs from a copy of the player's own game, it can show a map the player hasn't opened; it shows only the game's own map picture. We want this on record, and ask in question 4.
6. Sprite hiding and layer toggles change the renderer only, never emulated memory. Fast forward, in casual mode only, changes how many frames run per display frame.

## 6. Questions for the RetroAchievements team

1. Does a new frontend around libmgba count under the parent-project wording of section D (so GBA DS could be considered before Apr 13, 2027), or should we wait? Would an informal early review of this document be possible?
2. Is the extra `rcheevos/...` clause after the core segment acceptable, or should the user agent be exactly the three documented segments (`GBADS/1.6.2 (Linux) mgba/0.11.0`)? Does the user agent need to be added to your list on your side, and who adds it?
3. Is the internal save-and-reload-the-same-state snapshot in 5.2 acceptable, or should those pictures be replaced by the logo in hardcore?
4. Is a second-screen picture made by running a *separate copy* of the game (loaded from the live game's saved state, and never written back) acceptable in hardcore? It is used for the maps of Mega Man Battle Network 1 and 2, Baldur's Gate: Dark Alliance, Harvest Moon and about fifty other games. If not, we would need to show the logo in hardcore for those games.
5. Which privacy and data-retention statement of yours should the Privacy page link for the RetroAchievements side?
6. We'd like the first application to cover both installable builds (Linux and Android): one name and numeric version, with the OS segment telling them apart. Is that fine?

# Privacy

*Last updated November 22, 2026. This page covers the apps for ROCKNIX, BananaOS, the Stock Linux OS and Android. It is part of GBA DS's wiki; the wiki's history on GitHub keeps every earlier version.*

GBA DS runs on your device. It has no accounts of its own, no ads and no analytics or crash reporting, and **nobody who works on GBA DS runs a server that it talks to**. Your ROMs, saves, save states and screenshots stay on your device unless you copy them somewhere yourself.

The app uses the network in two cases only, both of which you can switch off.

## RetroAchievements (off unless you sign in)

If you sign in to [RetroAchievements](https://retroachievements.org) (on ROCKNIX and BananaOS, an account already set up in the system; on Android, **Settings > RetroAchievements: Sign In**), GBA DS talks to RetroAchievements' servers for the games you play. It sends:

- **Your user name and password**, once, to sign in. GBA DS keeps the token RetroAchievements answers with, not the password, and uses the token from then on.
- **The checksum of the game's ROM file** (an MD5 of the whole file), to find the game's achievements. The ROM itself is never sent. A game played with a patch sends nothing: RetroAchievements is off for it.
- **What you earn:** each achievement you unlock and each leaderboard entry, with the game, the time and your mode (casual or hardcore).
- **What you're playing now:** about every two minutes, the game and a line of text from the game's own Rich Presence script ("Skating in the Hangar, 12,000 points"), which RetroAchievements shows on your profile.
- **What any web request carries:** your device's IP address and a name for the app: `GBADS/<version> (Linux or Android) mgba/<version> rcheevos/<version>`.

It also downloads the badge pictures from RetroAchievements' servers, and keeps them on your device.

What RetroAchievements does with all that, how long it keeps it and where its servers are is for RetroAchievements to say: see the Privacy Policy linked at the bottom of [retroachievements.org](https://retroachievements.org). GBA DS has no copy of your data and can't delete yours there.

**On your device**, GBA DS keeps, in its `states/ra/` folder (`GBA DS/states/ra/` on Android):

- `login.cfg`: your user name and the sign-in token, readable only by the app's user. **Sign Out** (Android) deletes it; on other systems delete the file.
- `pending.txt`: unlocks and leaderboard entries the server hasn't confirmed yet (the connection dropped while you played). They are sent the next time you sign in, then removed; any still there after 60 days are dropped. The file keeps no token.
- `badges/`: the pictures.
- Beside each save state, `<state>.ra`: your progress on the game's achievements at that moment.

If you put `ra_username` and `ra_password` in `gba-ds.cfg`, that file holds your password as plain text. The sign-in token is safer: sign in once and take the password out. To stop RetroAchievements altogether, set `retroachievements = 0` or delete the `states/ra/` folder.

## Updates (on by default; Linux and Android)

Once a day, when the game list opens, GBA DS asks GitHub (`api.github.com`) which release is the latest. If there's a newer one it asks you before downloading anything, and the download comes from GitHub too. GitHub sees your IP address and the app's name and version (`GBA-DS/<version>`), as any website does; see [GitHub's privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement).

Switch the check off with **Settings > Updates > Automatic checks**, or `update_check = 0` in `gba-ds.cfg`.

## Logs and bug reports

The log (`gba-ds.log`) and **Submit a Bug Report** stay on your device. A bug report is a `.zip` in the `bug-reports` folder, and nothing sends it: you attach it to a GitHub issue if you choose to. It holds your note, the device and system, the settings, a save state of the moment and one from about ten seconds before, the buttons you pressed in between, and the end of the log, with any line that mentions a password or token left out. Anything you attach to a public issue is public.

## The browser version

The [browser version](https://kyannon.github.io/gba-ds/) runs the game inside your browser. Your ROM is not uploaded. It has no RetroAchievements and no update check. The page itself is loaded from GitHub Pages, so GitHub sees your visit like any other.

## Who keeps what, and for how long

| | Where | How long |
|---|---|---|
| Everything GBA DS makes (saves, states, settings, logs, bug reports, the RetroAchievements token and badges) | your device | until you delete it |
| Unlocks waiting to be sent | `states/ra/pending.txt` on your device | until sent, at most 60 days |
| Your RetroAchievements data | RetroAchievements' servers | their policy |
| Update requests | GitHub's servers | their policy |
| Anything else | nowhere: GBA DS doesn't collect it | |

Since GBA DS collects no personal data of its own and has no servers, it has no server locations, retention periods or data requests to report; the two services above are the only places your data goes, and each has its own policy.

Questions about this page: open an issue at [github.com/kyannon/gba-ds/issues](https://github.com/kyannon/gba-ds/issues).

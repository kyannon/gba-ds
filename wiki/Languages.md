GBA DS's menus, game list, messages and (on Android) its permission screen can be shown in your language. Choose it in **Settings > System > Language**, or set `language` in `gba-ds.cfg` ([[Settings]]). **Automatic** follows the system's language and falls back to English.

The first time GBA DS starts after it's installed, it asks: every language is listed by its own name, so you can find yours whatever language the screen opens in. Pick one with A (the screens change to it straight away), then press A again, START or Continue to keep it. If you start a game straight from your frontend, the game starts as usual and the question comes up the first time you open the pause menu. Updating GBA DS doesn't ask again.

## Languages

| Language | Status |
|---|---|
| English | the original |
| Español, Deutsch, Français, Italiano, Português (Brasil), 日本語, 한국어 | machine translated |
| 简体中文, 繁體中文 | machine translated (from [DokiOnjouji's pull request](https://github.com/kyannon/gba-ds/pull/62)) |

A machine-translated language says so in the Language list ("(machine translated)", in that language). It stays that way until a native speaker has gone through it. Wording that sounds odd, or is simply wrong, is likely: please report or fix it.

What isn't translated: game names, file names and paths, RetroAchievements' own text, the log and bug reports. Text that doesn't have a translation yet shows in English, so a half-finished language still works. Game and folder names in Chinese, Japanese or Korean draw in every language, paths included: the bold fonts hold the common Simplified and Traditional characters, Japan's extra kanji and the everyday Hangul syllables (paths draw the characters the translations don't use in the bold weight). A very rare kanji, hanja or Hangul syllable can still show as a box.

## Fixing or adding a language

Translations are plain text files, one per language: `assets/i18n/<code>.po` in the [source](https://github.com/kyannon/gba-ds/tree/dev/assets/i18n), the standard gettext format that tools such as Poedit and Weblate open. You don't need to build anything:

1. Open the language's `.po` file (or copy `gba-ds.pot` to `<code>.po` for a new one) and edit the `msgstr` lines. Keep placeholders like `%d` and `%s` as they are (their order can change with `%1$s`), and keep the words short: buttons are small.
2. Send it as a pull request, or attach it to an issue.
3. Say who checked it. A reviewed language loses its "machine translated" label.

The developer notes are in [`docs/I18N.md`](https://github.com/kyannon/gba-ds/blob/dev/docs/I18N.md). A new script (like Korean's) needs a font too: ask in the issue.

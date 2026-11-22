# Licenses

*Last updated November 22, 2026.*

GBA DS is free software under the **[GNU General Public License, version 3](https://github.com/kyannon/gba-ds/blob/main/LICENSE)**. Its source is at [github.com/kyannon/gba-ds](https://github.com/kyannon/gba-ds), and every release's source is the repository at that release's tag. GBA DS is free and has no paid features, in-app purchases or advertising.

What the release zip and the APK are made of, with each part's license and where its source is. Nothing here is a non-commercial license, and none is modified except where the table says so.

## The emulator

| Part | Version | License | Source |
|---|---|---|---|
| **libmgba**, the Game Boy Advance emulator (statically linked, not modified, built from the pinned commit in `scripts/mgba-options.sh`) | master, commit `143adf5` (October 2026) | [MPL-2.0](https://www.mozilla.org/MPL/2.0/) | [mgba-emu/mgba](https://github.com/mgba-emu/mgba) |
| zlib, the LZMA SDK and inih, bundled with mGBA | as in that commit | zlib license; public domain; BSD-3-Clause | inside mGBA's `src/third-party` |
| **SDL2**, windows, input and sound (built from source for each build) | `release-2.30.x` branch | [zlib license](https://www.libsdl.org/license.php) | [libsdl-org/SDL](https://github.com/libsdl-org/SDL) |

mGBA's source for the version GBA DS uses is public at the link above; GBA DS's own code is in this repository, so the MPL's requirement to make the source available is met for both.

## RetroAchievements

| Part | Version | License | Source |
|---|---|---|---|
| **rcheevos**, RetroAchievements' client library (`third_party/rcheevos`, trimmed: no hashing beyond MD5, no libretro or RAIntegration code) | v12.5.0 | MIT | [RetroAchievements/rcheevos](https://github.com/RetroAchievements/rcheevos) |
| MD5 (`rhash/md5.c`, inside rcheevos) | | Aladdin Enterprises' permissive license, in the file's header | same |

GBA DS talks to the RetroAchievements service through `curl` (a program already on ROCKNIX, or Banana's libcurl) or, on Android, through the system's own HTTP code. It doesn't ship curl.

## Pictures, text and formats

| Part | Version | License | Source |
|---|---|---|---|
| **libwebp**, decoder only, for the menus' pictures (`third_party/libwebp`) | v1.6.0 | BSD-3-Clause | [webmproject/libwebp](https://github.com/webmproject/libwebp) |
| **rzstd** and xxh64 from libretro-common, to read RetroArch's compressed save states (`third_party/rzstd`) | commit `52781c4` | MIT | [libretro/libretro-common](https://github.com/libretro/libretro-common) |
| **stb_truetype**, **stb_image**, **stb_image_write** (`src/`) | stb_truetype 1.26 | public domain (or MIT, at your choice) | [nothings/stb](https://github.com/nothings/stb) |
| **Jost**, the interface font (`assets/fonts`) | | [SIL Open Font License 1.1](https://github.com/kyannon/gba-ds/blob/main/assets/fonts/Jost-OFL.txt) | [indestructible-type/Jost](https://github.com/indestructible-type/Jost) |
| **Noto Sans JP, KR, SC and TC**, subsets for the Japanese, Korean and Chinese interface (`assets/fonts`) | | SIL Open Font License 1.1 (a copy beside each font) | [notofonts](https://github.com/notofonts) |

The SHA-256 code (`src/sha256.c`) is GBA DS's own.

## Android

The APK holds GBA DS, mGBA and SDL2 as above (SDL's Java classes are under the same zlib license) and no other libraries.

## The rest of what's on your device

GBA DS never includes game ROMs, BIOS files or anything from a game. The names, logos and pictures shown on the second screen are the games' own and belong to their owners; GBA DS draws them from the ROM you provide.

If you find something missing or wrong on this page, [open an issue](https://github.com/kyannon/gba-ds/issues).

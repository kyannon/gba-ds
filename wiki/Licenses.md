# Licenses

*Last updated November 26, 2026.*

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
| **Upscaling filters** for Display > Scaling filter: hq2x and OmniScale (Lior Halphon), MMPX (Morgan McGuire & Mara Gagiu), xBR-lv2 and xBR-lv3 (Hyllian), mGBA's shader files (`third_party/mgba-shaders`; two need small number-format fixes to compile on OpenGL ES, listed in `tools/embed_shaders.py`). Scale2x and Scale4x are GBA DS's own | mGBA commit `143adf5` | MIT | [mgba-emu/mgba `res/shaders`](https://github.com/mgba-emu/mgba/tree/master/res/shaders) |
| **rzstd** and xxh64 from libretro-common, to read RetroArch's compressed save states (`third_party/rzstd`) | commit `52781c4` | MIT | [libretro/libretro-common](https://github.com/libretro/libretro-common) |
| **stb_truetype**, **stb_image**, **stb_image_write** (`src/`) | stb_truetype 1.26 | public domain (or MIT, at your choice) | [nothings/stb](https://github.com/nothings/stb) |
| **Jost**, the interface font (`assets/fonts`) | | [SIL Open Font License 1.1](https://github.com/kyannon/gba-ds/blob/main/assets/fonts/Jost-OFL.txt) | [indestructible-type/Jost](https://github.com/indestructible-type/Jost) |
| **Noto Sans JP, KR, SC and TC**, subsets for the Japanese, Korean and Chinese interface (`assets/fonts`) | | SIL Open Font License 1.1 (a copy beside each font) | [notofonts](https://github.com/notofonts) |

The SHA-256 code (`src/sha256.c`) is GBA DS's own.

## Linux on the AYN Thor (Armada)

| Part | Version | License | Source |
|---|---|---|---|
| **Wayland**'s client headers and the drm-lease protocol (`third_party/wayland`): only headers and generated protocol code; the Wayland library itself is looked for on the system when the second screen is put on a lent panel, and isn't shipped | wayland 1.26.0, wayland-protocols 1.50 | MIT | [wayland.freedesktop.org](https://wayland.freedesktop.org) |
| The request for the lent panel (`src/drmlease.c`), a C port of **Azahar-Enhanced**'s DrmLeaseClient and of its connector and panel-orientation code | virtudude/azahar `drm-leasing` (commit `d16299140`), cerroverb/azahar `drm-leasing-link-crtc` (commit `71a132b3b`) | GPL v2 or later (so it fits GBA DS's GPL v3) | [virtudude/azahar](https://github.com/virtudude/azahar), [cerroverb/azahar](https://github.com/cerroverb/azahar) |

## Android

The APK holds GBA DS, mGBA and SDL2 as above (SDL's Java classes are under the same zlib license) and no other libraries.

## The rest of what's on your device

GBA DS never includes game ROMs, BIOS files or anything from a game. The names, logos and pictures shown on the second screen are the games' own and belong to their owners; GBA DS draws them from the ROM you provide.

If you find something missing or wrong on this page, [open an issue](https://github.com/kyannon/gba-ds/issues).

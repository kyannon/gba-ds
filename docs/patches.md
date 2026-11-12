# Known patches

Patches GBA DS knows by their file: colour restorations, voice removals,
translations and other romhacks, for the games it supports and for games it
doesn't support yet (so they're known when it does). `tools/make_dumps.py`
builds this table into `src/dumps_data.h` (`knownPatches[]`, and each
patched result as a mod in `knownDumps[]`), and the Patches menu (GEN-9)
uses it to name a patch and to say when an IPS is for another version of
the game (an IPS can't say which ROM it's for).

Columns:

- **Code**: the release the patch is made for (blank if not known). A patch
  made for several releases (or dumps) has a row for each.
- **File**: the file's name as it was published, for reference only: GBA
  DS knows a patch by its checksum, so players can name it as they like.
- **Size** and **Patch CRC32**: the file's size and the CRC32 of the file
  without its last four bytes (`tools/patch_id.py`; a UPS or BPS ends with
  its own checksum, so the whole file's CRC32 is the same for all of them).
- **Made for**: the CRC32 of the ROM it's made for (a No-Intro dump, or for
  an optional patch the main patch's result). Blank: any of the release's
  dumps, or not known.
- **Result**: the CRC32 of the patched ROM (from that base: worked out on
  the fixture, or written in the UPS / BPS itself). Blank: not known yet
  (the base isn't among the fixtures).
- **Tested**: "yes" when the game's cases pass on the result (rules and
  pictures checked; a colour patch's pictures differ only in colour), with
  a note; "no" with what doesn't work; blank for games GBA DS doesn't
  support or results it hasn't run.

To add a patch: `tools/patch_id.py FILE` for its size and checksum; the base
from its readme (a UPS or BPS names it: `tools/romhack.py` says which);
for a supported game, `tools/romhack.py PATCH --base CODE` for the result
and its cases. Then `tools/make_dumps.py --dat DAT`.

The patches are the owner's (Nov 11, 2026); the files themselves aren't in
the repo. Fixtures for the tested ones are in the fixtures repo's
`patches/`, named as here.

| Game | Code | Patch | Kind | File | Size | Patch CRC32 | Made for | Result | Tested |
|---|---|---|---|---|---|---|---|---|---|
| Blackthorne | `AQXP` | Color restoration | colour | Blackthorne (Europe) Color restoration.ips | 2532 | `2258fe18` | `e1d36a8c` |  |  |
| Breath of Fire | `ABFE` | Color restoration (081122) | colour | 0245 - Breath of Fire (U) 081122.ips | 29931 | `e1c840bc` | `f06422a8` |  |  |
| Breath of Fire II | `AB2E` | Palette fix v1.0 (gukid) | colour | bof2gba_palette_fix.ips | 42201 | `4da45fdb` | `6f098da3` |  |  |
| Breath of Fire II | `AB2P` | SNES palette | colour | bof2_eu_snes_palette.ips | 42152 | `8c9f2a02` | `ff5e8e39` |  |  |
| Breath of Fire II | `AB2J` | SNES palette | colour | bof2_jp_snes_palette.ips | 42152 | `c275ef24` | `8a824a70` |  |  |
| Donkey Kong Country | `A5NP` | Restoration v1.1 (marc_max) | colour | dkc_gba_restoration_v1_1.ips | 19955 | `702bd4af` | `41d277fe` |  |  |
| Final Fantasy IV Advance | `BZ4P` | Color improvement and restoration 1.1 | colour | Final Fantasy IV Advance Color Improvement and Restoration 1.1.ips | 88348 | `6145799d` | `27f2bc67` |  |  |
| Final Fantasy V Advance | `BZ5P` | Color correction and improvement | colour | Final Fantasy V Color Correction and Improvement.ips | 29491 | `9e8bd7d4` | `7197117e` |  |  |
| Final Fantasy VI Advance | `BZ6P` | Color restoration v1.01 | colour | ff6_gba_e_color_restoration.ips | 29738 | `44d098f5` | `d15deba5` |  |  |
| Final Fantasy VI Advance | `BZ6J` | Color restoration v1.01 | colour | ff6_gba_j_color_restoration.ips | 29790 | `2f796c54` | `d67478fe` |  |  |
| Final Fantasy VI Advance | `BZ6E` | Color restoration v1.01 | colour | ff6_gba_u_color_restoration.ips | 29552 | `563ccfa0` | `d708f5ab` |  |  |
| Final Fantasy VI Advance | `BZ6P` | Color restoration: animation (optional) | colour | ff6_gba_jue_color_restoration_animation.ips | 119 | `82dfc347` |  |  |  |
| Final Fantasy VI Advance | `BZ6J` | Color restoration: animation (optional) | colour | ff6_gba_jue_color_restoration_animation.ips | 119 | `82dfc347` |  |  |  |
| Final Fantasy VI Advance | `BZ6E` | Color restoration: animation (optional) | colour | ff6_gba_jue_color_restoration_animation.ips | 119 | `82dfc347` |  |  |  |
| Final Fantasy VI Advance | `BZ6P` | Color restoration: lightning (optional) | colour | ff6_gba_jue_color_restoration_lightning.ips | 43 | `1ae6602b` |  |  |  |
| Final Fantasy VI Advance | `BZ6J` | Color restoration: lightning (optional) | colour | ff6_gba_jue_color_restoration_lightning.ips | 43 | `1ae6602b` |  |  |  |
| Final Fantasy VI Advance | `BZ6E` | Color restoration: lightning (optional) | colour | ff6_gba_jue_color_restoration_lightning.ips | 43 | `1ae6602b` |  |  |  |
| Final Fantasy VI Advance | `BZ6P` | Color restoration: monster (optional) | colour | ff6_gba_jue_color_restoration_monster.ips | 1150 | `452e75de` |  |  |  |
| Final Fantasy VI Advance | `BZ6J` | Color restoration: monster (optional) | colour | ff6_gba_jue_color_restoration_monster.ips | 1150 | `452e75de` |  |  |  |
| Final Fantasy VI Advance | `BZ6E` | Color restoration: monster (optional) | colour | ff6_gba_jue_color_restoration_monster.ips | 1150 | `452e75de` |  |  |  |
| Final Fight One | `AFFJ` | Redux Edition | colour | Final Fight One - Redux Edition.ips | 213089 | `62445f39` | `c9e5e223` |  |  |
| The Lost Vikings | `ALVP` | Palette restoration v1.0 | colour | Lost Vikings, The (Europe) (En,Fr,De,Es) Palette Restoration v1_0.ips | 3485 | `c4356eb6` | `1c008d32` |  |  |
| Mother 1+2 | `A2UJ` | English v1.01 | translation | mother12.ips | 165587 | `4e04ba9e` | `0a44569c` |  |  |
| Mother 3 | `A3UJ` | English v1.3a (fan translation) | translation | mother3_1.3a.ups | 1547180 | `d849ce95` | `42ac9cb9` | `e73b0f7d` | yes |
| Mother 3 | `A3UJ` | English v1.3 (fan translation) | translation | mother3_1.3.ups | 1546281 | `89f254f4` | `42ac9cb9` | `8a3bc5a8` | yes |
| Mother 3 | `A3UJ` | HQ audio mix (MusicTheorist) | audio | Mother_3_Default_HQ_Audio_Japan.ups | 2318 | `69c5246a` | `42ac9cb9` | `add15282` | yes |
| Mother 3 | `A3UJ` | HQ audio mix alternative (MusicTheorist) | audio | Mother_3_Alt._HQ_Audio_Japan.ups | 2336 | `f02e4122` | `42ac9cb9` | `b7faa37d` | yes |
| Mother 3 | `A3UJ` | English v1.3 HQ audio mix (MusicTheorist) | audio | Mother_3_EN_v1.3.ups | 2340 | `cc81483c` | `0e013b7b` | `91ac31cb` | yes (made for one build of the English v1.3 ROM, not the published v1.3 UPS's result; a UPS can't follow another patch, so it doesn't go on top of the translation patch) |
| Mother 3 | `A3UJ` | Brazilian Portuguese (fan translation) | translation | MOTHER_3_Brazilian_Portuguese.ups | 1354866 | `c6b824db` | `42ac9cb9` | `00508b53` | yes |
| Mother 3 | `A3UJ` | Italian v1.1.1 (fan translation) | translation | MOTHER_3_Italian_1.1.1.ups | 1462865 | `6607345b` | `42ac9cb9` | `e7185f88` | yes |
| Mother 3 | `A3UJ` | German v1.1 (fan translation) | translation | MOTHER_3_German_1.1.ups | 1960371 | `887ef75e` | `42ac9cb9` | `b0b73624` | yes |
| Mother 3 | `A3UJ` | Simplified Chinese (fan translation) | translation | MOTHER_3_Simplified_Chinese.bps | 1051060 | `d9c06486` | `42ac9cb9` | `21fdf96c` | yes |
| Mother 3 | `A3UJ` | Polish (fan translation) | translation | MOTHER_3_Polish.bps | 1381012 | `074595ff` | `42ac9cb9` | `b439dc34` | yes |
| Mother 3 | `A3UJ` | French (fan translation) | translation | MOTHER_3_French.ups | 1670991 | `af332e9a` | `42ac9cb9` | `3c4f84e9` | yes |
| Mother 3 | `A3UJ` | Catalan v1.0 (fan translation) | translation | MOTHER_3_Catalan_1.0.ups | 1555589 | `231ead71` | `42ac9cb9` | `4f9f87b5` | yes |
| Mother 3 | `A3UJ` | Catalan v1.0.2 (fan translation) | translation | MOTHER_3_Catalan_1.0.2.ups | 1636872 | `d232c9db` | `42ac9cb9` | `5f7919f4` | yes |
| Fire Emblem: Fuuin no Tsurugi | `AFEJ` | English (Dark Twilkitri) | translation | fe6.ips | 610179 | `5befafab` | `d38763e1` |  |  |
| Rhythm Tengoku | `BRIJ` | Rhythm Heaven Silver beta 13a | translation | Rhythm.Heaven.Silver.-.Beta.13a.ips | 339033 | `be54e107` | `349d7025` |  |  |
| Super Mario Advance 4 | `AX4E` | GBP palette v1.0 (djedditt) | colour | AGB-AX4E_(USA)_GBPPalette.ips | 15 | `9fa926ad` | `a44ba79f` | `d4f7d398` | yes (SM4-9's check of the opening's frame colour doesn't apply: the frame is there, a shade darker) |
| Super Mario Advance 4 | `AX4E` | GBP palette v1.0 (djedditt) | colour | AGB-AX4E_(USA, Australia)_(Rev 1)_GBPPalette.ips | 15 | `a99fb3df` | `88dab27f` | `e6c145c2` | yes |
| Super Mario Advance 4 | `AX4E` | GBP palette v1.0 (djedditt) | colour | AGB-AX4E_(USA)_(Rev 1)_(Virtual Console)_GBPPalette.ips | 15 | `a99fb3df` | `d4c13ac3` |  |  |
| Super Mario Advance 4 | `AX4J` | GBP palette v1.0 (djedditt) | colour | AGB-AX4J_(Japan)_GBPPalette.ips | 15 | `92c0cb60` | `f3c87306` | `500b180f` | yes |
| Super Mario Advance 4 | `AX4J` | GBP palette v1.0 (djedditt) | colour | AGB-AX4J_(Japan)_(Rev 1)_GBPPalette.ips | 15 | `40f08492` | `5a676424` |  |  |
| Super Mario Advance 4 | `AX4J` | GBP palette v1.0 (djedditt) | colour | AGB-AX4J_(Japan)_(Rev 2)_GBPPalette.ips | 15 | `dc1dbffc` | `7b1a4c22` |  |  |
| Super Mario Advance 4 | `AX4J` | GBP palette v1.0 (djedditt) | colour | AGB-AX4J_(Japan)_(Rev 2)_(Virtual Console)_GBPPalette.ips | 15 | `dc1dbffc` | `4aee9c77` |  |  |
| Super Mario Advance 4 | `AX4P` | GBP palette v1.0 (djedditt) | colour | AGB-AX4P_(Europe)_GBPPalette.ips | 15 | `b7be6294` | `37141f32` | `f5362ce8` | yes |
| Super Mario Advance 4 | `AX4P` | GBP palette v1.0 (djedditt) | colour | AGB-AX4P_(Europe)_(Rev 1)_GBPPalette.ips | 15 | `95bafd62` | `8d84acfc` | `3996c9e4` | yes |
| Super Mario Advance 4 | `AX4P` | GBP palette v1.0 (djedditt) | colour | AGB-AX4P_(Europe)_(Rev 1)_(Virtual Console)_GBPPalette.ips | 15 | `95bafd62` | `d4f45b01` | `60e63e19` | yes |
| Super Mario Advance 4 | `AX4E` | Voice removal v0.2 | voice removal | smb3gba_voice_removal(U)0.2.ips | 15514 | `989a0b35` | `88dab27f` | `ec07d927` | yes |
| Super Mario Advance 4 | `AX4P` | Voice removal v0.2 | voice removal | smb3gba_voice_removal(E)0.2.ips | 15498 | `8eddafdb` | `8d84acfc` | `4da5dd12` | yes |
| Super Mario Advance 4 | `AX4P` | Voice removal v0.2 | voice removal | smb3gba_voice_removal(E)0.2.ips | 15498 | `8eddafdb` | `d4f45b01` | `14d52aef` | yes (on the Virtual Console dump) |
| Super Mario Advance 3 | `A3AE` | Colour restoration | colour | SMA3 - Yoshi's Island Colour Restoration (U).ips | 19180 | `b1cd1b98` | `40a48276` | `954c2cfe` | yes |
| Super Mario Advance 3 | `A3AP` | Colour restoration | colour | SMA3 - Yoshi's Island Colour Restoration (E).ips | 19210 | `386e3f75` | `639e9d3b` | `7323e988` | yes |
| Super Mario Advance 3 | `A3AE` | Voice removal v0.1 | voice removal | smw2yigba_voice_removal(U)0.1.ips | 43358 | `ee1f43b9` | `40a48276` | `6cda3e7d` | yes |
| Super Mario Advance 3 | `A3AP` | Voice removal v0.1 | voice removal | smw2yigba_voice_removal(E)0.1.ips | 43361 | `3458036c` | `639e9d3b` | `84ae96c9` | yes |
| Super Mario Advance 2 | `AA2E` | Super Mario World Color Restoration v0.91 | colour | smwcr091u.ips | 5975 | `0161e91a` | `5206880a` | `18301d3b` | yes |
| Super Mario Advance 2 | `AA2P` | Super Mario World Color Restoration v0.91 | colour | smwcr091e.ips | 16670 | `8c054cb5` | `fcfef343` | `df232c46` | yes |
| Super Mario Advance 2 | `AA2E` | Voice removal v1.0 | voice removal | smwgba_voices_removed(U)1.0.ips | 456 | `586584dd` | `5206880a` | `c8ebf77b` | yes |
| Super Mario Advance 2 | `AA2P` | Voice removal v1.0 | voice removal | smwgba_voices_removed(E)1.0.ips | 456 | `de50ae9d` | `fcfef343` | `9111275f` | yes |
| Super Mario Advance | `AMAE` | SNES color restoration v1.2 (Mister Man) | colour | SMA_USA.ips | 28032 | `5ecbe2cc` | `1e4c6d6a` | `21f4b076` | yes |
| Super Mario Advance | `AMAE` | SNES color restoration v1.2 (Mister Man) | colour | SMA_USA.ips | 28032 | `5ecbe2cc` | `5251f2bf` |  |  |
| Super Mario Advance | `AMAJ` | SNES color restoration v1.2 (Mister Man) | colour | SMA_JPN.ips | 27976 | `23fed75d` | `b993b92f` | `7d4b7cf8` | yes |
| Super Mario Advance | `AMAJ` | SNES color restoration v1.2 (Mister Man) | colour | SMA_JPN.ips | 27976 | `23fed75d` | `49684d37` |  |  |
| Super Mario Advance | `AMAC` | SNES color restoration v1.2 (Mister Man) | colour | SMA_CHN.ips | 32214 | `bb477387` | `14a87b75` |  |  |
| Super Mario Advance |  | SNES color restoration v1.2 (Mister Man) | colour | SMA_USA_KIOSK.ips | 28034 | `692fedb4` |  |  |  |
| Super Mario Advance | `AMAE` | Color restoration: GBA-style player palettes (optional) | colour | GBA-Style Player Palettes_USA.ips | 721 | `8b8d7e00` | `21f4b076` | `5c0dec72` | yes |
| Super Mario Advance | `AMAJ` | Color restoration: GBA-style player palettes (optional) | colour | GBA-Style Player Palettes_JPN.ips | 721 | `ceba7723` | `7d4b7cf8` | `95ec1d90` | yes |
| Super Mario Advance | `AMAC` | Color restoration: GBA-style player palettes (optional) | colour | GBA-Style Player Palettes_CHN.ips | 721 | `a5d3b381` |  |  |  |
| Super Mario Advance |  | Color restoration: GBA-style player palettes (optional) | colour | GBA-Style Player Palettes_USA_KIOSK.ips | 721 | `55811324` |  |  |  |
| Super Mario Advance | `AMAE` | Color restoration: original enemy colors (optional) | colour | Original Enemy Colors_USA.ips | 74 | `3a24187e` | `21f4b076` | `37f01824` | yes |
| Super Mario Advance | `AMAJ` | Color restoration: original enemy colors (optional) | colour | Original Enemy Colors_JPN.ips | 74 | `43568774` | `7d4b7cf8` | `269e759b` | yes |
| Super Mario Advance | `AMAC` | Color restoration: original enemy colors (optional) | colour | Original Enemy Colors_CHN.ips | 74 | `6aed9b89` |  |  |  |
| Super Mario Advance |  | Color restoration: original enemy colors (optional) | colour | Original Enemy Colors_USA_KIOSK.ips | 74 | `6fa53b0e` |  |  |  |
| Super Mario Advance | `AMAE` | Voice removal v1.0 | voice removal | smb2gba_voice_removal(UE)1.0.ips | 29795 | `5f77d74e` | `1e4c6d6a` | `074f757e` | yes |
| Castlevania: Aria of Sorrow | `A2CE` | Recolor v1.02 (JonataGuitar) | colour | Castlevania - AoS (USA) - Recolor by Jonata Guitar v1.02.ips | 211500 | `8158a3bf` | `35536183` | `47a42f0e` | yes |
| Castlevania: Aria of Sorrow | `A2CE` | Recolor v1.02 (JonataGuitar) | colour | Castlevania - AoS (USA) - Recolor by Jonata Guitar v1.02.bps | 209893 | `b4aecf06` | `35536183` | `47a42f0e` | yes |
| Castlevania: Aria of Sorrow | `A2CE` | Symphony of the Colors v1.0 (JonataGuitar) | colour | Castlevania - AoS (USA) - Symphony of the Colors by JonataGuitar v1.0.ips | 142985 | `d79af16f` | `35536183` | `00d0fddf` | yes |
| Castlevania: Aria of Sorrow | `A2CE` | Symphony of the Colors v1.0 (JonataGuitar) | colour | Castlevania - AoS (USA) - Symphony of the Colors by Jonata Guitar v1.0.bps | 141702 | `f3235cb8` | `35536183` | `00d0fddf` | yes |
| Castlevania: Harmony of Dissonance | `ACHE` | Recolor v1.0 (JonataGuitar and sorrow) | colour | Castlevania HoD Recolor by JonataGuitar and sorrow v1.0.ips | 480701 | `328ddf8c` | `88c1b562` | `5b8c3e50` | yes |
| Mario Kart: Super Circuit | `AMKE` | Better colors (N.Dpendent) | colour | Mario Kart Super Circuit (Better Colors).ips | 276 | `91a2b71e` | `ed316e37` | `68841f3a` | yes (the cases don't show the recoloured drivers) |
| Mario Kart: Super Circuit | `AMKP` | Better colors (N.Dpendent) | colour | Mario Kart Super Circuit (Better Colors).ips | 276 | `91a2b71e` | `20025842` | `a5b7294f` | yes (the cases don't show the recoloured drivers) |
| Metroid Fusion | `AMTE` | Color improvement v1.0 | colour | Metroid Fusion Color Improvement.ips | 25355 | `04198374` | `6c75479c` | `9ea6ec8e` | yes |
| Metroid: Zero Mission | `BMXE` | Color improvement v1 | colour | Metroid Zero Mission - Color Improvement v1.ips | 20959 | `01369813` | `5c61a844` | `70409097` | yes (its pause screen's colours swapped in the profile) |
| Metroid: Zero Mission | `BMXE` | Color improvement v2 | colour | Metroid Zero Mission - Color Improvement v2.ips | 23869 | `02f9f58b` | `5c61a844` | `4c0e3216` | yes (its pause screen's colours swapped in the profile) |
| Wario Land 4 | `AWAE` | Color palette enhancement 1.1 (Piggy Chan!) | colour | Wario Land 4 Color Palette Enhancement 1.1.ips | 42658 | `e6ae60dd` | `d6141609` | `c0ce2370` | yes |
| The Legend of Zelda: A Link to the Past | `AZLE` | Voice removal v0.1 | voice removal | lozalttpgba_voice removal(U)0.1.ips | 3850 | `18ef1718` | `8e91cd13` | `abd1a38b` | yes |
| The Legend of Zelda: A Link to the Past | `AZLP` | Voice removal v0.1 | voice removal | lozalttpgba_voice removal(E)0.1.ips | 3850 | `58129d2d` | `5a164321` | `72e519b7` | yes |

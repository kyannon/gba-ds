# Upscaling shaders from mGBA

The Scaling filter's upscalers (issue #89), copied unmodified from mGBA's
`res/shaders/*.shader` folders at commit `143adf5` (the libmgba commit GBA DS
builds, `scripts/mgba-options.sh`):

| Folder | Filter | Author | License |
|---|---|---|---|
| `xbr-lv2` | xBR-lv2 | Hyllian | MIT (header of `xbr.fs` / `xbr.vs`) |
| `hq2x` | hq2x | Lior Halphon | MIT (header of `hq2x.fs`) |

GBA DS doesn't run mGBA's shader code (libmgba is built without its GL
renderer): `tools/embed_shaders.py` turns these files into
`src/upscale_shaders.c` (generated: regenerate it, never edit it), applying
the few fixes OpenGL ES needs (its `FIXES` list: `2` written as `2.0` where
desktop GLSL turns it into a decimal itself), and
`src/upscale.c` runs them the way mGBA's GLES2 renderer does: GLSL ES 1.00,
attribute `position`, varying `texCoord` (GLSL ES 1.00, or 3.00 for the
ones that need it: both), uniforms `tex`, `texSize` (the
source's size), `outputSize` (the target's) and the manifest's own uniforms at
their defaults. The licence notices stay in the sources, which are compiled
into the program, and are listed in `UPSCALERS-LICENSE.txt` (shipped in the
zip's licenses folder) and `wiki/Licenses.md`.

Scale2x and Scale4x are GBA DS's own (`src/upscale.c`).

Not included: mGBA's MMPX, xBR-lv3 and OmniScale were tried and are too slow
for the RG DS Plus's Mali-G52 (about 200, 200 and 60 ms a frame, Nov 26,
2026); nothing ships that can't hold full speed there.

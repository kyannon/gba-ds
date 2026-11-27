# Upscaling shaders from mGBA

The Scaling filter's upscalers (issue #89), copied unmodified from mGBA's
`res/shaders/*.shader` folders at commit `143adf5` (the libmgba commit GBA DS
builds, `scripts/mgba-options.sh`):

| Folder | Filter | Author | License |
|---|---|---|---|
| `xbr-lv2` | xBR-lv2 | Hyllian | MIT (header of `xbr.fs` / `xbr.vs`) |
| `xbr-lv3` | xBR-lv3 | Hyllian | MIT (same) |
| `hq2x` | hq2x | Lior Halphon | MIT (header of `hq2x.fs`) |
| `mmpx` | MMPX | Morgan McGuire & Mara Gagiu | MIT (header of `mmpx.fs`) |
| `omniscale` | OmniScale | Lior Halphon | MIT (header of `omniscale.fs`) |

GBA DS doesn't run mGBA's shader code (libmgba is built without its GL
renderer): `tools/embed_shaders.py` turns these files into
`src/upscale_shaders.c` (generated: regenerate it, never edit it), applying
the few fixes OpenGL ES needs (its `FIXES` list: `2` written as `2.0` where
desktop GLSL turns it into a decimal itself), and
`src/upscale.c` runs them the way mGBA's GLES2 renderer does: GLSL ES 1.00,
attribute `position`, varying `texCoord` (GLSL ES 1.00, or 3.00 for the
ones that need it: hq2x, xBR, OmniScale), uniforms `tex`, `texSize` (the
source's size), `outputSize` (the target's) and the manifest's own uniforms at
their defaults. The licence notices stay in the sources, which are compiled
into the program, and are listed in `UPSCALERS-LICENSE.txt` (shipped in the
zip's licenses folder) and `wiki/Licenses.md`.

Scale2x and Scale4x are GBA DS's own (`src/upscale.c`).

MMPX, xBR-lv2, xBR-lv3 and OmniScale are heavy: on the RG DS Plus's Mali-G52
MMPX, xBR-lv3 and OmniScale took about 200, 200 and 60 ms a frame and xBR-lv2
held only about 40 fps (Nov 26, 2026), so `upscale.c` greys them out
on GPUs in its `tooWeak` list and warns on GPUs it hasn't been told about.

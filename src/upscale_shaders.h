/* mGBA's upscaling shaders, embedded by tools/embed_shaders.py into
 * src/upscale_shaders.c (issue #89; third_party/mgba-shaders). */
#ifndef GBADS_UPSCALE_SHADERS_H
#define GBADS_UPSCALE_SHADERS_H

typedef struct {
	const char* name;
	float value; /* the manifest's default */
} UpscaleUniform;

typedef struct {
	const char* name;
	const char* vs; /* NULL: mGBA's plain one (upscale.c) */
	const char* fs;
	const UpscaleUniform* uniforms;
	int nUniforms;
} UpscalePack;

/* in tools/embed_shaders.py's PACKS order */
enum { UP_PACK_HQ2X, UP_PACK_MMPX, UP_PACK_XBR_LV2, UP_PACK_XBR_LV3, UP_PACK_OMNISCALE, UP_PACK_COUNT };
extern const UpscalePack upscalePacks[UP_PACK_COUNT];

#endif

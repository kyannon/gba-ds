/* src/fonts_data.c for the 3DS, without the two Chinese bold faces: they carry whole character sets for players'
 * own file and folder names (7 MiB of the program, which on the 3DS is memory the games and their hidden copies
 * need). The regular Chinese faces, which hold the characters the interface's own text uses, stand in for them, so
 * the interface in Chinese is whole in one weight; a Chinese name outside those characters draws as boxes. The
 * linker drops what the table below doesn't name (--gc-sections). */
#define embeddedFonts embeddedFontsWhole
#include "../../src/fonts_data.c"
#undef embeddedFonts

const EmbeddedFont embeddedFonts[EMBEDDED_FONT_COUNT] = {
	[EMBEDDED_JOST_BOLD_ITALIC] = { jost_bold_italic_ttf, sizeof(jost_bold_italic_ttf) },
	[EMBEDDED_JOST_MEDIUM_ITALIC] = { jost_medium_italic_ttf, sizeof(jost_medium_italic_ttf) },
	[EMBEDDED_NOTO_SC_BOLD] = { noto_sc_regular_ttf, sizeof(noto_sc_regular_ttf) },
	[EMBEDDED_NOTO_SC_REGULAR] = { noto_sc_regular_ttf, sizeof(noto_sc_regular_ttf) },
	[EMBEDDED_NOTO_TC_BOLD] = { noto_tc_regular_ttf, sizeof(noto_tc_regular_ttf) },
	[EMBEDDED_NOTO_TC_REGULAR] = { noto_tc_regular_ttf, sizeof(noto_tc_regular_ttf) },
	[EMBEDDED_NOTO_JP_BOLD] = { noto_jp_bold_ttf, sizeof(noto_jp_bold_ttf) },
	[EMBEDDED_NOTO_JP_REGULAR] = { noto_jp_regular_ttf, sizeof(noto_jp_regular_ttf) },
	[EMBEDDED_NOTO_KR_BOLD] = { noto_kr_bold_ttf, sizeof(noto_kr_bold_ttf) },
	[EMBEDDED_NOTO_KR_REGULAR] = { noto_kr_regular_ttf, sizeof(noto_kr_regular_ttf) },
};

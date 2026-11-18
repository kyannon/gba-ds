/* The UI fonts, embedded (tools/embed_fonts.py). */
#ifndef GBADS_FONTS_DATA_H
#define GBADS_FONTS_DATA_H

#include <stddef.h>

enum {
	EMBEDDED_JOST_BOLD_ITALIC,
	EMBEDDED_JOST_MEDIUM_ITALIC,
	/* the Chinese interfaces' fonts (Noto Sans SC and TC, assets/fonts/), cut
	 * down to the characters the translations use: tools/make_zh_font.py */
	EMBEDDED_NOTO_SANS_SC_BOLD,
	EMBEDDED_NOTO_SANS_SC_REGULAR,
	EMBEDDED_NOTO_SANS_TC_BOLD,
	EMBEDDED_NOTO_SANS_TC_REGULAR,
	EMBEDDED_FONT_COUNT
};

typedef struct {
	const unsigned char* ttf; /* a zlib stream of the font's bytes */
	size_t size;
	size_t raw; /* how many bytes it decodes to */
} EmbeddedFont;

extern const EmbeddedFont embeddedFonts[EMBEDDED_FONT_COUNT];

#endif

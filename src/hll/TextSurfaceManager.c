/* TextSurfaceManager — v14 HLL for text measurement.
 * Dohna Dohna uses this to measure font glyph widths for layout. */

#include "system4/string.h"
#include "gfx/font.h"
#include "hll.h"

/* GetFontWidth(Text, &Width, Type, Size, R, G, B, BoldWeight, EdgeWeight, EdgeR, EdgeG, EdgeB) -> bool
 * Measures the pixel width of the given text string with the specified font properties.
 *
 * The CN (GBK) build without an .fnl returns the cell its renderer draws
 * (0x69fb30 -> 0x69c7a0; see gfx_text_cn_gdi): the width of the first
 * character plus 2e, where the edge e also counts the 太さ; an empty text is
 * 2e. CASFont@GetFontWidth passes one character at a time.
 *
 * Other games keep the byte-level heuristic (half-width for ASCII, full-width
 * for multi-byte). */
static bool TextSurfaceManager_GetFontWidth(struct string *text, int *width,
		int type, int size, int r, int g, int b,
		float bold_weight, float edge_weight,
		int edge_r, int edge_g, int edge_b)
{
	if (gfx_text_cn_gdi()) {
		int w = text && text->size ? gfx_text_cn_width(size, text->text[0]) : 0;
		if (width)
			*width = w + 2 * gfx_text_cn_edge(size, bold_weight, edge_weight);
		return true;
	}
	if (!text || size <= 0 || text->size == 0) {
		if (width) *width = 0;
		return true;
	}

	float w = 0.0f;
	const unsigned char *p = (const unsigned char *)text->text;
	while (*p) {
		if (*p <= 0x7f) {
			w += (float)size / 2.0f;
			p++;
		} else if (*p >= 0x81 && *p <= 0xFE && *(p+1)) {
			w += (float)size;
			p += 2;
		} else {
			w += (float)size / 2.0f;
			p++;
		}
	}
	if (width)
		*width = (int)w;
	return true;
}

HLL_LIBRARY(TextSurfaceManager,
	    HLL_EXPORT(GetFontWidth, TextSurfaceManager_GetFontWidth));

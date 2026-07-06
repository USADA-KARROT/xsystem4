/* TextSurfaceManager — v14 HLL for text measurement.
 * Dohna Dohna uses this to measure font glyph widths for layout. */

#include "system4/string.h"
#include "hll.h"

/* GetFontWidth(Text, &Width, Type, Size, R, G, B, BoldWeight, EdgeWeight, EdgeR, EdgeG, EdgeB) -> bool
 * Measures the pixel width of the given text string with the specified font properties.
 * Uses byte-level heuristic (half-width for ASCII, full-width for multi-byte)
 * because the game text is GB18030 and the engine's SJIS parser can't handle it. */
static bool TextSurfaceManager_GetFontWidth(struct string *text, int *width,
		int type, int size, int r, int g, int b,
		float bold_weight, float edge_weight,
		int edge_r, int edge_g, int edge_b)
{
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

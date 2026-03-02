/* TextSurfaceManager — v14 HLL for text measurement.
 * Dohna Dohna uses this to measure font glyph widths for layout. */

#include "system4/string.h"
#include "gfx/font.h"
#include "hll.h"

/* GetFontWidth(Text, &Width, Type, Size, R, G, B, BoldWeight, EdgeWeight, EdgeR, EdgeG, EdgeB) -> bool
 * Measures the pixel width of the given text string with the specified font properties. */
static bool TextSurfaceManager_GetFontWidth(struct string *text, int width_slot,
		int type, int size, int r, int g, int b,
		float bold_weight, float edge_weight,
		int edge_r, int edge_g, int edge_b)
{
	struct text_style ts = {
		.face = type,
		.size = (float)size,
		.bold_width = bold_weight,
		.weight = 0,
		.edge_left = edge_weight,
		.edge_up = edge_weight,
		.edge_right = edge_weight,
		.edge_down = edge_weight,
		.color = { r, g, b, 255 },
		.edge_color = { edge_r, edge_g, edge_b, 255 },
		.scale_x = 1.0f,
		.space_scale_x = 1.0f,
		.font_spacing = 0.0f,
	};

	float w = gfx_size_text(&ts, text->text);
	wrap_set_int(width_slot, 0, (int)w);
	return true;
}

HLL_LIBRARY(TextSurfaceManager,
	    HLL_EXPORT(GetFontWidth, TextSurfaceManager_GetFontWidth));

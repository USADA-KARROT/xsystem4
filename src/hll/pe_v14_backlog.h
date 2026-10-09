/* v14 functions the backlog (BackLog.pactex, backlog::detail) calls: the
 * 豎滾動條 amounts, a text state's font and spacing, AddChild. Included by
 * PartsEngine.c; registered before the stubs of pe_v14_prelink.h, which then
 * leave these names alone. */

/* The vertical scroll bar with that number (0x53daf0 finds only that type);
 * NULL for a missing parts and for any other widget, on which the original
 * functions below do nothing and return 0. Only a bar loaded from a pactex
 * has the type here. */
static struct parts *pe_v14_vscrollbar(int number)
{
	struct parts *parts = parts_try_get(number);
	return parts && parts->component_type == PARTS_COMPONENT_VSCROLLBAR ? parts : NULL;
}

/* void SetVScrollbarTotalSize(int Number, int Size): case 272 -> 0x591a10. */
static void PE_v14_SetVScrollbarTotalSize(int number, int size)
{
	struct parts *parts = pe_v14_vscrollbar(number);
	if (parts)
		parts_scrollbar_set_total(parts, size);
}

/* void SetVScrollbarViewSize(int Number, int Size): case 273 -> 0x591a50. */
static void PE_v14_SetVScrollbarViewSize(int number, int size)
{
	struct parts *parts = pe_v14_vscrollbar(number);
	if (parts)
		parts_scrollbar_set_view(parts, size);
}

/* void SetVScrollbarScrollPos(int Number, int Pos): case 274 -> 0x591a90. */
static void PE_v14_SetVScrollbarScrollPos(int number, int pos)
{
	struct parts *parts = pe_v14_vscrollbar(number);
	if (parts)
		parts_scrollbar_set_pos(parts, pos);
}

/* void SetVScrollbarScrollRate(int Number, float Rate): case 275 -> 0x591ac0. */
static void PE_v14_SetVScrollbarScrollRate(int number, float rate)
{
	struct parts *parts = pe_v14_vscrollbar(number);
	if (parts)
		parts_scrollbar_set_rate(parts, rate);
}

/* int GetVScrollbarTotalSize / ViewSize(int Number): cases 277 and 278 ->
 * 0x591b30 / 0x591b60. The original returns the size of the parts named by
 * 全體スクロール量サイズ連動 / 表示量サイズ連動 instead when there is one
 * (0x5749d0, +0x214 / +0x218); no pactex of the game names one, and the
 * link is not implemented. */
static int PE_v14_GetVScrollbarTotalSize(int number)
{
	struct parts *parts = pe_v14_vscrollbar(number);
	return parts ? parts->scrollbar.total : 0;
}

static int PE_v14_GetVScrollbarViewSize(int number)
{
	struct parts *parts = pe_v14_vscrollbar(number);
	return parts ? parts->scrollbar.view : 0;
}

/* int GetVScrollbarScrollPos(int Number): case 279 -> 0x591b90 (+0x118). */
static int PE_v14_GetVScrollbarScrollPos(int number)
{
	struct parts *parts = pe_v14_vscrollbar(number);
	return parts ? parts->scrollbar.pos : 0;
}

/* float GetVScrollbarScrollRate(int Number): case 280 -> 0x591bb0 (+0x11c). */
static float PE_v14_GetVScrollbarScrollRate(int number)
{
	struct parts *parts = pe_v14_vscrollbar(number);
	return parts ? parts->scrollbar.rate : 0.0f;
}

/* The text of a state for the getters below (0x53e510, then 0x5b77f0 with
 * type 21): a missing parts has none and the getters leave their outputs or
 * return 0; an existing one has the state made a text state, as the gauge
 * getters make theirs (pe_v14_gauge.h). */
static struct parts_text *pe_v14_text_state(int number, int state)
{
	if (state < 1 || state > PARTS_NR_STATES)
		return NULL;
	struct parts *parts = parts_try_get(number);
	if (!parts)
		return NULL;
	if (parts->states[state - 1].type != PARTS_TEXT)
		parts_dirty(parts);
	return parts_get_text(parts, state - 1);
}

/* void GetPartsTextFontProperty(int Number, wrap<int> Type, Size, R, G, B,
 * wrap<float> BoldWeight, wrap<int> EdgeR, EdgeG, EdgeB, wrap<float>
 * EdgeWeight, int State): case 730 -> 0x598850 -> 0x566240, the font
 * Parts_SetFont set (ITextParts@Font::get). */
static void PE_v14_GetPartsTextFontProperty(int number, int *type, int *size,
		int *r, int *g, int *b, float *bold_weight,
		int *edge_r, int *edge_g, int *edge_b, float *edge_weight, int state)
{
	struct parts_text *text = pe_v14_text_state(number, state);
	if (!text)
		return;
	if (type) *type = text->ts.face;
	if (size) *size = lroundf(text->ts.size);
	if (r) *r = text->ts.color.r;
	if (g) *g = text->ts.color.g;
	if (b) *b = text->ts.color.b;
	if (bold_weight) *bold_weight = text->ts.bold_weight;
	if (edge_r) *edge_r = text->ts.edge_color.r;
	if (edge_g) *edge_g = text->ts.edge_color.g;
	if (edge_b) *edge_b = text->ts.edge_color.b;
	if (edge_weight) *edge_weight = text_style_edge_width(&text->ts);
}

/* int Parts_GetTextCharSpace(int Number, int State): case 739 -> 0x598ae0
 * -> 0x5667b0 (the text state's +0x40). */
static int PE_v14_Parts_GetTextCharSpace(int number, int state)
{
	struct parts_text *text = pe_v14_text_state(number, state);
	return text ? lroundf(text->ts.font_spacing) : 0;
}

/* int Parts_GetTextLineSpace(int Number, int State): case 740 -> 0x598b10
 * -> 0x566800 (+0x44). */
static int PE_v14_Parts_GetTextLineSpace(int number, int state)
{
	struct parts_text *text = pe_v14_text_state(number, state);
	return text ? text->line_space : 0;
}

/* void AddChild(int Number, int ChildNumber): case 171 -> 0x58f310. The
 * child goes to the end of the parts' children (0x53afb0), where
 * Parts_SetParentPartsNumber puts it; nothing happens unless both exist
 * (0x540250). ILayoutBoxParts@AddChild is how CBackLogUnit@Build puts a
 * line into the backlog's box. */
static void PE_v14_AddChild(int number, int child)
{
	if (!parts_try_get(number) || !parts_try_get(child))
		return;
	PE_SetParentPartsNumber(child, number);
}

static void pe_v14_register_backlog(void)
{
	struct static_library *lib = &lib_PartsEngine;
	static_library_register(lib, "SetVScrollbarTotalSize", PE_v14_SetVScrollbarTotalSize);
	static_library_register(lib, "SetVScrollbarViewSize", PE_v14_SetVScrollbarViewSize);
	static_library_register(lib, "SetVScrollbarScrollPos", PE_v14_SetVScrollbarScrollPos);
	static_library_register(lib, "SetVScrollbarScrollRate", PE_v14_SetVScrollbarScrollRate);
	static_library_register(lib, "GetVScrollbarTotalSize", PE_v14_GetVScrollbarTotalSize);
	static_library_register(lib, "GetVScrollbarViewSize", PE_v14_GetVScrollbarViewSize);
	static_library_register(lib, "GetVScrollbarScrollPos", PE_v14_GetVScrollbarScrollPos);
	static_library_register(lib, "GetVScrollbarScrollRate", PE_v14_GetVScrollbarScrollRate);
	static_library_register(lib, "GetPartsTextFontProperty", PE_v14_GetPartsTextFontProperty);
	static_library_register(lib, "Parts_GetTextCharSpace", PE_v14_Parts_GetTextCharSpace);
	static_library_register(lib, "Parts_GetTextLineSpace", PE_v14_Parts_GetTextLineSpace);
	static_library_register(lib, "AddChild", PE_v14_AddChild);
}

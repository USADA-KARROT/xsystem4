/* text-size: the size a v14 text state and a font numeral report, and what is
 * placed by it, as the original computes them on the CN (GBK) glyph grid.
 *
 *   text extent 0x5bee50   every character is a run of its own (0x5bfe00 ->
 *                          0x5c0820); a line is its glyph textures and the
 *                          字間隔 between them, one less than the glyphs
 *                          (0x5bcd20, 0x5bef22); the widest line is the width
 *   line height 0x5bce00   (size made even) + 2e, or a taller glyph texture;
 *                          e = max(min(ceil(太さ), size), min(ceil(縁取り), size))
 *   lines                  a line break adds 行間隔 and the line's height
 *                          (0x5bef06); a text that ends in a line break has one
 *                          more, empty line (0x5bef63); no run, no size
 *   glyph texture 0x69c290 (cell width + 2e) x (size + 2e), the glyph at (e, e)
 *   glyph in its line      bottom aligned: y = line height - texture height
 *                          (0x5bcf60), 1 px down for an odd size
 *   origin and hit box     without a surface area, 0x5bf2c0 offsets by 0 /
 *                          -W/2 / -W and 0 / -H/2 / -H of that extent (jump
 *                          tables 0x5bf6ac, 0x5bf6c4; halves cut toward zero,
 *                          0x5bf37f) and keeps it for the hit test (+0x94/+0x98,
 *                          0x5bf7d0)
 *   font numeral 0x5b4df0  width: cells and (cells - 1) 字間隔; height: the
 *                          tallest glyph texture, size + 2e, not made even
 *                          (0x5b5133); placed from its top by 0x5b4380
 *
 * The expected numbers are worked out from these rules, not taken from
 * xsystem4, with these exceptions:
 *   - xsystem4's glyph texture is e rows higher than the original's cell,
 *     below it (the default font's descenders reach below the original's
 *     cell and need the room). TS1, TS2 and TS10 expect the texture as
 *     cell + e, and the "y" of TS6 the edge it leaves room for, which is also
 *     what the layout before this one drew. Those rows are no part of the
 *     layout: the cases take the cell for the line, the extent, the hit box
 *     and the glyph's place.
 *   - TS7 (v13, the message window), TS8, TS9, TS11 and TS12 expect what
 *     xsystem4 does off this grid, which must stay as it is.
 *   - With a surface area the original takes the origin offset from the
 *     extent cut by the area (0x59b760); xsystem4 takes it from the area's
 *     own size, as before. TS2 only checks that a text with an area is at
 *     the same place emptied as filled, however often its hit box is
 *     computed.
 *   - TS10's scaled text holds what xsystem4 does: the glyph's place in its
 *     line scales with the parts. How the original transforms the glyphs of
 *     a scaled parts was not read.
 *
 *   TS1  Line height and width of one line: even and odd sizes, 太さ, a
 *        fractional 縁取り, full-width characters with a negative 字間隔,
 *        mixed widths; every glyph's texture (the cell, e rows higher) and
 *        its offset in the line (line height - cell height); the on-cursor
 *        and key-down states of a parts.
 *   TS2  Several lines with 行間隔, a trailing and a leading line break, only
 *        line breaks, the emptied text (no size), Parts_AddPartsText giving
 *        the same as Parts_SetText; a centred text of an odd height (the
 *        half is cut toward zero); a text with a surface area is at the same
 *        place emptied as filled, and stays there when its hit box is
 *        computed again.
 *   TS3  Where a right-aligned, centred or bottom-aligned text is on the
 *        screen (all nine origin modes but 1 and 4), its hit box, and both
 *        after the script changes the text or the 字間隔; the same after
 *        PE_Save / PE_Load, also when the save holds the sizes, offsets and
 *        hit boxes of the layout before this one (multi-line texts and font
 *        numerals are left out:
 *        loading does not put the line breaks back and does not draw a
 *        numeral again, both as before).
 *   TS4  Font numerals: height size + 2e with a fractional 縁取り and with 太さ,
 *        and the upper-left corner by origin modes 9, 7 and 4 (an odd
 *        height too).
 *   TS5  In boxes: the 春銷 CountBase with its texts and numerals (the ／ is
 *        16 + 2 * 2 high), and a vertical box whose first child is a text
 *        with a 2.2 縁取り (26 high, so the child after it is 2 px lower and
 *        the middle-anchored box 1 px higher).
 *   TS6  The glyph inside its texture, with the real font and read back from
 *        the pixels: 縁取り 3, 1.5 and 太さ 0.3 move the glyph body 3, 2 and
 *        1 px down and right from where it is without them; the same for a
 *        numeral with 縁取り 2. And the edge below a descender: under the
 *        body of a "y" with 縁取り 2 there are 2 rows of edge, as there were
 *        before the glyph moved down.
 *   TS7  Unchanged: a v13 text on the same grid (a 字間隔 after every
 *        character, the line as high as the font size), and the message
 *        window's text, which is laid out by the same function.
 *   TS8  Unchanged: a v14 text under the SJIS rule (the glyph-less font skips
 *        its characters, so it has no size), and font numerals there.
 *   TS9  Unchanged: the same with the real font: three characters are three
 *        times one character wide, spacing included.
 *   TS10 Drawn: the glyph textures of a text, filled, and the parts drawn
 *        into an offscreen target by parts_render; the rows and columns that
 *        come back are where the original puts the glyphs (the top row is
 *        the line's bottom less the cell; the e rows of xsystem4's texture
 *        show below the cell, and the next line still starts a line height
 *        and the 行間隔 lower). The texture's first row is filled more
 *        opaque than the rest and must come back as the top row. A parts
 *        scaled by 2 (see above).
 *   TS11 Drawn, unchanged: a v13 text with an edge and the message window's
 *        text come back where they were.
 *   TS12 Unchanged: the GBK rule with an .fnl font library (not the grid): a
 *        text and font numerals.
 *
 * Every case forks and sets the character rule itself, so the mode checks
 * the same things with and without XS4_PROBE_GBK. Glyph textures and numerals
 * need the construction mode's GL context (the text-default one creates no
 * texture), so every case uses it and its fork: an environment failure ends
 * the mode with rc 2. TS10 and TS11 also build the parts shader and draw one
 * parts that is no text, before the setup is reported: a shader that does
 * not build, or that draw coming back empty, counts as the environment's.
 * After that, a text that comes back empty is a failed case.
 * The font is the one whose glyphs never load, except in TS6 and TS9. Only
 * functions and fields that exist before the fix are used, so before-check
 * builds the old tree. Requires layout_box_fixture.inc and
 * construction_fixture.inc.
 */

static int ts_failures;
#define TS_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ts_failures++; } } while (0)

#define TS_JINZAI "\xc8\xcb\xb2\xc4"	/* 人材 */
#define TS_JIN "\xc8\xcb"		/* 人 */
#define TS_SLASH "\xa3\xaf"		/* ／ */

/* A 數字部件 drawn with a font, with a フォント太さ. */
static struct ex_tree ts_numeral(const char *name, const struct lb_place *pl, int size, float weight, float edge,
		int space)
{
	struct ex_tree info = bu_branch(BU_INFO, 2);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = bu_branch(BU_NORMAL, 6);
	struct ex_tree *st = &info.children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BU_NUMERAL);
	st->children[1] = bu_ileaf(LB_SHOW_TYPE, 2);
	st->children[2] = bu_ileaf(TD_SIZE, size);
	st->children[3] = lb_fleaf(LB_WEIGHT, weight);
	st->children[4] = lb_fleaf(LB_EDGE, edge);
	st->children[5] = bu_ileaf(LB_SPACE, space);
	return lb_node(name, pl, info, 0);
}
/* A named text state: 文本 and 文本裝飾. */
static struct ex_tree ts_text_state(const char *state, const char *text, int size, float weight, float edge, int space)
{
	struct ex_tree st = td_state(state, 2);
	st.children[1] = bu_sleaf(TD_TEXT, text);
	st.children[2] = bu_branch(TD_DECO, 4);
	st.children[2].children[0] = bu_ileaf(TD_SIZE, size);
	st.children[2].children[1] = lb_fleaf(LB_WEIGHT, weight);
	st.children[2].children[2] = lb_fleaf(LB_EDGE, edge);
	st.children[2].children[3] = bu_ileaf(LB_SPACE, space);
	return st;
}
/* A 文本部件 with a normal, an on-cursor and a key-down text. */
static struct ex_tree ts_text3(const char *name, const struct lb_place *pl, struct ex_tree normal, struct ex_tree hover,
		struct ex_tree down)
{
	struct ex_tree info = bu_branch(BU_INFO, 4);
	info.children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	info.children[1] = normal;
	info.children[2] = hover;
	info.children[3] = down;
	return lb_node(name, pl, info, 0);
}

static int ts_no(int act, const char *name)
{
	int n = bu_number(act, name);
	TS_CHECK(n > 0, "%s has no parts number", name);
	return n;
}
static int ts_load(const char *act_name, struct ex *e)
{
	struct string *name = cstr_to_string(act_name);
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	TS_CHECK(loaded, "%s did not load", act_name);
	return sf_str(act_name, -1);
}
/* The text of a state (1-based, as the scripts count them). */
static struct parts_text *ts_text_of(int no, int state)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	return p && p->states[state - 1].type == PARTS_TEXT ? &p->states[state - 1].text : NULL;
}
static void ts_expect_size_of(int no, int state, int w, int h, const char *what)
{
	int gw = PE_GetPartsWidth(no, state), gh = PE_GetPartsHeight(no, state);
	TS_CHECK(gw == w && gh == h, "%s: size %dx%d, want %dx%d", what, gw, gh, w, h);
}
static void ts_expect_size(int no, int w, int h, const char *what)
{
	ts_expect_size_of(no, 1, w, h, what);
}
static void ts_expect_ul(int no, int x, int y, const char *what)
{
	int gx = PE_GetPartsUpperLeftPosX(no, 1), gy = PE_GetPartsUpperLeftPosY(no, 1);
	TS_CHECK(gx == x && gy == y, "%s: upper left (%d,%d), want (%d,%d)", what, gx, gy, x, y);
}
static Point ts_ul(int no)
{
	return (Point) { PE_GetPartsUpperLeftPosX(no, 1), PE_GetPartsUpperLeftPosY(no, 1) };
}
/* The offset an origin mode gives a w x h box: halves cut toward zero. */
static Point ts_origin_offset(int om, int w, int h)
{
	int kx = (om - 1) % 3, ky = (om - 1) / 3;
	return (Point) { kx == 1 ? -(w / 2) : kx == 2 ? -w : 0, ky == 1 ? -(h / 2) : ky == 2 ? -h : 0 };
}
static void ts_expect_pos(int no, int x, int y, int om, const char *what)
{
	struct parts *p = parts_try_get(no);
	TS_CHECK(p && p->local.pos.x == x && p->local.pos.y == y && p->origin_mode == om,
		 "%s at (%d,%d) origin %d, want (%d,%d) origin %d", what, p ? p->local.pos.x : -1,
		 p ? p->local.pos.y : -1, p ? p->origin_mode : -1, x, y, om);
}
/* Size, upper-left corner and hit box of a parts: the rectangle w x h at
 * (x, y) on the screen. */
static void ts_expect_placed(int no, int w, int h, int x, int y, const char *what)
{
	ts_expect_size(no, w, h, what);
	ts_expect_ul(no, x, y, what);
	int x1 = x + w - 1, y1 = y + h - 1;
	TS_CHECK(PE_IsCursorIn(no, x, y, 1) && PE_IsCursorIn(no, x1, y1, 1),
		 "%s: corners (%d,%d) and (%d,%d) are not under the cursor", what, x, y, x1, y1);
	TS_CHECK(!PE_IsCursorIn(no, x - 1, y, 1) && !PE_IsCursorIn(no, x, y - 1, 1)
		 && !PE_IsCursorIn(no, x1 + 1, y1, 1) && !PE_IsCursorIn(no, x1, y1 + 1, 1),
		 "%s: a point just outside (%d,%d)-(%d,%d) is under the cursor", what, x, y, x1, y1);
}
static void ts_set_text(int no, const char *s)
{
	struct string *str = cstr_to_string(s);
	PE_SetText(no, str, 1);
	free_string(str);
}
static void ts_add_text(int no, const char *s)
{
	struct string *str = cstr_to_string(s);
	PE_AddPartsText(no, str, 1);
	free_string(str);
}
/* A laid-out text: the number of lines and the height of each; every glyph's
 * texture, cw x th, and its offset in the line, (0, gy). cw -1: the glyphs
 * are not looked at; cw -2: their widths are not; gy < 0: their offsets are
 * not. */
static void ts_expect_layout_of(struct parts_text *t, unsigned lines, int line_h, int cw, int th, int gy,
		const char *what)
{
	if (!t) {
		TS_CHECK(0, "%s: not a text state", what);
		return;
	}
	TS_CHECK(t->nr_lines == lines, "%s: %u line(s), want %u", what, t->nr_lines, lines);
	for (unsigned i = 0; i < t->nr_lines; i++)
		TS_CHECK(t->lines[i].height == (unsigned)line_h, "%s: line %u is %u high, want %d", what, i,
			 t->lines[i].height, line_h);
	if (cw == -1)
		return;
	int glyphs = 0;
	for (unsigned i = 0; i < t->nr_lines; i++) {
		for (int c = 0; c < t->lines[i].nr_chars; c++, glyphs++) {
			struct parts_text_char *ch = &t->lines[i].chars[c];
			TS_CHECK(cw < 0 || ch->t.w == cw, "%s: glyph %d,%d texture is %d wide, want %d", what, i, c,
				 ch->t.w, cw);
			TS_CHECK(ch->t.h == th, "%s: glyph %d,%d texture is %d high, want %d", what, i, c, ch->t.h, th);
			TS_CHECK(gy < 0 || (ch->off.x == 0 && ch->off.y == gy),
				 "%s: glyph %d,%d is drawn (%d,%d) from its place in the line, want (0,%d)",
				 what, i, c, ch->off.x, ch->off.y, gy);
		}
	}
	TS_CHECK(glyphs, "%s: no glyph", what);
}
static void ts_expect_layout(int no, unsigned lines, int line_h, int cw, int th, int gy, const char *what)
{
	ts_expect_layout_of(ts_text_of(no, 1), lines, line_h, cw, th, gy, what);
}

/* --- TS1 --- */
static void ts_line_height(void)
{
	gk_enable();
	cn_gl_setup();
	/* w, h: the state. cw: a glyph's cell width (0: mixed, checked below);
	 * cell: its height, size + 2e; gy = line height - cell. A half-width cell
	 * is (size + 1) / 2 + 2e wide, a full-width one size + 2e (0x69b9d0). */
	static const struct { const char *name, *text; int size; float bold, edge; int space, w, h, cw, cell, e, gy; } c[] = {
		{ "a", "AB", 14, 0, 0, 0,		14, 14, 7, 14, 0, 0 },
		{ "b", "AB", 19, 0, 0, 0,		20, 20, 10, 19, 0, 1 },	/* odd: 20 */
		{ "c", "AB", 19, 0.3f, 0, 0,		24, 22, 12, 21, 1, 1 },
		{ "d", "AB", 16, 0, 1.5f, 0,		24, 20, 12, 20, 2, 0 },
		{ "e", "AB", 20, 0, 2.2f, 0,		32, 26, 16, 26, 3, 0 },
		{ "f", TS_JINZAI, 14, 0, 2, -4,		32, 18, 18, 18, 2, 0 },	/* 2 * 18 - 4 */
		{ "g", TS_SLASH, 11, 1, 0, -4,		13, 14, 13, 13, 1, 1 },	/* one glyph: no spacing */
		{ "h", "ABC", 25, 0.8f, 0, -3,		39, 28, 15, 27, 1, 1 },	/* 3 * 15 - 2 * 3 */
		{ "i", "A" TS_JIN "1", 19, 0, 2, -3,	45, 24, 0, 23, 2, 1 },	/* 14 + 23 + 14 - 2 * 3 */
	};
	enum { N = sizeof c / sizeof c[0] };
	struct ex_tree *kids;
	struct ex *e = lb_activity("TextSizeLines", N + 1, &kids);
	struct lb_place p = { 10, 20, 1 };
	for (int i = 0; i < N; i++)
		kids->children[i] = lb_text_w(c[i].name, &p, c[i].text, c[i].size, c[i].bold, c[i].edge, c[i].space);
	kids->children[N] = ts_text3("j", &p, ts_text_state(TD_NORMAL, "AB", 14, 0, 0, 0),
			ts_text_state(TD_ON, "ABC", 19, 0.3f, 0, -2), ts_text_state(TD_DOWN, "A", 25, 0, 3, 0));
	int act = ts_load("TextSizeLines", e);
	int no[N];
	for (int i = 0; i < N; i++)
		no[i] = ts_no(act, c[i].name);
	int j = ts_no(act, "j");
	heap_unref(act);
	PE_UpdateComponent(0);
	for (int i = 0; i < N; i++) {
		char what[96];
		snprintf(what, sizeof what, "TS1 %s (size %d 太さ %g 縁取り %g 字間隔 %d)", c[i].name, c[i].size,
			 (double)c[i].bold, (double)c[i].edge, c[i].space);
		ts_expect_size(no[i], c[i].w, c[i].h, what);
		ts_expect_layout(no[i], 1, c[i].h, c[i].cw ? c[i].cw : -2, c[i].cell + c[i].e, c[i].gy, what);
		/* origin 1: the box starts at the position */
		ts_expect_ul(no[i], 10, 20, what);
	}
	/* i: a half-width, a full-width and a half-width cell */
	struct parts_text *mixed = ts_text_of(no[N - 1], 1);
	static const int mixed_w[3] = { 14, 23, 14 };
	TS_CHECK(mixed && mixed->nr_lines == 1 && mixed->lines[0].nr_chars == 3, "TS1 i is not three glyphs on a line");
	if (mixed && mixed->nr_lines == 1 && mixed->lines[0].nr_chars == 3) {
		for (int k = 0; k < 3; k++)
			TS_CHECK(mixed->lines[0].chars[k].t.w == mixed_w[k], "TS1 i glyph %d is %d wide, want %d", k,
				 mixed->lines[0].chars[k].t.w, mixed_w[k]);
	}
	/* j: the on-cursor and key-down states are laid out by the same rules
	 * (3 * 12 - 2 * 2 by 22; a cell of 19x31 in a line of 32). */
	ts_expect_size_of(j, 1, 14, 14, "TS1 j normal state");
	ts_expect_size_of(j, 2, 32, 22, "TS1 j on-cursor state");
	ts_expect_layout_of(ts_text_of(j, 2), 1, 22, 12, 22, 1, "TS1 j on-cursor state");
	ts_expect_size_of(j, 3, 19, 32, "TS1 j key-down state");
	ts_expect_layout_of(ts_text_of(j, 3), 1, 32, 19, 34, 1, "TS1 j key-down state");
	printf("TS1 line height and width: %s\n", ts_failures ? "FAIL" : "ok");
}

/* --- TS2 --- */
static void ts_multi_line(void)
{
	gk_enable();
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("TextSizeMulti", 3, &kids);
	struct lb_place p = { 10, 20, 1 }, p9 = { 300, 200, 9 }, p5 = { 300, 200, 5 };
	/* size 19, 太さ 0.3: cell 12x21, line 22 */
	kids->children[0] = lb_text_w("m", &p, "AB", 19, 0.3f, 0, -2);
	kids->children[1] = lb_text_w("m9", &p9, "AB", 19, 0.3f, 0, -2);
	kids->children[2] = lb_text_w("m5", &p5, "AB", 19, 0.3f, 0, -2);
	int act = ts_load("TextSizeMulti", e);
	int m = ts_no(act, "m"), m9 = ts_no(act, "m9"), m5 = ts_no(act, "m5");
	heap_unref(act);
	PE_SetTextLineSpace(m, 7, 1);
	PE_SetTextLineSpace(m9, 7, 1);
	PE_SetTextLineSpace(m5, 7, 1);
	PE_UpdateComponent(0);
	ts_expect_size(m, 22, 22, "TS2 \"AB\"");
	ts_set_text(m, "AB\nC");
	ts_expect_size(m, 22, 51, "TS2 \"AB\\nC\" (22 + 7 + 22)");
	ts_expect_layout(m, 2, 22, 12, 22, 1, "TS2 \"AB\\nC\"");
	/* Appended in two calls: the same layout. */
	ts_set_text(m, "");
	ts_add_text(m, "AB");
	ts_expect_size(m, 22, 22, "TS2 Parts_AddPartsText \"AB\"");
	ts_add_text(m, "\nC");
	ts_expect_size(m, 22, 51, "TS2 Parts_AddPartsText \"AB\" then \"\\nC\"");
	ts_expect_layout(m, 2, 22, 12, 22, 1, "TS2 Parts_AddPartsText \"AB\" then \"\\nC\"");
	ts_set_text(m, "A\nBCD\nE");
	ts_expect_size(m, 32, 80, "TS2 three lines (3 * 12 - 2 * 2 wide, 3 * 22 + 2 * 7 high)");
	ts_set_text(m, "A\n");
	ts_expect_size(m, 12, 51, "TS2 \"A\\n\" (an empty last line)");
	ts_set_text(m, "\nA");
	ts_expect_size(m, 12, 51, "TS2 \"\\nA\" (an empty first line)");
	ts_expect_layout(m, 2, 22, 12, 22, 1, "TS2 \"\\nA\"");
	ts_set_text(m, "\n");
	ts_expect_size(m, 0, 51, "TS2 \"\\n\" (two empty lines)");
	ts_set_text(m, "\n\n");
	ts_expect_size(m, 0, 80, "TS2 \"\\n\\n\"");
	ts_set_text(m, "ABC");
	ts_expect_size(m, 32, 22, "TS2 \"ABC\"");
	ts_set_text(m, "");
	ts_expect_size(m, 0, 0, "TS2 emptied");
	TS_CHECK(!PE_IsCursorIn(m, 11, 21, 1), "TS2 the emptied text is still under the cursor");
	/* bottom right and centre: the corner follows both sizes */
	ts_set_text(m9, "AB\nCDE\nF");
	ts_set_text(m5, "AB\nCDE\nF");
	PE_UpdateComponent(0);
	ts_expect_size(m9, 32, 80, "TS2 origin 9");
	ts_expect_ul(m9, 300 - 32, 200 - 80, "TS2 origin 9");
	ts_expect_size(m5, 32, 80, "TS2 origin 5");
	ts_expect_ul(m5, 300 - 16, 200 - 40, "TS2 origin 5");
	/* An odd height: -51 / 2 is -25 (0x5bf37f: cdq, sub, sar 1, neg). */
	ts_set_text(m5, "AB\nC");
	PE_UpdateComponent(0);
	ts_expect_size(m5, 22, 51, "TS2 origin 5, two lines");
	ts_expect_ul(m5, 300 - 11, 200 - 25, "TS2 origin 5, two lines");
	/* With a surface area: emptied, the text has no size and stays at its
	 * position however often its hit box is computed again. */
	ts_set_text(m, "ABC");
	PE_SetPartsTextSurfaceArea(m, 0, 0, 10, 10, 1);
	ts_expect_ul(m, 10, 20, "TS2 with a surface area");
	ts_set_text(m, "");
	ts_expect_size(m, 0, 0, "TS2 emptied with a surface area");
	ts_expect_ul(m, 10, 20, "TS2 emptied with a surface area");
	for (int i = 0; i < 3; i++)
		PE_SetPos(m, 10, 20);
	ts_expect_ul(m, 10, 20, "TS2 emptied with a surface area, placed three more times");
	TS_CHECK(!PE_IsCursorIn(m, 10, 20, 1), "TS2 the emptied text with a surface area is under the cursor");
	/* An area that does not start at (0,0): the emptied text is where the
	 * filled one was (0x59b760 writes the area's x and y whatever the size).
	 * Only that is checked, here and for the bottom-right text below: where
	 * a text with a surface area is depends on the origin offset, which
	 * xsystem4 takes from the area's own size and the original from the
	 * extent cut by the area. */
	static const struct { const char *name; int area[4], x, y; } sa[] = {
		{ "m", { 5, 5, 10, 10 }, 10, 20 }, { "m9", { 4, 6, 100, 30 }, 300, 200 } };
	for (int k = 0; k < 2; k++) {
		int no = k ? m9 : m;
		char what[96];
		ts_set_text(no, "ABC");
		PE_SetPartsTextSurfaceArea(no, sa[k].area[0], sa[k].area[1], sa[k].area[2], sa[k].area[3], 1);
		PE_UpdateComponent(0);
		Point filled = ts_ul(no);
		ts_set_text(no, "");
		snprintf(what, sizeof what, "TS2 %s emptied with the surface area (%d,%d,%d,%d)", sa[k].name,
			 sa[k].area[0], sa[k].area[1], sa[k].area[2], sa[k].area[3]);
		ts_expect_size(no, 0, 0, what);
		Point emptied = ts_ul(no);
		TS_CHECK(emptied.x == filled.x && emptied.y == filled.y, "%s: upper left (%d,%d), was (%d,%d) with the text",
			 what, emptied.x, emptied.y, filled.x, filled.y);
		for (int i = 0; i < 3; i++)
			PE_SetPos(no, sa[k].x, sa[k].y);
		Point again = ts_ul(no);
		TS_CHECK(again.x == filled.x && again.y == filled.y,
			 "%s, placed three more times: upper left (%d,%d), was (%d,%d) with the text", what, again.x,
			 again.y, filled.x, filled.y);
	}
	printf("TS2 several lines: %s\n", ts_failures ? "FAIL" : "ok");
}

/* --- TS3 --- */
static void ts_origins(void)
{
	gk_enable();
	cn_gl_setup();
	/* All at (300,100). ulx, uly: the upper-left corner on the screen.
	 * ow, oh: the size the layout before this one gave (an advance, spacing
	 * included, per character; the font size and both edges, cut to an
	 * integer). */
	static const struct { const char *name, *text; int size; float bold, edge; int space, om, w, h, ulx, uly, ow, oh; } c[] = {
		{ "r3", "ABCD", 20, 0, 2.2f, -5, 3,	49, 26, 251, 100, 44, 24 },	/* 4 * 16 - 3 * 5 */
		{ "c5", "ABC", 19, 0.3f, 0, -2, 5,	32, 22, 284, 89, 30, 19 },	/* 3 * 12 - 2 * 2 */
		{ "b7", "A", 19, 0, 0, 0, 7,		10, 20, 300, 80, 10, 19 },
		{ "r9", "123", 54, 0, 0, 1, 9,		83, 54, 217, 46, 84, 54 },	/* 3 * 27 + 2 */
		{ "c2", "AB", 14, 0, 2, -4, 2,		18, 18, 291, 100, 14, 18 },	/* 2 * 11 - 4 */
		{ "m6", "ABC", 25, 0, 1.5f, -4, 6,	43, 30, 257, 85, 39, 28 },	/* 3 * 17 - 2 * 4; 26 + 4 */
		{ "c8", "AB", 11, 0.4f, 0, -3, 8,	13, 14, 294, 86, 10, 11 },	/* 2 * 8 - 3; -13 / 2 = -6 */
	};
	enum { N = sizeof c / sizeof c[0] };
	struct ex_tree *kids;
	struct ex *e = lb_activity("TextSizeOrigins", N, &kids);
	for (int i = 0; i < N; i++) {
		struct lb_place p = { 300, 100, c[i].om };
		kids->children[i] = lb_text_w(c[i].name, &p, c[i].text, c[i].size, c[i].bold, c[i].edge, c[i].space);
	}
	int act = ts_load("TextSizeOrigins", e);
	int no[N];
	for (int i = 0; i < N; i++)
		no[i] = ts_no(act, c[i].name);
	heap_unref(act);
	PE_UpdateComponent(0);
	char what[N][96];
	for (int i = 0; i < N; i++) {
		snprintf(what[i], sizeof what[i], "TS3 %s (origin %d)", c[i].name, c[i].om);
		ts_expect_placed(no[i], c[i].w, c[i].h, c[i].ulx, c[i].uly, what[i]);
	}
	/* The script changes the text, then the 字間隔: right edge stays at 300. */
	ts_set_text(no[0], "AB");
	PE_UpdateComponent(0);
	ts_expect_size(no[0], 27, 26, "TS3 r3 after Parts_SetText \"AB\" (2 * 16 - 5)");
	ts_expect_ul(no[0], 273, 100, "TS3 r3 after Parts_SetText \"AB\"");
	PE_SetTextCharSpace(no[0], 0, 1);
	PE_UpdateComponent(0);
	ts_expect_ul(no[0], 268, 100, "TS3 r3 with 字間隔 0");
	/* One character has no spacing: centred on its cell alone. */
	ts_set_text(no[1], "A");
	PE_UpdateComponent(0);
	ts_expect_size(no[1], 12, 22, "TS3 c5 \"A\"");
	ts_expect_ul(no[1], 294, 89, "TS3 c5 \"A\"");
	/* PE_Save / PE_Load: the texts are laid out again as they are read. */
	PE_SetTextCharSpace(no[0], -5, 1);
	ts_set_text(no[0], c[0].text);
	ts_set_text(no[1], c[1].text);
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	TS_CHECK(PE_Save(&saved), "TS3 PE_Save failed");
	if (!saved) {
		TS_CHECK(0, "TS3 PE_Save gave no buffer");
		return;
	}
	TS_CHECK(PE_Load(&saved), "TS3 PE_Load failed");
	PE_UpdateComponent(0);
	for (int i = 0; i < N; i++) {
		char after[128];
		snprintf(after, sizeof after, "%s after PE_Save / PE_Load", what[i]);
		ts_expect_placed(no[i], c[i].w, c[i].h, c[i].ulx, c[i].uly, after);
	}
	ca_free_buffer(saved);
	/* A save from before this layout holds the sizes, origin offsets and
	 * hit boxes of the one before (made here by setting the fields back;
	 * the rest of the record is the current writer's): read, the texts get
	 * this layout's. */
	for (int i = 0; i < N; i++) {
		struct parts *p = parts_try_get(no[i]);
		if (p) {
			struct parts_common *common = &p->states[0].common;
			common->w = c[i].ow;
			common->h = c[i].oh;
			common->origin_offset = ts_origin_offset(c[i].om, c[i].ow, c[i].oh);
			common->hitbox = (Rectangle) { 300 + common->origin_offset.x, 100 + common->origin_offset.y,
				c[i].ow, c[i].oh };
		}
	}
	saved = NULL;
	TS_CHECK(PE_Save(&saved), "TS3 PE_Save failed");
	if (!saved) {
		TS_CHECK(0, "TS3 PE_Save gave no buffer");
		return;
	}
	TS_CHECK(PE_Load(&saved), "TS3 PE_Load failed");
	PE_UpdateComponent(0);
	for (int i = 0; i < N; i++) {
		char after[128];
		snprintf(after, sizeof after, "%s loaded from a save with the size %dx%d", what[i], c[i].ow, c[i].oh);
		ts_expect_placed(no[i], c[i].w, c[i].h, c[i].ulx, c[i].uly, after);
	}
	ca_free_buffer(saved);
	printf("TS3 origins and hit boxes: %s\n", ts_failures ? "FAIL" : "ok");
}

/* --- TS4 --- */
static void ts_numerals(void)
{
	gk_enable();
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("TextSizeNumerals", 6, &kids);
	struct lb_place p1 = { 10, 20, 1 }, p2 = { 200, 100, 9 }, p3 = { 50, 100, 7 }, p4 = { 239, 47, 9 },
		p5 = { 400, 300, 4 }, p6 = { 500, 300, 4 };
	kids->children[0] = ts_numeral("n1", &p1, 19, 0, 2, -5);
	kids->children[1] = ts_numeral("n2", &p2, 16, 0, 1.5f, 0);
	kids->children[2] = ts_numeral("n3", &p3, 19, 0.3f, 0, 0);
	kids->children[3] = ts_numeral("n4", &p4, 27, 0, 0, -1);
	kids->children[4] = ts_numeral("n5", &p5, 16, 0, 1.5f, 0);
	kids->children[5] = ts_numeral("n6", &p6, 19, 0.3f, 0, 0);
	int act = ts_load("TextSizeNumerals", e);
	int n1 = ts_no(act, "n1"), n2 = ts_no(act, "n2"), n3 = ts_no(act, "n3"), n4 = ts_no(act, "n4"),
		n5 = ts_no(act, "n5"), n6 = ts_no(act, "n6");
	heap_unref(act);
	BU_PE("Parts_SetNumeralNumber", BU_I(n1), BU_I(3), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(n2), BU_I(12), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(n3), BU_I(5), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(n4), BU_I(100), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(n5), BU_I(12), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(n6), BU_I(5), BU_I(1));
	PE_UpdateComponent(0);
	/* 0x5b5133: the height is a glyph texture's, size + 2e; odd stays odd. */
	ts_expect_size(n1, 14, 23, "TS4 n1 (size 19 縁取り 2)");
	ts_expect_size(n2, 24, 20, "TS4 n2 (size 16 縁取り 1.5: 16 + 2 * 2)");
	ts_expect_ul(n2, 176, 80, "TS4 n2 (origin 9)");
	ts_expect_size(n3, 12, 21, "TS4 n3 (size 19 太さ 0.3: 19 + 2 * 1)");
	ts_expect_ul(n3, 50, 79, "TS4 n3 (origin 7)");
	ts_expect_size(n4, 40, 27, "TS4 n4 (size 27 字間隔 -1: 3 * 14 - 2)");
	ts_expect_ul(n4, 199, 20, "TS4 n4 (origin 9)");
	ts_expect_size(n5, 24, 20, "TS4 n5 (as n2)");
	ts_expect_ul(n5, 400, 290, "TS4 n5 (origin 4: -20 / 2)");
	ts_expect_size(n6, 12, 21, "TS4 n6 (as n3)");
	ts_expect_ul(n6, 500, 290, "TS4 n6 (origin 4, an odd height: -21 / 2 is -10)");
	printf("TS4 font numerals: %s\n", ts_failures ? "FAIL" : "ok");
}

/* --- TS5 --- */
static void ts_boxes(void)
{
	gk_enable();
	cn_gl_setup();
	struct ex_tree *kids;
	struct ex *e = lb_activity("TextSizeBoxes", 2, &kids);
	/* SceneWorkMatching's CustomerCount/CountBase: horizontal, 配置 7,
	 * origin 5, at the centre of its 128x40 base. */
	struct lb_place pb = { 64, 20, 5 };
	kids->children[0] = lb_box("Count", &pb, 2, 7, 4);
	struct ex_tree *k = lb_kids(&kids->children[0]);
	struct lb_place c1 = { -47, 12, 7, { 0, 2, 0, -2 } }, c2 = { 7, 14, 7 },
		c3 = { 19, 12, 7, { 0, 2, -5, -6 } }, c4 = { 31, 14, 7 };
	k->children[0] = lb_text("Caption", &c1, TS_JINZAI, 14, 2, -4);
	k->children[1] = ts_numeral("Remain", &c2, 19, 0, 2, -5);
	k->children[2] = lb_text("Slash", &c3, TS_SLASH, 16, 1.5f, -4);
	k->children[3] = ts_numeral("Max", &c4, 19, 0, 2, -5);
	/* RivalShopView's name over its 容貌 row: vertical, 配置 3, origin 6. */
	struct lb_place pv = { 500, 300, 6 };
	kids->children[1] = lb_box("Info", &pv, 1, 3, 2);
	k = lb_kids(&kids->children[1]);
	struct lb_place v1 = { 1, -38, 3, { -6, 0, 0, -1 } }, v2 = { 0, 0, 1 };
	k->children[0] = lb_text("Name", &v1, "ABCD", 20, 2.2f, -5);
	k->children[1] = lb_rect("Row", &v2, 30, 20, 0, 0);
	int act = ts_load("TextSizeBoxes", e);
	int count = ts_no(act, "Count"), cap = ts_no(act, "Caption"), rem = ts_no(act, "Remain"),
		sl = ts_no(act, "Slash"), mx = ts_no(act, "Max"), info = ts_no(act, "Info"),
		name = ts_no(act, "Name"), row = ts_no(act, "Row");
	heap_unref(act);
	BU_PE("Parts_SetNumeralNumber", BU_I(rem), BU_I(1), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(mx), BU_I(4), BU_I(1));
	PE_UpdateComponent(0);
	/* 人材 32x18, digits 14x23, ／ 20x20. W = (32 - 2) + 14 + (20 - 11) + 14
	 * = 67, H = 23 (the digits; 18 + 2 and 20 + 2 are less). The box starts
	 * at -33 and ends at -11 + 23 = 12; bottoms less the bottom margin. */
	ts_expect_size(count, 67, 23, "TS5 CountBase");
	ts_expect_pos(cap, -33, 10, 7, "TS5 Caption");
	ts_expect_pos(rem, -3, 12, 7, "TS5 Remain");
	ts_expect_pos(sl, 6, 10, 7, "TS5 Slash");
	ts_expect_pos(mx, 20, 12, 7, "TS5 Max");
	ts_expect_size(cap, 32, 18, "TS5 Caption");
	ts_expect_size(sl, 20, 20, "TS5 Slash");
	ts_expect_size(rem, 14, 23, "TS5 Remain");
	ts_expect_ul(cap, 31, 12, "TS5 Caption");
	ts_expect_ul(rem, 61, 9, "TS5 Remain");
	ts_expect_ul(sl, 70, 10, "TS5 Slash");
	ts_expect_ul(mx, 84, 9, "TS5 Max");
	/* Name 49x26 with margins top -6, right -1; Row 30x20. W = 48, H = 20 +
	 * 20 = 40: the box spans -20..20, its right edge at 0. */
	ts_expect_size(info, 48, 40, "TS5 Info");
	ts_expect_pos(name, 1, -26, 3, "TS5 Name");
	ts_expect_pos(row, 0, 0, 3, "TS5 Row");
	ts_expect_ul(name, 500 + 1 - 49, 300 - 26, "TS5 Name");
	ts_expect_ul(row, 500 - 30, 300, "TS5 Row");
	printf("TS5 texts and numerals in boxes: %s\n", ts_failures ? "FAIL" : "ok");
}

/* The repo's fonts instead of the glyph-less one. */
static void ts_real_fonts(void)
{
	const char *src = getenv("XS4_SRC");
	static char gothic[4096], mincho[4096];
	snprintf(gothic, sizeof gothic, "%s/fonts/VL-Gothic-Regular.ttf", src ? src : ".");
	snprintf(mincho, sizeof mincho, "%s/fonts/HanaMinA.ttf", src ? src : ".");
	config.font_paths[FONT_GOTHIC] = gothic;
	config.font_paths[FONT_MINCHO] = mincho;
	config.fnl_path = NULL;
	gfx_font_init();
}

/* What a glyph's texture holds, drawn in white with a black edge: the first
 * row and column and the last row of the body (more white than black), and
 * the last row that is at least half opaque (body or edge). */
struct ts_ink { int top, left, bottom, opaque_bottom; };
static bool ts_ink(Texture *t, struct ts_ink *ink)
{
	*ink = (struct ts_ink) { -1, -1, -1, -1 };
	if (!t->handle || t->w <= 0 || t->h <= 0)
		return false;
	uint8_t *px = gfx_get_pixels(t);
	int min_x = t->w;
	for (int y = 0; y < t->h; y++) {
		for (int x = 0; x < t->w; x++) {
			const uint8_t *p = px + (y * t->w + x) * 4;
			if (p[3] >= 128)
				ink->opaque_bottom = y;
			if (p[0] >= 128 && p[3] >= 128) {
				if (x < min_x) min_x = x;
				if (ink->top < 0) ink->top = y;
				ink->bottom = y;
			}
		}
	}
	free(px);
	if (ink->top < 0)
		return false;
	ink->left = min_x;
	return true;
}

/* --- TS6 --- */
static void ts_glyph_position(void)
{
	gk_enable();
	cn_gl_setup();
	ts_real_fonts();
	struct ex_tree *kids;
	struct ex *e = lb_activity("TextSizeGlyph", 7, &kids);
	struct lb_place p = { 10, 20, 1 };
	kids->children[0] = lb_text_w("g0", &p, "H", 20, 0, 0, 0);
	kids->children[1] = lb_text_w("g3", &p, "H", 20, 0, 3, 0);
	kids->children[2] = lb_text_w("g2", &p, "H", 20, 0, 1.5f, 0);
	kids->children[3] = lb_text_w("g1", &p, "H", 20, 0.3f, 0, 0);
	kids->children[4] = ts_numeral("d0", &p, 19, 0, 0, 0);
	kids->children[5] = ts_numeral("d2", &p, 19, 0, 2, 0);
	kids->children[6] = lb_text_w("gy", &p, "y", 20, 0, 2, 0);
	int act = ts_load("TextSizeGlyph", e);
	int g[4] = { ts_no(act, "g0"), ts_no(act, "g3"), ts_no(act, "g2"), ts_no(act, "g1") };
	int d0 = ts_no(act, "d0"), d2 = ts_no(act, "d2"), gy = ts_no(act, "gy");
	heap_unref(act);
	BU_PE("Parts_SetNumeralNumber", BU_I(d0), BU_I(7), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(d2), BU_I(7), BU_I(1));
	PE_UpdateComponent(0);
	static const int edge[4] = { 0, 3, 2, 1 };
	struct ink { int top, left; } at[4];
	for (int i = 0; i < 4; i++) {
		struct parts_text *t = ts_text_of(g[i], 1);
		struct ts_ink ink;
		if (!t || t->nr_lines != 1 || t->lines[0].nr_chars != 1 || !ts_ink(&t->lines[0].chars[0].t, &ink))
			cn_env_fail("the real font drew no glyph body");
		at[i].top = ink.top;
		at[i].left = ink.left;
	}
	for (int i = 1; i < 4; i++) {
		/* 0x69c290 / 0x69c3d0: the glyph is at (e, e) of its texture. */
		TS_CHECK(at[i].top - at[0].top == edge[i],
			 "TS6 \"H\" with e %d: body %d px lower than without (top %d, %d), want %d",
			 edge[i], at[i].top - at[0].top, at[i].top, at[0].top, edge[i]);
		TS_CHECK(at[i].left - at[0].left == edge[i],
			 "TS6 \"H\" with e %d: body %d px further right (left %d, %d), want %d",
			 edge[i], at[i].left - at[0].left, at[i].left, at[0].left, edge[i]);
	}
	struct ts_ink i0, i2;
	struct parts *p0 = parts_try_get(d0), *p2 = parts_try_get(d2);
	if (!p0 || !p2 || !ts_ink(&p0->states[0].common.texture, &i0) || !ts_ink(&p2->states[0].common.texture, &i2))
		cn_env_fail("the real font drew no digit body");
	TS_CHECK(i2.top - i0.top == 2 && i2.left - i0.left == 2,
		 "TS6 digit with 縁取り 2: body (%d,%d) px from where it is without, want (2,2)",
		 i2.left - i0.left, i2.top - i0.top);
	/* The edge of a descender: 縁取り 2 puts 2 rows of edge under the body's
	 * last row, as before the glyph moved down in its texture. */
	struct parts_text *ty = ts_text_of(gy, 1);
	struct ts_ink iy;
	if (!ty || ty->nr_lines != 1 || ty->lines[0].nr_chars != 1 || !ts_ink(&ty->lines[0].chars[0].t, &iy))
		cn_env_fail("the real font drew no \"y\"");
	printf("  (TS6 \"y\" 20/0/2: body rows %d-%d, opaque to row %d, texture %d high)\n", iy.top, iy.bottom,
	       iy.opaque_bottom, ty->lines[0].chars[0].t.h);
	TS_CHECK(iy.opaque_bottom - iy.bottom == 2, "TS6 \"y\" with 縁取り 2: %d row(s) of edge under the body, want 2",
		 iy.opaque_bottom - iy.bottom);
	printf("TS6 glyph inside its texture: %s\n", ts_failures ? "FAIL" : "ok");
}

/* --- TS7 --- */
static void ts_unchanged(void)
{
	gk_enable();
	cn_gl_setup();
	/* v13 on the same grid: an advance (cell 12, 字間隔 -4) per character,
	 * lines as high as the font size. */
	ain->version = 13;
	const int T = 9811;
	parts_get(T);
	PE_SetFont(T, 0, 19, 255, 255, 255, 0.3f, 0, 0, 0, 0, 1);
	PE_SetTextCharSpace(T, -4, 1);
	PE_SetTextLineSpace(T, 7, 1);
	ts_set_text(T, "ABC\nD");
	ts_expect_size(T, 24, 45, "TS7 v13 \"ABC\\nD\" (3 * 8 wide, 19 + 7 + 19 high)");
	ts_expect_layout(T, 2, 19, 12, 19, 0, "TS7 v13");
	PE_SetPartsOriginPosMode(T, 9);
	PE_SetPos(T, 300, 100);
	ts_expect_ul(T, 276, 55, "TS7 v13 origin 9");
	ain->version = 14;
	/* The message window lays its text out with the same function. Its
	 * widget is another one in the original (type 10), not traced, so its
	 * text stays as it was: lines of 25 + 1.5 + 1.5, an advance of 17 - 4
	 * per character. These numbers are what xsystem4 does now, not the
	 * original's (whose lines are 36 apart on the screen for this font, not
	 * 35): when that widget is done by the original, they change with it. */
	const int M = 9812;
	PE_SetMessageWindowTextFont(M, 0, 25, 255, 255, 255, 0, 0, 0, 0, 1.5f);
	PE_SetMessageWindowTextSpace(M, -4, 10);
	struct string *s = cstr_to_string("AB\nC");
	PE_SetMessageWindowText(M, s, 0, NULL, 0, 0);
	free_string(s);
	Point pos;
	struct parts_text *mt = parts_message_window_render_text(parts_get(M), &pos);
	TS_CHECK(mt && mt->nr_lines == 2 && mt->lines[0].nr_chars == 2, "TS7 message window text was not laid out");
	if (mt && mt->nr_lines == 2 && mt->lines[0].nr_chars == 2) {
		ts_expect_layout_of(mt, 2, 28, 17, 28, -1, "TS7 message window");
		TS_CHECK(mt->lines[0].width == 26, "TS7 message window: line %g wide, want 26", (double)mt->lines[0].width);
	}
	printf("TS7 v13 and the message window unchanged: %s\n", ts_failures ? "FAIL" : "ok");
}

/* A text and two font numerals that are not on the grid: the text has lines
 * of 19 + 2 + 2 but no width (the upstream layout skips a character whose
 * glyph does not load), the numerals are ceil(size + 2 * 縁取り) high. */
static void ts_off_grid(const char *act_name, const char *tag)
{
	struct ex_tree *kids;
	struct ex *e = lb_activity(act_name, 3, &kids);
	struct lb_place p = { 300, 100, 9 }, pn = { 10, 20, 1 };
	kids->children[0] = lb_text_w("s", &p, "ABC", 19, 0.3f, 2, -4);
	kids->children[1] = ts_numeral("n1", &pn, 16, 0, 1.5f, 0);
	kids->children[2] = ts_numeral("n2", &pn, 19, 0.3f, 0, 0);
	int act = ts_load(act_name, e);
	int s = ts_no(act, "s"), n1 = ts_no(act, "n1"), n2 = ts_no(act, "n2");
	heap_unref(act);
	BU_PE("Parts_SetNumeralNumber", BU_I(n1), BU_I(12), BU_I(1));
	BU_PE("Parts_SetNumeralNumber", BU_I(n2), BU_I(5), BU_I(1));
	PE_UpdateComponent(0);
	char what[96];
	snprintf(what, sizeof what, "%s text", tag);
	ts_expect_size(s, 0, 0, what);
	ts_expect_layout(s, 1, 23, -1, 0, -1, what);
	int w1 = PE_GetPartsWidth(n1, 1), h1 = PE_GetPartsHeight(n1, 1), w2 = PE_GetPartsWidth(n2, 1),
		h2 = PE_GetPartsHeight(n2, 1);
	TS_CHECK(w1 > 0 && h1 == 19, "%s numeral 16/0/1.5: %dx%d, want 19 high (16 + 1.5 + 1.5, rounded up)", tag, w1, h1);
	TS_CHECK(w2 > 0 && h2 == 19, "%s numeral 19/0.3/0: %dx%d, want 19 high", tag, w2, h2);
}

/* --- TS8 --- */
static void ts_sjis_rule(void)
{
	gk_force_sjis();
	cn_gl_setup();
	ts_off_grid("TextSizeSjis", "TS8 v14 under the SJIS rule:");
	printf("TS8 v14 under the SJIS rule unchanged: %s\n", ts_failures ? "FAIL" : "ok");
}

/* --- TS9 --- */
static void ts_sjis_real_font(void)
{
	gk_force_sjis();
	cn_gl_setup();
	ts_real_fonts();
	struct ex_tree *kids;
	struct ex *e = lb_activity("TextSizeSjisFont", 2, &kids);
	struct lb_place p = { 300, 100, 3 };
	kids->children[0] = lb_text_w("s1", &p, "A", 20, 0, 2, -4);
	kids->children[1] = lb_text_w("s3", &p, "AAA", 20, 0, 2, -4);
	int act = ts_load("TextSizeSjisFont", e);
	int s1 = ts_no(act, "s1"), s3 = ts_no(act, "s3");
	heap_unref(act);
	PE_UpdateComponent(0);
	int w1 = PE_GetPartsWidth(s1, 1), w3 = PE_GetPartsWidth(s3, 1);
	if (w1 <= 0)
		cn_env_fail("the real font gave \"A\" no width");
	/* An advance, spacing included, after every character. */
	TS_CHECK(w3 == 3 * w1, "TS9 \"AAA\" is %d wide and \"A\" %d, want three times as wide", w3, w1);
	TS_CHECK(PE_GetPartsHeight(s1, 1) == 24 && PE_GetPartsHeight(s3, 1) == 24,
		 "TS9 heights %d and %d, want 20 + 2 + 2", PE_GetPartsHeight(s1, 1), PE_GetPartsHeight(s3, 1));
	ts_expect_ul(s3, 300 - w3, 100, "TS9 \"AAA\" origin 3");
	printf("TS9 v14 under the SJIS rule, real font, unchanged: %s\n", ts_failures ? "FAIL" : "ok");
}

/* Drawing a parts offscreen. The view is the target's size, so a pixel of
 * the target is a pixel of the screen. */
#define TS_VIEW_W 96
#define TS_VIEW_H 128

/* One parts alone, drawn into a view-sized target: its pixels, RGBA from the
 * top row. */
static uint8_t *ts_draw(struct parts *p)
{
	Texture dst = { 0 };
	gfx_init_texture_rgba(&dst, TS_VIEW_W, TS_VIEW_H, (SDL_Color){ 0, 0, 0, 0 });
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, &dst, 0, 0, dst.w, dst.h);
	parts_render(p);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	uint8_t *px = gfx_get_pixels(&dst);
	gfx_delete_texture(&dst);
	return px;
}

/* cn_gl_setup, the parts shader, and one parts that is no text (an 8x8
 * texture) drawn with it. All of that comes before the setup is reported:
 * the engine ends the process when the shader does not build, and that, or
 * the draw coming back empty, is the environment's failure, not a case's. */
static void ts_render_setup(void)
{
	int fd = cn_ready_fd;
	cn_ready_fd = -1;
	cn_gl_setup();
	parts_render_init();
	config.view_width = TS_VIEW_W;
	config.view_height = TS_VIEW_H;
	const int C = 9899;
	struct parts *check = parts_get(C);
	struct parts_cg *cg = parts_get_cg(check, 0);
	gfx_init_texture_rgba(&cg->common.texture, 8, 8, (SDL_Color){ 255, 255, 255, 255 });
	parts_set_dims(check, &cg->common, 8, 8);
	PE_SetPos(C, 4, 4);
	PE_UpdateComponent(0);
	uint8_t *px = ts_draw(check);
	int any = 0;
	for (int i = 0; i < TS_VIEW_W * TS_VIEW_H; i++)
		any += px[i * 4 + 3] != 0;
	free(px);
	if (!any)
		cn_env_fail("parts_render drew nothing of a parts with a filled texture");
	PE_SetShow(C, false);
	cn_ready_fd = fd;
	if (cn_ready_fd >= 0 && write(cn_ready_fd, "r", 1) != 1)
		cn_env_fail("cannot report the setup to the parent");
}
/* The glyph textures of the probe's font are empty: filled, they show where
 * each one is drawn. The first row is opaque and the others half, so that
 * what comes back also shows which row of the texture went where. */
static void ts_fill_glyphs(struct parts_text *t)
{
	for (unsigned i = 0; t && i < t->nr_lines; i++) {
		for (int c = 0; c < t->lines[i].nr_chars; c++) {
			Texture *g = &t->lines[i].chars[c].t;
			if (!g->handle)
				continue;
			gfx_fill_with_alpha(g, 0, 0, g->w, g->h, 255, 255, 255, 128);
			gfx_fill_with_alpha(g, 0, 0, g->w, 1, 255, 255, 255, 255);
		}
	}
}
/* Draws one parts alone and checks the columns and rows that have alpha
 * within the rows y_from..y_to: x0..x1 and y0..y1, inclusive. The most
 * opaque rows there are the textures' first rows: they are the top row, or
 * the top `scale` rows of a parts scaled up. */
static void ts_expect_drawn(struct parts *p, int y_from, int y_to, int x0, int x1, int y0, int y1, int scale,
		const char *what)
{
	uint8_t *px = ts_draw(p);
	int gx0 = TS_VIEW_W, gx1 = -1, gy0 = TS_VIEW_H, gy1 = -1, amax = 0, top0 = -1, top1 = -1;
	for (int y = y_from; y <= y_to; y++) {
		for (int x = 0; x < TS_VIEW_W; x++) {
			int a = px[(y * TS_VIEW_W + x) * 4 + 3];
			if (!a)
				continue;
			if (x < gx0) gx0 = x;
			if (x > gx1) gx1 = x;
			if (y < gy0) gy0 = y;
			if (y > gy1) gy1 = y;
			if (a > amax) {
				amax = a;
				top0 = y;
			}
			if (a == amax)
				top1 = y;
		}
	}
	free(px);
	if (gx1 < 0) {
		TS_CHECK(0, "%s: nothing was drawn in rows %d-%d", what, y_from, y_to);
		return;
	}
	TS_CHECK(gx0 == x0 && gx1 == x1 && gy0 == y0 && gy1 == y1,
		 "%s: drawn in columns %d-%d, rows %d-%d; want columns %d-%d, rows %d-%d", what, gx0, gx1, gy0, gy1,
		 x0, x1, y0, y1);
	TS_CHECK(top0 == y0 && top1 <= y0 + scale - 1,
		 "%s: the textures' first row is drawn in rows %d-%d, want row %d (%d row(s))", what, top0, top1, y0,
		 scale);
}

/* --- TS10 --- */
static void ts_drawn_grid(void)
{
	gk_enable();
	ts_render_setup();
	enum { N = 7 };
	struct ex_tree *kids;
	struct ex *e = lb_activity("TextSizeDrawn", N, &kids);
	struct lb_place p = { 10, 20, 1 }, p9 = { 60, 100, 9 };
	kids->children[0] = lb_text_w("o1", &p, "A", 19, 0, 0, 0);
	kids->children[1] = lb_text_w("o2", &p, "A", 14, 0, 0, 0);
	kids->children[2] = lb_text_w("o3", &p, "AB", 19, 0.3f, 0, -2);
	kids->children[3] = lb_text_w("o4", &p, "A", 19, 0, 0, 0);
	kids->children[4] = lb_text_w("o5", &p, "A", 25, 0, 3, 0);
	kids->children[5] = lb_text_w("o6", &p9, "AB", 19, 0.3f, 0, -2);
	kids->children[6] = lb_text_w("o7", &p, "A", 25, 0, 3, 0);
	int act = ts_load("TextSizeDrawn", e);
	static const char *names[N] = { "o1", "o2", "o3", "o4", "o5", "o6", "o7" };
	int no[N];
	for (int i = 0; i < N; i++)
		no[i] = ts_no(act, names[i]);
	heap_unref(act);
	PE_SetTextLineSpace(no[3], 7, 1);
	ts_set_text(no[3], "A\nB");
	PE_SetTextLineSpace(no[6], 7, 1);
	ts_set_text(no[6], "A\nB");
	PE_UpdateComponent(0);
	struct parts *o[N];
	for (int i = 0; i < N; i++) {
		o[i] = parts_try_get(no[i]);
		if (!o[i]) {
			TS_CHECK(0, "TS10 %s is no parts", names[i]);
			return;
		}
		ts_fill_glyphs(ts_text_of(no[i], 1));
	}
	const int all = TS_VIEW_H - 1;
	/* 0x5bcf60: a glyph's top is the line's bottom less the cell's height;
	 * 0x5bd0c6: the next glyph is a cell and the 字間隔 further. The texture
	 * is e rows higher than the cell, so those rows show below it.
	 * o1 19/0/0: cell 10x19 in a line of 20, at 20 + 1. */
	ts_expect_drawn(o[0], 0, all, 10, 19, 21, 39, 1, "TS10 \"A\" 19/0/0");
	/* o2 14/0/0: cell 7x14 in a line of 14. */
	ts_expect_drawn(o[1], 0, all, 10, 16, 20, 33, 1, "TS10 \"A\" 14/0/0");
	/* o3 19/太さ 0.3/字間隔 -2: cells 12x21 in a line of 22, the second at
	 * 10 + 12 - 2; rows 21..41 and the texture's one more. */
	ts_expect_drawn(o[2], 0, all, 10, 31, 21, 42, 1, "TS10 \"AB\" 19/0.3/0/-2");
	/* o4 two lines, 行間隔 7: the second line starts at 20 + 20 + 7. */
	ts_expect_drawn(o[3], 0, 43, 10, 19, 21, 39, 1, "TS10 \"A\\nB\" first line");
	ts_expect_drawn(o[3], 44, all, 10, 19, 48, 66, 1, "TS10 \"A\\nB\" second line");
	/* o5 25/0/縁取り 3: cell 19x31 in a line of 32; rows 21..51 and 3 more. */
	ts_expect_drawn(o[4], 0, all, 10, 28, 21, 54, 1, "TS10 \"A\" 25/0/3");
	/* o6 origin 9 at (60,100): the extent 22x22 starts at (38,78). */
	ts_expect_drawn(o[5], 0, all, 38, 59, 79, 100, 1, "TS10 \"AB\" 19/0.3/0/-2 origin 9");
	/* o7 25/0/縁取り 3 on two lines, 行間隔 7: the texture, 34 rows, is
	 * higher than the line, 32, and the second line is still a line and the
	 * 行間隔 lower (0x5bf60b adds the line's height, not a texture's): it
	 * starts at 20 + 32 + 7, its cell 1 lower, rows 60..90 and 3 more. */
	ts_expect_drawn(o[6], 0, 56, 10, 28, 21, 54, 1, "TS10 \"A\\nB\" 25/0/3 first line");
	ts_expect_drawn(o[6], 57, all, 10, 28, 60, 93, 1, "TS10 \"A\\nB\" 25/0/3 second line");
	/* o1 scaled by 2 around its position: the cell's offset in the line, 1,
	 * and the cell, 10x19, are doubled. This is what xsystem4 does (the
	 * glyphs are placed in the parts' frame); how the original transforms
	 * the glyphs of a scaled parts was not read. */
	PE_SetPartsMagX(no[0], 2);
	PE_SetPartsMagY(no[0], 2);
	PE_UpdateComponent(0);
	ts_expect_drawn(o[0], 0, all, 10, 29, 22, 59, 2, "TS10 \"A\" 19/0/0 scaled by 2");
	printf("TS10 glyphs where they are drawn: %s\n", ts_failures ? "FAIL" : "ok");
}

/* --- TS11 --- */
static void ts_drawn_unchanged(void)
{
	gk_enable();
	ts_render_setup();
	/* v13 with an edge: cells 14x23 (19 + 2 + 2 high) from the top of the
	 * line, an advance of 14 - 4. */
	ain->version = 13;
	const int T = 9821;
	parts_get(T);
	PE_SetFont(T, 0, 19, 255, 255, 255, 0.3f, 0, 0, 0, 2, 1);
	PE_SetTextCharSpace(T, -4, 1);
	ts_set_text(T, "AB");
	PE_SetPos(T, 10, 20);
	PE_UpdateComponent(0);
	ts_fill_glyphs(ts_text_of(T, 1));
	ts_expect_drawn(parts_get(T), 0, TS_VIEW_H - 1, 10, 33, 20, 42, 1, "TS11 v13 \"AB\" 19/0.3/2/-4");
	ain->version = 14;
	/* The message window's text (TS7): cells 17x28, an advance of 13, the
	 * second line 28 + 10 lower. What xsystem4 does now. */
	const int M = 9822;
	PE_SetMessageWindowTextFont(M, 0, 25, 255, 255, 255, 0, 0, 0, 0, 1.5f);
	PE_SetMessageWindowTextSpace(M, -4, 10);
	struct string *s = cstr_to_string("AB\nC");
	PE_SetMessageWindowText(M, s, 0, NULL, 0, 0);
	free_string(s);
	PE_SetPos(M, 10, 20);
	PE_UpdateComponent(0);
	Point pos;
	ts_fill_glyphs(parts_message_window_render_text(parts_get(M), &pos));
	ts_expect_drawn(parts_get(M), 0, 52, 10, 39, 20, 47, 1, "TS11 message window first line");
	ts_expect_drawn(parts_get(M), 53, TS_VIEW_H - 1, 10, 26, 58, 85, 1, "TS11 message window second line");
	printf("TS11 v13 and the message window drawn where they were: %s\n", ts_failures ? "FAIL" : "ok");
}

/* --- TS12 --- */
static void ts_fnl(void)
{
	gk_enable();
	cn_gl_setup();
	/* An .fnl font library: not the grid (no file is needed, nothing here
	 * loads a font). */
	config.fnl_path = "probe-without-fnl.fnl";
	ts_off_grid("TextSizeFnl", "TS12 the GBK rule with an .fnl:");
	config.fnl_path = NULL;
	printf("TS12 the GBK rule with an .fnl unchanged: %s\n", ts_failures ? "FAIL" : "ok");
}

static int test_text_size(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "TS1", ts_line_height }, { "TS2", ts_multi_line }, { "TS3", ts_origins },
		{ "TS4", ts_numerals }, { "TS5", ts_boxes }, { "TS6", ts_glyph_position },
		{ "TS7", ts_unchanged }, { "TS8", ts_sjis_rule }, { "TS9", ts_sjis_real_font },
		{ "TS10", ts_drawn_grid }, { "TS11", ts_drawn_unchanged }, { "TS12", ts_fnl },
	};
	const int n = sizeof cases / sizeof cases[0];
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		int r = cn_fork_case(cases[i].name, cases[i].fn, &ts_failures);
		failed += r == 1;
		env += r == 2;
	}
	if (env) {
		printf("TEXT-SIZE ENVIRONMENT FAILURE: %d case(s) could not run, %d of %d cases failed\n", env, failed, n);
		return 2;
	}
	printf("TEXT-SIZE %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}

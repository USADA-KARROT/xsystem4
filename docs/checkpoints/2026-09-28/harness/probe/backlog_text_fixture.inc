/* backlog-text: what the backlog (劇情回顧, Scene/10_Adv/System/BackLog) needs
 * to show its lines, as the original engine does it (static disassembly
 * only):
 *   - the text construction commands 7, 8, 23 and 24 (0x4fc7e0, 0x4fccb0,
 *     0x4fd250, 0x4fd720) run one loop: the text is cut into characters, a
 *     "\n" moves the pen down by the font's line (the size made even plus
 *     2e, 0x69f540) and the line spacing and back to X, any other character
 *     is drawn into a cell of its own, the cell is cut to what is left of
 *     the surface and put at the pen, and the pen moves on by the cell and
 *     the character spacing. They differ in how the cell is put on the
 *     surface: 7 blends the colours and leaves the alpha (0x494320:
 *     c += ((s - c) * sa) >> 8), 8 copies the cell (0x4941b0, colours only
 *     on a surface without alpha), 23 blends as 7 and raises the alpha to
 *     the cell's (0x494420), 24 copies the alpha (0x494270);
 *   - Array.EmplaceBack (case 11 -> 0x6476e0) grows the array by one
 *     default-initialized element (Realloc 0x67f4d0 -> 0x680170 -> 0x656970),
 *     which for a struct is a constructed object (0x656a12 -> 0x679b30);
 *   - the 豎滾動條 keeps 全體スクロール量, 表示量, スクロール位置 and スクロー
 *     ルレート (+0x110..+0x11c; loader 0x544802..0x544937): a position is
 *     kept within 0 and total - view (at least 0) and the rate is the
 *     position over that, 1 when nothing can be scrolled (0x542360); a rate
 *     gives the position, truncated (0x542400); a new total or view places
 *     the position again (0x591a10, 0x591a50); on an update a bar whose
 *     position or rate changed sends message 21 with the position and the
 *     largest position (0x5738e0 at 0x573a95, 0x4df5b0);
 *   - GetPartsTextFontProperty, Parts_GetTextCharSpace and
 *     Parts_GetTextLineSpace read a text state (0x598850 -> 0x566240,
 *     0x598ae0 -> 0x5667b0, 0x598b10 -> 0x566800);
 *   - AddChild (case 171 -> 0x58f310) puts the child at the end of the
 *     parts' children; a box a script makes (SetComponentType 8) lays them
 *     out;
 *   - AnteaterADVLogList.AddVoice keeps a voice's name and filter name with
 *     the page being written (case 4, 0x446e59), GetNumofADVLogVoice,
 *     GetADVLogVoice and GetADVLogVoiceFilter give them back; a page or a
 *     page's last line that has no text is not counted (GetNumofADVLog
 *     0x447180, GetNumofADVLogText 0x4476f0), and AddNewPage does not turn a
 *     page without text (0x447860).
 * Before the fix command 23 was drawn as 7 (the text could not be seen on a
 * base of alpha 0), 24 wrote the colours, none of the four broke a line,
 * EmplaceBack gave an object no constructor had run on, the scroll bar's
 * functions were stubs returning 0 (CBackLogView@SetLineIndex then built no
 * line), Parts_GetTextLineSpace and AddChild were missing, a script's box
 * did not lay out, and AddVoice kept nothing (no line of the backlog had a
 * voice).
 *
 * The glyphs come from a font of the probe's own with a known pattern, so
 * that the pixels can be computed; the cells of the model are drawn with the
 * engine's own text renderer, one character at a time, and the model lays
 * them out and composes them. On the CN glyph grid (the GBK rule) the cells
 * are the ones a text state draws; without it 23 and 24 put the one line
 * command 8 would draw there.
 *
 * Cases (each in its own fork):
 *   BT1 command 23 on the grid through the real factory
 *       CASConstructionProcess::AlphaBlendText: characters, a line break, a
 *       negative character spacing (two cells overlap), a line spacing;
 *       every pixel against the model, three by hand, an odd font size's
 *       line, and an edge (the cells wider and the lines higher by 2e).
 *   BT2 commands 7, 8 and 24 on the grid: every pixel against the model; on
 *       a surface without alpha (CreatePixelOnly) the alpha stays; a text
 *       that does not fit, a pen left of, above and far outside the surface.
 *       The pens left of and above it are held to what xsystem4 draws, which
 *       is not the original's: there the cell's left and top stay and its
 *       right and bottom are cut (0x5abed0, see bt_put_rect).
 *   BT3 without the grid: 23 and 24 put the one line on the surface; 7 still
 *       leaves the alpha and 8 still writes it (guards).
 *   BT4 PE_Save / PE_Load rebuild the pixels from the two new operations
 *       (XPE version 9).
 *   BT5 Array.EmplaceBack constructs: a CASFont element has the size and
 *       colour its constructor gives it; the real CBackLogUnitList@AddLog,
 *       @AddSeparate and CBackLogUnit@CreateLog give back the lines added;
 *       a struct without a constructor gets its members' defaults; without
 *       an array page no struct is known; an int element is 0 and v13 keeps
 *       alloc_struct (guards).
 *   BT6 the scroll bar: loaded from a synthetic pactex, the amounts and the
 *       position rules above, the message, the amounts that follow another
 *       parts' height (0x5749d0), and what is not a bar or is not there
 *       (guards; that a parts which is no bar stays what it is and gives 0
 *       is xsystem4's choice: the original's lookup 0x53daf0 makes it one).
 *   BT7 a text state's getters; AddChild and a script's box.
 *   BT8 the pages and voices of the backlog: a page or a last line without
 *       text is not counted and not turned (0x447180, 0x4476f0, 0x447860);
 *       names and filters by page, none while the log is disabled or after
 *       Clear; before v14 the older rules (guard).
 *   BT9 without the grid, a line wider than the widest texture: the pixels
 *       read back are as wide as the texture (xsystem4's own staging).
 *   BT10 a font whose e is negative (no script gives one) draws nothing, and
 *   BT11 so does one whose line height is past an int (guards of xsystem4's
 *       own).
 * Types and functions the fix adds are reached by number, through the AIN
 * declaration or by dlsym, so the file builds before the fix.
 */
#include <dlfcn.h>

static int bt_failures;
#define BT_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); bt_failures++; } } while (0)

/* --- a font whose glyphs are a known pattern --- */
static struct font bt_font;
static struct font_size bt_sizes[8];
static int bt_nr_sizes;

static struct font_size *bt_get_size(struct font *font, float size)
{
	size = roundf(size);
	for (int i = 0; i < bt_nr_sizes; i++)
		if (bt_sizes[i].size == size)
			return &bt_sizes[i];
	assert(bt_nr_sizes < 8);
	struct font_size *fs = &bt_sizes[bt_nr_sizes++];
	*fs = (struct font_size) { .size = size, .y_offset = 0, .font = font };
	return fs;
}
static float bt_actual_size(struct font *font, float size) { (void)font; return roundf(size); }
/* The alpha of a glyph's pixel: 0 on a diagonal lattice, 3..255 elsewhere
 * (1 and 2 are below the glyph shader's discard threshold). */
static int bt_glyph_alpha(uint32_t code, int col, int row)
{
	if ((col + 2 * row + (int)(code % 5)) % 5 == 0)
		return 0;
	return 3 + (col * 53 + row * 101 + (int)(code % 97) * 7) % 253;
}
static int bt_glyph_width(int size, uint32_t code) { return code < 0x80 ? (size + 1) / 2 : size; }
static bool bt_get_glyph(struct font_size *size, struct glyph *dst, uint32_t code, enum font_weight weight)
{
	const int s = size->size, w = bt_glyph_width(s, code);
	uint8_t *bm = xmalloc(w * s);
	for (int row = 0; row < s; row++)
		for (int col = 0; col < w; col++)
			bm[row * w + col] = bt_glyph_alpha(code, col, row);
	gfx_init_texture_rmap(&dst->t[weight], w, s, bm);
	free(bm);
	dst->rect = (Rectangle) { 0, 0, w, s };
	dst->advance = w;
	return true;
}
static float bt_size_char(struct font_size *size, uint32_t code) { return bt_glyph_width(size->size, code); }
static float bt_kerning(struct font_size *size, uint32_t code, uint32_t next) { (void)next; return bt_glyph_width(size->size, code); }

/* The construction fixture's GL, the pattern font, and the string rule. */
static void bt_gl_setup(bool gbk)
{
	cn_gl_setup();
	bt_font = (struct font) {
		.charmap = CHARMAP_UNICODE,
		.get_size = bt_get_size,
		.get_actual_size = bt_actual_size,
		.get_actual_size_round_down = bt_actual_size,
		.get_glyph = bt_get_glyph,
		.size_char = bt_size_char,
		.size_char_kerning = bt_kerning,
	};
	font_ttf[0] = font_ttf[1] = &bt_font;
	if (gbk)
		gk_enable();
	else
		gk_force_sjis();
	if (gfx_text_cn_gdi() != gbk) {
		printf("BACKLOG-TEXT the glyph grid is %s, want %s\n", gfx_text_cn_gdi() ? "on" : "off", gbk ? "on" : "off");
		fflush(stdout);
		_exit(CN_ENV_EXIT);
	}
}

/* --- the script's font and factories --- */
struct bt_style {
	int size;
	int rgb[3];
	float bold, edge;
	int edge_rgb[3];
};

/* A CASFont with these values (a constructed struct, its members set). */
static int bt_casfont(const struct bt_style *s)
{
	int st = ain_get_struct(ain, "CASFont");
	if (st < 0) { printf("BACKLOG-TEXT struct CASFont missing\n"); fflush(stdout); _exit(CN_ENV_EXIT); }
	int f = vm_construct_struct(st);
	struct page *p = heap[f].page;
	p->values[br_member(st, "Type")].i = 0;
	p->values[br_member(st, "Size")].i = s->size;
	p->values[br_member(st, "BoldWeight")].f = s->bold;
	p->values[br_member(st, "EdgeWeight")].f = s->edge;
	struct page *c = heap[p->values[br_member(st, "Color")].i].page;
	struct page *ec = heap[p->values[br_member(st, "EdgeColor")].i].page;
	for (int i = 0; i < 3; i++) {
		c->values[i].i = s->rgb[i];
		ec->values[i].i = s->edge_rgb[i];
	}
	return f;
}

/* factory(x, y, text, font, charSpace, lineSpace) of CASConstructionProcess,
 * added to the parts by the real parts::detail::AddConstructProcess. */
static void bt_add_text(int no, const char *factory, int x, int y, const char *text, int font, int char_space, int line_space)
{
	int ref = HEAP_REF(font);
	heap_ref(font);
	int proc = pn_process(factory, 6, (int[]){ x, y, sf_str(text, -1), font, char_space, line_space });
	if (HEAP_REF(font) > ref)
		heap_unref(font);
	pn_add(no, proc);
	heap_unref(proc);
	bt_failures += pn_failures;
	pn_failures = 0;
}
static const char *bt_factory(int cmd)
{
	return cmd == 7 ? "CASConstructionProcess::DrawText" : cmd == 8 ? "CASConstructionProcess::CopyText"
		: cmd == 23 ? "CASConstructionProcess::AlphaBlendText" : "CASConstructionProcess::OnlyAlphaText";
}

/* A w x h surface filled with (r, g, b, a): Create (or CreatePixelOnly) and
 * the four-argument Fill, command 6 over all of it. */
static void bt_surface(int no, int w, int h, bool pixel_only, int r, int g, int b, int a)
{
	parts_get(no);
	if (pixel_only)
		PN_ADD(no, "CASConstructionProcess::CreatePixelOnly", w, h);
	else
		PN_ADD(no, "CASConstructionProcess::Create", w, h);
	PN_ADD(no, "CASConstructionProcess::Fill", r, g, b, a);
	bt_failures += pn_failures;
	pn_failures = 0;
}

static uint8_t *bt_pixels(int no, int w, int h, const char *what)
{
	PE_BuildPartsConstructionProcess(no, 1);
	struct parts *p = parts_try_get(no);
	struct parts_state *s = p ? &p->states[0] : NULL;
	bool built = s && s->type == PARTS_CONSTRUCTION_PROCESS && s->common.texture.handle
		&& s->common.texture.w == w && s->common.texture.h == h;
	BT_CHECK(built, "%s: not a built %dx%d construction", what, w, h);
	return built ? gfx_get_pixels(&s->common.texture) : NULL;
}

/* --- the model --- */
static struct text_style bt_text_style(const struct bt_style *s, int char_space)
{
	return (struct text_style) {
		.face = 0, .size = s->size, .bold_width = s->bold,
		.edge_left = s->edge, .edge_up = s->edge, .edge_right = s->edge, .edge_down = s->edge,
		.color = { s->rgb[0], s->rgb[1], s->rgb[2], 255 },
		.edge_color = { s->edge_rgb[0], s->edge_rgb[1], s->edge_rgb[2], 255 },
		.scale_x = 1.0f, .space_scale_x = 1.0f, .font_spacing = char_space,
	};
}

/* Text drawn at (0, y) on a w x h surface of its own, transparent in the
 * edge colour where no glyph is: its pixels (to be freed). */
static uint8_t *bt_render(struct text_style *ts, const char *text, int w, int h, int y)
{
	Texture t = { 0 };
	gfx_init_texture_rgba(&t, w, h, (SDL_Color){ ts->edge_color.r, ts->edge_color.g, ts->edge_color.b, 0 });
	gfx_render_textf(&t, 0, y, (char *)text, ts, false);
	uint8_t *px = gfx_get_pixels(&t);
	gfx_delete_texture(&t);
	return px;
}

/* One staged pixel s onto d, as the command puts it. */
static void bt_put(uint8_t *d, const uint8_t *s, int cmd, bool has_alpha)
{
	if (cmd == 7 || cmd == 23) {
		for (int c = 0; c < 3; c++) {
			int v = (s[c] - d[c]) * s[3];
			d[c] = d[c] + (v >= 0 ? v / 256 : -((-v + 255) / 256));	/* sar 8 */
		}
		if (cmd == 23 && s[3] > d[3])
			d[3] = s[3];
	} else if (cmd == 8) {
		memcpy(d, s, 3);
		if (has_alpha)
			d[3] = s[3];
	} else if (has_alpha) {
		d[3] = s[3];
	}
}

/* src (sw x sh) put at (dx, dy) of px (tw x th), cut to both. For a dx or dy
 * below 0 this is xsystem4's cut, the cell's left or top lost. The original
 * moves the destination into the surface and reads the cell from its corner
 * all the same (0x5abed0, 0x4fd5bb), so that the left or top stays and the
 * right or bottom is lost; the pens below 0 of BT2 do not hold that. */
static void bt_put_rect(uint8_t *px, int tw, int th, long dx, long dy, const uint8_t *src, int sw, int sh, int cmd, bool has_alpha)
{
	for (int row = 0; row < sh; row++) {
		for (int col = 0; col < sw; col++) {
			long x = dx + col, y = dy + row;
			if (x < 0 || y < 0 || x >= tw || y >= th)
				continue;
			bt_put(px + (y * tw + x) * 4, src + (row * sw + col) * 4, cmd, has_alpha);
		}
	}
}

static int bt_char_bytes(const char *p)
{
	unsigned char c = *p;
	if (gfx_text_cn_gdi())
		return c >= 0x81 && c <= 0xfe && p[1] ? 2 : 1;
	return SJIS_2BYTE(c) && p[1] ? 2 : 1;
}

/* The command on the glyph grid: the cells of a text state ((width + 2e) x
 * (size + 3e) here, the glyph at (e, e)), one per character, at a pen that
 * a "\n" moves down by the line and the line spacing. */
static void bt_model_grid(uint8_t *px, int tw, int th, bool has_alpha, int cmd, int X, int Y, const char *text,
		const struct bt_style *s, int char_space, int line_space)
{
	struct text_style ts = bt_text_style(s, 0);
	const int e = gfx_text_cn_style_edge(&ts);
	const int line = s->size + (s->size & 1) + 2 * e;
	long x = X, y = Y;
	for (const char *p = text; *p;) {
		if (*p == '\n') {
			y += line + line_space;
			x = X;
			p++;
			continue;
		}
		int n = bt_char_bytes(p);
		char ch[4] = { 0 };
		memcpy(ch, p, n);
		const int cw = gfx_text_cn_width(s->size, *p) + 2 * e, chh = s->size + 3 * e;
		uint8_t *cell = bt_render(&ts, ch, cw, chh, e);
		bt_put_rect(px, tw, th, x, y, cell, cw, chh, cmd, has_alpha);
		free(cell);
		x += cw + char_space;
		p += n;
	}
}

/* 23 and 24 without the grid: the one line command 8 draws there. */
static void bt_model_line(uint8_t *px, int tw, int th, bool has_alpha, int cmd, int X, int Y, const char *text,
		const struct bt_style *s, int char_space)
{
	struct text_style ts = bt_text_style(s, char_space);
	const int w = ceilf(gfx_size_text(&ts, text)), h = ceilf(s->size + 2 * s->edge);
	if (w <= 0 || h <= 0)
		return;
	uint8_t *line = bt_render(&ts, text, w, h, 0);
	bt_put_rect(px, tw, th, X, Y, line, w, h, cmd, has_alpha);
	free(line);
}

static void bt_fill(uint8_t *px, int w, int h, int r, int g, int b, int a)
{
	for (int i = 0; i < w * h; i++) {
		px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a;
	}
}

static void bt_compare(uint8_t *got, int w, int h, const uint8_t *model, const char *what)
{
	if (!got) {
		BT_CHECK(0, "%s: no pixels to compare", what);
		return;
	}
	int bad = 0;
	for (int i = 0; i < w * h && bad < 4; i++) {
		if (memcmp(got + i * 4, model + i * 4, 4)) {
			BT_CHECK(0, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, i % w, i / w,
				 got[i * 4], got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3],
				 model[i * 4], model[i * 4 + 1], model[i * 4 + 2], model[i * 4 + 3]);
			bad++;
		}
	}
}
static int bt_count_alpha(const uint8_t *px, int n, int a, bool above)
{
	int c = 0;
	for (int i = 0; px && i < n; i++)
		c += above ? px[i * 4 + 3] > a : px[i * 4 + 3] == a;
	return c;
}
#define BT_PX(px, w, x, y) ((px) + ((y) * (w) + (x)) * 4)
static void bt_expect_px(const uint8_t *px, int w, int x, int y, int r, int g, int b, int a, const char *what)
{
	const uint8_t *p = px ? BT_PX(px, w, x, y) : NULL;
	BT_CHECK(p && p[0] == r && p[1] == g && p[2] == b && p[3] == a, "%s (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)", what, x, y,
		 p ? p[0] : -1, p ? p[1] : -1, p ? p[2] : -1, p ? p[3] : -1, r, g, b, a);
}

/* One surface: base, command, model, comparison. Returns the built pixels. */
struct bt_case {
	int no, w, h;
	bool pixel_only;
	int base[4];
	int cmd, x, y;
	const char *text;
	struct bt_style style;
	int char_space, line_space;
	const char *what;
};
static uint8_t *bt_run(const struct bt_case *c, bool grid)
{
	int font = bt_casfont(&c->style);
	bt_surface(c->no, c->w, c->h, c->pixel_only, c->base[0], c->base[1], c->base[2], c->base[3]);
	bt_add_text(c->no, bt_factory(c->cmd), c->x, c->y, c->text, font, c->char_space, c->line_space);
	heap_unref(font);
	uint8_t *got = bt_pixels(c->no, c->w, c->h, c->what);
	uint8_t *model = xmalloc(c->w * c->h * 4);
	bt_fill(model, c->w, c->h, c->base[0], c->base[1], c->base[2], c->pixel_only ? 255 : c->base[3]);
	if (grid)
		bt_model_grid(model, c->w, c->h, !c->pixel_only, c->cmd, c->x, c->y, c->text, &c->style, c->char_space, c->line_space);
	else
		bt_model_line(model, c->w, c->h, !c->pixel_only, c->cmd, c->x, c->y, c->text, &c->style, c->char_space);
	bt_compare(got, c->w, c->h, model, c->what);
	free(model);
	return got;
}

#define BT_GBK_A "\xb0\xa1"	/* 啊: a full-width character */
static const struct bt_style bt_plain = { 12, { 200, 100, 50 }, 0, 0, { 9, 8, 7 } };

/* --- BT1 --- */
static void bt_alpha_blend(void)
{
	bt_gl_setup(true);
	/* The cell itself: the pattern's alpha in the font's colour, the edge
	 * colour without alpha where the pattern has none. */
	struct text_style ts = bt_text_style(&bt_plain, 0);
	uint8_t *cell = bt_render(&ts, "A", 6, 12, 0);
	int a10 = bt_glyph_alpha('A', 1, 0), a23 = bt_glyph_alpha('A', 2, 3);
	BT_CHECK(a10 >= 3 && a23 >= 3 && bt_glyph_alpha('A', 0, 0) == 0, "the pattern: (1,0) %d, (2,3) %d, (0,0) %d",
		 a10, a23, bt_glyph_alpha('A', 0, 0));
	bt_expect_px(cell, 6, 1, 0, 200, 100, 50, a10, "the cell of A");
	bt_expect_px(cell, 6, 2, 3, 200, 100, 50, a23, "the cell of A");
	bt_expect_px(cell, 6, 0, 0, 9, 8, 7, 0, "the cell of A");
	free(cell);

	/* "A啊\nB" at (3,2), character spacing -2, line spacing 3, on a
	 * transparent base of another colour: A's cell is 6 wide, 啊's 12 and
	 * starts at 3 + 6 - 2 = 7, so that columns 7 and 8 take both; the
	 * second line starts at y = 2 + 12 + 3. */
	const struct bt_case c = { 9501, 40, 34, false, { 10, 20, 30, 0 }, 23, 3, 2, "A" BT_GBK_A "\nB", bt_plain, -2, 3,
		"BT1 command 23" };
	uint8_t *got = bt_run(&c, true);
	BT_CHECK(bt_count_alpha(got, 40 * 34, 0, true) > 100, "BT1: %d pixels can be seen, want more than 100 (the text)",
		 bt_count_alpha(got, 40 * 34, 0, true));
	/* By hand, where only A's cell lands: column 1, row 0 of the glyph at
	 * (4,2). c += ((s - c) * a) >> 8 from (10,20,30). */
	if (got) {
		int r = 10 + ((200 - 10) * a10 >> 8), g = 20 + ((100 - 20) * a10 >> 8), b = 30 + ((50 - 30) * a10 >> 8);
		bt_expect_px(got, 40, 4, 2, r, g, b, a10, "BT1 by hand, A");
		/* outside every cell: the base */
		bt_expect_px(got, 40, 0, 0, 10, 20, 30, 0, "BT1 outside the text");
		bt_expect_px(got, 40, 30, 20, 10, 20, 30, 0, "BT1 outside the text");
		/* the second line: B's column 1, row 0 at (4, 17) */
		int ab = bt_glyph_alpha('B', 1, 0);
		bt_expect_px(got, 40, 4, 17, 10 + ((200 - 10) * ab >> 8), 20 + ((100 - 20) * ab >> 8), 30 + ((50 - 30) * ab >> 8), ab,
			     "BT1 by hand, the second line");
		/* rows 14..16 lie between the lines */
		BT_CHECK(bt_count_alpha(BT_PX(got, 40, 0, 14), 40 * 3, 0, false) == 120, "BT1: something was drawn between the lines");
		/* the overlap: A's column 4 (alpha 0 there or not) and then 啊's
		 * column 0 at (7,2): the larger alpha stays */
		uint32_t code = 0x554a;	/* 啊 */
		int a1 = bt_glyph_alpha('A', 4, 0), a2 = bt_glyph_alpha(code, 0, 0);
		BT_CHECK(BT_PX(got, 40, 7, 2)[3] == (a1 > a2 ? a1 : a2), "BT1 the overlap (7,2): alpha %d, want max(%d, %d)",
			 BT_PX(got, 40, 7, 2)[3], a1, a2);
	}
	free(got);

	/* A blend towards a darker colour rounds down: base 250, text 50,
	 * alpha 3 gives 250 + ((50 - 250) * 3 >> 8) = 247. */
	uint8_t d[4] = { 250, 250, 250, 0 }, s[4] = { 50, 50, 50, 3 };
	bt_put(d, s, 23, true);
	BT_CHECK(d[0] == 247 && d[3] == 3, "the model itself: %d, alpha %d; want 247, 3", d[0], d[3]);

	/* An odd size: the line is the size made even (11 -> 12). */
	const struct bt_style odd = { 11, { 255, 255, 255 }, 0, 0, { 0, 0, 0 } };
	const struct bt_case c2 = { 9502, 16, 30, false, { 255, 255, 255, 0 }, 23, 0, 0, "A\nA", odd, 0, 1, "BT1 an odd size" };
	got = bt_run(&c2, true);
	if (got) {
		int a = bt_glyph_alpha('A', 1, 0);
		BT_CHECK(BT_PX(got, 16, 1, 13)[3] == a && BT_PX(got, 16, 1, 12)[3] == 0,
			 "BT1 an odd size: the second line's (1,0) at row 13 has alpha %d (want %d), row 12 has %d (want 0)",
			 BT_PX(got, 16, 1, 13)[3], a, BT_PX(got, 16, 1, 12)[3]);
	}
	free(got);

	/* An edge of 1.5: e = 2. The cells are 4 wider, the line 4 higher, the
	 * glyph 2 in from the cell's corner. */
	const struct bt_style edged = { 12, { 255, 254, 113 }, 0, 1.5f, { 0, 0, 0 } };
	const struct bt_case c3 = { 9503, 48, 40, false, { 0, 0, 0, 0 }, 23, 1, 1, "AB\nA", edged, -3, 9, "BT1 an edge" };
	got = bt_run(&c3, true);
	if (got) {
		/* nothing left of the first cell or above it; the second line's
		 * cell starts at y = 1 + 12 + 4 + 9 = 26 */
		int seen_first_rows = bt_count_alpha(BT_PX(got, 48, 0, 0), 48, 0, true);
		BT_CHECK(seen_first_rows == 0, "BT1 an edge: row 0 has %d visible pixels", seen_first_rows);
		BT_CHECK(bt_count_alpha(BT_PX(got, 48, 0, 26), 48 * 14, 0, true) > 20
			 && bt_count_alpha(BT_PX(got, 48, 0, 19), 48 * 7, 0, true) == 0,
			 "BT1 an edge: the second line is not at row 26 (%d visible below, %d between)",
			 bt_count_alpha(BT_PX(got, 48, 0, 26), 48 * 14, 0, true), bt_count_alpha(BT_PX(got, 48, 0, 19), 48 * 7, 0, true));
	}
	free(got);
	printf("BT1 command 23 on the grid: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT2 --- */
static void bt_other_commands(void)
{
	bt_gl_setup(true);
	static const int cmds[] = { 7, 8, 24 };
	for (int i = 0; i < 3; i++) {
		char what[64];
		snprintf(what, sizeof what, "BT2 command %d", cmds[i]);
		const struct bt_case c = { 9510 + i, 40, 34, false, { 10, 20, 30, 77 }, cmds[i], 3, 2, "A" BT_GBK_A "\nB", bt_plain, -2, 3, what };
		uint8_t *got = bt_run(&c, true);
		if (got && cmds[i] == 7)
			BT_CHECK(bt_count_alpha(got, 40 * 34, 77, false) == 40 * 34, "BT2 command 7 changed the alpha of %d pixels",
				 40 * 34 - bt_count_alpha(got, 40 * 34, 77, false));
		if (got && cmds[i] == 24) {
			int a = bt_glyph_alpha('A', 1, 0);
			bt_expect_px(got, 40, 4, 2, 10, 20, 30, a, "BT2 command 24 by hand");
			bt_expect_px(got, 40, 3, 2, 10, 20, 30, 0, "BT2 command 24 where the cell has no glyph");
		}
		if (got && cmds[i] == 8) {
			bt_expect_px(got, 40, 4, 2, 200, 100, 50, bt_glyph_alpha('A', 1, 0), "BT2 command 8 by hand");
			bt_expect_px(got, 40, 3, 2, 9, 8, 7, 0, "BT2 command 8 where the cell has no glyph");
			bt_expect_px(got, 40, 2, 2, 10, 20, 30, 77, "BT2 command 8 left of the cell");
		}
		free(got);
		/* a surface without alpha */
		snprintf(what, sizeof what, "BT2 command %d without alpha", cmds[i]);
		const struct bt_case c2 = { 9520 + i, 40, 34, true, { 10, 20, 30, 0 }, cmds[i], 3, 2, "A" BT_GBK_A "\nB", bt_plain, -2, 3, what };
		got = bt_run(&c2, true);
		BT_CHECK(!got || bt_count_alpha(got, 40 * 34, 255, false) == 40 * 34, "%s: the alpha is not 255 everywhere", what);
		free(got);
	}
	const struct bt_case c23 = { 9530, 40, 34, true, { 10, 20, 30, 0 }, 23, 3, 2, "A" BT_GBK_A "\nB", bt_plain, -2, 3,
		"BT2 command 23 without alpha" };
	free(bt_run(&c23, true));

	/* A text that does not fit, and pens outside. The pens left of and above
	 * the surface, (-3,-2), (-100000,0) and INT_MIN, pin xsystem4's cut and
	 * are not the original's behaviour (bt_put_rect). */
	static const struct { int x, y; } pens[] = { { 0, 0 }, { -3, -2 }, { 7, 9 }, { 1000, 5 }, { 5, 1000 }, { INT_MIN, INT_MIN },
		{ INT_MAX, INT_MAX }, { INT_MAX - 3, 0 }, { -100000, 0 } };
	for (unsigned i = 0; i < sizeof pens / sizeof pens[0]; i++) {
		char what[64];
		snprintf(what, sizeof what, "BT2 a pen at (%d,%d)", pens[i].x, pens[i].y);
		const struct bt_case c = { 9540 + i, 10, 10, false, { 1, 2, 3, 4 }, 23, pens[i].x, pens[i].y, "AB" BT_GBK_A "\n\nAAAA", bt_plain,
			1, INT_MAX / 4, what };
		free(bt_run(&c, true));
	}
	/* An empty text and a size of 0 draw nothing. */
	const struct bt_case empty = { 9560, 8, 8, false, { 1, 2, 3, 4 }, 23, 0, 0, "", bt_plain, 0, 0, "BT2 an empty text" };
	free(bt_run(&empty, true));
	printf("BT2 commands 7, 8 and 24 on the grid: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT3 --- */
static void bt_no_grid(void)
{
	bt_gl_setup(false);
	const struct bt_case c = { 9570, 40, 20, false, { 10, 20, 30, 0 }, 23, 3, 2, "AB", bt_plain, 2, 0, "BT3 command 23, one line" };
	uint8_t *got = bt_run(&c, false);
	BT_CHECK(bt_count_alpha(got, 40 * 20, 0, true) > 40, "BT3: %d pixels can be seen", bt_count_alpha(got, 40 * 20, 0, true));
	free(got);
	const struct bt_case c24 = { 9571, 40, 20, false, { 10, 20, 30, 77 }, 24, 3, 2, "AB", bt_plain, 2, 0, "BT3 command 24, one line" };
	free(bt_run(&c24, false));
	const struct bt_case c23p = { 9572, 40, 20, true, { 10, 20, 30, 0 }, 23, 3, 2, "AB", bt_plain, 2, 0, "BT3 command 23 without alpha" };
	free(bt_run(&c23p, false));

	/* Guards: 7 blends on the texture and leaves the alpha, 8 writes it. */
	int font = bt_casfont(&bt_plain);
	bt_surface(9573, 40, 20, false, 10, 20, 30, 77);
	bt_add_text(9573, bt_factory(7), 3, 2, "AB", font, 2, 0);
	got = bt_pixels(9573, 40, 20, "BT3 command 7");
	if (got) {
		int changed = 0;
		for (int i = 0; i < 40 * 20; i++)
			changed += got[i * 4] != 10;
		BT_CHECK(bt_count_alpha(got, 40 * 20, 77, false) == 40 * 20 && changed > 40,
			 "BT3 command 7: %d pixels kept the alpha, %d changed colour", bt_count_alpha(got, 40 * 20, 77, false), changed);
	}
	free(got);
	bt_surface(9574, 40, 20, false, 10, 20, 30, 77);
	bt_add_text(9574, bt_factory(8), 3, 2, "AB", font, 2, 0);
	got = bt_pixels(9574, 40, 20, "BT3 command 8");
	BT_CHECK(!got || bt_count_alpha(got, 40 * 20, 77, false) < 40 * 20, "BT3 command 8 wrote no alpha");
	free(got);
	heap_unref(font);
	printf("BT3 without the grid: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT4 --- */
static void bt_save_load(void)
{
	bt_gl_setup(true);
	const int NO = 9580;
	int font = bt_casfont(&bt_plain);
	bt_surface(NO, 40, 34, false, 10, 20, 30, 0);
	bt_add_text(NO, bt_factory(23), 3, 2, "A" BT_GBK_A "\nB", font, -2, 3);
	bt_add_text(NO, bt_factory(24), 20, 16, "B", font, 0, 0);
	heap_unref(font);
	uint8_t *before = bt_pixels(NO, 40, 34, "BT4 before");
	struct parts *p = parts_try_get(NO);
	int nr = p ? cn_nr_ops(p) : -1;
	BT_CHECK(nr == 4, "BT4: %d operations before the save, want 4", nr);
	PE_UpdateComponent(0);
	struct page *saved = NULL;
	BT_CHECK(PE_Save(&saved), "PE_Save failed");
	if (!saved || !before) {
		free(before);
		return;
	}
	BT_CHECK(ca_save_version(saved) >= 9, "a save with the text operations is XPE v%d, want 9 or later", ca_save_version(saved));
	BT_CHECK(PE_Load(&saved), "PE_Load failed");
	PE_UpdateComponent(0);
	p = parts_try_get(NO);
	BT_CHECK(p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS && cn_nr_ops(p) == 4, "after PE_Load: %d operations, want 4",
		 p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS ? cn_nr_ops(p) : -1);
	if (p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS) {
		int types[4], n = 0;
		struct parts_cp_op *op;
		TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry)
			if (n < 4)
				types[n++] = op->type;
		BT_CHECK(n == 4 && types[2] == 23 && types[3] == 24, "after PE_Load the last operations are of type %d and %d, want 23 and 24",
			 n == 4 ? types[2] : -1, n == 4 ? types[3] : -1);
	}
	uint8_t *after = bt_pixels(NO, 40, 34, "BT4 after");
	BT_CHECK(after && !memcmp(before, after, 40 * 34 * 4), "BT4: the pixels differ after PE_Load");
	BT_CHECK(bt_count_alpha(after, 40 * 34, 0, true) > 100, "BT4: the loaded surface shows no text");
	free(before);
	free(after);
	printf("BT4 save and load: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT5 --- */
static int bt_array_slot(struct page *page)
{
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, page);
	return slot;
}
static int bt_emplace_back(int array, int arg3)
{
	return bu_call("Array", "EmplaceBack", 1, arg3, 1, (union vm_value[]){ BU_I(array) }).i;
}
static int bt_struct(const char *name)
{
	int st = ain_get_struct(ain, (char *)name);
	if (st < 0) { printf("BACKLOG-TEXT struct %s missing\n", name); fflush(stdout); _exit(CN_ENV_EXIT); }
	return st;
}
static int bt_member_slot(int obj, const char *struct_name, const char *member)
{
	return heap[obj].page->values[br_member(bt_struct(struct_name), member)].i;
}

static void bt_emplace(void)
{
	lr_setup();
	int font_st = bt_struct("CASFont");
	union vm_value dim = { .i = 0 };
	int fonts = bt_array_slot(alloc_array(1, &dim, AIN_ARRAY_STRUCT, font_st, false));
	int f1 = bt_emplace_back(fonts, 2);
	struct page *a = heap[fonts].page;
	BT_CHECK(a && a->nr_vars == 1 && a->values[0].i == f1 && da_live_page(f1), "EmplaceBack: %d element(s), [0] = %d, returned %d",
		 a ? a->nr_vars : -1, a && a->nr_vars ? a->values[0].i : -1, f1);
	if (da_live_page(f1)) {
		int size = heap[f1].page->values[br_member(font_st, "Size")].i;
		int color = heap[f1].page->values[br_member(font_st, "Color")].i;
		struct page *c = da_live_page(color) ? heap[color].page : NULL;
		/* CASFont@0 -> CASFont@2: Size 16, Color (255,255,255,255) */
		BT_CHECK(size == 16, "the emplaced CASFont has Size %d, want 16 (its constructor)", size);
		BT_CHECK(c && c->values[0].i == 255 && c->values[1].i == 255 && c->values[2].i == 255 && c->values[3].i == 255,
			 "the emplaced CASFont's Color is (%d,%d,%d,%d), want white", c ? c->values[0].i : -1, c ? c->values[1].i : -1,
			 c ? c->values[2].i : -1, c ? c->values[3].i : -1);
		BT_CHECK(HEAP_REF(f1) == 2, "the emplaced element has %d references, want 2 (the array's and the result's)", HEAP_REF(f1));
	}
	int f2 = bt_emplace_back(fonts, 2);
	a = heap[fonts].page;
	BT_CHECK(a && a->nr_vars == 2 && a->values[0].i == f1 && a->values[1].i == f2 && f2 != f1,
		 "a second EmplaceBack: %d elements [%d, %d]", a ? a->nr_vars : -1, a && a->nr_vars > 0 ? a->values[0].i : -1,
		 a && a->nr_vars > 1 ? a->values[1].i : -1);

	/* The backlog's own use: CBackLogUnitList@PushUnit emplaces a
	 * CBackLogUnit, whose constructor makes its parts and its m_modelList;
	 * @AddLog adds a line to the last unit, @AddSeparate a unit of its own
	 * and a new one; CBackLogUnit@CreateLog joins a unit's lines. */
	int list = vm_construct_struct(bt_struct("backlog::detail::CBackLogUnitList"));
	int model_st = bt_struct("backlog::detail::SBackLogUnitModel");
	const char *lines[] = { "L1", "L2", "", "L3" };
	for (int i = 0; i < 4; i++) {
		int m = vm_construct_struct(model_st);
		int old = heap[m].page->values[br_member(model_st, "m_Text")].i;
		heap[m].page->values[br_member(model_st, "m_Text")].i = sf_str(lines[i], -1);
		if (old > 0)
			heap_unref(old);
		heap[m].page->values[br_member(model_st, "m_Separator")].i = i == 2;
		heap_ref(m);
		lr_method("backlog::detail::CBackLogUnitList@AddUnit", list, 1, (int[]){ m });
	}
	int units = bt_member_slot(list, "backlog::detail::CBackLogUnitList", "m_list");
	struct page *up = da_live_page(units) ? heap[units].page : NULL;
	/* [L1, L2] [separator] [L3] */
	BT_CHECK(up && up->nr_vars == 3, "the list has %d units, want 3", up ? up->nr_vars : -1);
	static const char *want[] = { "L1\nL2", " ", "L3" };
	for (int i = 0; up && i < up->nr_vars && i < 3; i++) {
		int unit = up->values[i].i;
		int text_parts = da_live_page(unit) ? heap[unit].page->values[0].i : -1;
		BT_CHECK(da_live_page(text_parts), "unit %d has no construction parts object (m_text %d): its constructor did not run", i, text_parts);
		int s = lr_method("backlog::detail::CBackLogUnit@CreateLog", unit, 0, NULL);
		BT_CHECK(!strcmp(bu_str(s), want[i]), "unit %d: CreateLog gives '%s', want '%s'", i, bu_str(s), want[i]);
		if (s > 0)
			heap_unref(s);
	}

	/* A struct without a constructor, what CASTimerManager@CreateHandle
	 * emplaces: an object with its members' defaults (0x679c2c). */
	int timer_st = bt_struct("CASTimerImp");
	BT_CHECK(ain->structures[timer_st].constructor < 0, "CASTimerImp has a constructor (%d): this case wants a struct without one",
		 ain->structures[timer_st].constructor);
	int timers = bt_array_slot(alloc_array(1, &dim, AIN_ARRAY_STRUCT, timer_st, false));
	int t1 = bt_emplace_back(timers, 2);
	a = heap[timers].page;
	bool timer = a && a->nr_vars == 1 && a->values[0].i == t1 && da_live_page(t1) && heap[t1].page->type == STRUCT_PAGE
		&& heap[t1].page->index == timer_st && heap[t1].page->nr_vars == ain->structures[timer_st].nr_members;
	BT_CHECK(timer, "EmplaceBack of a struct without a constructor: %d element(s), [0] = %d, returned %d", a ? a->nr_vars : -1,
		 a && a->nr_vars ? a->values[0].i : -1, t1);
	for (int i = 0; timer && i < heap[t1].page->nr_vars; i++)
		BT_CHECK(heap[t1].page->values[i].i == 0, "the emplaced CASTimerImp's member %d is %d, want 0", i, heap[t1].page->values[i].i);
	BT_CHECK(!timer || HEAP_REF(t1) == 2, "the emplaced CASTimerImp has %d references, want 2", timer ? HEAP_REF(t1) : -1);
	/* One with a string member, the first such struct of the AIN that has
	 * neither a constructor nor an interface: the string is an empty one,
	 * not the -1 of an object nobody initialized. */
	int str_st = -1, str_member = -1;
	for (int i = 0; i < ain->nr_structures && str_st < 0; i++) {
		struct ain_struct *st = &ain->structures[i];
		if (st->constructor >= 0 || st->nr_interfaces || st->is_interface)
			continue;
		for (int m = 0; m < st->nr_members && str_st < 0; m++) {
			if (st->members[m].type.data == AIN_STRING) {
				str_st = i;
				str_member = m;
			}
		}
	}
	BT_CHECK(str_st >= 0, "the AIN has no struct with a string member and no constructor");
	if (str_st >= 0) {
		int strs = bt_array_slot(alloc_array(1, &dim, AIN_ARRAY_STRUCT, str_st, false));
		int s1 = bt_emplace_back(strs, 2);
		int text = da_live_page(s1) ? heap[s1].page->values[str_member].i : -2;
		BT_CHECK(text > 0 && heap_index_valid(text) && heap[text].type == VM_STRING && !strcmp(bu_str(text), ""),
			 "struct %d without a constructor: the emplaced element's string member %d is slot %d ('%s'), want an empty string",
			 str_st, str_member, text, bu_str(text));
	}

	/* No array page yet: no struct is known, whatever array the page the
	 * new one is made from was before (a freed page is used again with its
	 * metadata). Before the fix the element was an object of the struct
	 * that page had been an array of. */
	for (int i = 0; i < 4; i++) {
		struct page *was = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, 1);
		was->array.rank = 1;
		was->array.struct_type = font_st;
		free_page(was);
	}
	int none = heap_alloc_slot(VM_PAGE);
	heap[none].page = NULL;
	bt_emplace_back(none, 2);
	struct page *np = heap[none].page;
	BT_CHECK(np && np->nr_vars == 1 && np->values[0].i == 0 && np->array.struct_type == -1,
		 "EmplaceBack on no array: %d element(s), the first %d, the page's struct %d; want one 0, -1", np ? np->nr_vars : -1,
		 np && np->nr_vars ? np->values[0].i : -1, np ? np->array.struct_type : -99);

	/* Guards: an int element is 0; v13 keeps the unconstructed object (two
	 * tests keep it, Array.c's of the version and vm_construct_struct's
	 * own, and this check does not tell which). */
	int ints = bt_array_slot(alloc_array(1, &dim, AIN_ARRAY_INT, -1, false));
	bt_emplace_back(ints, 1);
	BT_CHECK(heap[ints].page && heap[ints].page->nr_vars == 1 && heap[ints].page->values[0].i == 0,
		 "EmplaceBack on an int array: %d element(s), the first %d; want one 0", heap[ints].page ? heap[ints].page->nr_vars : -1,
		 heap[ints].page && heap[ints].page->nr_vars ? heap[ints].page->values[0].i : -1);
	int version = ain->version;
	ain->version = 13;
	int fonts13 = bt_array_slot(alloc_array(1, &dim, AIN_ARRAY_STRUCT, font_st, false));
	int f13 = bt_emplace_back(fonts13, 2);
	ain->version = version;
	BT_CHECK(da_live_page(f13) && heap[f13].page->values[br_member(font_st, "Size")].i == 0,
		 "v13: the emplaced CASFont has Size %d, want 0 (no constructor is called here)",
		 da_live_page(f13) ? heap[f13].page->values[br_member(font_st, "Size")].i : -1);
	printf("BT5 Array.EmplaceBack constructs: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT6 --- */
#define BT_VSCROLL "\xd8\x51\x9d\x4c\x84\xd3\x97\x6c"						/* 豎滾動條 */
#define BT_TOTAL "\xc8\xab\xf3\x77\xa5\xb9\xa5\xaf\xa5\xed\xa9\x60\xa5\xeb\xc1\xbf"		/* 全體スクロール量 */
#define BT_VIEW "\xb1\xed\xca\xbe\xc1\xbf"							/* 表示量 */
#define BT_POS "\xa5\xb9\xa5\xaf\xa5\xed\xa9\x60\xa5\xeb\xce\xbb\xd6\xc3"			/* スクロール位置 */
#define BT_RATE "\xa5\xb9\xa5\xaf\xa5\xed\xa9\x60\xa5\xeb\xa5\xec\xa9\x60\xa5\xc8"		/* スクロールレート */

#define BT_TOTAL_LINK BT_TOTAL "\xa5\xb5\xa5\xa4\xa5\xba\xdf\x42\x84\xd3"				/* 全體スクロール量サイズ連動 */
#define BT_VIEW_LINK BT_VIEW "\xa5\xb5\xa5\xa4\xa5\xba\xdf\x42\x84\xd3"					/* 表示量サイズ連動 */

/* The probe's ReadActivityFile: a box with a scroll bar as BackLog's (500,
 * 16), one with a position and a rate, a plain user component, and for the
 * size links two more parts and two bars: one as SceneStandViewer's (1935,
 * 720, 271, 0.223045, linked to "Img" and "Frame"), one whose total names a
 * parts that is not there. */
static bool bt_read_activity_file(struct string *name, possibly_unused struct string *filename, possibly_unused bool edit)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("BacklogTextActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("BacklogTextActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("\xa5\xeb\xa9\x60\xa5\xc8\xb2\xbf\xbc\xfe", BU_LAYOUT, 0, 7);
	struct ex_tree *kids = &top->children[0].children[1];
	kids->children[0] = bu_comp("Bar", BT_VSCROLL, 4, 0);
	struct ex_tree *info = &kids->children[0].children[0];
	info->children[1] = bu_ileaf(BT_TOTAL, 500);
	info->children[2] = bu_ileaf(BT_VIEW, 16);
	info->children[3] = bu_ileaf(BT_POS, 0);
	info->children[4] = bu_leaf(BT_RATE, (struct ex_value){ .type = EX_FLOAT, .f = 0.0f });
	kids->children[1] = bu_comp("Bar2", BT_VSCROLL, 4, 0);
	info = &kids->children[1].children[0];
	info->children[1] = bu_ileaf(BT_TOTAL, 40);
	info->children[2] = bu_ileaf(BT_VIEW, 10);
	info->children[3] = bu_ileaf(BT_POS, 7);
	info->children[4] = bu_leaf(BT_RATE, (struct ex_value){ .type = EX_FLOAT, .f = 0.25f });
	kids->children[2] = bu_comp("Other", BU_UC, 0, 0);
	/* The linked bar comes before the parts it names. */
	kids->children[3] = bu_comp("Bar3", BT_VSCROLL, 6, 0);
	info = &kids->children[3].children[0];
	info->children[1] = bu_ileaf(BT_TOTAL, 1935);
	info->children[2] = bu_ileaf(BT_VIEW, 720);
	info->children[3] = bu_ileaf(BT_POS, 271);
	info->children[4] = bu_leaf(BT_RATE, (struct ex_value){ .type = EX_FLOAT, .f = 0.223045f });
	info->children[5] = bu_sleaf(BT_TOTAL_LINK, "Img");
	info->children[6] = bu_sleaf(BT_VIEW_LINK, "Frame");
	kids->children[4] = bu_comp("Img", BU_UC, 0, 0);
	kids->children[5] = bu_comp("Frame", BU_UC, 0, 0);
	kids->children[6] = bu_comp("Bar4", BT_VSCROLL, 6, 0);
	info = &kids->children[6].children[0];
	info->children[1] = bu_ileaf(BT_TOTAL, 100);
	info->children[2] = bu_ileaf(BT_VIEW, 10);
	info->children[3] = bu_ileaf(BT_POS, 45);
	info->children[4] = bu_leaf(BT_RATE, (struct ex_value){ .type = EX_FLOAT, .f = 0.5f });
	info->children[5] = bu_sleaf(BT_TOTAL_LINK, "NoSuchParts");
	info->children[6] = bu_sleaf(BT_VIEW_LINK, "");
	bool ok = probe_load_activity_tree(name, e);
	ex_free(e);
	return ok;
}

struct bt_bar { int total, view, pos; float rate; };
static struct bt_bar bt_bar(int no)
{
	return (struct bt_bar) {
		BU_PE("GetVScrollbarTotalSize", BU_I(no)).i, BU_PE("GetVScrollbarViewSize", BU_I(no)).i,
		BU_PE("GetVScrollbarScrollPos", BU_I(no)).i, BU_PE("GetVScrollbarScrollRate", BU_I(no)).f };
}
static void bt_expect_bar(int no, int total, int view, int pos, float rate, const char *what)
{
	struct bt_bar b = bt_bar(no);
	BT_CHECK(b.total == total && b.view == view && b.pos == pos && b.rate == rate,
		 "%s: total %d, view %d, position %d, rate %g; want %d, %d, %d, %g", what, b.total, b.view, b.pos, b.rate,
		 total, view, pos, rate);
}
/* The messages an update of the bars leaves: their number and the first. */
struct bt_msg { int n, type, no, delegate, unique, pos, max; };
static struct bt_msg bt_update(void)
{
	void (*update)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "parts_scrollbar_update");
	int (*delegate)(void) = (int (*)(void))br_pe_fn("GetMessageDelegateIndex");
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	struct bt_msg m = { .type = -1, .no = -1 };
	BT_CHECK(update, "no scroll bar update (parts_scrollbar_update)");
	if (!update)
		return m;
	update();
	for (int t; (t = br_msg_type()) >= 0; br_msg_pop()) {
		if (!m.n++) {
			m.type = t;
			m.no = br_msg_parts();
			m.delegate = delegate();
			m.unique = cp_message_unique();
			m.pos = var(0);
			m.max = var(1);
		}
	}
	return m;
}

static void bt_scrollbar(void)
{
	lr_setup();
	lr_replace_hll("PartsEngine", "ReadActivityFile", (void *)bt_read_activity_file);
	int n = sf_str("BacklogText", -1), f = sf_str("BacklogText", -1);
	bool ok = BU_PE("ReadActivityFile", BU_I(n), BU_I(f), BU_I(0)).i;
	int bar = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("Bar", -1))).i;
	int bar2 = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("Bar2", -1))).i;
	int other = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("Other", -1))).i;
	BT_CHECK(ok && parts_try_get(bar) && parts_try_get(bar2) && parts_try_get(other), "the activity: loaded %d, parts %d, %d, %d",
		 ok, bar, bar2, other);
	if (!parts_try_get(bar) || !parts_try_get(bar2) || !parts_try_get(other))
		return;
	/* as loaded: the four keys as they are */
	bt_expect_bar(bar, 500, 16, 0, 0.0f, "BT6 as loaded");
	bt_expect_bar(bar2, 40, 10, 7, 0.25f, "BT6 as loaded, a position and a rate");
	/* The size links (0x5749d0): the getters give the height of the parts
	 * the pactex names, 0 while that shows nothing, and keep nothing; the
	 * position and the rate are still the loaded ones. A name that is no
	 * parts of the activity, or "", is no link. */
	int bar3 = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("Bar3", -1))).i;
	int bar4 = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("Bar4", -1))).i;
	int img = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("Img", -1))).i;
	int frame = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("Frame", -1))).i;
	bool linked = parts_try_get(bar3) && parts_try_get(bar4) && parts_try_get(img) && parts_try_get(frame);
	BT_CHECK(linked, "the activity's linked bars: parts %d, %d, %d, %d", bar3, bar4, img, frame);
	if (linked) {
		bt_expect_bar(bar3, 0, 0, 271, 0.223045f, "BT6 linked to parts that show nothing");
		PE_SetPartsRectangleDetectionSize(img, 50, 1000, 1);
		PE_SetPartsRectangleDetectionSize(frame, 60, 300, 1);
		bt_expect_bar(bar3, 1000, 300, 271, 0.223045f, "BT6 linked, before an update");
		bt_expect_bar(bar4, 100, 10, 45, 0.5f, "BT6 a link to no parts");
	}
	/* nothing moved: no message */
	cp_register_event(bar, 5, 77);
	struct bt_msg m = bt_update();
	BT_CHECK(m.n == 0, "BT6: %d message(s) from a bar that did not move", m.n);

	/* CBackLogView@InitVScrollbar: TotalSize = lines, ScrollPos = lines */
	BU_PE("SetVScrollbarTotalSize", BU_I(bar), BU_I(184));
	BU_PE("SetVScrollbarScrollPos", BU_I(bar), BU_I(184));
	bt_expect_bar(bar, 184, 16, 168, 1.0f, "BT6 a position beyond the end");
	m = bt_update();
	BT_CHECK(m.n == 1 && m.type == 21 && m.no == bar && m.delegate == 5 && m.unique == 77 && m.pos == 168 && m.max == 168,
		 "BT6 the message: %d, type %d, parts %d, delegate %d, unique %d, (%d, %d); want 1, 21, %d, 5, 77, (168, 168)",
		 m.n, m.type, m.no, m.delegate, m.unique, m.pos, m.max, bar);
	m = bt_update();
	BT_CHECK(m.n == 0, "BT6: %d message(s) on the next update, want none", m.n);

	/* the wheel: ScrollPos = ScrollPos - 3, and below 0 */
	BU_PE("SetVScrollbarScrollPos", BU_I(bar), BU_I(165));
	bt_expect_bar(bar, 184, 16, 165, 165.0f / 168.0f, "BT6 a position");
	m = bt_update();
	BT_CHECK(m.n == 1 && m.pos == 165 && m.max == 168, "BT6 after a step: %d message(s), (%d, %d)", m.n, m.pos, m.max);
	BU_PE("SetVScrollbarScrollPos", BU_I(bar), BU_I(-5));
	bt_expect_bar(bar, 184, 16, 0, 0.0f, "BT6 a position below 0");
	/* a rate gives the position, truncated; beyond 0..1 it is the end */
	BU_PE("SetVScrollbarScrollRate", BU_I(bar), BU_F(0.5f));
	bt_expect_bar(bar, 184, 16, 84, 0.5f, "BT6 a rate");
	BU_PE("SetVScrollbarScrollRate", BU_I(bar), BU_F(0.999f));
	bt_expect_bar(bar, 184, 16, 167, 0.999f, "BT6 a rate, truncated");
	BU_PE("SetVScrollbarScrollRate", BU_I(bar), BU_F(7.0f));
	bt_expect_bar(bar, 184, 16, 168, 1.0f, "BT6 a rate above 1");
	BU_PE("SetVScrollbarScrollRate", BU_I(bar), BU_F(-1.0f));
	bt_expect_bar(bar, 184, 16, 0, 0.0f, "BT6 a rate below 0");
	/* a new view or total places the position again */
	BU_PE("SetVScrollbarScrollPos", BU_I(bar), BU_I(168));
	BU_PE("SetVScrollbarViewSize", BU_I(bar), BU_I(100));
	bt_expect_bar(bar, 184, 100, 84, 1.0f, "BT6 a larger view");
	BU_PE("SetVScrollbarTotalSize", BU_I(bar), BU_I(120));
	bt_expect_bar(bar, 120, 100, 20, 1.0f, "BT6 a smaller total");
	/* nothing to scroll: position 0, rate 1 (0 / 0 does not compare) */
	BU_PE("SetVScrollbarTotalSize", BU_I(bar), BU_I(12));
	bt_expect_bar(bar, 12, 100, 0, 1.0f, "BT6 fewer lines than the view");
	m = bt_update();
	BT_CHECK(m.n == 1 && m.pos == 0 && m.max == 0, "BT6 fewer lines than the view: %d message(s), (%d, %d); want 1, (0, 0)",
		 m.n, m.pos, m.max);
	/* the same total again changes nothing (the loaded rate stays) */
	BU_PE("SetVScrollbarTotalSize", BU_I(bar2), BU_I(40));
	bt_expect_bar(bar2, 40, 10, 7, 0.25f, "BT6 the same total");
	/* a bar without an event sends nothing, and still remembers */
	BU_PE("SetVScrollbarScrollPos", BU_I(bar2), BU_I(9));
	m = bt_update();
	BT_CHECK(m.n == 0, "BT6: %d message(s) from a bar nobody listens to", m.n);
	cp_register_event(bar2, 6, 78);
	m = bt_update();
	BT_CHECK(m.n == 0, "BT6: %d message(s) for a move reported before the event was added", m.n);

	/* Guards: what is not a bar, or is not there. That the user component
	 * stays one and the functions give 0 for it is xsystem4's choice: the
	 * original's 0x53daf0 makes any existing parts a bar first, and its
	 * total would be 50 here. A parts that is not there is not made in the
	 * original either (0x53db37). */
	BU_PE("SetVScrollbarTotalSize", BU_I(other), BU_I(50));
	BU_PE("SetVScrollbarViewSize", BU_I(other), BU_I(5));
	BU_PE("SetVScrollbarScrollPos", BU_I(other), BU_I(9));
	BU_PE("SetVScrollbarScrollRate", BU_I(other), BU_F(0.5f));
	bt_expect_bar(other, 0, 0, 0, 0.0f, "BT6 a user component");
	BU_PE("SetVScrollbarScrollPos", BU_I(987654), BU_I(9));
	bt_expect_bar(987654, 0, 0, 0, 0.0f, "BT6 no parts");
	BT_CHECK(!parts_try_get(987654), "BT6: a scroll bar function made parts 987654");
	m = bt_update();
	BT_CHECK(m.n == 0, "BT6: %d message(s) from what is not a bar", m.n);

	/* The linked bar after the updates above (0x57390c .. 0x573964): each
	 * amount became the linked height and placed the position again, 271
	 * of 1000 - 300. Nobody listened, and it was remembered. */
	if (linked) {
		bt_expect_bar(bar3, 1000, 300, 271, 271.0f / 700.0f, "BT6 linked, updated");
		bt_expect_bar(bar4, 100, 10, 45, 0.5f, "BT6 a link to no parts, updated");
		cp_register_event(bar3, 7, 79);
		m = bt_update();
		BT_CHECK(m.n == 0, "BT6 linked: %d message(s) for what was reported before", m.n);
		/* The picture changes: the getter follows at once, the position
		 * and the message with the next update. Another state's size
		 * does not count while the parts shows its first. */
		PE_SetPartsRectangleDetectionSize(img, 50, 400, 1);
		PE_SetPartsRectangleDetectionSize(img, 50, 777, 2);
		bt_expect_bar(bar3, 400, 300, 271, 271.0f / 700.0f, "BT6 linked, a smaller picture before the update");
		m = bt_update();
		bt_expect_bar(bar3, 400, 300, 100, 1.0f, "BT6 linked, a smaller picture");
		BT_CHECK(m.n == 1 && m.type == 21 && m.no == bar3 && m.delegate == 7 && m.pos == 100 && m.max == 100,
			 "BT6 linked, the message: %d, type %d, parts %d, delegate %d, (%d, %d); want 1, 21, %d, 7, (100, 100)",
			 m.n, m.type, m.no, m.delegate, m.pos, m.max, bar3);
		/* A total the script sets lasts until the next update. */
		BU_PE("SetVScrollbarTotalSize", BU_I(bar3), BU_I(5000));
		BT_CHECK(BU_PE("GetVScrollbarTotalSize", BU_I(bar3)).i == 400 && BU_PE("GetVScrollbarScrollRate", BU_I(bar3)).f == 100.0f / 4700.0f,
			 "BT6 linked, a total set by hand: the getter gives %d, the rate is %g; want 400 (the link), %g (of 5000 - 300)",
			 BU_PE("GetVScrollbarTotalSize", BU_I(bar3)).i, BU_PE("GetVScrollbarScrollRate", BU_I(bar3)).f, 100.0f / 4700.0f);
		m = bt_update();
		bt_expect_bar(bar3, 400, 300, 100, 1.0f, "BT6 linked, after that update");
		BT_CHECK(m.n == 0, "BT6 linked: %d message(s) for the position and rate reported before", m.n);
		/* The frame larger than the picture: nothing to scroll. */
		PE_SetPartsRectangleDetectionSize(frame, 60, 500, 1);
		m = bt_update();
		bt_expect_bar(bar3, 400, 500, 0, 1.0f, "BT6 linked, a frame larger than the picture");
		BT_CHECK(m.n == 1 && m.pos == 0 && m.max == 0, "BT6 linked, a larger frame: %d message(s), (%d, %d); want 1, (0, 0)", m.n, m.pos, m.max);
		/* The linked parts gone: the amount stays as it was. */
		PE_ReleaseParts(img);
		m = bt_update();
		bt_expect_bar(bar3, 400, 500, 0, 1.0f, "BT6 linked, the picture released");
		BT_CHECK(m.n == 0 && !parts_try_get(img), "BT6 linked, the picture released: %d message(s), the parts %s", m.n,
			 parts_try_get(img) ? "is still there" : "is gone");
	}
	printf("BT6 the scroll bar: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT7 --- */
struct bt_font_prop { int type, size, r, g, b, er, eg, eb; float bold, edge; };
/* GetPartsTextFontProperty(Number, wrap<int> Type, Size, R, G, B, wrap<float>
 * BoldWeight, wrap<int> EdgeR, EdgeG, EdgeB, wrap<float> EdgeWeight, State):
 * each wrap is (page, index) of a page whose slots hold `fill` before. */
static struct bt_font_prop bt_font_property(int no, int state, int fill)
{
	int lib = bu_lib("PartsEngine"), fn = sf_find(lib, "GetPartsTextFontProperty", 12);
	if (fn < 0) { printf("BACKLOG-TEXT GetPartsTextFontProperty/12 not declared\n"); fflush(stdout); _exit(CN_ENV_EXIT); }
	int out = bt_array_slot(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 10));
	for (int i = 0; i < 10; i++)
		heap[out].page->values[i].i = fill;
	int sp = stack_ptr;
	stack_push(no);
	for (int i = 0; i < 10; i++) {
		stack_push(out);
		stack_push(i);
	}
	stack_push(state);
	hll_call(lib, fn, 0);
	stack_ptr = sp;
	union vm_value *v = heap[out].page->values;
	struct bt_font_prop p = { v[0].i, v[1].i, v[2].i, v[3].i, v[4].i, v[6].i, v[7].i, v[8].i, v[5].f, v[9].f };
	heap_unref(out);
	return p;
}

static void bt_getters_and_box(void)
{
	lr_setup();
	const int TEXT = 9600, NONE = 9601;
	BU_PE("Parts_SetFont", BU_I(TEXT), BU_I(0), BU_I(25), BU_I(255), BU_I(254), BU_I(113), BU_F(0.5f),
	      BU_I(1), BU_I(2), BU_I(3), BU_F(1.5f), BU_I(2));
	BU_PE("Parts_SetTextCharSpace", BU_I(TEXT), BU_I(-3), BU_I(2));
	BU_PE("Parts_SetTextLineSpace", BU_I(TEXT), BU_I(9), BU_I(2));
	struct bt_font_prop p = bt_font_property(TEXT, 2, -7);
	BT_CHECK(p.type == 0 && p.size == 25 && p.r == 255 && p.g == 254 && p.b == 113 && p.bold == 0.5f
		 && p.er == 1 && p.eg == 2 && p.eb == 3 && p.edge == 1.5f,
		 "GetPartsTextFontProperty: type %d, size %d, (%d,%d,%d), weight %g, edge (%d,%d,%d) %g; want 0, 25, (255,254,113), 0.5, (1,2,3) 1.5",
		 p.type, p.size, p.r, p.g, p.b, p.bold, p.er, p.eg, p.eb, p.edge);
	int cs = BU_PE("Parts_GetTextCharSpace", BU_I(TEXT), BU_I(2)).i, ls = BU_PE("Parts_GetTextLineSpace", BU_I(TEXT), BU_I(2)).i;
	BT_CHECK(cs == -3 && ls == 9, "Parts_GetTextCharSpace %d, Parts_GetTextLineSpace %d; want -3, 9", cs, ls);
	/* another state has its own; a parts that is not there gets nothing */
	cs = BU_PE("Parts_GetTextCharSpace", BU_I(TEXT), BU_I(1)).i;
	ls = BU_PE("Parts_GetTextLineSpace", BU_I(TEXT), BU_I(1)).i;
	BT_CHECK(cs == 0 && ls == 0, "the normal state: spacing %d, %d; want 0, 0", cs, ls);
	p = bt_font_property(NONE, 1, -7);
	BT_CHECK(p.type == -7 && p.size == -7 && p.r == -7 && p.eb == -7, "a missing parts: outputs %d, %d, %d, %d; want -7 (untouched)",
		 p.type, p.size, p.r, p.eb);
	cs = BU_PE("Parts_GetTextCharSpace", BU_I(NONE), BU_I(1)).i;
	ls = BU_PE("Parts_GetTextLineSpace", BU_I(NONE), BU_I(1)).i;
	BT_CHECK(cs == 0 && ls == 0 && !parts_try_get(NONE), "a missing parts: spacing %d, %d, the parts %s", cs, ls,
		 parts_try_get(NONE) ? "was made" : "is still missing");

	/* A box a script makes and AddChild: the children one below the other,
	 * as CBackLogUnitList's lines. */
	const int BOX = 9610, C1 = 9611, C2 = 9612, C3 = 9613, GONE = 9620;
	BU_PE("SetComponentType", BU_I(BOX), BU_I(8), BU_I(1));
	PE_SetPos(BOX, 76, 108);
	parts_get(C1);
	parts_get(C2);
	parts_get(C3);
	PE_SetPartsRectangleDetectionSize(C1, 30, 10, 1);
	PE_SetPartsRectangleDetectionSize(C2, 30, 20, 1);
	PE_SetPartsRectangleDetectionSize(C3, 30, 5, 1);
	BU_PE("AddChild", BU_I(BOX), BU_I(C1));
	BU_PE("AddChild", BU_I(BOX), BU_I(C2));
	BU_PE("AddChild", BU_I(BOX), BU_I(C3));
	PE_UpdateComponent(0);
	PE_UpdateComponent(0);
	struct parts *c1 = parts_try_get(C1), *c2 = parts_try_get(C2), *c3 = parts_try_get(C3), *box = parts_try_get(BOX);
	BT_CHECK(lr_parent(C1) == BOX && lr_parent(C2) == BOX && lr_parent(C3) == BOX && lr_nchild(BOX) == 3
		 && lr_child(BOX, 0) == C1 && lr_child(BOX, 2) == C3,
		 "AddChild: parents %d, %d, %d (want %d), %d children, first %d, last %d", lr_parent(C1), lr_parent(C2), lr_parent(C3),
		 BOX, lr_nchild(BOX), lr_child(BOX, 0), lr_child(BOX, 2));
	BT_CHECK(box && box->states[0].type == PARTS_LAYOUT_BOX, "SetComponentType 8 did not make the parts a box");
	BT_CHECK(c1 && c2 && c3 && c1->local.pos.y == 0 && c2->local.pos.y == 10 && c3->local.pos.y == 30
		 && c1->local.pos.x == 0 && c2->local.pos.x == 0,
		 "the box's children at y %d, %d, %d (x %d, %d); want 0, 10, 30 (0, 0)", c1 ? c1->local.pos.y : -1,
		 c2 ? c2->local.pos.y : -1, c3 ? c3->local.pos.y : -1, c1 ? c1->local.pos.x : -1, c2 ? c2->local.pos.x : -1);
	BT_CHECK(c2 && c2->global.pos.x == 76 && c2->global.pos.y == 118, "the second child on the screen at (%d,%d), want (76,118)",
		 c2 ? c2->global.pos.x : -1, c2 ? c2->global.pos.y : -1);
	/* neither a missing parent nor a missing child is made */
	BU_PE("AddChild", BU_I(GONE), BU_I(C1));
	BU_PE("AddChild", BU_I(BOX), BU_I(GONE + 1));
	PE_UpdateComponent(0);
	BT_CHECK(!parts_try_get(GONE) && !parts_try_get(GONE + 1) && lr_parent(C1) == BOX && lr_nchild(BOX) == 3,
		 "AddChild with a missing parts: %s, %s, C1's parent %d, %d children", parts_try_get(GONE) ? "the parent was made" : "-",
		 parts_try_get(GONE + 1) ? "the child was made" : "-", lr_parent(C1), lr_nchild(BOX));
	printf("BT7 text getters, AddChild and a script's box: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT8 --- */
static union vm_value bt_log(const char *fn, int n, const union vm_value *v)
{
	return bu_call("AnteaterADVLogList", fn, n, 0, n, v);
}
static void bt_log_voice_is(int page, int index, const char *name, const char *filter)
{
	int v = bt_log("GetADVLogVoice", 2, (union vm_value[]){ BU_I(page), BU_I(index) }).i;
	int f = bt_log("GetADVLogVoiceFilter", 2, (union vm_value[]){ BU_I(page), BU_I(index) }).i;
	BT_CHECK(!strcmp(bu_str(v), name) && !strcmp(bu_str(f), filter), "page %d voice %d is '%s' with filter '%s'; want '%s', '%s'",
		 page, index, bu_str(v), bu_str(f), name, filter);
}
static int bt_log_pages(void) { return bt_log("GetNumofADVLog", 0, NULL).i; }
static int bt_log_lines(int page) { return bt_log("GetNumofADVLogText", 1, (union vm_value[]){ BU_I(page) }).i; }
static void bt_log_add(const char *text) { bt_log("AddText", 2, (union vm_value[]){ BU_I(sf_str(text, -1)), BU_I(sf_str("", 0)) }); }
static void bt_log_text_is(int page, int line, const char *want)
{
	int s = bt_log("GetADVLogText", 2, (union vm_value[]){ BU_I(page), BU_I(line) }).i;
	BT_CHECK(!strcmp(bu_str(s), want), "page %d line %d is '%s', want '%s'", page, line, bu_str(s), want);
}

static void bt_log_voices(void)
{
	bt_log("Clear", 0, NULL);
	bt_log("SetEnable", 1, (union vm_value[]){ BU_I(1) });
	/* What has no text is not counted (0x447180, 0x4476f0) and not turned
	 * (0x447860): no page after Clear; a new page counts once it has a
	 * text; AddNewPage twice opens one page. */
	BT_CHECK(bt_log_pages() == 0, "after Clear: %d page(s), want 0 (the empty page is not one)", bt_log_pages());
	bt_log_add("L0");
	BT_CHECK(bt_log_pages() == 1, "one text: %d page(s), want 1", bt_log_pages());
	bt_log("AddNewPage", 0, NULL);
	BT_CHECK(bt_log_pages() == 1, "the page turned: %d page(s), want 1 (the new one is empty)", bt_log_pages());
	bt_log("AddNewPage", 0, NULL);
	bt_log_add("L1");
	BT_CHECK(bt_log_pages() == 2, "AddNewPage twice, then a text: %d page(s), want 2", bt_log_pages());
	bt_log_text_is(1, 0, "L1");
	bt_log("AddVoice", 2, (union vm_value[]){ BU_I(sf_str("V1", -1)), BU_I(sf_str("F1", -1)) });
	bt_log("AddVoice", 2, (union vm_value[]){ BU_I(sf_str("V2", -1)), BU_I(sf_str("", 0)) });
	bt_log("AddNewPage", 0, NULL);
	bt_log_add("L2");
	int pages = bt_log_pages(), last = pages - 1, prev = pages - 2;
	BT_CHECK(pages == 3, "%d pages, want 3", pages);
	if (pages < 2)
		return;
	int n = bt_log("GetNumofADVLogVoice", 1, (union vm_value[]){ BU_I(prev) }).i;
	BT_CHECK(n == 2, "the page with two voices has %d", n);
	bt_log_voice_is(prev, 0, "V1", "F1");
	bt_log_voice_is(prev, 1, "V2", "");
	n = bt_log("GetNumofADVLogVoice", 1, (union vm_value[]){ BU_I(last) }).i;
	BT_CHECK(n == 0, "the page without a voice has %d", n);
	/* An index out of range gives "" here; the original does not check it
	 * (0x447240). */
	bt_log_voice_is(last, 0, "", "");
	bt_log_voice_is(prev, 2, "", "");
	bt_log_voice_is(prev, -1, "", "");
	/* nothing is added while the log is disabled */
	bt_log("SetEnable", 1, (union vm_value[]){ BU_I(0) });
	bt_log("AddVoice", 2, (union vm_value[]){ BU_I(sf_str("V3", -1)), BU_I(sf_str("", 0)) });
	bt_log("AddNewPage", 0, NULL);
	bt_log("SetEnable", 1, (union vm_value[]){ BU_I(1) });
	n = bt_log("GetNumofADVLogVoice", 1, (union vm_value[]){ BU_I(last) }).i;
	BT_CHECK(n == 0, "a voice was added while the log was disabled (%d)", n);
	bt_log("AddVoice", 2, (union vm_value[]){ BU_I(sf_str("V4", -1)), BU_I(sf_str("F4", -1)) });
	bt_log_voice_is(last, 0, "V4", "F4");

	/* A page's empty last line is not counted (0x447735). */
	BT_CHECK(bt_log_lines(last) == 1, "a page of one text has %d line(s), want 1", bt_log_lines(last));
	bt_log("AddNewLine", 0, NULL);
	BT_CHECK(bt_log_lines(last) == 1, "after AddNewLine: %d line(s), want 1 (the new line is empty)", bt_log_lines(last));
	bt_log_add("x");
	BT_CHECK(bt_log_lines(last) == 2 && bt_log_pages() == 3, "a text on the second line: %d line(s) on %d pages, want 2 on 3",
		 bt_log_lines(last), bt_log_pages());
	bt_log_text_is(last, 0, "L2");
	bt_log_text_is(last, 1, "x");
	/* A page of lines without a byte: counted as soon as it has a line that
	 * is not its last (0x4471b4 looks at the number of lines alone), and
	 * not turned (0x4478e2), so the next text still goes to it. */
	bt_log("AddNewPage", 0, NULL);
	BT_CHECK(bt_log_pages() == 3, "the page turned: %d pages, want 3", bt_log_pages());
	bt_log("AddNewLine", 0, NULL);
	bt_log("AddNewLine", 0, NULL);
	BT_CHECK(bt_log_pages() == 4 && bt_log_lines(3) == 2, "two empty lines and the one being written: %d pages, %d lines; want 4, 2",
		 bt_log_pages(), bt_log_lines(3));
	bt_log("AddNewPage", 0, NULL);
	bt_log_add("L3");
	BT_CHECK(bt_log_pages() == 4 && bt_log_lines(3) == 3, "AddNewPage on a page without a byte, then a text: %d pages, %d lines; want 4, 3",
		 bt_log_pages(), bt_log_lines(3));
	bt_log_text_is(3, 2, "L3");

	/* Guard: before v14 these names keep the DLL's rules, where the page
	 * being written counts and so does its empty line. */
	bt_log("AddNewPage", 0, NULL);
	int v14_pages = bt_log_pages();
	int version = ain->version;
	ain->version = 13;
	int v13_pages = bt_log_pages(), v13_lines = bt_log_lines(v13_pages - 1);
	bt_log("AddNewPage", 0, NULL);
	int v13_turned = bt_log_pages();
	ain->version = version;
	BT_CHECK(v14_pages == 4 && v13_pages == 5 && v13_lines == 1 && v13_turned == 6,
		 "v14 counts %d pages; v13 %d, %d line(s) on the last, %d after AddNewPage; want 4; 5, 1, 6", v14_pages, v13_pages,
		 v13_lines, v13_turned);

	/* AddNewPage right after Clear turns nothing (before the fix it wrote
	 * to a log that was not there). */
	bt_log("Clear", 0, NULL);
	bt_log("AddNewPage", 0, NULL);
	bt_log("AddNewPage", 0, NULL);
	BT_CHECK(bt_log_pages() == 0, "AddNewPage after Clear: %d page(s), want 0", bt_log_pages());
	n = bt_log("GetNumofADVLogVoice", 1, (union vm_value[]){ BU_I(0) }).i;
	BT_CHECK(bt_log_pages() == 0 && n == 0, "after Clear: %d page(s), %d voice(s) on the new page; want 0, 0", bt_log_pages(), n);
	bt_log_add("Z");
	BT_CHECK(bt_log_pages() == 1 && bt_log_lines(0) == 1, "a text after that: %d page(s), %d line(s); want 1, 1", bt_log_pages(),
		 bt_log_lines(0));
	bt_log_text_is(0, 0, "Z");
	bt_log("Clear", 0, NULL);
	printf("BT8 the pages and voices of the backlog: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT9 --- */
/* A line wider than the widest texture, without the grid (there 23 and 24
 * draw the whole line on one texture of its own): the texture is made as
 * wide as it can be, and what is read back has that width. Before the fix
 * the rows read back were taken to be as long as the line, and the pixels
 * were read past their end. This is about xsystem4's own staging; the
 * original has no such texture. */
static void bt_wide_line(void)
{
	bt_gl_setup(false);
	Texture limit = { 0 };
	gfx_init_texture_rgba(&limit, 1 << 20, 1, (SDL_Color){ 0, 0, 0, 0 });
	const int max_w = limit.w;
	gfx_delete_texture(&limit);
	BT_CHECK(max_w >= 2048 && max_w < (1 << 20), "the widest texture is %d wide", max_w);
	if (max_w < 2048 || max_w >= (1 << 20))
		return;
	/* Size 100: an 'A' is 50 wide. The line is 2000 wider than a texture. */
	const struct bt_style big = { 100, { 200, 100, 50 }, 0, 0, { 9, 8, 7 } };
	const int W = 64, H = 100, n = max_w / 50 + 40;
	char *line = xmalloc(n + 1);
	memset(line, 'A', n);
	line[n] = '\0';
	int font = bt_casfont(&big);

	/* At the surface's corner: what four characters draw there. */
	bt_surface(9590, W, H, false, 10, 20, 30, 0);
	bt_add_text(9590, bt_factory(23), 3, 0, line, font, 0, 0);
	uint8_t *got = bt_pixels(9590, W, H, "BT9 a wide line");
	uint8_t *model = xmalloc(W * H * 4);
	bt_fill(model, W, H, 10, 20, 30, 0);
	bt_model_line(model, W, H, true, 23, 3, 0, "AAAA", &big, 0);
	bt_compare(got, W, H, model, "BT9 a wide line at the corner");
	BT_CHECK(bt_count_alpha(got, W * H, 0, true) > 1000, "BT9: %d pixels of the wide line can be seen", bt_count_alpha(got, W * H, 0, true));
	free(got);

	/* The texture's last 10 columns at the surface's left: the line ends
	 * there, and the rest of the surface is as it was. Column c of the line
	 * is column c % 50 of an 'A'. */
	bt_surface(9591, W, H, false, 10, 20, 30, 0);
	bt_add_text(9591, bt_factory(23), -(max_w - 10), 0, line, font, 0, 0);
	got = bt_pixels(9591, W, H, "BT9 the end of the texture");
	struct text_style ts = bt_text_style(&big, 0);
	uint8_t *cell = bt_render(&ts, "A", 50, H, 0);
	bt_fill(model, W, H, 10, 20, 30, 0);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < 10; x++)
			bt_put(BT_PX(model, W, x, y), BT_PX(cell, 50, (max_w - 10 + x) % 50, y), 23, true);
	bt_compare(got, W, H, model, "BT9 the end of the texture");
	BT_CHECK(got && bt_count_alpha(got, W * H, 0, true) > 100, "BT9: %d pixels of the line's end can be seen",
		 got ? bt_count_alpha(got, W * H, 0, true) : -1);
	free(cell);
	free(model);
	free(got);
	free(line);
	heap_unref(font);
	printf("BT9 a line wider than a texture: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT10 --- */
/* Font values no script gives, on the grid. These are guards of xsystem4's
 * own (what the original does with them was not read):
 *   - a 太さ and a 縁取り below 0 leave e at 0 and the text is drawn;
 *   - an operation whose style has a negative e all the same (set here in
 *     the operation itself; a cell's width is then below 0) draws nothing.
 *     Before the fix the cells were read from before their pixels. */
static void bt_negative_edge(void)
{
	bt_gl_setup(true);
	const struct bt_style neg = { 20, { 200, 100, 50 }, -6, -6, { 9, 8, 7 } };
	const struct bt_case c = { 9592, 40, 30, false, { 10, 20, 30, 0 }, 23, 2, 1, "a" BT_GBK_A, neg, 0, 0,
		"BT10 a weight and an edge below 0" };
	uint8_t *got = bt_run(&c, true);
	BT_CHECK(bt_count_alpha(got, 40 * 30, 0, true) > 100, "BT10: %d pixels can be seen with a weight and an edge below 0",
		 bt_count_alpha(got, 40 * 30, 0, true));
	free(got);

	/* The same operation with a font weight below 0 as well: e is -6, the
	 * half-width cell -2 wide and the full-width one 8. */
	struct parts *p = parts_try_get(9592);
	struct parts_cp_op *op, *text_op = NULL;
	if (p && p->states[0].type == PARTS_CONSTRUCTION_PROCESS)
		TAILQ_FOREACH(op, &p->states[0].cproc.ops, entry)
			if (op->type == 23)
				text_op = op;
	BT_CHECK(text_op, "BT10: the text operation was not found");
	if (text_op) {
		text_op->text.style.bold_weight = -6;
		BT_CHECK(gfx_text_cn_style_edge(&text_op->text.style) == -6, "BT10: e is %d, want -6", gfx_text_cn_style_edge(&text_op->text.style));
		got = bt_pixels(9592, 40, 30, "BT10 a negative e");
		BT_CHECK(got && bt_count_alpha(got, 40 * 30, 0, false) == 40 * 30, "BT10 a negative e: %d pixels can be seen, want none",
			 got ? 40 * 30 - bt_count_alpha(got, 40 * 30, 0, false) : -1);
		if (got)
			bt_expect_px(got, 40, 5, 5, 10, 20, 30, 0, "BT10 a negative e");
		free(got);
	}
	printf("BT10 a negative e: %s\n", bt_failures ? "FAIL" : "ok");
}

/* --- BT11 --- */
/* A size and a 太さ whose line height does not fit an int draw nothing, on
 * the grid: size 1200000000 with a 太さ of 2e9, so that e is the size and
 * the size plus 2e is past INT_MAX. Before the fix the sum overflowed. Also
 * a guard of xsystem4's own. */
static void bt_huge_font(void)
{
	bt_gl_setup(true);
	uint8_t *got;
	const struct bt_style huge = { 1200000000, { 200, 100, 50 }, 2e9f, 0, { 9, 8, 7 } };
	int font = bt_casfont(&huge);
	bt_surface(9593, 16, 16, false, 10, 20, 30, 0);
	bt_add_text(9593, bt_factory(23), 0, 0, "A\nA", font, 0, 0);
	heap_unref(font);
	got = bt_pixels(9593, 16, 16, "BT11 a huge size");
	BT_CHECK(got && bt_count_alpha(got, 16 * 16, 0, false) == 16 * 16, "BT11 a huge size: %d pixels can be seen, want none",
		 got ? 16 * 16 - bt_count_alpha(got, 16 * 16, 0, false) : -1);
	if (got)
		bt_expect_px(got, 16, 3, 3, 10, 20, 30, 0, "BT11 a huge size");
	free(got);
	printf("BT11 a line height past an int: %s\n", bt_failures ? "FAIL" : "ok");
}

static int bt_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		bt_failures = 0;
		alarm(120);
		fn();
		fflush(stdout);
		_exit(bt_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_backlog_text(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "BT1", bt_alpha_blend }, { "BT2", bt_other_commands }, { "BT3", bt_no_grid }, { "BT4", bt_save_load },
		{ "BT5", bt_emplace }, { "BT6", bt_scrollbar }, { "BT7", bt_getters_and_box }, { "BT8", bt_log_voices },
		{ "BT9", bt_wide_line }, { "BT10", bt_negative_edge }, { "BT11", bt_huge_font },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += bt_fork(cases[i].name, cases[i].fn);
	printf("BACKLOG-TEXT %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}

/* save-text-lines: a text state with several lines through PE_Save and
 * PE_Load. The save keeps a text as its lines, each line a string without
 * the line break (save_parts_text). Before the fix load_parts_text appended
 * the lines one after the other, so the text came back as one line, and it
 * never freed the strings it read. Fixed, a loaded text is what
 * PE_SetText makes of the lines joined by line breaks, which is the text
 * before saving: the same lines, characters and line heights (and, on the
 * GBK rule's glyph grid, SX1 and SX3, the same width and height: the
 * SJIS-rule and version-13 cases have a font with no glyphs and no grid, so
 * their sizes are 0 x 0 before and after and only the lines, characters and
 * heights are compared there), and a save of it that is the same, word for
 * word.
 *
 * The save is the port's own format (XPE), not the original's, and the
 * writer is the same for every engine version and character rule, so the fix
 * is not limited to v14 or to the GBK rule: SX2 (v14, SJIS rule) and SX6 (an
 * XPE version 3 save of engines before v14) come back in lines too. What was
 * single-line before the fix stays the same (SX5, SX7), and the save's
 * layout does not change (SX8).
 *
 *   SX1 v14, the GBK rule (the CN glyph grid): four lines, one of them
 *       empty, with a GBK character whose lead byte is in the SJIS lead
 *       range (it is saved as one word) and one whose is not.
 *   SX2 v14, the SJIS rule: three lines with SJIS characters.
 *   SX3 v14, the GBK rule: a trailing line break, a leading one, only one
 *       line break, two.
 *   SX4 The strings read back are freed: after a save of five lines is
 *       loaded twice, no string with a line's text is left from the first
 *       load, and no other string either (a line break made to append, the
 *       lines joined; the second load frees what the first one made for its
 *       parts, so what is left was lost).
 *   SX5 Unchanged: one line under each rule, an empty text (one line with no
 *       character), loaded as it was saved.
 *   SX6 ain->version 13 (an XPE version 3 save, the layout off the grid):
 *       three lines come back as three lines.
 *   SX7 Unchanged: a single line with ain->version 13.
 *   SX8 Unchanged, the save's layout: a text of three lines is saved as the
 *       number 3 and the three lines, each its characters and a 0, no line
 *       break among them (what saves made before the fix hold).
 *
 * Every case forks, sets the character rule itself (so the mode checks the
 * same with and without XS4_PROBE_GBK) and has the construction fixture's
 * GL context and its glyph-less font (the characters are kept, whether a
 * glyph is drawn does not matter here). Requires forked_cases.inc,
 * gbk_chars_fixture.inc and construction_fixture.inc.
 */

#define SX_JIA "\xbc\xd7"	/* 甲: lead byte outside the SJIS lead range */
#define SX_CE "\x9c\x79"	/* 測: lead byte inside it */
#define SX_YI "\xd2\xd2"	/* 乙 */
#define SX_DING "\xb6\xa1"	/* 丁 */
#define SX_A_SJ "\x82\xa0"	/* あ */
#define SX_I_SJ "\x82\xa2"	/* い */

enum { SX_LINES = 8, SX_CHARS = 8 };
struct sx_shot {
	unsigned nr_lines;
	int nr_chars[SX_LINES];
	unsigned height[SX_LINES];
	float width[SX_LINES];
	char ch[SX_LINES][SX_CHARS][5];
	int w, h;
	char text[64];
};

static struct parts_text *sx_text(int no)
{
	struct parts *p = parts_try_get(no);
	return p && p->states[0].type == PARTS_TEXT ? &p->states[0].text : NULL;
}

static bool sx_shot(int no, struct sx_shot *s, const char *what)
{
	memset(s, 0, sizeof *s);
	struct parts_text *t = sx_text(no);
	XC_CHECK(t, "%s: parts %d has no text state", what, no);
	if (!t)
		return false;
	s->nr_lines = t->nr_lines;
	for (unsigned i = 0; i < t->nr_lines && i < SX_LINES; i++) {
		s->nr_chars[i] = t->lines[i].nr_chars;
		s->height[i] = t->lines[i].height;
		s->width[i] = t->lines[i].width;
		for (int c = 0; c < t->lines[i].nr_chars && c < SX_CHARS; c++)
			memcpy(s->ch[i][c], t->lines[i].chars[c].ch, 5);
	}
	s->w = PE_GetPartsWidth(no, 1);
	s->h = PE_GetPartsHeight(no, 1);
	struct string *text = parts_text_get(t);
	snprintf(s->text, sizeof s->text, "%s", text->text);
	free_string(text);
	return true;
}

static void sx_hex(char *out, size_t n, const char *s)
{
	size_t k = 0;
	out[0] = 0;
	for (; *s && k + 3 < n; s++)
		k += snprintf(out + k, n - k, "%02x", (uint8_t)*s);
}

static void sx_expect_same(const struct sx_shot *a, const struct sx_shot *b, const char *what)
{
	char ha[160], hb[160];
	sx_hex(ha, sizeof ha, a->text);
	sx_hex(hb, sizeof hb, b->text);
	XC_CHECK(b->nr_lines == a->nr_lines, "%s: %u line(s) after loading, %u before (text %s, before %s)", what,
		 b->nr_lines, a->nr_lines, hb, ha);
	XC_CHECK(!strcmp(a->text, b->text), "%s: the text is %s after loading, %s before", what, hb, ha);
	XC_CHECK(b->w == a->w && b->h == a->h, "%s: %dx%d after loading, %dx%d before", what, b->w, b->h, a->w, a->h);
	for (unsigned i = 0; i < a->nr_lines && i < b->nr_lines && i < SX_LINES; i++) {
		XC_CHECK(b->nr_chars[i] == a->nr_chars[i] && b->height[i] == a->height[i] && b->width[i] == a->width[i],
			 "%s: line %u has %d characters, %u high, %g wide after loading; %d, %u, %g before", what, i,
			 b->nr_chars[i], b->height[i], (double)b->width[i], a->nr_chars[i], a->height[i],
			 (double)a->width[i]);
		for (int c = 0; c < a->nr_chars[i] && c < b->nr_chars[i] && c < SX_CHARS; c++)
			XC_CHECK(!memcmp(a->ch[i][c], b->ch[i][c], 5), "%s: character %u,%d differs after loading", what, i,
				 c);
	}
}

static void sx_free_page(struct page *p)
{
	if (p) {
		delete_page_vars(p);
		free_page(p);
	}
}

static bool sx_same_page(struct page *a, struct page *b)
{
	if (!a || !b || a->nr_vars != b->nr_vars)
		return false;
	for (int i = 0; i < a->nr_vars; i++)
		if (a->values[i].i != b->values[i].i)
			return false;
	return true;
}

static unsigned sx_count_lines(const char *text)
{
	unsigned n = 1;
	for (; *text; text++)
		n += *text == '\n';
	return n;
}

/* SetText, PE_Save, PE_Load: the loaded text is the text set, and saving it
 * again gives the same page. */
static void sx_roundtrip(int no, const char *text, const char *what)
{
	struct string *s = cstr_to_string(text);
	XC_CHECK(PE_SetText(no, s, 1), "%s: SetText failed", what);
	free_string(s);
	struct sx_shot before, after;
	if (!sx_shot(no, &before, what))
		return;
	XC_CHECK(before.nr_lines == sx_count_lines(text) && !strcmp(before.text, text),
		 "%s: SetText made %u line(s), want %u", what, before.nr_lines, sx_count_lines(text));
	struct page *first = NULL, *second = NULL;
	XC_CHECK(PE_Save(&first) && first, "%s: PE_Save failed", what);
	if (!first)
		return;
	XC_CHECK(PE_Load(&first), "%s: PE_Load failed", what);
	if (sx_shot(no, &after, what))
		sx_expect_same(&before, &after, what);
	XC_CHECK(PE_Save(&second) && sx_same_page(first, second),
		 "%s: the save of the loaded text is not the save it was loaded from (%d and %d words)", what,
		 first->nr_vars, second ? second->nr_vars : -1);
	printf("  %s: %u line(s) saved, %u loaded, %dx%d\n", what, before.nr_lines, after.nr_lines, after.w, after.h);
	sx_free_page(first);
	sx_free_page(second);
}

/* The character rule, the GL context, and the case's setup done. */
static void sx_setup(bool gbk)
{
	if (gbk)
		gk_enable();
	else
		gk_force_sjis();
	cn_gl_setup();
	xc_ready();
}

static void sx1_gbk(void)
{
	sx_setup(true);
	sx_roundtrip(8101, SX_JIA SX_CE "\n" SX_YI "\n\n" SX_DING, "SX1 v14 GBK rule");
}

static void sx2_sjis(void)
{
	sx_setup(false);
	sx_roundtrip(8201, "AB\n" SX_A_SJ SX_I_SJ "\nC", "SX2 v14 SJIS rule");
}

static void sx3_breaks(void)
{
	sx_setup(true);
	sx_roundtrip(8301, SX_JIA "\n", "SX3 trailing line break");
	sx_roundtrip(8302, "\n" SX_JIA, "SX3 leading line break");
	sx_roundtrip(8303, "\n", "SX3 one line break");
	sx_roundtrip(8304, "\n\n", "SX3 two line breaks");
}

static void sx4_freed(void)
{
	sx_setup(true);
	static const char *lines[5] = { SX_JIA, SX_CE SX_YI, "AB", SX_DING "C", "xyz" };
	const char *text = SX_JIA "\n" SX_CE SX_YI "\nAB\n" SX_DING "C\nxyz";
	struct string *s = cstr_to_string(text);
	XC_CHECK(PE_SetText(8401, s, 1), "SX4: SetText failed");
	free_string(s);
	struct page *saved = NULL;
	XC_CHECK(PE_Save(&saved) && saved, "SX4: PE_Save failed");
	if (!saved)
		return;
	xc_track_begin();
	XC_CHECK(PE_Load(&saved), "SX4: the first PE_Load failed");
	xc_track_end();
	XC_CHECK(PE_Load(&saved), "SX4: the second PE_Load failed");
	int kept = 0;
	for (int i = 0; i < 5; i++)
		kept += xc_track_strings(lines[i]);
	const int breaks = xc_track_strings("\n"), any = xc_track_any_strings();
	printf("  SX4: %d of the 5 line strings read by the first load are still allocated; %d line break string(s);"
	       " %d string(s) in all\n", kept, breaks, any);
	XC_CHECK(kept == 0, "SX4: %d line string(s) from the first PE_Load were not freed", kept);
	XC_CHECK(breaks == 0, "SX4: %d line break string(s) from the first PE_Load were not freed", breaks);
	XC_CHECK(any == 0, "SX4: %d string(s) in all from the first PE_Load were not freed", any);
	sx_free_page(saved);
}

static void sx5_single(void)
{
	sx_setup(true);
	sx_roundtrip(8501, SX_JIA SX_CE "AB", "SX5 one line, GBK rule");
	sx_roundtrip(8502, "", "SX5 empty text, GBK rule");
	gk_force_sjis();
	sx_roundtrip(8503, "AB" SX_A_SJ, "SX5 one line, SJIS rule");
	sx_roundtrip(8504, "", "SX5 empty text, SJIS rule");
}

static void sx_v13(int no, const char *text, const char *what)
{
	const int version = ain->version;
	ain->version = 13;
	struct page *probe_page = NULL;
	struct string *s = cstr_to_string(text);
	PE_SetText(no, s, 1);
	free_string(s);
	XC_CHECK(PE_Save(&probe_page) && probe_page && ca_save_version(probe_page) == 3,
		 "%s: the save is not XPE version 3", what);
	sx_free_page(probe_page);
	sx_roundtrip(no, text, what);
	ain->version = version;
}

static void sx6_v13_lines(void)
{
	sx_setup(false);
	sx_v13(8601, "AB\nC\n" SX_A_SJ, "SX6 v13, three lines");
}

static void sx7_v13_single(void)
{
	sx_setup(false);
	sx_v13(8701, "ABC" SX_A_SJ, "SX7 v13, one line");
}

static void sx8_layout(void)
{
	sx_setup(false);
	struct string *s = cstr_to_string("AB\nC\nD");
	XC_CHECK(PE_SetText(8801, s, 1), "SX8: SetText failed");
	free_string(s);
	struct page *saved = NULL;
	XC_CHECK(PE_Save(&saved) && saved, "SX8: PE_Save failed");
	if (!saved)
		return;
	static const int want[] = { 3, 'A', 'B', 0, 'C', 0, 'D', 0 };
	const int n = sizeof want / sizeof want[0];
	int found = 0, breaks = 0;
	for (int i = 0; i + n <= saved->nr_vars; i++) {
		int k = 0;
		while (k < n && saved->values[i + k].i == want[k])
			k++;
		found += k == n;
	}
	for (int i = 0; i < saved->nr_vars; i++)
		breaks += saved->values[i].i == '\n';
	printf("  SX8: the lines found %d time(s) in the %d words; %d line break word(s)\n", found, saved->nr_vars,
	       breaks);
	XC_CHECK(found == 1, "SX8: the save holds the three lines %d time(s), want once", found);
	XC_CHECK(breaks == 0, "SX8: the save holds %d line break word(s)", breaks);
	sx_free_page(saved);
}

static int test_save_text_lines(void)
{
	static const struct xc_case cases[] = {
		{ "SX1", sx1_gbk }, { "SX2", sx2_sjis }, { "SX3", sx3_breaks }, { "SX4", sx4_freed },
		{ "SX5", sx5_single }, { "SX6", sx6_v13_lines }, { "SX7", sx7_v13_single }, { "SX8", sx8_layout },
	};
	return xc_run("SAVE-TEXT-LINES", cases, sizeof cases / sizeof cases[0], 60);
}

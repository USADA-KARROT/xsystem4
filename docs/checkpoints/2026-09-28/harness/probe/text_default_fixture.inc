/* text-default: a v14 pactex text state starts with its 文本 (native text
 * state loader 0x5c2d20 reads it at 0x5c2fac, default "", into the state's
 * text +0xd4). The 春銷 count labels (人材 ／, 顧客 ／) and 剩餘時間 are
 * such fixed texts; the AIN never sets them, so without the default they
 * were blank while the numbers next to them showed.
 *
 *   TD1 Each of the three named states keeps its own 文本 (普通 "人材",
 *       オン指針 "B"); an empty 文本 leaves its state empty.
 *   TD2 Exact key: a 文本位置 leaf before 文本 (both start with 文本) does
 *       not hide the text; a state with only 文本位置／文本裝飾 stays empty.
 *   TD3 The SJIS key テキスト is read too; the text state carries the
 *       state's 文本裝飾 (size 14) and holds the laid-out text (ASCII, so both
 *       character rules split it the same way). Whether the style or the text
 *       came first is not observable here: a later font change re-lays out
 *       the text, and the probe's glyph-less font has size-independent
 *       advances.
 *   TD4 The AIN still owns the text: Parts_SetText (the call behind
 *       parts::detail::SetPartsText) replaces a placeholder default and
 *       Parts_AddPartsText appends to it.
 *   TD5 A parts whose text state has no 文本 key keeps an empty text.
 *
 * Laying text out creates per-character textures, so each case runs in a
 * fork with a windowless CGL context and the text-metrics font whose glyphs
 * never load (nothing is drawn). Only functions that exist before the fix
 * are used, so before-check builds the old tree.
 */
#include <OpenGL/OpenGL.h>

static int td_failures;
#define TD_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); td_failures++; } } while (0)

#define TD_NORMAL "\xc6\xd5\xcd\xa8\xa0\xee\x91\x42"			/* 普通狀態 */
#define TD_ON "\xa5\xaa\xa5\xf3\xd6\xb8\xe1\x98\xa0\xee\x91\x42"	/* オン指針狀態 */
#define TD_DOWN "\xa5\xad\xa9\x60\xa5\xc0\xa5\xa6\xa5\xf3\xa0\xee\x91\x42"	/* キーダウン狀態 */
#define TD_TEXT_TYPE "\xce\xc4\xb1\xbe\xb2\xbf\xbc\xfe"			/* 文本部件 */
#define TD_TEXT "\xce\xc4\xb1\xbe"					/* 文本 */
#define TD_TEXT_SJIS "\x83\x65\x83\x4c\x83\x58\x83\x67"			/* テキスト */
#define TD_TEXT_POS "\xce\xc4\xb1\xbe\xce\xbb\xd6\xc3"			/* 文本位置 */
#define TD_DECO "\xce\xc4\xb1\xbe\xd1\x62\xef\x97"			/* 文本裝飾 */
#define TD_SIZE "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xb5\xa5\xa4\xa5\xba"	/* フォントサイズ */
#define TD_JINZAI "\xc8\xcb\xb2\xc4"					/* 人材 */
#define TD_PLACEHOLDER "\xb2\xbf\xbc\xfe\xce\xc4\xb1\xbe"		/* 部件文本 */

/* A text state: 部件タイプ = 文本部件 plus the given leaves/branches. */
static struct ex_tree td_state(const char *name, int n)
{
	struct ex_tree s = bu_branch(name, 1 + n);
	s.children[0] = bu_sleaf(BU_TYPE, TD_TEXT_TYPE);
	return s;
}
static struct ex_tree td_deco(int size)
{
	struct ex_tree d = bu_branch(TD_DECO, 1);
	d.children[0] = bu_ileaf(TD_SIZE, size);
	return d;
}
/* A low-level component whose 種類別情報 holds the given states. */
static struct ex_tree td_comp(const char *name, int nstates, struct ex_tree *states)
{
	struct ex_tree t = bu_branch(name, 1);
	t.children[0] = bu_branch(BU_INFO, 1 + nstates);
	t.children[0].children[0] = bu_sleaf(BU_TYPE, BU_LOW);
	for (int i = 0; i < nstates; i++)
		t.children[0].children[1 + i] = states[i];
	return t;
}

static const char *td_names[] = { "Label", "PosFirst", "NoText", "SjisKey", "Placeholder", "Empty" };
#define TD_N 6

static void td_load(int nums[TD_N])
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("TextDefaultActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("TextDefaultActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("TextDefaultRoot", BU_LAYOUT, 0, TD_N);
	struct ex_tree *kids = &top->children[0].children[1];

	struct ex_tree label[3];
	label[0] = td_state(TD_NORMAL, 2);
	label[0].children[1] = bu_sleaf(TD_TEXT, TD_JINZAI);
	label[0].children[2] = td_deco(14);
	label[1] = td_state(TD_ON, 1);
	label[1].children[1] = bu_sleaf(TD_TEXT, "B");
	label[2] = td_state(TD_DOWN, 1);
	label[2].children[1] = bu_sleaf(TD_TEXT, "");
	kids->children[0] = td_comp(td_names[0], 3, label);

	struct ex_tree pos[1];
	pos[0] = td_state(TD_NORMAL, 3);
	pos[0].children[1] = bu_ileaf(TD_TEXT_POS, 7);
	pos[0].children[2] = td_deco(14);
	pos[0].children[3] = bu_sleaf(TD_TEXT, "3/3");
	kids->children[1] = td_comp(td_names[1], 1, pos);

	struct ex_tree none[1];
	none[0] = td_state(TD_NORMAL, 2);
	none[0].children[1] = bu_ileaf(TD_TEXT_POS, 7);
	none[0].children[2] = td_deco(14);
	kids->children[2] = td_comp(td_names[2], 1, none);

	struct ex_tree sjis[1];
	sjis[0] = td_state(TD_NORMAL, 2);
	sjis[0].children[1] = td_deco(14);
	sjis[0].children[2] = bu_sleaf(TD_TEXT_SJIS, "ABC");
	kids->children[3] = td_comp(td_names[3], 1, sjis);

	struct ex_tree ph[1];
	ph[0] = td_state(TD_NORMAL, 1);
	ph[0].children[1] = bu_sleaf(TD_TEXT, TD_PLACEHOLDER);
	kids->children[4] = td_comp(td_names[4], 1, ph);

	struct ex_tree empty[1];
	empty[0] = td_state(TD_NORMAL, 0);
	kids->children[5] = td_comp(td_names[5], 1, empty);

	struct string *name = cstr_to_string("TextDefaultFixture");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name);
	ex_free(e);
	TD_CHECK(loaded, "synthetic activity did not load");
	int act = sf_str("TextDefaultFixture", -1);
	for (int i = 0; i < TD_N; i++) {
		nums[i] = bu_number(act, td_names[i]);
		TD_CHECK(nums[i] > 0, "%s has no parts number", td_names[i]);
	}
	heap_unref(act);
}

/* Text of a state (0-based), or NULL when the state is not a text state. */
static char *td_text(int no, int state)
{
	struct parts *p = no > 0 ? parts_try_get(no) : NULL;
	if (!p || p->states[state].type != PARTS_TEXT)
		return NULL;
	struct string *s = parts_text_get(&p->states[state].text);
	char *r = strdup(s->text);
	free_string(s);
	return r;
}
static void td_expect(int no, int state, const char *want, const char *what)
{
	char *got = td_text(no, state);
	TD_CHECK(got && !strcmp(got, want), "%s: text \"%s\", want \"%s\"",
		 what, got ? display_sjis0(got) : "(not a text state)", display_sjis1(want));
	free(got);
}

/* Windowless GL context and a font that never loads glyphs. */
static void td_setup(void)
{
	CGLPixelFormatAttribute attrs[] = { kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy, 0 };
	CGLPixelFormatObj pf = NULL;
	GLint npf = 0;
	CGLContextObj ctx = NULL;
	if (CGLChoosePixelFormat(attrs, &pf, &npf) || !pf || CGLCreateContext(pf, NULL, &ctx) || CGLSetCurrentContext(ctx)) {
		printf("TEXT-DEFAULT cannot create an offscreen GL context\n");
		exit(2);
	}
	tm_noglyph = (struct font) {
		.charmap = CHARMAP_UNICODE,
		.get_size = tm_noglyph_get_size,
		.get_actual_size = tm_noglyph_actual_size,
		.get_actual_size_round_down = tm_noglyph_actual_size,
		.get_glyph = tm_noglyph_get_glyph,
		.size_char = tm_noglyph_size_char,
		.size_char_kerning = tm_noglyph_kerning,
	};
	font_ttf[0] = font_ttf[1] = &tm_noglyph;
	bu_postlink();
}

static void td_states(void)
{
	td_setup();
	int n[TD_N];
	td_load(n);
	td_expect(n[0], 0, TD_JINZAI, "TD1 Label normal");
	td_expect(n[0], 1, "B", "TD1 Label on-cursor");
	td_expect(n[0], 2, "", "TD1 Label key-down (empty 文本)");
}

static void td_exact_key(void)
{
	td_setup();
	int n[TD_N];
	td_load(n);
	td_expect(n[1], 0, "3/3", "TD2 文本 after 文本位置");
	td_expect(n[2], 0, "", "TD2 only 文本位置/文本裝飾");
}

static void td_sjis_style(void)
{
	td_setup();
	int n[TD_N];
	td_load(n);
	td_expect(n[3], 0, "ABC", "TD3 SJIS テキスト");
	/* ASCII so the character split is the same under the SJIS and GBK rules
	 * (TD1 covers the GBK text itself). */
	struct parts *p = parts_try_get(n[3]);
	if (p && p->states[0].type == PARTS_TEXT) {
		struct parts_text *t = &p->states[0].text;
		TD_CHECK(t->ts.size == 14, "TD3 style size %g, want 14", (double)t->ts.size);
		TD_CHECK(t->nr_lines == 1 && t->lines[0].nr_chars == 3,
			 "TD3 ABC laid out as %d line(s), %d char(s); want 1, 3",
			 (int)t->nr_lines, t->nr_lines ? t->lines[0].nr_chars : 0);
	}
}

static void td_ain_owns(void)
{
	td_setup();
	int n[TD_N];
	td_load(n);
	td_expect(n[4], 0, TD_PLACEHOLDER, "TD4 placeholder default");
	int x = heap_alloc_string(make_string("X", 1));
	BU_PE("Parts_SetText", BU_I(n[4]), BU_I(x), BU_I(1));
	td_expect(n[4], 0, "X", "TD4 after Parts_SetText");
	int y = heap_alloc_string(make_string("Y", 1));
	BU_PE("Parts_AddPartsText", BU_I(n[4]), BU_I(y), BU_I(1));
	td_expect(n[4], 0, "XY", "TD4 after Parts_AddPartsText");
	heap_unref(x);
	heap_unref(y);
}

static void td_empty(void)
{
	td_setup();
	int n[TD_N];
	td_load(n);
	td_expect(n[5], 0, "", "TD5 no 文本 key");
}

static int test_text_default(void)
{
	void (*cases[])(void) = { td_states, td_exact_key, td_sjis_style, td_ain_owns, td_empty };
	const char *names[] = { "TD1", "TD2", "TD3", "TD4", "TD5" };
	int failed = 0;
	for (int i = 0; i < 5; i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			td_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(td_failures ? 1 : 0);
		}
		int st = 0;
		waitpid(pid, &st, 0);
		bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("TEXT-DEFAULT %s: %d of 5 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 1 : 0;
}

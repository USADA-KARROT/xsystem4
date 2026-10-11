/* read_color_fixture.inc — the colour of message window text that was read
 * before (2026-10-11). Mode: read-color.
 *
 * Original (static disassembly of the one executable): SetMessageWindowText
 * 0x595610 -> 0x4f2a60 -> 0x4eea50 decides the window's read flag (+0x201)
 * from the message number through MsgSkip's GetFlag (0x4c7700), or from the
 * ADV flags when a function name is given, and leaves it for a number of 0
 * or less without a name; the same text under the same number changes
 * nothing (0x4f2ad3). The text shown is cleared at once (0x4f2b44) and set
 * again by the next update (0x4f25e0) or Fix (0x4f1f80), which build the
 * window in 0x4f2110: with the flag set, the text font is copied and its
 * colour replaced by what the script's
 * message::detail::GetReadMessageTextColor(ref int, ref int, ref int) makes
 * of the font's own red, green and blue (0x4f24e0: values outside 0..255
 * become 0 or 255, alpha 255, the edge colour stays).
 *
 * Real AIN, real ffi, the script's own bytecode (GetReadMessageTextColor,
 * config::detail::GetReadTextColorMode and GetReadTextColor, EX_AInt) and
 * the real MainEXFile.AInt over a synthetic EX file; the read table is the
 * MsgSkip library started as init_libraries does, in a private save folder.
 * Colours are read from the glyph textures the window lays out with the
 * repo's font, and in RC8 from a drawing through the parts shader. Every
 * case runs in a forked child. No game assets: the texts are "H", the EX
 * colours are made up (RC13 reads the game's own EX file from XS4_GAME and
 * compares with what that file says, without a number of it written here).
 *
 * RC1  a read message is drawn in the script's colour, an unread one in the
 *      font's; the edge keeps its colour; two windows in one update; the
 *      callback runs once per build and not again without a change; no heap
 *      slot is left behind.
 * RC2  the setting off: the script leaves the three values, so the colour is
 *      the font's own (the callback still runs); on again after the text is
 *      cleared and set as the script's DrawAll does; no EX entry gives the
 *      script's defaults.
 * RC3  a number of 0 or less without a function name keeps the flag
 *      (0x4eeacf); two calls before one update: the last number decides;
 *      numbers outside the table are unread.
 * RC4  the same text under the same number is not looked up again; another
 *      text or another number is.
 * RC5  a function name takes the ADV flags instead (0x4eeaee).
 * RC6  values outside 0..255 (0x4f255b).
 * RC7  nothing of new text is shown before it is built, whatever the text
 *      speed, and the script is not asked while drawing; an update or Fix
 *      asks it.
 * RC8  with the fade: the first frame is already in the read colour, half
 *      faded in the drawing, the times as before; fixed text stays fixed
 *      when the same text is set again.
 * RC9  guard, AIN v13: no lookup, no callback, shown at once.
 * RC10 the callback frees the window, or frees it and makes another under
 *      the same number: nothing of the old one is touched; unread text the
 *      callback sets in the window is not given the colour.
 * RC11 across runs: unread at first, read once the flag is set and the text
 *      shown again, and read after the table was written and loaded again.
 * RC12 guard: a script function of another shape is not called.
 * RC13 the game's own EX file: the colour is the one its list holds.
 */
#include <sys/wait.h>
#include <unistd.h>

static int rc_failures;
#define RC_CHECK(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); puts(""); rc_failures++; } } while (0)

/* Ｅ＿已讀信息色, the EX list config::detail::GetReadTextColor reads. */
#define RC_EX_COLOR "\xa3\xc5\xa3\xdf\xd2\xd1\xd7\x78\xd0\xc5\xcf\xa2\xc9\xab"
/* CASConfigData: member 13 is what config::detail::GetReadTextColorMode returns. */
enum { RC_MODE_MEMBER = 13 };
enum { RC_A = 9830, RC_B = 9831, RC_C = 9832 };
/* The made-up read colour, the font's colour in most cases, and its edge. */
#define RC_READ 200, 150, 50
#define RC_WHITE 255, 255, 255
#define RC_EDGE 10, 20, 30

static int rc_fno;

static struct page *rc_config(void)
{
	int g = -1;
	for (int i = 0; i < ain->nr_globals; i++)
		if (!strcmp(ain->globals[i].name, "g_AFConfig"))
			g = i;
	return heap[heap[global_page_slot].page->values[g].i].page;
}

/* A synthetic EX file: the colour list, or (have false) a list of another name. */
static void rc_ex(bool have, int r, int g, int b)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string(have ? RC_EX_COLOR : "ReadColorNothing");
	struct ex_list *l = xcalloc(1, sizeof(*l));
	const int v[3] = { r, g, b };
	l->nr_items = 3;
	l->items = xcalloc(3, sizeof(*l->items));
	for (int i = 0; i < 3; i++)
		l->items[i].value = (struct ex_value){ .type = EX_INT, .i = v[i] };
	e->blocks[0].val = (struct ex_value){ .type = EX_LIST, .list = l };
	probe_mainex_set(e);
}

/* The GL context, the game's globals, the setting, the repo's font, the read
 * table and an EX file with the made-up colour. */
static void rc_setup(bool mode, bool render)
{
	df_setup(.5f);
	if (render)
		ts_render_setup();
	ts_real_fonts();
	rc_config()->values[RC_MODE_MEMBER].i = mode;
	rc_fno = ain_get_function(ain, "message::detail::GetReadMessageTextColor");
	if (rc_fno < 0)
		cn_env_fail("the AIN has no message::detail::GetReadMessageTextColor");
	rc_ex(true, RC_READ);
	mk_start();
}

/* A font of the given colour with the edge RC_EDGE, and a text speed. */
static void rc_font(int no, int r, int g, int b, float edge, int speed)
{
	PE_SetMessageWindowTextFont(no, 0, 24, r, g, b, 0, RC_EDGE, edge);
	BU_PE("SetMessageWindowTextSpeed", BU_I(no), BU_I(speed));
}

static void rc_text_adv(int no, const char *s, int msg, const char *fn, int ver, int step)
{
	BU_PE("SetMessageWindowText", BU_I(no), BU_I(sf_str(s, -1)), BU_I(msg), BU_I(sf_str(fn, -1)), BU_I(ver),
	      BU_I(step));
}

/* The script's way of showing a page again: cleared, then set. */
static void rc_show(int no, const char *s, int msg)
{
	df_text(no, "", 0);
	df_text(no, s, msg);
}

static struct parts_text *rc_drawn(int no)
{
	Point p;
	return parts_message_window_render_text(parts_get(no), &p);
}

/* How many opaque pixels of the window's glyph textures have exactly this
 * colour; -1 without a glyph. */
static int rc_count(int no, int r, int g, int b)
{
	struct parts_text *t = rc_drawn(no);
	int n = 0, glyphs = 0;
	for (unsigned l = 0; t && l < t->nr_lines; l++) {
		for (int c = 0; c < t->lines[l].nr_chars; c++) {
			Texture *tex = &t->lines[l].chars[c].t;
			if (!tex->handle || tex->w <= 0 || tex->h <= 0)
				continue;
			glyphs++;
			uint8_t *px = gfx_get_pixels(tex);
			for (int i = 0; i < tex->w * tex->h; i++)
				n += px[i * 4 + 3] == 255 && px[i * 4] == r && px[i * 4 + 1] == g && px[i * 4 + 2] == b;
			free(px);
		}
	}
	return glyphs ? n : -1;
}

/* The glyphs' bodies have the first colour and nothing has the second. */
static void rc_expect(int no, int r, int g, int b, int nr, int ng, int nb, const char *what, int line)
{
	int have = rc_count(no, r, g, b), other = rc_count(no, nr, ng, nb);
	if (have < 20 || other != 0) {
		printf("  FAIL line %d: %s: %d opaque pixel(s) of (%d,%d,%d), %d of (%d,%d,%d); want at least 20 and 0\n", line,
		       what, have, r, g, b, other, nr, ng, nb);
		rc_failures++;
	}
}
#define RC_IS_READ(no, what) rc_expect(no, RC_READ, RC_WHITE, what, __LINE__)
#define RC_IS_WHITE(no, what) rc_expect(no, RC_WHITE, RC_READ, what, __LINE__)

static float rc_alpha(int no, int i)
{
	struct parts_text *t = rc_drawn(no);
	return t ? df_alpha(no, t, i) : -1;
}

static void rc_near(float got, float want, int line)
{
	if (fabsf(got - want) >= 0.001f) {
		printf("  FAIL line %d: alpha %.4f, want %.4f\n", line, got, want);
		rc_failures++;
	}
}
#define RC_NEAR(got, want) rc_near(got, want, __LINE__)

/* --- RC1 --- */
static void rc_read_unread(void)
{
	rc_setup(true, false);
	mk_set(40);
	rc_font(RC_A, RC_WHITE, 2, 0);
	rc_font(RC_B, RC_WHITE, 2, 0);
	df_text(RC_A, "H", 40);
	df_text(RC_B, "H", 41);
	unsigned long n = calls[rc_fno];
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "a read message");
	RC_IS_WHITE(RC_B, "an unread message in the same update");
	/* 0x4f24e0 leaves the edge colour (+0x24). */
	RC_CHECK(rc_count(RC_A, RC_EDGE) > 0 && rc_count(RC_B, RC_EDGE) > 0, "edge pixels: %d of the read text, %d of the unread; want some of both",
		 rc_count(RC_A, RC_EDGE), rc_count(RC_B, RC_EDGE));
	RC_CHECK(calls[rc_fno] == n + 1, "the script's colour function ran %lu time(s) for one read and one unread window, want 1",
		 calls[rc_fno] - n);
	RC_CHECK(df_fixed(RC_A) && df_fixed(RC_B), "text of speed 0 is not fixed after its update");
	parts_message_window_update(0);
	parts_message_window_update(16);
	RC_CHECK(calls[rc_fno] == n + 1, "updates without a change asked for the colour again (%lu)", calls[rc_fno] - n);
	RC_IS_READ(RC_A, "the read message after two more updates");
	/* The window's own font is not changed (0x4f2244 works on a copy): an
	 * unread message in the same window is in the font's colour again. */
	df_text(RC_A, "H", 41);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "an unread message after a read one in the same window");
	RC_CHECK(calls[rc_fno] == n + 1, "the colour was asked for an unread message");
	df_text(RC_A, "HH", 40);
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "a read message after an unread one");
	/* The three integers the script gets by reference are released. */
	struct string *h = cstr_to_string("H");
	for (int i = 0; i < 3; i++) {
		PE_SetMessageWindowText(RC_A, &EMPTY_STRING, 0, NULL, 0, 0);
		PE_SetMessageWindowText(RC_A, h, 40, NULL, 0, 0);
		parts_message_window_update(0);
	}
	size_t live = live_slots();
	int sp = stack_ptr;
	n = calls[rc_fno];
	for (int i = 0; i < 40; i++) {
		PE_SetMessageWindowText(RC_A, &EMPTY_STRING, 0, NULL, 0, 0);
		PE_SetMessageWindowText(RC_A, h, 40, NULL, 0, 0);
		parts_message_window_update(0);
	}
	free_string(h);
	RC_CHECK(calls[rc_fno] >= n + 40, "40 read texts asked for the colour %lu time(s)", calls[rc_fno] - n);
	RC_CHECK(live_slots() == live, "40 read texts left %ld heap slot(s) behind", (long)live_slots() - (long)live);
	RC_CHECK(stack_ptr == sp, "40 read texts left %d value(s) on the stack", stack_ptr - sp);
	RC_IS_READ(RC_A, "the read message after 40 rounds");
}

/* --- RC2 --- */
static void rc_setting(void)
{
	rc_setup(false, false);
	mk_set(40);
	/* A font that is not white: the script gets the font's own red, green
	 * and blue and, with the setting off, leaves them (FUNC 2). */
	rc_font(RC_A, 100, 110, 120, 0, 0);
	df_text(RC_A, "H", 40);
	unsigned long n = calls[rc_fno];
	parts_message_window_update(0);
	rc_expect(RC_A, 100, 110, 120, RC_READ, "a read message with the setting off", __LINE__);
	RC_CHECK(rc_count(RC_A, RC_WHITE) == 0 && rc_count(RC_A, 0, 0, 0) == 0, "the setting off gave white or black pixels (%d, %d)",
		 rc_count(RC_A, RC_WHITE), rc_count(RC_A, 0, 0, 0));
	/* The flag alone decides the callback (0x4f223c); the setting is the script's. */
	RC_CHECK(calls[rc_fno] == n + 1, "the colour function ran %lu time(s) with the setting off, want 1", calls[rc_fno] - n);
	rc_config()->values[RC_MODE_MEMBER].i = 1;
	rc_show(RC_A, "H", 40);
	BU_PE("FixMessageWindowText", BU_I(RC_A));
	rc_expect(RC_A, RC_READ, 100, 110, 120, "the setting on, the page shown again", __LINE__);
	rc_config()->values[RC_MODE_MEMBER].i = 0;
	rc_show(RC_A, "H", 40);
	BU_PE("FixMessageWindowText", BU_I(RC_A));
	rc_expect(RC_A, 100, 110, 120, RC_READ, "the setting off again", __LINE__);
	/* Without the EX list the script's defaults: 255, 127, 127 (FUNC 4019). */
	rc_config()->values[RC_MODE_MEMBER].i = 1;
	rc_ex(false, 1, 2, 3);
	rc_show(RC_A, "H", 40);
	parts_message_window_update(0);
	rc_expect(RC_A, 255, 127, 127, 100, 110, 120, "no EX entry", __LINE__);
}

/* --- RC3 --- */
static void rc_keep_flag(void)
{
	rc_setup(true, false);
	mk_set(40);
	rc_font(RC_A, RC_WHITE, 0, 0);
	df_text(RC_A, "H", 40);
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "a read message");
	/* The script's Clear: ("", 0, "", 0, 0). */
	df_text(RC_A, "", 0);
	parts_message_window_update(0);
	df_text(RC_A, "H", 0);
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "text under number 0 after a read message");
	df_text(RC_A, "HH", -1);
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "text under number -1 after a read message");
	df_text(RC_A, "H", 41);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "an unread message");
	rc_show(RC_A, "H", 0);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "text under number 0 after an unread message");
	/* Two lines of a page set before one update: the window has one flag,
	 * the last number's. */
	df_text(RC_A, "", 0);
	df_text(RC_A, "H", 40);
	df_text(RC_A, "H\nH", 41);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "a read line followed by an unread one");
	df_text(RC_A, "", 0);
	df_text(RC_A, "H", 41);
	df_text(RC_A, "H\nH", 40);
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "an unread line followed by a read one");
	/* Outside the table (0x4c7714 .. 0x4c771b). */
	df_text(RC_A, "H", ain->nr_messages);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "the number one past the table");
	df_text(RC_A, "HH", INT_MAX);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "INT_MAX");
}

/* --- RC4 --- */
static void rc_same_text(void)
{
	rc_setup(true, false);
	rc_font(RC_A, RC_WHITE, 0, 0);
	df_text(RC_A, "H", 41);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "an unread message");
	mk_set(41);
	unsigned long n = calls[rc_fno];
	df_text(RC_A, "H", 41);
	RC_CHECK(df_fixed(RC_A), "the same text under the same number made the window dirty");
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "the same text under the same number, read meanwhile");
	RC_CHECK(calls[rc_fno] == n, "the same text under the same number asked for the colour");
	df_text(RC_A, "HH", 41);
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "another text under the number read meanwhile");
	/* Only the number differs. */
	rc_font(RC_B, RC_WHITE, 0, 0);
	df_text(RC_B, "H", 42);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_B, "window B unread");
	mk_set(43);
	df_text(RC_B, "H", 43);
	parts_message_window_update(0);
	RC_IS_READ(RC_B, "the same text under a read number");
	df_text(RC_B, "H", 42);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_B, "the same text under the unread number again");
}

/* --- RC5 --- */
static void rc_adv(void)
{
	rc_setup(true, false);
	mk_set(40);
	mk_set_adv("rc_fn", 3, 5);
	rc_font(RC_A, RC_WHITE, 0, 0);
	static const struct { int msg; const char *fn; int ver, step; bool read; const char *what; } c[] = {
		{ 0, "rc_fn", 3, 5, true, "the recorded step" },
		{ 0, "rc_fn", 3, 4, true, "a step before the recorded one" },
		{ 0, "rc_fn", 3, 6, false, "a step past the recorded one" },
		{ 0, "rc_fn", 4, 1, false, "another version" },
		{ 0, "rc_other", 3, 1, false, "a name that is not recorded" },
		/* The name decides also when the number says otherwise. */
		{ 40, "rc_fn", 3, 6, false, "a read number with a step past the recorded one" },
		{ 41, "rc_fn", 3, 5, true, "an unread number with the recorded step" },
		{ 40, "", 3, 6, true, "a read number without a name" },
	};
	for (unsigned i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
		/* Another text each time, so that nothing is skipped as the same. */
		rc_text_adv(RC_A, i % 2 ? "H" : "HH", c[i].msg, c[i].fn, c[i].ver, c[i].step);
		parts_message_window_update(0);
		if (c[i].read)
			RC_IS_READ(RC_A, c[i].what);
		else
			RC_IS_WHITE(RC_A, c[i].what);
	}
	/* 0 or less and no name: kept, here from an ADV decision. */
	rc_text_adv(RC_A, "H", 0, "rc_fn", 3, 5);
	rc_text_adv(RC_A, "HH", 0, "", 0, 0);
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "number 0 without a name after an ADV step that was read");
}

/* --- RC6 --- */
static void rc_clamp(void)
{
	rc_setup(true, false);
	mk_set(40);
	rc_font(RC_A, RC_WHITE, 0, 0);
	static const int ex[][3] = { { -5, 300, 128 }, { 256, -1, 255 }, { 0, 255, 1 }, { INT_MIN, INT_MAX, 77 } };
	static const int want[][3] = { { 0, 255, 128 }, { 255, 0, 255 }, { 0, 255, 1 }, { 0, 255, 77 } };
	for (int i = 0; i < 4; i++) {
		rc_ex(true, ex[i][0], ex[i][1], ex[i][2]);
		rc_show(RC_A, "H", 40);
		parts_message_window_update(0);
		char what[64];
		snprintf(what, sizeof what, "EX colour (%d,%d,%d)", ex[i][0], ex[i][1], ex[i][2]);
		rc_expect(RC_A, want[i][0], want[i][1], want[i][2], RC_WHITE, what, __LINE__);
	}
}

/* --- RC7 --- */
static void rc_draw_before_update(void)
{
	rc_setup(true, false);
	mk_set(40);
	static const int speeds[] = { 0, 23 };
	for (int i = 0; i < 2; i++) {
		int no = RC_A + i;
		rc_font(no, RC_WHITE, 0, speeds[i]);
		BU_PE("FixMessageWindowText", BU_I(no));
		df_text(no, "H", 40);
		unsigned long n = calls[rc_fno];
		/* A drawing before the update: the original has emptied the text
		 * it shows (0x4f2b44) and builds it in the update. */
		RC_CHECK(rc_alpha(no, 0) == 0, "speed %d: new read text is drawn with alpha %g before it is built, want 0", speeds[i],
			 rc_alpha(no, 0));
		RC_CHECK(calls[rc_fno] == n, "speed %d: the drawing asked the script for the colour", speeds[i]);
		RC_CHECK(!df_fixed(no), "speed %d: fixed before it is built", speeds[i]);
		parts_message_window_update(0);
		RC_CHECK(calls[rc_fno] == n + 1, "speed %d: the update asked for the colour %lu time(s), want 1", speeds[i],
			 calls[rc_fno] - n);
		RC_IS_READ(no, "read text after its update");
		RC_CHECK(rc_alpha(no, 0) == (speeds[i] ? 0 : 1), "speed %d: alpha %g after the update at time 0", speeds[i], rc_alpha(no, 0));
		/* Unread text waits for its build as well. */
		df_text(no, "HH", 41);
		RC_CHECK(rc_alpha(no, 0) == 0 && rc_alpha(no, 1) == 0, "speed %d: new unread text is drawn before it is built (alpha %g)",
			 speeds[i], rc_alpha(no, 0));
		/* Fix builds it too, and asks. */
		df_text(no, "H", 40);
		n = calls[rc_fno];
		RC_CHECK(rc_alpha(no, 0) == 0, "speed %d: read text set again is drawn before it is built", speeds[i]);
		BU_PE("FixMessageWindowText", BU_I(no));
		RC_CHECK(calls[rc_fno] == n + 1, "speed %d: Fix asked for the colour %lu time(s), want 1", speeds[i], calls[rc_fno] - n);
		RC_CHECK(rc_alpha(no, 0) == 1 && df_fixed(no), "speed %d: after Fix alpha %g, fixed %d", speeds[i], rc_alpha(no, 0),
			 df_fixed(no));
		RC_IS_READ(no, "read text after Fix");
	}
}

/* The most opaque pixel of a drawing. */
static void rc_peak(uint8_t *px, int c[4])
{
	c[3] = -1;
	for (int i = 0; i < TS_VIEW_W * TS_VIEW_H; i++) {
		if (px[i * 4 + 3] > c[3]) {
			for (int k = 0; k < 4; k++)
				c[k] = px[i * 4 + k];
		}
	}
	free(px);
}

/* --- RC8 --- */
static void rc_fade(void)
{
	rc_setup(true, true);
	mk_set(40);
	rc_font(RC_A, RC_WHITE, 0, 100);
	PE_SetPos(RC_A, 10, 20);
	df_text(RC_A, "HH", 40);
	PE_UpdateComponent(0);
	parts_message_window_update(0);
	/* The glyphs are in the read colour from the first frame of the fade. */
	RC_IS_READ(RC_A, "the first frame of the fade");
	RC_CHECK(rc_alpha(RC_A, 0) == 0 && rc_alpha(RC_A, 1) == 0 && !df_fixed(RC_A), "time 0: alpha %g, %g, fixed %d; want 0, 0, 0",
		 rc_alpha(RC_A, 0), rc_alpha(RC_A, 1), df_fixed(RC_A));
	int c[4];
	rc_peak(ts_draw(parts_get(RC_A)), c);
	RC_CHECK(c[3] == 0, "time 0: something is drawn (alpha %d)", c[3]);
	parts_message_window_update(100);
	RC_NEAR(rc_alpha(RC_A, 0), .5f);
	RC_NEAR(rc_alpha(RC_A, 1), 0);
	rc_peak(ts_draw(parts_get(RC_A)), c);
	printf("  RC8 half faded: (%d,%d,%d,%d)\n", c[0], c[1], c[2], c[3]);
	RC_CHECK(c[3] >= 124 && c[3] <= 131, "half faded: alpha %d, want about 128", c[3]);
	/* Red : green : blue is 200 : 150 : 50, straight or premultiplied. */
	RC_CHECK(c[0] >= 60 && abs(c[1] * 200 - c[0] * 150) <= c[0] * 8 && abs(c[2] * 200 - c[0] * 50) <= c[0] * 8,
		 "half faded: colour (%d,%d,%d) is not the read colour's hue", c[0], c[1], c[2]);
	parts_message_window_update(100);
	RC_NEAR(rc_alpha(RC_A, 0), 1);
	RC_NEAR(rc_alpha(RC_A, 1), .5f);
	parts_message_window_update(99);
	RC_CHECK(!df_fixed(RC_A), "fixed 1 ms before the last character is at full alpha");
	parts_message_window_update(1);
	RC_CHECK(df_fixed(RC_A), "not fixed at 300 ms");
	rc_peak(ts_draw(parts_get(RC_A)), c);
	RC_CHECK(abs(c[0] - 200) <= 1 && abs(c[1] - 150) <= 1 && abs(c[2] - 50) <= 1 && c[3] == 255,
		 "fully faded: (%d,%d,%d,%d), want the read colour, opaque", c[0], c[1], c[2], c[3]);
	/* A fade Fix has ended is not started again by the same text, and
	 * the script is not asked again. */
	rc_show(RC_A, "HH", 40);
	parts_message_window_update(50);
	RC_CHECK(!df_fixed(RC_A), "the page shown again is fixed at once");
	BU_PE("FixMessageWindowText", BU_I(RC_A));
	RC_CHECK(df_fixed(RC_A) && rc_alpha(RC_A, 1) == 1, "Fix: fixed %d, alpha %g", df_fixed(RC_A), rc_alpha(RC_A, 1));
	unsigned long n = calls[rc_fno];
	df_text(RC_A, "HH", 40);
	RC_CHECK(df_fixed(RC_A), "the same text again: not fixed");
	parts_message_window_update(16);
	RC_CHECK(df_fixed(RC_A) && rc_alpha(RC_A, 1) == 1 && calls[rc_fno] == n,
		 "the same text again and an update: fixed %d, alpha %g, %lu colour call(s); want 1, 1, 0", df_fixed(RC_A),
		 rc_alpha(RC_A, 1), calls[rc_fno] - n);
	RC_IS_READ(RC_A, "fixed text after the same text was set again");
}

/* --- RC9 --- */
static void rc_v13(void)
{
	rc_setup(true, false);
	mk_set(40);
	ain->version = 13;
	rc_font(RC_A, RC_WHITE, 0, 0);
	struct string *h = cstr_to_string("H");
	unsigned long n = calls[rc_fno];
	PE_SetMessageWindowText(RC_A, h, 40, NULL, 0, 0);
	free_string(h);
	RC_CHECK(rc_alpha(RC_A, 0) == 1, "v13: new text is drawn with alpha %g before an update, want 1", rc_alpha(RC_A, 0));
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "v13: a message whose flag is set");
	PE_FixMessageWindowText(RC_A);
	RC_CHECK(calls[rc_fno] == n, "v13: the script's colour function ran");
	/* The same when the update comes before any drawing: the query of
	 * the alpha above has laid the first text out already. */
	h = cstr_to_string("HH");
	PE_SetMessageWindowText(RC_A, h, 40, NULL, 0, 0);
	free_string(h);
	parts_message_window_update(0);
	RC_CHECK(calls[rc_fno] == n, "v13: an update before any drawing ran the script's colour function");
	RC_IS_WHITE(RC_A, "v13: text built by an update");
	ain->version = 14;
}

/* --- RC10 --- */
static int rc_victim, rc_remake;
static int rc_aint_release(possibly_unused struct string *name, possibly_unused int index, int dflt, possibly_unused int id)
{
	if (rc_victim) {
		int no = rc_victim;
		rc_victim = 0;
		struct string *h = cstr_to_string("HH");
		if (rc_remake == 2) {
			/* The same window gets unread text. */
			PE_SetMessageWindowText(no, h, 41, NULL, 0, 0);
			free_string(h);
			return dflt;
		}
		PE_ReleaseParts(no);
		if (rc_remake) {
			/* Another window under the same number, with unread text. */
			PE_SetMessageWindowTextFont(no, 0, 24, RC_WHITE, 0, RC_EDGE, 0);
			PE_SetMessageWindowTextSpeed(no, 0);
			PE_SetMessageWindowText(no, h, 41, NULL, 0, 0);
		}
		free_string(h);
	}
	return dflt;
}

static void rc_release(void)
{
	rc_setup(true, false);
	mk_set(40);
	lr_replace_hll("MainEXFile", "AInt", (void *)rc_aint_release);
	/* Freed in the callback of an update, with another read window after it. */
	rc_font(RC_A, RC_WHITE, 0, 0);
	rc_font(RC_B, RC_WHITE, 0, 0);
	df_text(RC_A, "H", 40);
	df_text(RC_B, "H", 40);
	rc_victim = RC_A;
	parts_message_window_update(0);
	RC_CHECK(!rc_victim && !parts_try_get(RC_A), "the callback did not run, or the window is still there");
	rc_expect(RC_B, 255, 127, 127, RC_WHITE, "the window after the freed one", __LINE__);
	/* Freed in the callback of Fix. */
	rc_font(RC_A, RC_WHITE, 0, 0);
	df_text(RC_A, "H", 40);
	rc_victim = RC_A;
	BU_PE("FixMessageWindowText", BU_I(RC_A));
	RC_CHECK(!rc_victim && !parts_try_get(RC_A), "Fix: the callback did not run, or the window is still there");
	/* Freed and made again under its number: the update that asked for
	 * the old window's colour leaves the new one alone. */
	rc_font(RC_C, RC_WHITE, 0, 0);
	df_text(RC_C, "H", 40);
	rc_victim = RC_C;
	rc_remake = 1;
	unsigned long n = calls[rc_fno];
	parts_message_window_update(0);
	RC_CHECK(!rc_victim && parts_try_get(RC_C), "the callback did not run, or made no window");
	RC_CHECK(rc_alpha(RC_C, 0) == 0, "the new window's text was built by the old one's update (alpha %g)", rc_alpha(RC_C, 0));
	parts_message_window_update(0);
	RC_CHECK(rc_alpha(RC_C, 0) == 1 && rc_alpha(RC_C, 1) == 1, "the new window's text is not shown after its own update");
	rc_expect(RC_C, RC_WHITE, 255, 127, 127, "the new window's unread text", __LINE__);
	RC_CHECK(calls[rc_fno] == n + 1, "the colour function ran %lu time(s), want 1 (the old window's)", calls[rc_fno] - n);
	/* The callback sets unread text in the window it was asked for: the
	 * colour it returns is not used for that text. */
	rc_font(RC_B, RC_WHITE, 0, 0);
	df_text(RC_B, "HHH", 40);
	rc_victim = RC_B;
	rc_remake = 2;
	parts_message_window_update(0);
	parts_message_window_update(0);
	RC_CHECK(!rc_victim, "the callback did not run");
	struct parts_text *t = rc_drawn(RC_B);
	RC_CHECK(t && t->nr_lines == 1 && t->lines[0].nr_chars == 2, "the text the callback set is not the one laid out");
	rc_expect(RC_B, RC_WHITE, 255, 127, 127, "unread text set by the callback", __LINE__);
}

/* --- RC11 --- */
static void rc_across_runs(void)
{
	rc_setup(true, false);
	rc_font(RC_A, RC_WHITE, 0, 0);
	df_text(RC_A, "H", 50);
	parts_message_window_update(0);
	RC_IS_WHITE(RC_A, "first run, first time");
	/* The script sets the flag when the page is cleared (FUNC 7069). */
	df_text(RC_A, "", 0);
	mk_set(50);
	parts_message_window_update(0);
	df_text(RC_A, "H", 50);
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "first run, shown again");
	RC_CHECK(!mk_exists(MK_MSK), "the table was written before the end");
	mk_stop();
	RC_CHECK(mk_exists(MK_MSK), "the table was not written at the end");
	/* The next run reads the file. */
	mk_start();
	rc_font(RC_B, RC_WHITE, 0, 0);
	rc_font(RC_C, RC_WHITE, 0, 0);
	df_text(RC_B, "H", 50);
	df_text(RC_C, "H", 51);
	parts_message_window_update(0);
	RC_IS_READ(RC_B, "second run, the message read in the first");
	RC_IS_WHITE(RC_C, "second run, its neighbour");
}

/* --- RC12 --- */
static void rc_shape_guard(void)
{
	rc_setup(true, false);
	mk_set(40);
	rc_font(RC_A, RC_WHITE, 0, 0);
	struct ain_function *f = &ain->functions[rc_fno];
	RC_CHECK(f->nr_args == 6 && f->return_type.data == AIN_VOID && f->vars[0].type.data == AIN_REF_INT
		 && f->vars[2].type.data == AIN_REF_INT && f->vars[4].type.data == AIN_REF_INT,
		 "the script function is not (ref int, ref int, ref int) -> void: %d argument slot(s)", f->nr_args);
	unsigned long n = calls[rc_fno];
	int text = 0;
#define RC_GUARD(what, change, restore) do { \
		change; \
		df_text(RC_A, ++text % 2 ? "H" : "HH", 40); \
		int sp = stack_ptr; \
		parts_message_window_update(0); \
		restore; \
		RC_CHECK(stack_ptr == sp, "%s: the update left %d value(s) on the stack", what, stack_ptr - sp); \
		RC_IS_WHITE(RC_A, what); \
		RC_CHECK(calls[rc_fno] == n, "%s: the function was called", what); \
		RC_CHECK(rc_alpha(RC_A, 0) == 1, "%s: the text is not shown", what); \
	} while (0)
	RC_GUARD("four argument slots", f->nr_args = 4, f->nr_args = 6);
	RC_GUARD("an int for the second reference", f->vars[2].type.data = AIN_INT, f->vars[2].type.data = AIN_REF_INT);
	RC_GUARD("a float reference first", f->vars[0].type.data = AIN_REF_FLOAT, f->vars[0].type.data = AIN_REF_INT);
	RC_GUARD("a return value", f->return_type.data = AIN_INT, f->return_type.data = AIN_VOID);
	uint32_t address = f->address;
	RC_GUARD("no code", f->address = ain->code_size, f->address = address);
#undef RC_GUARD
	df_text(RC_A, "HHH", 40);
	parts_message_window_update(0);
	RC_IS_READ(RC_A, "the function as the game declares it");
	RC_CHECK(calls[rc_fno] == n + 1, "the function as the game declares it was not called once");
}

/* --- RC13 --- */
static void rc_real_ex(void)
{
	rc_setup(true, false);
	mk_set(40);
	const char *game = getenv("XS4_GAME");
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/dohnadohnaEx.ex", game ? game : ".");
	struct ex *e = game ? ex_read_file(path) : NULL;
	if (!e)
		cn_env_fail("no EX file in XS4_GAME");
	struct ex_value *v = ex_get(e, RC_EX_COLOR);
	if (!v || v->type != EX_LIST || v->list->nr_items != 3 || v->list->items[0].value.type != EX_INT
	    || v->list->items[1].value.type != EX_INT || v->list->items[2].value.type != EX_INT)
		cn_env_fail("the game's EX file has no list of three integers under the read colour's name");
	int want[3];
	for (int i = 0; i < 3; i++)
		want[i] = v->list->items[i].value.i;
	if (want[0] < 0 || want[0] > 255 || want[1] < 0 || want[1] > 255 || want[2] < 0 || want[2] > 255
	    || (want[0] == 255 && want[1] == 255 && want[2] == 255) || (want[0] == 255 && want[1] == 127 && want[2] == 127))
		cn_env_fail("the game's read colour is white, the script's default or out of range: this case cannot tell");
	probe_mainex_set(e);
	rc_font(RC_A, RC_WHITE, 0, 0);
	df_text(RC_A, "H", 40);
	parts_message_window_update(0);
	rc_expect(RC_A, want[0], want[1], want[2], RC_WHITE, "the game's EX file", __LINE__);
	RC_CHECK(rc_count(RC_A, 255, 127, 127) == 0, "the script's default colour: its EX query found nothing");
}

static int test_read_color(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "RC1 read and unread", rc_read_unread },
		{ "RC2 the setting and the script's defaults", rc_setting },
		{ "RC3 numbers that keep the flag", rc_keep_flag },
		{ "RC4 the same text again", rc_same_text },
		{ "RC5 ADV flags", rc_adv },
		{ "RC6 values outside 0..255", rc_clamp },
		{ "RC7 nothing is shown before the build", rc_draw_before_update },
		{ "RC8 with the fade", rc_fade },
		{ "RC9 guard: AIN v13", rc_v13 },
		{ "RC10 the callback frees the window", rc_release },
		{ "RC11 across runs", rc_across_runs },
		{ "RC12 guard: another shape of the script function", rc_shape_guard },
		{ "RC13 the game's EX file", rc_real_ex },
	};
	const int nr = sizeof cases / sizeof cases[0];
	int failed = 0;
	sp_setup_dir();
	for (int i = 0; i < nr; i++) {
		printf("== %s\n", cases[i].name);
		fflush(NULL);
		pid_t p = fork();
		if (!p) {
			alarm(120);
			rc_failures = 0;
			cases[i].fn();
			fflush(NULL);
			_exit(rc_failures ? 1 : 0);
		}
		int s = 0;
		waitpid(p, &s, 0);
		chmod(sp_dir, 0700);
		sp_clear_dir();
		const char *result = "PASS";
		if (WIFSIGNALED(s))
			result = "FAIL (signal)";
		else if (WEXITSTATUS(s) == CN_ENV_EXIT)
			result = "FAIL (environment)";
		else if (WEXITSTATUS(s))
			result = "FAIL";
		if (WIFSIGNALED(s) || WEXITSTATUS(s))
			failed++;
		printf("%-52s %s\n", cases[i].name, result);
	}
	sp_remove_dir();
	printf("READ-COLOR %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, nr);
	return !!failed;
}

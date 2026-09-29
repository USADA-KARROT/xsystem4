/* text-metrics: text layout of the CN build (spacing D1-D4, 2026-09-29).
 *
 * The CN engine has no .fnl. It draws every character with GDI into a cell and
 * advances by the cell plus the character spacing; TextSurfaceManager.
 * GetFontWidth returns the same cell (0x69fb30 -> 0x69c7a0):
 *
 *   e       = max(min(ceil(太さ), size), min(ceil(縁取り), size))
 *   width   = size when the first byte is 0x81..0xFE, else (size+1)>>1
 *   cell    = width + 2e          advance = cell + character spacing
 *
 * For real pactex styles (message windows, name plates, backlog,
 * DungeonSelector, achievement notice, styles with 太さ) the style is set
 * through the real AIN declarations (Parts_SetFont, Parts_SetTextCharSpace,
 * Parts_SetPartsFontBoldWeight, SetMessageWindowTextFont/TextSpace), and then:
 *
 *   fw     GetFontWidth through the real AIN declaration and ffi, for one
 *          character, an empty text and a text of three characters
 *   cell   text_style_width, the texture width of one character of parts text
 *   size   gfx_size_text of "10萬"
 *   adv    what gfx_render_textf returns, the advance parts text lays out with.
 *          The probe has no GL context, so the fonts are swapped for one whose
 *          glyphs never load and nothing is drawn; the grid must still advance
 *          by the cell. The upstream path skips a glyph that fails to load,
 *          so under the SJIS rule this is 0 (pinned below).
 *   fb     the missing-glyph fallback: U+83C8 is not in VL Gothic but is in
 *          HanaMinA; only the CN configuration may draw it from HanaMinA
 *
 * The SJIS rule (the default probe state) must keep the upstream numbers: the
 * "SJIS " lines are the same before and after the fix, except the fallback
 * line (05d2441 turned the fallback on for every game, D4). GBK with an .fnl
 * is only checked for GetFontWidth, as the probe has no .fnl file.
 *
 * Each configuration runs in a forked child because gfx_font_init runs once per
 * process. The fonts are the repo's fonts/ (via $XS4_SRC). Only functions and
 * fields that exist before the fix are used, so before-check.sh can build it.
 * Requires gbk_chars_fixture.inc (gk_enable, gk_force_sjis). */
#include <math.h>
#include "gfx/font.h"
#include "parts_internal.h"

extern struct font *font_ttf[2];
extern void probe_init_parts_table(void);
bool probe_ft_font_fallback(struct font *font, uint32_t code, unsigned size); // build_probe.py

static int tm_fails;
static const char *tm_cfg = "?";

static void tm_check_f(const char *what, float got, float want)
{
	bool ok = fabsf(got - want) < 0.001f;
	if (!ok)
		tm_fails++;
	printf("%s %-58s got=%-7g want=%-7g %s\n", tm_cfg, what, (double)got, (double)want, ok ? "ok" : "FAIL");
}

static int tm_fn(const char *lib_name, const char *fn_name, int *lib_out)
{
	int l = ain_get_library(ain, lib_name);
	for (int f = 0; l >= 0 && f < ain->libraries[l].nr_functions; f++) {
		if (!strcmp(ain->libraries[l].functions[f].name, fn_name)) {
			*lib_out = l;
			return f;
		}
	}
	printf("TEXT-METRICS %s.%s not declared\n", lib_name, fn_name);
	fflush(stdout);
	_exit(2);
}

static int tm_str(const char *s) { return heap_alloc_string(make_string(s, strlen(s))); }

static int tm_width_page = -1;

// TextSurfaceManager.GetFontWidth(Text, wrap<int> Width, Type, Size, R, G, B,
//                                 BoldWeight, EdgeWeight, EdgeR, EdgeG, EdgeB)
static int tm_get_font_width(const char *text, int size, float bold, float edge)
{
	int lib, f = tm_fn("TextSurfaceManager", "GetFontWidth", &lib);
	if (tm_width_page < 0) {
		union vm_value dim = { .i = 1 };
		tm_width_page = heap_alloc_page(alloc_array(1, &dim, AIN_ARRAY_INT, -1, false));
	}
	heap[tm_width_page].page->values[0].i = -12345;
	int sp = stack_ptr;
	stack_push(tm_str(text));
	stack_push(tm_width_page);
	stack_push(0);
	stack_push(0); stack_push(size); stack_push(255); stack_push(255); stack_push(255);
	stack_push(bold); stack_push(edge);
	stack_push(0); stack_push(0); stack_push(0);
	hll_call(lib, f, 0);
	stack_ptr = sp;
	return heap[tm_width_page].page->values[0].i;
}

static void tm_call_parts(const char *fn, const union vm_value *args, int nr_args)
{
	int lib, f = tm_fn("PartsEngine", fn, &lib);
	int sp = stack_ptr;
	for (int i = 0; i < nr_args; i++)
		stack_push(args[i]);
	hll_call(lib, f, 0);
	stack_ptr = sp;
}

enum tm_how { TM_PARTS, TM_WINDOW, TM_PARTS_BOLD };

struct tm_style {
	const char *name;
	enum tm_how how;
	int size;
	float bold, edge;
	int space;
	int e, adv2, adv1; // the original: e, advance of a 2-byte and of a 1-byte character
};

// size/太さ/縁取り/字距 from dohnadohnaPact.afa (spacing.md §2.5); the last
// three columns are the original's numbers (0x69c7a0 + spacing)
static const struct tm_style tm_styles[] = {
	{ "AdvMessageWindow_main",          TM_WINDOW,     25, 0.0f, 0.0f, -1, 0, 24, 12 },
	{ "AdvMessageWindow_event",         TM_WINDOW,     25, 0.0f, 1.5f, -4, 2, 25, 13 },
	{ "AdvMessageWindow_plot",          TM_WINDOW,     20, 0.0f, 1.0f, -2, 1, 20, 10 },
	{ "AdvNamePlate",                   TM_PARTS,      28, 0.0f, 3.0f, -4, 3, 30, 16 },
	{ "AdvNamePlate_event",             TM_PARTS,      25, 0.0f, 3.0f, -4, 3, 27, 15 },
	{ "BackLog SYS_text",               TM_PARTS,      25, 0.0f, 1.0f, -3, 1, 24, 12 },
	{ "DungeonSelector TextName",       TM_PARTS,      22, 0.0f, 2.0f, -4, 2, 22, 11 },
	{ "AchievementNotify Text",         TM_PARTS,      32, 0.0f, 2.7f, -2, 3, 36, 20 },
	{ "AchievementNotify Caption",      TM_PARTS,      40, 0.0f, 3.5f, -7, 4, 41, 21 },
	{ "WorkerParamView bold 0.8",       TM_PARTS,      25, 0.8f, 0.0f, -3, 1, 24, 12 },
	{ "AdvCaption bold 0.5 edge 3",     TM_PARTS,      56, 0.5f, 3.0f, -6, 3, 56, 28 },
	{ "SetPartsFontBoldWeight 2 edge 4", TM_PARTS_BOLD, 19, 2.0f, 4.0f,  0, 4, 27, 18 },
};

static struct text_style tm_set_style(const struct tm_style *s, int no)
{
	if (s->how == TM_WINDOW) {
		union vm_value font[] = { vm_int(no), vm_int(0), vm_int(s->size), vm_int(255), vm_int(255),
			vm_int(255), vm_float(s->bold), vm_int(0), vm_int(0), vm_int(0), vm_float(s->edge) };
		tm_call_parts("SetMessageWindowTextFont", font, 11);
		union vm_value space[] = { vm_int(no), vm_int(s->space), vm_int(0) };
		tm_call_parts("SetMessageWindowTextSpace", space, 3);
		Point pos;
		struct parts_text *t = parts_message_window_render_text(parts_get(no), &pos);
		if (!t) {
			printf("TEXT-METRICS no message window text for %d\n", no);
			_exit(3);
		}
		return t->ts;
	}
	union vm_value font[] = { vm_int(no), vm_int(0), vm_int(s->size), vm_int(255), vm_int(255),
		vm_int(255), vm_float(s->bold), vm_int(0), vm_int(0), vm_int(0), vm_float(s->edge), vm_int(1) };
	tm_call_parts("Parts_SetFont", font, 12);
	union vm_value space[] = { vm_int(no), vm_int(s->space), vm_int(1) };
	tm_call_parts("Parts_SetTextCharSpace", space, 3);
	if (s->how == TM_PARTS_BOLD) {
		union vm_value bold[] = { vm_int(no), vm_float(s->bold), vm_int(1) };
		tm_call_parts("Parts_SetPartsFontBoldWeight", bold, 3);
	}
	return parts_get_text(parts_get(no), 0)->ts;
}

/* A font whose glyphs never load: gfx_render_textf then draws nothing and
 * needs no GL context, but still lays the text out. */
static struct font tm_noglyph;
static struct font_size tm_noglyph_sizes[32];
static int tm_noglyph_nr;

static struct font_size *tm_noglyph_get_size(struct font *font, float size)
{
	size = roundf(size);
	for (int i = 0; i < tm_noglyph_nr; i++)
		if (tm_noglyph_sizes[i].size == size)
			return &tm_noglyph_sizes[i];
	struct font_size *fs = &tm_noglyph_sizes[tm_noglyph_nr++];
	*fs = (struct font_size) { .size = size, .y_offset = 0, .font = font };
	return fs;
}
static float tm_noglyph_actual_size(struct font *font, float size) { (void)font; return roundf(size); }
static bool tm_noglyph_get_glyph(struct font_size *size, struct glyph *dst, uint32_t code, enum font_weight weight)
{
	(void)size; (void)dst; (void)code; (void)weight;
	return false;
}
static float tm_noglyph_size_char(struct font_size *size, uint32_t code) { (void)code; return size->size; }
static float tm_noglyph_kerning(struct font_size *size, uint32_t code, uint32_t next) { (void)next; (void)code; return size->size; }

static float tm_render_advance(const struct text_style *ts, const char *text)
{
	tm_noglyph = (struct font) {
		.charmap = CHARMAP_UNICODE,
		.get_size = tm_noglyph_get_size,
		.get_actual_size = tm_noglyph_actual_size,
		.get_actual_size_round_down = tm_noglyph_actual_size,
		.get_glyph = tm_noglyph_get_glyph,
		.size_char = tm_noglyph_size_char,
		.size_char_kerning = tm_noglyph_kerning,
	};
	struct font *saved[2] = { font_ttf[0], font_ttf[1] };
	font_ttf[0] = font_ttf[1] = &tm_noglyph;
	struct text_style t = *ts;
	t.font_size = NULL;
	float adv = gfx_render_textf(NULL, 0, 0, (char *)text, &t, false);
	font_ttf[0] = saved[0];
	font_ttf[1] = saved[1];
	return adv;
}

static void tm_init_fonts(void)
{
	const char *src = getenv("XS4_SRC");
	if (!src) {
		printf("TEXT-METRICS XS4_SRC is not set (fonts come from $XS4_SRC/fonts)\n");
		_exit(4);
	}
	static char gothic[4096], mincho[4096];
	snprintf(gothic, sizeof gothic, "%s/fonts/VL-Gothic-Regular.ttf", src);
	snprintf(mincho, sizeof mincho, "%s/fonts/HanaMinA.ttf", src);
	config.font_paths[FONT_GOTHIC] = gothic;
	config.font_paths[FONT_MINCHO] = mincho;
	config.fnl_path = NULL;
	gfx_font_init();
	probe_init_parts_table();
	// the v14 message window functions are registered at _PostLink
	extern struct static_library lib_PartsEngine;
	for (int i = 0; lib_PartsEngine.functions[i].name; i++) {
		if (!strcmp(lib_PartsEngine.functions[i].name, "_PostLink")) {
			((void (*)(void))lib_PartsEngine.functions[i].fun)();
			break;
		}
	}
}

#define TM_DBCS "\xC8\x66"      // 萬 U+842C
#define TM_PINYIN "\xA8\xA4"    // à U+00E0, a 2-byte GBK letter (Unicode half-width)
#define TM_TEXT "10" TM_DBCS
#define TM_SJIS_DBCS "\x88\x9F" // 亜 in SJIS

static void tm_child_gbk(void)
{
	gk_enable();
	tm_cfg = "GBK ";
	tm_init_fonts();
	char what[128];
	for (size_t i = 0; i < sizeof tm_styles / sizeof tm_styles[0]; i++) {
		const struct tm_style *s = &tm_styles[i];
		struct text_style ts = tm_set_style(s, 9100 + (int)i);
		int cell2 = s->adv2 - s->space, cell1 = s->adv1 - s->space;
		printf("GBK  [%s] size=%d 太さ=%g 縁取り=%g 字距=%d e=%d\n", s->name, s->size,
		       (double)s->bold, (double)s->edge, s->space, s->e);
		// GetFontWidth gets the style as CASFont holds it (the pactex numbers)
		snprintf(what, sizeof what, "fw  2-byte");
		tm_check_f(what, tm_get_font_width(TM_DBCS, s->size, s->bold, s->edge), cell2);
		snprintf(what, sizeof what, "fw  1-byte");
		tm_check_f(what, tm_get_font_width("1", s->size, s->bold, s->edge), cell1);
		snprintf(what, sizeof what, "fw  pinyin A8A4 (2-byte)");
		tm_check_f(what, tm_get_font_width(TM_PINYIN, s->size, s->bold, s->edge), cell2);
		snprintf(what, sizeof what, "fw  empty text = 2e");
		tm_check_f(what, tm_get_font_width("", s->size, s->bold, s->edge), 2 * s->e);
		snprintf(what, sizeof what, "fw  \"10萬\" = first character only");
		tm_check_f(what, tm_get_font_width(TM_TEXT, s->size, s->bold, s->edge), cell1);
		snprintf(what, sizeof what, "fw + spacing = adv (what the scripts wrap by)");
		tm_check_f(what, tm_get_font_width(TM_DBCS, s->size, s->bold, s->edge) + s->space, s->adv2);
		snprintf(what, sizeof what, "cell 2-byte");
		tm_check_f(what, text_style_width(&ts, TM_DBCS), cell2);
		snprintf(what, sizeof what, "cell 1-byte");
		tm_check_f(what, text_style_width(&ts, "1"), cell1);
		snprintf(what, sizeof what, "cell pinyin A8A4");
		tm_check_f(what, text_style_width(&ts, TM_PINYIN), cell2);
		snprintf(what, sizeof what, "size \"10萬\"");
		tm_check_f(what, gfx_size_text(&ts, TM_TEXT), 2 * cell1 + cell2);
		snprintf(what, sizeof what, "adv 2-byte");
		tm_check_f(what, tm_render_advance(&ts, TM_DBCS), s->adv2);
		snprintf(what, sizeof what, "adv 1-byte");
		tm_check_f(what, tm_render_advance(&ts, "1"), s->adv1);
		snprintf(what, sizeof what, "adv \"10萬\"");
		tm_check_f(what, tm_render_advance(&ts, TM_TEXT), 2 * s->adv1 + s->adv2);
	}
	// the missing-glyph fallback, CN configuration
	tm_check_f("fb  U+83C8 drawn from HanaMinA", probe_ft_font_fallback(font_ttf[0], 0x83C8, 25), 1);
	tm_check_f("fb  U+4E0B stays in VL Gothic", probe_ft_font_fallback(font_ttf[0], 0x4E0B, 25), 0);
	// GBK with an .fnl keeps the byte heuristic (no .fnl file here: only
	// GetFontWidth, which does not load fonts)
	config.fnl_path = "probe-without-fnl.fnl";
	tm_cfg = "GBK+fnl";
	tm_check_f("fw  2-byte, 25/0/1.5 (legacy)", tm_get_font_width(TM_DBCS, 25, 0.0f, 1.5f), 25);
	tm_check_f("fw  1-byte, 25/0/1.5 (legacy)", tm_get_font_width("1", 25, 0.0f, 1.5f), 12);
	tm_check_f("fw  empty (legacy)", tm_get_font_width("", 25, 0.0f, 1.5f), 0);
	config.fnl_path = NULL;
	printf("TEXT-METRICS GBK summary: %d failure(s)\n", tm_fails);
	fflush(stdout);
	_exit(tm_fails ? 1 : 0);
}

// upstream numbers under the SJIS rule (no .fnl): size/2 for a half-width
// character, no edges in the advance, the byte heuristic in GetFontWidth
static void tm_child_sjis(void)
{
	gk_force_sjis();
	tm_cfg = "SJIS";
	tm_init_fonts();
	char what[128];
	for (size_t i = 0; i < sizeof tm_styles / sizeof tm_styles[0]; i++) {
		const struct tm_style *s = &tm_styles[i];
		struct text_style ts = tm_set_style(s, 9200 + (int)i);
		float half = s->size / 2.0f;
		float bold_width = s->how == TM_PARTS_BOLD ? s->bold : 0.0f;
		printf("SJIS [%s]\n", s->name);
		snprintf(what, sizeof what, "fw  2-byte");
		tm_check_f(what, tm_get_font_width(TM_SJIS_DBCS, s->size, s->bold, s->edge), s->size);
		snprintf(what, sizeof what, "fw  1-byte");
		tm_check_f(what, tm_get_font_width("1", s->size, s->bold, s->edge), (int)half);
		snprintf(what, sizeof what, "fw  empty");
		tm_check_f(what, tm_get_font_width("", s->size, s->bold, s->edge), 0);
		snprintf(what, sizeof what, "fw  \"1亜\" (sum)");
		tm_check_f(what, tm_get_font_width("1" TM_SJIS_DBCS, s->size, s->bold, s->edge), (int)(half + s->size));
		snprintf(what, sizeof what, "cell 2-byte");
		tm_check_f(what, text_style_width(&ts, TM_SJIS_DBCS), s->size + 2 * bold_width + 2 * s->edge);
		snprintf(what, sizeof what, "cell 1-byte");
		tm_check_f(what, text_style_width(&ts, "1"), half + 2 * bold_width + 2 * s->edge);
		snprintf(what, sizeof what, "size \"1亜\"");
		tm_check_f(what, gfx_size_text(&ts, "1" TM_SJIS_DBCS), half + s->size);
		snprintf(what, sizeof what, "adv (glyph not loaded: skipped)");
		tm_check_f(what, tm_render_advance(&ts, TM_SJIS_DBCS), 0);
	}
	tm_check_f("fb  U+83C8 keeps the .notdef of VL Gothic", probe_ft_font_fallback(font_ttf[0], 0x83C8, 25), 0);
	printf("TEXT-METRICS SJIS summary: %d failure(s)\n", tm_fails);
	fflush(stdout);
	_exit(tm_fails ? 1 : 0);
}

static int tm_run(void (*child)(void), const char *name)
{
	fflush(stdout);
	pid_t pid = fork();
	if (!pid)
		child();
	int st = 0;
	waitpid(pid, &st, 0);
	bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
	printf("TEXT-METRICS %s child: %s (status %d)\n", name, ok ? "pass" : "FAIL", st);
	return ok ? 0 : 1;
}

static int test_text_metrics(void)
{
	int failed = tm_run(tm_child_gbk, "GBK") + tm_run(tm_child_sjis, "SJIS");
	printf(failed ? "TEXT-METRICS FAIL %d configuration(s)\n" : "TEXT-METRICS PASS GBK grid and SJIS unchanged\n", failed);
	return failed ? 86 : 0;
}

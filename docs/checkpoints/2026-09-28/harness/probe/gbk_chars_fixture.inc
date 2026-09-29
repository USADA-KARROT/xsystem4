/* GBK character rule for GB18030 AINs (HANDOFF item 5, 2026-09-29).
 *
 *   gbk-string  String HLL through the real ffi with the GBK rule on; also the
 *               GB18030 text walkers of the renderer (NUL guards)
 *   gbk-vm      real AIN bytecode (C_REF/C_ASSIGN with String Length/PushBack/
 *               PopBack), libsys4 character access, %D, iarray round trips,
 *               and the Motion parser on the name-plate Show/Hide strings
 *   gbk-detect  detection scores on the real AIN and on synthetic lists
 *   sjis-chars  the String and libsys4 cases of gbk-string/gbk-vm under the
 *               default (SJIS) rule; pins the pre-fix behaviour, so its
 *               "SJIS " lines must be identical before and after the fix
 *
 * Only a few byte examples are used (GBK/SJIS/UTF-8). New engine functions
 * are looked up with dlsym so this file builds against revisions without them
 * (before-check.sh); there the GBK cases fail as they should. Cases that call
 * ERROR in the SJIS rule run in a forked child. XS4_PROBE_GBK=1 turns the GBK
 * rule on for every mode (see runtime_probe.c); the modes here set the rule
 * they need themselves.
 * Requires first_overload_fixture.inc and string_fixture.inc. */
#include <dlfcn.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>
#include "hll/iarray.h"

// wrappers added to shadow copies of src/text.c and src/parts/text.c by build_probe.py
int probe_gb18030_skip_char_bytes(const char *s);
int probe_extract_multibyte_char(const char *src, char *dst);

static int gk_fails, gk_gbk;

static void *gk_sym(const char *name) { return dlsym(RTLD_DEFAULT, name); }

static const char *gk_charset(void)
{
	int (*get)(void) = (int (*)(void))gk_sym("sys4_get_string_charset");
	return !get ? "n/a" : get() ? "GBK" : "SJIS";
}

// What the engine does for a GB18030 AIN: gbk_string_rules_enable() after the
// fix; before it, only the legacy flag existed.
static void gk_enable(void)
{
	void (*en)(void) = (void (*)(void))gk_sym("gbk_string_rules_enable");
	if (en)
		en();
	else
		ain_is_gb18030 = true;
	gk_gbk = 1;
	printf("GBK enable via %s (ain_is_gb18030=%d, libsys4 rule=%s)\n",
	       en ? "gbk_string_rules_enable" : "legacy flag only", ain_is_gb18030, gk_charset());
}

// Default probe state: SJIS rule, no GB18030 flag (undoes XS4_PROBE_GBK).
static void gk_force_sjis(void)
{
	void (*set)(int) = (void (*)(int))gk_sym("sys4_set_string_charset");
	if (set)
		set(0);
	ain_is_gb18030 = false;
	gk_gbk = 0;
}

void gk_probe_enable_from_env(void)
{
	const char *e = getenv("XS4_PROBE_GBK");
	if (!e || strcmp(e, "1"))
		return;
	void (*en)(void) = (void (*)(void))gk_sym("gbk_string_rules_enable");
	if (en)
		en();
	else
		ain_is_gb18030 = true;
	printf("PROBE XS4_PROBE_GBK=1: %s (ain_is_gb18030=%d, libsys4 rule=%s)\n",
	       en ? "gbk_string_rules_enable" : "legacy flag only", ain_is_gb18030, gk_charset());
	fflush(stdout);
}

static const char *gk_tag(void) { return gk_gbk ? "GBK" : "SJIS"; }

static const char *gk_hex(const char *p, int n)
{
	static char ring[8][700];
	static int k;
	char *o = ring[k++ & 7];
	if (n < 0) {
		strcpy(o, "(none)");
		return o;
	}
	if (n == 0) {
		strcpy(o, "\"\"");
		return o;
	}
	int j = 0;
	for (int i = 0; i < n && j < 680; i++)
		j += snprintf(o + j, 700 - j, "%s%02X", i ? " " : "", (uint8_t)p[i]);
	return o;
}

struct gk_bytes { const char *p; int n; };
#define GB(lit) ((struct gk_bytes){ (lit), (int)sizeof(lit) - 1 })
#define GKX(g, s) (gk_gbk ? (g) : (s))

static void gk_expect_bytes(const char *what, const char *got, int gl, struct gk_bytes want)
{
	bool ok = want.n == gl && (gl <= 0 || !memcmp(got, want.p, gl));
	printf("%s %-46s got=%s want=%s %s\n", gk_tag(), what, gk_hex(got, gl), gk_hex(want.p, want.n),
	       ok ? "ok" : "FAIL");
	if (!ok) gk_fails++;
}

static void gk_expect_str(const char *what, const char *got, const char *want)
{
	bool ok = !strcmp(got, want);
	printf("%s %-46s got=%s want=%s %s\n", gk_tag(), what, got, want, ok ? "ok" : "FAIL");
	if (!ok) gk_fails++;
}

static void gk_expect_int(const char *what, int got, int want)
{
	bool ok = got == want;
	printf("%s %-46s got=%d want=%d %s\n", gk_tag(), what, got, want, ok ? "ok" : "FAIL");
	if (!ok) gk_fails++;
}

/* ---- String HLL calls through the real ffi ---- */

struct gk_arg { int str; int i; struct gk_bytes s; };
#define AI(x) ((struct gk_arg){ 0, (x), { NULL, 0 } })
#define AS(lit) ((struct gk_arg){ 1, 0, GB(lit) })

struct gk_res { union vm_value r; struct string *self; };

// String.<name>(self, args...) with the declaration that has 1 + nargs arguments.
static struct gk_res gk_call(const char *name, struct gk_bytes self, int nargs, const struct gk_arg *args)
{
	int lib = fo_find_lib("String");
	int fno = sf_find(lib, name, 1 + nargs);
	assert(lib >= 0 && fno >= 0 && nargs <= 4);
	bool has_ret = ain->libraries[lib].functions[fno].return_type.data != AIN_VOID;
	int slot = sf_str(self.p, self.n);
	int argslot[4] = { 0 };
	int sp = stack_ptr;
	stack_push(slot);
	for (int i = 0; i < nargs; i++) {
		if (args[i].str) {
			argslot[i] = sf_str(args[i].s.p, args[i].s.n);
			stack_push(argslot[i]);
		} else {
			stack_push(args[i].i);
		}
	}
	hll_call(lib, fno, 0);
	assert(stack_ptr == sp + (has_ret ? 1 : 0));
	struct gk_res res = { .r = { .i = 0 } };
	if (has_ret)
		res.r = stack_pop();
	res.self = make_string(heap[slot].s->text, heap[slot].s->size);
	heap_unref(slot);
	// by-value string arguments were released by the ffi (variable_fini)
	return res;
}

static int gk_call_int(const char *name, struct gk_bytes self, int nargs, const struct gk_arg *args)
{
	struct gk_res r = gk_call(name, self, nargs, args);
	free_string(r.self);
	return r.r.i;
}

// Checks a string result; self_want.n < 0 means "self unchanged".
static void gk_str_case(const char *what, const char *name, struct gk_bytes self, int nargs,
			const struct gk_arg *args, struct gk_bytes want, struct gk_bytes self_want)
{
	struct gk_res r = gk_call(name, self, nargs, args);
	struct string *s = heap[r.r.i].s;
	gk_expect_bytes(what, s->text, s->size, want);
	char label[96];
	snprintf(label, sizeof label, "  self after %s", name);
	if (self_want.n < 0)
		self_want = self;
	gk_expect_bytes(label, r.self->text, r.self->size, self_want);
	heap_unref(r.r.i);
	free_string(r.self);
}

// Checks self after a void call (PushBack/PopBack/Erase/Insert).
static void gk_mut_case(const char *what, const char *name, struct gk_bytes self, int nargs,
			const struct gk_arg *args, struct gk_bytes want)
{
	struct gk_res r = gk_call(name, self, nargs, args);
	gk_expect_bytes(what, r.self->text, r.self->size, want);
	free_string(r.self);
}

// "[hex]|[hex]|..." for an array<string> page slot.
static const char *gk_list(int slot)
{
	static char out[2][1400];
	static int k;
	char *o = out[k++ & 1];
	int j = 0;
	struct page *a = heap[slot].page;
	j += snprintf(o + j, sizeof out[0] - j, "%d:", a ? a->nr_vars : -1);
	for (int i = 0; a && i < a->nr_vars && j < 1300; i++) {
		struct string *s = heap[a->values[i].i].s;
		j += snprintf(o + j, sizeof out[0] - j, "%s[%s]", i ? "|" : "", s->size ? gk_hex(s->text, s->size) : "");
	}
	return o;
}

static void gk_split_case(const char *what, struct gk_bytes self, struct gk_bytes seps, int mode, const char *want)
{
	struct gk_arg args[2] = { { 1, 0, seps }, AI(mode) };
	struct gk_res r = gk_call("Split", self, 2, args);
	gk_expect_str(what, gk_list(r.r.i), want);
	heap_unref(r.r.i);
	free_string(r.self);
}

static void gk_searchall_case(const char *what, struct gk_bytes self, const char *want)
{
	int lib = fo_find_lib("String");
	int fno = sf_find(lib, "SearchAll", 3);
	assert(fno >= 0);
	int slot = sf_str(self.p, self.n);
	struct page *empty = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 0);
	empty->array.rank = 1;
	int list = heap_alloc_page(empty);
	int rx = sf_str("([^\\[\\]]+?)+|\\[[^\\]]+?\\]", -1);
	int sp = stack_ptr;
	stack_push(slot);
	stack_push(list);
	stack_push(rx);
	hll_call(lib, fno, 0);
	assert(stack_ptr == sp + 1);
	int found = stack_pop().i;
	char label[96];
	snprintf(label, sizeof label, "%s (found=%d)", what, found);
	gk_expect_str(label, gk_list(list), want);
	heap_unref(slot);
	heap_unref(list);
	// rx (by-value string) was released by the ffi
}

static float gk_call_float(const char *name, struct gk_bytes self)
{
	struct gk_res r = gk_call(name, self, 0, NULL);
	free_string(r.self);
	return r.r.f;
}

// GBK sample bytes
#define QI_LA   "\xBE\x5F\xC7\x89"                              /* 2 characters */
#define PANEL   "\xC1\xA2\xC0\x4C\xA3\xAF\xB0\xA2\xD0\xDC"      /* 5, the 3rd is A3 AF */
#define PANEL4  PANEL "\xA3\xAF\xB4\xF2\xF4\x59\xA3\xAF\xB0\xA7\xA3\xC4" /* 4 names, 3 separators */
#define ACHV    "\xD6\xF7\xD2\xAA\xA3\xAF\xA3\xB2\xA3\xB6\xA3\xB0\xA3\xB1\xA3\xC3" /* 8 characters */
#define BASE4   "\x93\xFE\xFC\x63\xAD\x68\xB9\x9D"              /* 4 characters */
#define SLASH   "\xA3\xAF"
#define TAIL_BS "\xD5\x5C"                                      /* trail byte '\' */
#define TAIL_VB "\xDF\x7C"                                      /* trail byte '|' */
#define TAIL_RB "\xD5\x5D"                                      /* trail byte ']' */

static void gk_string_cases(void)
{
	gk_expect_int("Length(QI_LA QI_LA)", gk_call_int("Length", GB(QI_LA QI_LA), 0, NULL), GKX(4, 7));
	gk_str_case("GetPart#0(PANEL, 3)", "GetPart", GB(PANEL), 1, (struct gk_arg[]){ AI(3) },
		    GKX(GB("\xB0\xA2\xD0\xDC"), GB("\x4C\xA3\xAF\xB0\xA2\xD0\xDC")), (struct gk_bytes){ NULL, -1 });
	int len = gk_call_int("Length", GB(ACHV), 0, NULL);
	gk_expect_int("Length(ACHV)", len, GKX(8, 15));
	gk_str_case("GetPart#1(ACHV, Length-1, 1)", "GetPart", GB(ACHV), 2, (struct gk_arg[]){ AI(len - 1), AI(1) },
		    GKX(GB("\xA3\xC3"), GB("\xC3")), (struct gk_bytes){ NULL, -1 });

	// per-character pieces, as AnimateText splits a title
	{
		int n = gk_call_int("Length", GB(BASE4), 0, NULL);
		char joined[200];
		int j = 0;
		for (int i = 0; i < n && j < 180; i++) {
			struct gk_res r = gk_call("GetPart", GB(BASE4), 2, (struct gk_arg[]){ AI(i), AI(1) });
			struct string *s = heap[r.r.i].s;
			j += snprintf(joined + j, sizeof joined - j, "%s[%s]", i ? "|" : "", gk_hex(s->text, s->size));
			heap_unref(r.r.i);
			free_string(r.self);
		}
		joined[j] = '\0';
		gk_expect_str("GetPart(i,1) for i < Length(BASE4)", joined,
			      GKX("[93 FE]|[FC 63]|[AD 68]|[B9 9D]", "[93 FE]|[FC 63]|[AD]|[68]|[B9]|[9D]"));
	}

	gk_expect_int("Find(PANEL, B0A2 D0DC)", gk_call_int("Find", GB(PANEL), 1, (struct gk_arg[]){ AS("\xB0\xA2\xD0\xDC") }), GKX(3, 6));
	gk_expect_int("Contains(TAIL_BS, \\)", gk_call_int("Contains", GB(TAIL_BS), 1, (struct gk_arg[]){ AS("\\") }), GKX(0, 1));
	gk_expect_int("Find(TAIL_VB x, |)", gk_call_int("Find", GB(TAIL_VB "x"), 1, (struct gk_arg[]){ AS("|") }), GKX(-1, 1));
	gk_expect_int("Find(PANEL, \"\")", gk_call_int("Find", GB(PANEL), 1, (struct gk_arg[]){ AS("") }), 0);
	gk_expect_int("FindLast(A/B/C, SLASH)", gk_call_int("FindLast", GB("A" SLASH "B" SLASH "C"), 1, (struct gk_arg[]){ AS(SLASH) }), GKX(3, 4));
	gk_expect_int("FindLast(QI_LA, \"\")", gk_call_int("FindLast", GB(QI_LA), 1, (struct gk_arg[]){ AS("") }), GKX(1, -1));
	gk_expect_int("FindLast(\"\", \"\")", gk_call_int("FindLast", GB(""), 1, (struct gk_arg[]){ AS("") }), -1);

	gk_mut_case("Erase(QI_LA QI_LA, 1, 2)", "Erase", GB(QI_LA QI_LA), 2, (struct gk_arg[]){ AI(1), AI(2) },
		    GKX(GB(QI_LA), GB("\xBE\x89\xBE\x5F\xC7\x89")));
	gk_mut_case("Erase(QI_LA QI_LA, -1, 1)", "Erase", GB(QI_LA QI_LA), 2, (struct gk_arg[]){ AI(-1), AI(1) },
		    GKX(GB(QI_LA QI_LA), GB("\x5F\xC7\x89\xBE\x5F\xC7\x89")));
	gk_mut_case("Erase(QI_LA QI_LA, 1, -1)", "Erase", GB(QI_LA QI_LA), 2, (struct gk_arg[]){ AI(1), AI(-1) },
		    GKX(GB("\xBE\x5F"), GB(QI_LA QI_LA)));
	gk_mut_case("Erase(QI_LA QI_LA, 9, 1)", "Erase", GB(QI_LA QI_LA), 2, (struct gk_arg[]){ AI(9), AI(1) }, GB(QI_LA QI_LA));
	gk_mut_case("Erase(QI_LA QI_LA, 4, 1)", "Erase", GB(QI_LA QI_LA), 2, (struct gk_arg[]){ AI(4), AI(1) },
		    GKX(GB(QI_LA QI_LA), GB("\xBE\x5F\xC7\x89\xBE\xC7\x89")));
	gk_mut_case("Erase(QI_LA QI_LA, 1, 0)", "Erase", GB(QI_LA QI_LA), 2, (struct gk_arg[]){ AI(1), AI(0) }, GB(QI_LA QI_LA));
	// (not QI_LA QI_LA: the SJIS rule would erase a lone lead byte there and overrun)
	gk_mut_case("Erase(PANEL, 3, 5)", "Erase", GB(PANEL), 2, (struct gk_arg[]){ AI(3), AI(5) },
		    GKX(GB("\xC1\xA2\xC0\x4C\xA3\xAF"), GB("\xC1\xA2\xC0\xD0\xDC")));

	gk_mut_case("PopBack(QI_LA)", "PopBack", GB(QI_LA), 0, NULL, GKX(GB("\xBE\x5F"), GB("\xBE\x5F\xC7")));
	gk_mut_case("PopBack(\"\")", "PopBack", GB(""), 0, NULL, GB(""));
	gk_mut_case("PushBack(\"\", 0xA3B0)", "PushBack", GB(""), 1, (struct gk_arg[]){ AI(0xA3B0) }, GKX(GB("\xA3\xB0"), GB("\xB0")));
	gk_mut_case("PushBack(\"\", 0)", "PushBack", GB(""), 1, (struct gk_arg[]){ AI(0) }, GKX(GB(""), GB("\0")));
	gk_mut_case("PushBack(\"a\", 92)", "PushBack", GB("a"), 1, (struct gk_arg[]){ AI(92) }, GB("a\\"));
	gk_mut_case("Insert(QI_LA, 1, X)", "Insert", GB(QI_LA), 2, (struct gk_arg[]){ AI(1), AS("X") },
		    GKX(GB("\xBE\x5F" "X" "\xC7\x89"), GB("\xBE" "X" "\x5F\xC7\x89")));
	gk_mut_case("Insert(QI_LA, 9, X)", "Insert", GB(QI_LA), 2, (struct gk_arg[]){ AI(9), AS("X") }, GB(QI_LA "X"));

	gk_str_case("Replace(TAIL_VB |, |, !)", "Replace", GB(TAIL_VB "|"), 2, (struct gk_arg[]){ AS("|"), AS("!") },
		    GKX(GB(TAIL_VB "!"), GB("\xDF!!")), GKX(((struct gk_bytes){ NULL, -1 }), GB("\xDF!!")));
	gk_str_case("Replace(SLASH a SLASH, SLASH, ,)", "Replace", GB(SLASH "a" SLASH), 2, (struct gk_arg[]){ AS(SLASH), AS(",") },
		    GB(",a,"), GKX(((struct gk_bytes){ NULL, -1 }), GB(",a,")));
	gk_str_case("Replace(QI_LA, \"\", x)", "Replace", GB(QI_LA), 2, (struct gk_arg[]){ AS(""), AS("x") },
		    GB(QI_LA), (struct gk_bytes){ NULL, -1 });
	gk_str_case("Replace(QI_LA, zz, x) no match", "Replace", GB(QI_LA), 2, (struct gk_arg[]){ AS("zz"), AS("x") },
		    GB(QI_LA), (struct gk_bytes){ NULL, -1 });

	gk_split_case("Split(PANEL4, SLASH, 0)", GB(PANEL4), GB(SLASH), 0,
		      GKX("4:[C1 A2 C0 4C]|[B0 A2 D0 DC]|[B4 F2 F4 59]|[B0 A7 A3 C4]",
			  "8:[C1 A2 C0 4C]|[]|[B0 A2 D0 DC]|[]|[B4 F2 F4 59]|[]|[B0 A7]|[C4]"));
	const char *sjis_split9 = "9:[]|[]|[61]|[]|[]|[]|[62]|[]|[]";
	gk_split_case("Split(/a//b/, SLASH, 0)", GB(SLASH "a" SLASH SLASH "b" SLASH), GB(SLASH), 0,
		      GKX("2:[61]|[62]", sjis_split9));
	gk_split_case("Split(/a//b/, SLASH, 1)", GB(SLASH "a" SLASH SLASH "b" SLASH), GB(SLASH), 1,
		      GKX("6:[A3 AF]|[61]|[A3 AF]|[A3 AF]|[62]|[A3 AF]", sjis_split9));
	gk_split_case("Split(/a//b/, SLASH, 2)", GB(SLASH "a" SLASH SLASH "b" SLASH), GB(SLASH), 2,
		      GKX("4:[]|[61]|[]|[62]", sjis_split9));
	gk_split_case("Split(/a//b/, SLASH, 3)", GB(SLASH "a" SLASH SLASH "b" SLASH), GB(SLASH), 3,
		      GKX("8:[]|[A3 AF]|[61]|[A3 AF]|[]|[A3 AF]|[62]|[A3 AF]", sjis_split9));
	gk_split_case("Split(TAIL_VB x, |, 0)", GB(TAIL_VB "x"), GB("|"), 0, GKX("1:[DF 7C 78]", "2:[DF]|[78]"));
	gk_split_case("Split(a,b A3AC c, \", A3AC\", 0)", GB("a,b\xA3\xAC" "c"), GB(",\xA3\xAC"), 0,
		      GKX("3:[61]|[62]|[63]", "4:[61]|[62]|[]|[63]"));
	gk_split_case("Split(a|b, \"\", 0)", GB("a|b"), GB(""), 0, "1:[61 7C 62]");
	gk_split_case("Split(\"\", |, 0)", GB(""), GB("|"), 0, "0:");

	gk_str_case("ToUpper(B461 a 8281)", "ToUpper", GB("\xB4\x61" "a" "\x82\x81"), 0, NULL,
		    GKX(GB("\xB4\x61" "A" "\x82\x60"), GB("\xB4" "AA" "\x82\x81")), (struct gk_bytes){ NULL, -1 });
	gk_str_case("ToLower(8260 A)", "ToLower", GB("\x82\x60" "A"), 0, NULL,
		    GKX(GB("\x82\x81" "a"), GB("\x82\x60" "a")), (struct gk_bytes){ NULL, -1 });

	gk_expect_int("ToInt(A3B1 A3B2)", gk_call_int("ToInt", GB("\xA3\xB1\xA3\xB2"), 0, NULL), GKX(12, 0));
	gk_expect_int("ToInt(-12)", gk_call_int("ToInt", GB("-12"), 0, NULL), -12);
	gk_expect_int("ToInt(1 A3)", gk_call_int("ToInt", GB("1\xA3"), 0, NULL), 1);
	gk_expect_int("ToFloat(A3B1 8144 A3B5)*10", (int)(gk_call_float("ToFloat", GB("\xA3\xB1\x81\x44\xA3\xB5")) * 10), GKX(15, 0));
	gk_expect_int("ToFloat(A3B1 A3AE A3B5)*10", (int)(gk_call_float("ToFloat", GB("\xA3\xB1\xA3\xAE\xA3\xB5")) * 10), GKX(10, 0));

	gk_searchall_case("SearchAll(a[b]TAIL_RB c)", GB("a[b]" TAIL_RB "c"),
			  GKX("3:[61]|[5B 62 5D]|[D5 5D 63]", "4:[61]|[5B 62 5D]|[D5]|[63]"));
	gk_str_case("Trim#0(8140 hi 8140) control", "Trim", GB("\x81\x40hi\x81\x40"), 0, NULL, GB("hi"), (struct gk_bytes){ NULL, -1 });
	gk_str_case("GetPart#0(abc, 1) control", "GetPart", GB("abc"), 1, (struct gk_arg[]){ AI(1) }, GB("bc"), (struct gk_bytes){ NULL, -1 });
}

/* ---- renderer walkers (probe wrappers around the static helpers) ---- */

// Runs f on a heap copy of exactly n bytes + NUL, so ASan sees any over-read.
static void gk_walk_case(const char *what, const char *bytes, int n, int want_len, struct gk_bytes want_dst, bool extract)
{
	char *buf = xmalloc(n + 1);
	memcpy(buf, bytes, n);
	buf[n] = '\0';
	char dst[8];
	memset(dst, 0x5a, sizeof dst);
	int got = extract ? probe_extract_multibyte_char(buf, dst) : probe_gb18030_skip_char_bytes(buf);
	gk_expect_int(what, got, want_len);
	if (extract) {
		char label[96];
		snprintf(label, sizeof label, "  copied by %s", what);
		gk_expect_bytes(label, dst, (int)strnlen(dst, 5), want_dst);
	}
	free(buf);
}

static void gk_render_cases(void)
{
	bool saved = ain_is_gb18030;
	ain_is_gb18030 = true;
	// normal characters keep their widths
	gk_walk_case("skip(BE5F)", "\xBE\x5F", 2, 2, GB(""), false);
	gk_walk_case("skip(a)", "a", 1, 1, GB(""), false);
	gk_walk_case("skip(B1 31 41 42) 4-byte form kept", "\xB1\x31\x41\x42", 4, 4, GB(""), false);
	gk_walk_case("skip(80)", "\x80", 1, 1, GB(""), false);
	// a truncated character never steps over the NUL
	gk_walk_case("skip(BE) lone lead", "\xBE", 1, 1, GB(""), false);
	gk_walk_case("skip(BE 31) truncated 4-byte", "\xBE\x31", 2, 2, GB(""), false);
	gk_walk_case("skip(BE 31 81) truncated 4-byte", "\xBE\x31\x81", 3, 3, GB(""), false);
	gk_walk_case("extract(BE5F)", "\xBE\x5F", 2, 2, GB("\xBE\x5F"), true);
	gk_walk_case("extract(B1 31 41 42) 4-byte form kept", "\xB1\x31\x41\x42", 4, 4, GB("\xB1\x31\x41\x42"), true);
	gk_walk_case("extract(80 41) width kept", "\x80\x41", 2, 2, GB("\x80\x41"), true);
	gk_walk_case("extract(BE) lone lead", "\xBE", 1, 1, GB("\xBE"), true);
	gk_walk_case("extract(FF) lone byte", "\xFF", 1, 1, GB("\xFF"), true);
	gk_walk_case("extract(BE 31 81) truncated 4-byte", "\xBE\x31\x81", 3, 3, GB("\xBE\x31\x81"), true);
	// last: before the fix this one reads past the buffer (ASan stops the run)
	gk_walk_case("extract(BE 31) truncated 4-byte", "\xBE\x31", 2, 2, GB("\xBE\x31"), true);
	ain_is_gb18030 = saved;
}

/* ---- libsys4 character access, bytecode and serializers ---- */

// Runs f(arg) in a child; returns its printed int, or INT_MIN if it aborted.
static int gk_child_int(int (*f)(const void *), const void *arg)
{
	int fds[2];
	assert(!pipe(fds));
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		close(fds[0]);
		int v = f(arg);
		if (write(fds[1], &v, sizeof v) != sizeof v)
			_exit(2);
		_exit(0);
	}
	close(fds[1]);
	int v = 0;
	ssize_t n = read(fds[0], &v, sizeof v);
	close(fds[0]);
	int st = 0;
	waitpid(pid, &st, 0);
	if (n != sizeof v || !WIFEXITED(st) || WEXITSTATUS(st))
		return INT_MIN;
	return v;
}

static int gk_get_char_99(const void *arg) { (void)arg; struct string *s = make_string(QI_LA, 4); return string_get_char(s, 99); }
static int gk_get_char_m1(const void *arg) { (void)arg; struct string *s = make_string("ab", 2); return string_get_char(s, -1); }
static int gk_set_char_5(const void *arg)
{
	(void)arg;
	struct string *s = make_string(QI_LA, 4);
	string_set_char(&s, 5, 'x');
	return s->size == 4 && !memcmp(s->text, QI_LA, 4) ? 1 : 0;
}

// Calls a bytecode function string f(string) and returns its result bytes.
static struct string *gk_run_str_func(const char *fname, struct gk_bytes arg)
{
	int fno = ain_get_function(ain, (char *)fname);
	assert(fno >= 0);
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t saved_ip = instr_ptr;
	int a = sf_str(arg.p, arg.n);
	heap[a].ref |= HEAP_TEMP_FLAG;
	stack_push(a);
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = saved_ip;
	assert(call_stack_ptr == csp && stack_ptr == sp + 1);
	int r = stack_pop().i;
	struct string *out = make_string(heap[r].s->text, heap[r].s->size);
	heap_unref(r);
	return out;
}

static int gk_add_punct_empty(const void *arg)
{
	(void)arg;
	struct string *s = gk_run_str_func("SYS_AddPunct", GB(""));
	return s->size == 1 ? (uint8_t)s->text[0] : -s->size;
}

static void gk_func_case(const char *what, const char *fname, struct gk_bytes arg, struct gk_bytes want)
{
	struct string *s = gk_run_str_func(fname, arg);
	gk_expect_bytes(what, s->text, s->size, want);
	free_string(s);
}

static struct string *gk_iarray_roundtrip(struct string *in)
{
	struct iarray_writer w;
	iarray_init_writer(&w, NULL);
	iarray_write_string(&w, in);
	struct page *page = iarray_to_page(&w);
	struct iarray_reader r;
	iarray_init_reader(&r, page, NULL);
	struct string *out = iarray_read_string(&r);
	free_page(page);
	iarray_free_writer(&w);
	return out;
}

static void gk_iarray_case(const char *what, struct gk_bytes in, struct gk_bytes want)
{
	struct string *s = make_string(in.p, in.n);
	struct string *out = gk_iarray_roundtrip(s);
	gk_expect_bytes(what, out->text, out->size, want);
	free_string(out);
	free_string(s);
}

// Round-trips every non-ASCII STR0 string; returns how many came back different.
static int gk_iarray_str0(int *nonascii)
{
	int changed = 0;
	*nonascii = 0;
	for (int i = 0; i < ain->nr_strings; i++) {
		struct string *s = ain->strings[i];
		if (!s)
			continue;
		bool high = false;
		for (int k = 0; k < s->size; k++)
			if ((uint8_t)s->text[k] >= 0x80) high = true;
		if (!high)
			continue;
		(*nonascii)++;
		struct string *out = gk_iarray_roundtrip(s);
		if (out->size != s->size || memcmp(out->text, s->text, s->size))
			changed++;
		free_string(out);
	}
	return changed;
}

static void gk_format_case(const char *what, const char *fmt, int v, struct gk_bytes want)
{
	struct string *f = make_string(fmt, strlen(fmt));
	struct string *s = string_format(f, (union vm_value){ .i = v }, STRFMT_INT);
	gk_expect_bytes(what, s->text, s->size, want);
	free_string(s);
	free_string(f);
}

static void gk_vm_cases(void)
{
	struct string *s = make_string(QI_LA, 4);
	gk_expect_int("string_get_char(QI_LA, 1)", string_get_char(s, 1), GKX(0xC789, 0x5F));
	gk_expect_int("string_get_char(QI_LA, 2)", string_get_char(s, 2), GKX(0, (int)(char)0xC7));
	free_string(s);
	s = make_string("\x82\xA0", 2);
	gk_expect_int("string_get_char(82A0, 0)", string_get_char(s, 0), GKX(0x82A0, 0xA082));
	free_string(s);
	s = make_string("\xB1", 1);
	gk_expect_int("string_get_char(B1, 0)", string_get_char(s, 0), GKX(0xB100, (int)(char)0xB1));
	free_string(s);
	gk_expect_int("string_get_char(QI_LA, 99) [child]", gk_child_int(gk_get_char_99, NULL), GKX(0, INT_MIN));
	gk_expect_int("string_get_char(ab, -1) [child]", gk_child_int(gk_get_char_m1, NULL), GKX('a', INT_MIN));
	gk_expect_int("string_set_char(QI_LA, 5, x) unchanged [child]", gk_child_int(gk_set_char_5, NULL), GKX(1, INT_MIN));

	gk_func_case("SYS_AddPunct(ab TAIL_BS)", "SYS_AddPunct", GB("ab" TAIL_BS),
		     GKX(GB("ab" TAIL_BS "\\"), GB("ab" TAIL_BS)));
	gk_func_case("SYS_DeletePunct(ab TAIL_BS)", "SYS_DeletePunct", GB("ab" TAIL_BS),
		     GKX(GB("ab" TAIL_BS), GB("ab\xD5")));
	gk_func_case("SYS_AddPunct(ab)", "SYS_AddPunct", GB("ab"), GB("ab\\"));
	gk_expect_int("SYS_AddPunct(\"\") [child]", gk_child_int(gk_add_punct_empty, NULL), GKX('\\', INT_MIN));
	gk_func_case("SYS_ToUpper(B461 a 8281)", "SYS_ToUpper", GB("\xB4\x61" "a" "\x82\x81"),
		     GKX(GB("\xB4\x61" "A" "\x82\x60"), GB("\xB4" "AA" "\x82\x81")));
	gk_func_case("SYS_ToLower(8260 A)", "SYS_ToLower", GB("\x82\x60" "A"),
		     GKX(GB("\x82\x81" "a"), GB("\x82\x60" "a")));

	gk_format_case("format(C2B7 C8CB %D, 12)", "\xC2\xB7\xC8\xCB%D", 12,
		       GKX(GB("\xC2\xB7\xC8\xCB\xA3\xB1\xA3\xB2"), GB("\xC2\xB7\xC8\xCB\x82\x50\x82\x51")));
	gk_format_case("format(SLASH %02D, 5)", SLASH "%02D", 5,
		       GKX(GB(SLASH "\xA3\xB0\xA3\xB5"), GB(SLASH "\x82\x4F\x82\x54")));
	gk_format_case("format(%3D, -7)", "%3D", -7,
		       GKX(GB("\x81\x40\x81\x7C\xA3\xB7"), GB("\x81\x40\x81\x7C\x82\x56")));
	gk_format_case("format(%d, 12)", "%d", 12, GB("12"));

	gk_iarray_case("iarray round trip QI_LA", GB(QI_LA), GKX(GB(QI_LA), GB(QI_LA "\xFF")));
	gk_iarray_case("iarray round trip BE", GB("\xBE"), GB("\xBE"));
	gk_iarray_case("iarray round trip 82A0 a", GB("\x82\xA0" "a"), GB("\x82\xA0" "a"));
	gk_iarray_case("iarray round trip 89", GB("\x89"), GKX(GB("\x89"), GB("\x89\xFF")));
	int nonascii = 0, changed = gk_iarray_str0(&nonascii);
	char label[96];
	snprintf(label, sizeof label, "iarray round trip STR0 (%d non-ASCII) changed", nonascii);
	gk_expect_int(label, changed, GKX(0, 703));
}

/* ---- Motion parser: same result under both rules ---- */

static void gk_dump_page(struct string **out, struct page *page, int depth);

static void gk_dump_value(struct string **out, enum ain_data_type t, union vm_value v, int depth)
{
	char buf[64];
	switch (t) {
	case AIN_INT: case AIN_BOOL: case AIN_LONG_INT: case AIN_ENUM: case AIN_ENUM2:
		snprintf(buf, sizeof buf, "%d", v.i);
		break;
	case AIN_FLOAT:
		snprintf(buf, sizeof buf, "%g", v.f);
		break;
	case AIN_STRING:
		if (v.i > 0 && string_index_valid(v.i) && heap[v.i].s) {
			string_append_cstr(out, "\"", 1);
			string_append(out, heap[v.i].s);
			string_append_cstr(out, "\"", 1);
			return;
		}
		snprintf(buf, sizeof buf, "s%d", v.i < 0 ? -1 : 0);
		break;
	case AIN_STRUCT: case AIN_ARRAY_INT: case AIN_ARRAY_FLOAT: case AIN_ARRAY_STRING: case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_BOOL: case AIN_ARRAY_LONG_INT: case AIN_ARRAY: case AIN_WRAP:
		if (v.i > 0 && page_index_valid(v.i) && heap[v.i].page) {
			if (depth < 12)
				gk_dump_page(out, heap[v.i].page, depth + 1);
			else
				string_append_cstr(out, "...", 3);
			return;
		}
		snprintf(buf, sizeof buf, "p%d", v.i < 0 ? -1 : 0);
		break;
	default:
		snprintf(buf, sizeof buf, "t%d", (int)t);
		break;
	}
	string_append_cstr(out, buf, strlen(buf));
}

static void gk_dump_page(struct string **out, struct page *page, int depth)
{
	char buf[32];
	snprintf(buf, sizeof buf, "{%d.%d:", page->type, page->index);
	string_append_cstr(out, buf, strlen(buf));
	for (int i = 0; i < page->nr_vars && i < 256; i++) {
		if (i)
			string_append_cstr(out, ",", 1);
		int st = -1;
		enum ain_data_type t = variable_type(page, i, &st, NULL);
		if (t == AIN_OPTION && i + 1 < page->nr_vars) {
			// [value, discriminant]: follow the value only when it is a page of
			// the payload struct type; a none value can hold a stale slot number
			union vm_value v = page->values[i];
			snprintf(buf, sizeof buf, "opt%d:", page->values[i+1].i);
			string_append_cstr(out, buf, strlen(buf));
			if (st >= 0 && v.i > 0 && page_index_valid(v.i) && heap[v.i].page
			    && heap[v.i].page->type == STRUCT_PAGE && heap[v.i].page->index == st)
				gk_dump_page(out, heap[v.i].page, depth + 1);
			else
				string_append_cstr(out, "-", 1);
			continue;
		}
		gk_dump_value(out, t, page->values[i], depth);
	}
	string_append_cstr(out, "}", 1);
}

static struct string *gk_motion_dump(const char *task)
{
	int fno = ain_get_function(ain, "Motion::GetCompiled");
	assert(fno >= 0);
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t saved_ip = instr_ptr;
	int a = sf_str(task, -1);
	heap[a].ref |= HEAP_TEMP_FLAG;
	stack_push(a);
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = saved_ip;
	assert(call_stack_ptr == csp && stack_ptr == sp + 1);
	int r = stack_pop().i;
	struct string *out = make_string("", 0);
	gk_dump_value(&out, AIN_STRUCT, (union vm_value){ .i = r }, 0);
	heap_unref(r);
	return out;
}

static const char *gk_motion_tasks[] = {
	"Section:AdvNamePlate[Time:150|X:0 -30 EaseOutQuad Rel|Alpha: 255 0]",
	"Section:AdvNamePlate[Time:150|X:-30 0 EaseIn Rel|Alpha:0 255]",
};

// Child: parse each task with the pre-fix setup (legacy flag, SJIS rule), then
// with the GBK rule, and compare the parsed MotionSet trees.
static int gk_motion_child(const void *arg)
{
	(void)arg;
	int differ = 0;
	struct string *before[2];
	ain_is_gb18030 = true;
	for (int i = 0; i < 2; i++)
		before[i] = gk_motion_dump(gk_motion_tasks[i]);
	void (*en)(void) = (void (*)(void))gk_sym("gbk_string_rules_enable");
	if (en)
		en();
	for (int i = 0; i < 2; i++) {
		struct string *after = gk_motion_dump(gk_motion_tasks[i]);
		bool same = after->size == before[i]->size && !memcmp(after->text, before[i]->text, after->size);
		printf("GBK Motion::GetCompiled task %d: %d-byte tree, %s (%.160s)\n", i, after->size,
		       same ? "same as the SJIS rule" : "DIFFERS from the SJIS rule", after->text);
		if (!same) {
			printf("    SJIS rule: %.400s\n    GBK rule:  %.400s\n", before[i]->text, after->text);
			differ++;
		}
		fflush(stdout);
		free_string(after);
		free_string(before[i]);
	}
	return differ;
}

/* ---- detection ---- */

struct gk_scores { int legacy, boundary, sjis_invalid, nonascii, utf8_valid; };
typedef bool (*gk_detect_fn)(struct string **, int, struct gk_scores *);

static bool gk_detect_list(gk_detect_fn detect, const char *what, const char *const *items, int n, int times,
			   struct gk_scores *sc)
{
	int total = n * times;
	struct string **strs = xcalloc(total ? total : 1, sizeof *strs);
	for (int i = 0; i < total; i++)
		strs[i] = items[i % n] ? make_string(items[i % n], strlen(items[i % n])) : NULL;
	memset(sc, 0x7f, sizeof *sc);
	bool strict = detect(strs, total, sc);
	printf("DETECT %-32s n=%d legacy=%d boundary=%d sjis_invalid=%d nonascii=%d utf8_valid=%d strict=%d\n",
	       what, total, sc->legacy, sc->boundary, sc->sjis_invalid, sc->nonascii, sc->utf8_valid, strict);
	for (int i = 0; i < total; i++)
		if (strs[i]) free_string(strs[i]);
	free(strs);
	return strict;
}

static int test_gbk_detect(void)
{
	gk_gbk = 1;
	gk_detect_fn detect = (gk_detect_fn)gk_sym("gb18030_detect_strings");
	if (!detect) {
		printf("GBK gb18030_detect_strings not found: FAIL\n");
		return 89;
	}
	struct gk_scores sc;
	memset(&sc, 0x7f, sizeof sc);
	bool strict = detect(ain->strings, ain->nr_strings, &sc);
	printf("DETECT real AIN n=%d legacy=%d boundary=%d sjis_invalid=%d nonascii=%d utf8_valid=%d strict=%d\n",
	       ain->nr_strings, sc.legacy, sc.boundary, sc.sjis_invalid, sc.nonascii, sc.utf8_valid, strict);
	gk_expect_int("real AIN legacy score", sc.legacy, 92);
	gk_expect_int("real AIN strict", strict, 1);
	gk_expect_int("real AIN boundary > 5", sc.boundary > 5, 1);
	gk_expect_int("real AIN sjis_invalid >= 16", sc.sjis_invalid >= 16, 1);
	gk_expect_int("real AIN utf8_valid below half", sc.utf8_valid * 2 < sc.nonascii, 1);

	static const char *const sjis[] = {
		"\x82\xA0\x82\xA2\x82\xA4",     // hiragana
		"\x83\x65\x83\x58\x83\x67",     // katakana
		"\xB1\xB2\xB3",                 // half-width katakana
		"menu",
	};
	strict = gk_detect_list(detect, "synthetic SJIS", sjis, 4, 25, &sc);
	gk_expect_int("synthetic SJIS legacy > 5 (old check misfires)", sc.legacy > 5, 1);
	gk_expect_int("synthetic SJIS strict", strict, 0);

	static const char *const utf8[] = {
		"\xE3\x82\xBB\xE3\x83\xBC\xE3\x83\x96", "\xE3\x83\xAD\xE3\x83\xBC\xE3\x83\x89",
		"\xE8\xA8\xAD\xE5\xAE\x9A", "\xE7\xB5\x82\xE4\xBA\x86", "\xE3\x81\xAF\xE3\x81\x84",
		"\xE3\x81\x84\xE3\x81\x84\xE3\x81\x88", "\xE7\xA2\xBA\xE8\xAA\x8D",
		"\xE3\x82\xAD\xE3\x83\xA3\xE3\x83\xB3\xE3\x82\xBB\xE3\x83\xAB", "\xE5\x90\x8D\xE5\x89\x8D",
		"\xE3\x81\x8A\xE5\xBA\x97", "\xE3\x81\x8A\xE9\x87\x91", "\xE6\x97\xA5\xE4\xBB\x98",
		"\xE6\x99\x82\xE9\x96\x93", "\xE3\x82\xAD\xE3\x83\xA3\xE3\x83\xA9",
		"\xE3\x83\xA1\xE3\x83\x83\xE3\x82\xBB\xE3\x83\xBC\xE3\x82\xB8", "\xE6\x88\xBB\xE3\x82\x8B",
	};
	strict = gk_detect_list(detect, "synthetic UTF-8", utf8, 16, 7, &sc);
	gk_expect_int("synthetic UTF-8 legacy > 5", sc.legacy > 5, 1);
	gk_expect_int("synthetic UTF-8 boundary > 5 && sjis_invalid >= 16", sc.boundary > 5 && sc.sjis_invalid >= 16, 1);
	gk_expect_int("synthetic UTF-8 strict (UTF-8 excluded)", strict, 0);

	static const char *const gbk[] = { QI_LA, PANEL, ACHV, BASE4, "ok" };
	strict = gk_detect_list(detect, "synthetic GBK", gbk, 5, 20, &sc);
	gk_expect_int("synthetic GBK strict", strict, 1);

	static const char *const holes[] = { NULL, "x", NULL };
	strict = gk_detect_list(detect, "NULL entries", holes, 3, 1, &sc);
	gk_expect_int("NULL entries strict", strict, 0);
	strict = gk_detect_list(detect, "empty list", holes, 0, 1, &sc);
	gk_expect_int("empty list strict", strict, 0);
	gk_expect_int("empty list legacy", sc.legacy, 0);
	printf("GBK-DETECT summary: %d failure(s)\n", gk_fails);
	return gk_fails ? 89 : 0;
}

static int test_gbk_string(void)
{
	gk_enable();
	gk_string_cases();
	gk_render_cases();
	printf("GBK-STRING summary: %d failure(s)\n", gk_fails);
	return gk_fails ? 89 : 0;
}

static int test_gbk_vm(void)
{
	gk_force_sjis();
	int differ = gk_child_int(gk_motion_child, NULL);
	gk_gbk = 1;
	gk_expect_int("Motion parse trees differing between rules [child]", differ, 0);
	gk_force_sjis();
	gk_iarray_case("iarray round trip QI_LA, SJIS rule (control)", GB(QI_LA), GB(QI_LA "\xFF"));
	gk_enable();
	gk_vm_cases();
	printf("GBK-VM summary: %d failure(s)\n", gk_fails);
	return gk_fails ? 89 : 0;
}

static int test_sjis_chars(void)
{
	gk_force_sjis();
	printf("SJIS-CHARS rule=%s ain_is_gb18030=%d\n", gk_charset(), ain_is_gb18030);
	gk_string_cases();
	gk_vm_cases();
	// real SJIS text (hiragana a/i/u)
	gk_expect_int("Length(82A0 82A2 82A4)", gk_call_int("Length", GB("\x82\xA0\x82\xA2\x82\xA4"), 0, NULL), 3);
	gk_expect_int("Find(82A0 82A2 82A4, 82A4)",
		      gk_call_int("Find", GB("\x82\xA0\x82\xA2\x82\xA4"), 1, (struct gk_arg[]){ AS("\x82\xA4") }), 2);
	gk_str_case("GetPart#1(82A0 82A2 82A4, 1, 1)", "GetPart", GB("\x82\xA0\x82\xA2\x82\xA4"), 2,
		    (struct gk_arg[]){ AI(1), AI(1) }, GB("\x82\xA2"), (struct gk_bytes){ NULL, -1 });
	gk_mut_case("PopBack(82A0 82A2)", "PopBack", GB("\x82\xA0\x82\xA2"), 0, NULL, GB("\x82\xA0"));
	gk_mut_case("PushBack(\"\", 0xA082)", "PushBack", GB(""), 1, (struct gk_arg[]){ AI(0xA082) }, GB("\x82\xA0"));
	gk_walk_case("extract(82A0) SJIS", "\x82\xA0", 2, 2, GB("\x82\xA0"), true);
	gk_walk_case("extract(B1) SJIS half-width", "\xB1", 1, 1, GB("\xB1"), true);
	printf("SJIS-CHARS summary: %d failure(s)\n", gk_fails);
	return gk_fails ? 89 : 0;
}

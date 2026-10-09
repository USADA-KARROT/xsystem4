/* key-wait: the key wait mark (キー待ちマーク, the NEXT sign) of a message
 * window, 2026-10-09.
 *
 * The original keeps the mark as the third private child of the window
 * object (0x4f1d30: background, text, mark) and builds it from the window's
 * key wait properties (0x4f2370): the CG of the name when the three loop
 * numbers are 0, else a loop CG whose name is a printf format (0x5b30f0); it
 * shows while KeyWaitShow is set, at the background's corner plus the
 * position (0x4f2790), and a loop CG steps as 0x5b3090 says. The ten HLL
 * calls are native cases 513 to 522 (0x5953d0 .. 0x5955f0), the pactex block
 * is read at 0x4f0821 .. 0x4f0c5a.
 *
 * KW1 the properties through the HLL (the empty stubs return nothing).
 * KW2 a single CG: when it is there, where, what is loaded and when.
 * KW3 a loop CG: the names asked for, the frame after each update, the
 *     return to the first frame when the mark is shown again.
 * KW4 the pactex block.
 * KW5 the loop advances with the engine's own updates (PE_Update,
 *     PE_UpdateParts).
 * KW6 a loop CG's name with a wide number in it: the name goes through the
 *     engine's string_format, whose width went to int_to_cstr as it was, and
 *     512 or more wrote past a buffer on the stack. The width is kept below
 *     that now; the usual formats give what they gave (guards). The limit is
 *     xsystem4's own: the original formats the name with a formatter of its
 *     own (0x41dda0), which was not read.
 *
 * The probe has no archives: the mark's CGs come from kw_load_cg, put in the
 * engine's loader hook. Requires base_ui, layer_root and construction
 * fixtures. */
#include <dlfcn.h>

static int kw_failures;
#define KW_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); kw_failures++; } } while (0)

/* The probe's CGs: "markB" is 8x6, "mark_01" .. "mark_04" are 11x7 .. 14x7
 * (the width tells the frame), anything else does not exist. kw_missing
 * takes one of the four away. */
static char kw_log[256];
static int kw_loads, kw_missing;
static size_t kw_last_len;
static struct cg *kw_load_cg(const char *name)
{
	kw_loads++;
	kw_last_len = strlen(name);
	size_t used = strlen(kw_log);
	snprintf(kw_log + used, sizeof(kw_log) - used, "%s;", name);
	int w = 0, h = 0;
	if (!strcmp(name, "markB")) {
		w = 8;
		h = 6;
	} else if (strlen(name) == 7 && !strncmp(name, "mark_0", 6) && name[6] >= '1' && name[6] <= '4'
		   && name[6] - '0' != kw_missing) {
		w = 10 + (name[6] - '0');
		h = 7;
	}
	if (!w)
		return NULL;
	struct cg *cg = xcalloc(1, sizeof(*cg));
	cg->metrics.w = w;
	cg->metrics.h = h;
	cg->metrics.bpp = 32;
	cg->metrics.has_pixel = true;
	cg->metrics.has_alpha = true;
	cg->pixels = xcalloc(w * h, 4);
	return cg;
}

static Texture *(*kw_mark_fn)(struct parts *, Point *);
static void (*kw_update_fn)(int);
static bool kw_hooks(void)
{
	kw_mark_fn = (Texture *(*)(struct parts *, Point *))dlsym(RTLD_DEFAULT, "parts_message_window_key_wait");
	kw_update_fn = (void (*)(int))dlsym(RTLD_DEFAULT, "parts_message_window_update");
	struct cg *(**loader)(const char *) = (struct cg *(**)(const char *))dlsym(RTLD_DEFAULT, "parts_key_wait_load_cg");
	KW_CHECK(kw_mark_fn && kw_update_fn && loader, "the parts engine has no key wait mark (mark %d, update %d, loader %d)",
		 !!kw_mark_fn, !!kw_update_fn, !!loader);
	if (!kw_mark_fn || !kw_update_fn || !loader)
		return false;
	*loader = kw_load_cg;
	return true;
}

static void kw_set_cg(int no, const char *name, int start, int nr, int time)
{
	BU_PE("SetKeyWaitCGName", BU_I(no), BU_I(sf_str(name, -1)), BU_I(start), BU_I(nr), BU_I(time));
}
static void kw_show(int no, bool show) { BU_PE("SetKeyWaitShow", BU_I(no), BU_I(show)); }

/* GetKeyWaitCGName(Number, wrap<string> Name, wrap<int> StartNo, NumofCG,
 * TimePerCG): the string wrap is the slot of the target string, each int
 * wrap is (page, index). The outputs hold "?" and `fill` before. */
struct kw_cg { char name[64]; int start, nr, time; };
static struct kw_cg kw_get_cg(int no, int fill)
{
	int lib = bu_lib("PartsEngine"), fn = sf_find(lib, "GetKeyWaitCGName", 5);
	if (fn < 0) { printf("KEY-WAIT GetKeyWaitCGName/5 not declared\n"); fflush(stdout); _exit(CN_ENV_EXIT); }
	int name = sf_str("?", -1);
	int out = heap_alloc_slot(VM_PAGE);
	heap_set_page(out, alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 3));
	for (int i = 0; i < 3; i++)
		heap[out].page->values[i].i = fill;
	int sp = stack_ptr;
	stack_push(no);
	stack_push(name);
	for (int i = 0; i < 3; i++) {
		stack_push(out);
		stack_push(i);
	}
	hll_call(lib, fn, 0);
	stack_ptr = sp;
	struct kw_cg g = { .start = heap[out].page->values[0].i, .nr = heap[out].page->values[1].i,
			   .time = heap[out].page->values[2].i };
	snprintf(g.name, sizeof(g.name), "%s", heap[name].s ? heap[name].s->text : "(null)");
	heap_unref(out);
	heap_unref(name);
	return g;
}
static void kw_expect_cg(int no, const char *name, int start, int nr, int time, const char *what)
{
	struct kw_cg g = kw_get_cg(no, -7);
	KW_CHECK(!strcmp(g.name, name) && g.start == start && g.nr == nr && g.time == time,
		 "%s: GetKeyWaitCGName gives \"%s\", %d, %d, %d; want \"%s\", %d, %d, %d", what, g.name, g.start, g.nr,
		 g.time, name, start, nr, time);
}
static void kw_expect_pos(int no, int x, int y, int z, const char *what)
{
	int gx = BU_PE("GetKeyWaitPosX", BU_I(no)).i, gy = BU_PE("GetKeyWaitPosY", BU_I(no)).i,
	    gz = BU_PE("GetKeyWaitPosZ", BU_I(no)).i;
	KW_CHECK(gx == x && gy == y && gz == z, "%s: position (%d,%d,%d), want (%d,%d,%d)", what, gx, gy, gz, x, y, z);
}
static void kw_expect_flat(int no, const char *name, const char *what)
{
	int s = BU_PE("GetKeyWaitFlatName", BU_I(no)).i;
	const char *got = s > 0 && heap[s].s ? heap[s].s->text : "(none)";
	KW_CHECK(!strcmp(got, name), "%s: flat name \"%s\", want \"%s\"", what, got, name);
}

/* --- KW1 --- */
static void kw_properties(void)
{
	lr_setup();
	const int W = 9700, NONE = 9701;
	/* A window that nothing was set on: the defaults (0x4ee4c0). */
	BU_PE("SetMessageWindowTextArea", BU_I(W), BU_I(1), BU_I(2), BU_I(3), BU_I(4));
	kw_expect_cg(W, "", 0, 0, 0, "a new window");
	kw_expect_pos(W, 0, 0, 0, "a new window");
	kw_expect_flat(W, "", "a new window");
	KW_CHECK(!BU_PE("IsKeyWaitShow", BU_I(W)).i, "a new window shows its mark");

	kw_set_cg(W, "mark_%02d", 1, 4, 500);
	kw_expect_cg(W, "mark_%02d", 1, 4, 500, "after SetKeyWaitCGName");
	BU_PE("SetKeyWaitPos", BU_I(W), BU_I(930), BU_I(674), BU_I(5));
	kw_expect_pos(W, 930, 674, 5, "after SetKeyWaitPos");
	BU_PE("SetKeyWaitFlatName", BU_I(W), BU_I(sf_str("flat", -1)));
	kw_expect_flat(W, "flat", "after SetKeyWaitFlatName");
	kw_show(W, true);
	KW_CHECK(BU_PE("IsKeyWaitShow", BU_I(W)).i == 1, "IsKeyWaitShow is not 1 after SetKeyWaitShow(true)");
	kw_show(W, false);
	KW_CHECK(BU_PE("IsKeyWaitShow", BU_I(W)).i == 0, "IsKeyWaitShow is not 0 after SetKeyWaitShow(false)");
	/* One value of the four changes. */
	kw_set_cg(W, "mark_%02d", 1, 4, 250);
	kw_expect_cg(W, "mark_%02d", 1, 4, 250, "the time alone changed");
	kw_set_cg(W, "markB", 0, 0, 0);
	kw_expect_cg(W, "markB", 0, 0, 0, "a single CG");
	kw_expect_pos(W, 930, 674, 5, "the position after the name changed");

	/* No window object for a number of 0 or less (0x539c80): the setters do
	 * nothing, the getters give 0, false and "" and leave their outputs. */
	static const int bad[] = { 0, -3 };
	for (int i = 0; i < 2; i++) {
		int n = bad[i];
		kw_set_cg(n, "markB", 1, 2, 3);
		BU_PE("SetKeyWaitPos", BU_I(n), BU_I(7), BU_I(8), BU_I(9));
		BU_PE("SetKeyWaitFlatName", BU_I(n), BU_I(sf_str("flat", -1)));
		kw_show(n, true);
		KW_CHECK(!parts_try_get(n), "a setter made the parts %d", n);
		struct kw_cg g = kw_get_cg(n, -7);
		KW_CHECK(!strcmp(g.name, "?") && g.start == -7 && g.nr == -7 && g.time == -7,
			 "number %d: GetKeyWaitCGName wrote \"%s\", %d, %d, %d", n, g.name, g.start, g.nr, g.time);
		kw_expect_pos(n, 0, 0, 0, "number 0 or less");
		kw_expect_flat(n, "", "number 0 or less");
		KW_CHECK(!BU_PE("IsKeyWaitShow", BU_I(n)).i, "number %d shows a mark", n);
	}
	/* The getters do not make a parts that is not there either. This is
	 * xsystem4's choice, not the original's: there a getter makes the parts
	 * a message window as a setter does (0x578f24 -> 0x578cb0, 0x536c20) and
	 * gives its defaults, "" and 0 (0x595410). No script reads the mark of
	 * a window that is not there. */
	struct kw_cg g = kw_get_cg(NONE, -7);
	KW_CHECK(!strcmp(g.name, "?") && g.start == -7, "a missing parts: GetKeyWaitCGName wrote \"%s\", %d", g.name, g.start);
	kw_expect_pos(NONE, 0, 0, 0, "a missing parts");
	kw_expect_flat(NONE, "", "a missing parts");
	KW_CHECK(!BU_PE("IsKeyWaitShow", BU_I(NONE)).i && !parts_try_get(NONE), "a getter made the missing parts or it shows a mark");
	printf("KW1 the properties: %s\n", kw_failures ? "FAIL" : "ok");
}

/* The mark of the parts now: its width (the frame), 0 without one. */
static int kw_width(int no, Point *pos)
{
	Point p = { -1, -1 };
	Texture *t = kw_mark_fn(parts_get(no), &p);
	if (pos)
		*pos = p;
	return t ? t->w * 100 + t->h : 0;
}
#define KW_FRAME(n) ((10 + (n)) * 100 + 7)	/* mark_0n */
#define KW_SINGLE (8 * 100 + 6)			/* markB */

/* --- KW2 --- */
static void kw_single(void)
{
	cn_gl_setup();
	lr_setup();
	if (!kw_hooks()) {
		printf("KW2 a single CG: FAIL\n");
		return;
	}
	const int W = 9710, PLAIN = 9711;
	struct parts *p = parts_get(W);
	kw_set_cg(W, "markB", 0, 0, 0);
	BU_PE("SetKeyWaitPos", BU_I(W), BU_I(30), BU_I(40), BU_I(0));
	p->global.pos = (Point){ 100, 200 };
	p->states[PARTS_STATE_DEFAULT].common.origin_offset = (Point){ -64, -72 };
	Point at;
	KW_CHECK(kw_width(W, &at) == 0, "a hidden mark is drawn");
	kw_show(W, true);
	int got = kw_width(W, &at);
	KW_CHECK(got == KW_SINGLE && at.x == 100 - 64 + 30 && at.y == 200 - 72 + 40,
		 "the mark: size %d at (%d,%d), want %d at (66,168): the owner's position, the background's origin offset and the position",
		 got, at.x, at.y, KW_SINGLE);
	KW_CHECK(kw_loads == 1 && !strcmp(kw_log, "markB;"), "loaded %d time(s): %s, want markB once", kw_loads, kw_log);
	/* Hidden and shown again, and time passing: the same CG, not loaded
	 * again. */
	kw_show(W, false);
	kw_update_fn(100);
	KW_CHECK(kw_width(W, NULL) == 0, "the mark is drawn after SetKeyWaitShow(false)");
	kw_show(W, true);
	kw_update_fn(100000);
	kw_update_fn(100000);
	KW_CHECK(kw_width(W, NULL) == KW_SINGLE && kw_loads == 1, "shown again: size %d, loaded %d time(s); want %d, once",
		 kw_width(W, NULL), kw_loads, KW_SINGLE);
	/* It moves with its owner. */
	p->global.pos = (Point){ 300, 10 };
	kw_width(W, &at);
	KW_CHECK(at.x == 300 - 64 + 30 && at.y == 10 - 72 + 40, "the owner moved: the mark is at (%d,%d), want (266,-22)", at.x, at.y);
	BU_PE("SetKeyWaitPos", BU_I(W), BU_I(1), BU_I(2), BU_I(9));
	kw_width(W, &at);
	KW_CHECK(at.x == 300 - 64 + 1 && at.y == 10 - 72 + 2 && kw_loads == 1,
		 "SetKeyWaitPos: the mark is at (%d,%d), loaded %d time(s); want (237,-60), once", at.x, at.y, kw_loads);
	/* A CG that does not exist, and no name: no mark. */
	kw_set_cg(W, "nomark", 0, 0, 0);
	KW_CHECK(kw_width(W, NULL) == 0 && kw_loads == 2, "a CG that does not exist: size %d, loads %d; want none, 2",
		 kw_width(W, NULL), kw_loads);
	kw_set_cg(W, "", 0, 0, 0);
	KW_CHECK(kw_width(W, NULL) == 0 && kw_loads == 2, "no name: size %d, loads %d; want none, still 2", kw_width(W, NULL), kw_loads);
	kw_set_cg(W, "markB", 0, 0, 0);
	KW_CHECK(kw_width(W, NULL) == KW_SINGLE && kw_loads == 3, "the name again: size %d, loads %d; want %d, 3",
		 kw_width(W, NULL), kw_loads, KW_SINGLE);
	/* Guard: a parts that is not a message window has no mark, and the
	 * update does not make it one. */
	struct parts *plain = parts_get(PLAIN);
	kw_update_fn(16);
	KW_CHECK(!plain->message && kw_mark_fn(plain, &at) == NULL && !plain->message, "a plain parts got a message window");
	/* Releasing the window frees the mark (the sanitizers watch). */
	PE_ReleaseParts(W);
	kw_update_fn(16);
	printf("KW2 a single CG: %s\n", kw_failures ? "FAIL" : "ok");
}

/* --- KW3 --- */
static void kw_step(int no, int ms, int want, const char *what)
{
	kw_update_fn(ms);
	int got = kw_width(no, NULL);
	KW_CHECK(got == want, "%s (+%d ms): frame of size %d, want %d", what, ms, got, want);
}
static void kw_loop(void)
{
	cn_gl_setup();
	lr_setup();
	if (!kw_hooks()) {
		printf("KW3 a loop CG: FAIL\n");
		return;
	}
	const int W = 9720;
	parts_get(W);
	/* The script's mark of the main window: ("...A_%02d", 1, 4, 500). */
	kw_set_cg(W, "mark_%02d", 1, 4, 500);
	kw_show(W, true);
	KW_CHECK(kw_width(W, NULL) == KW_FRAME(1), "the first frame has size %d, want %d", kw_width(W, NULL), KW_FRAME(1));
	KW_CHECK(kw_loads == 4 && !strcmp(kw_log, "mark_01;mark_02;mark_03;mark_04;"), "loaded %d: %s; want mark_01 to mark_04",
		 kw_loads, kw_log);
	/* 0x5b3090: the first update after the build does not advance; then
	 * frame = (frame + (passed + rest) / time) % frames, rest = the
	 * remainder. */
	kw_step(W, 499, KW_FRAME(1), "the first update does not advance");
	kw_step(W, 499, KW_FRAME(1), "499 ms");
	kw_step(W, 1, KW_FRAME(2), "500 ms");
	kw_step(W, 1250, KW_FRAME(4), "two frames and 250 ms");
	kw_step(W, 250, KW_FRAME(1), "past the last frame");
	kw_step(W, 2600, KW_FRAME(2), "five frames and 100 ms");
	/* The same values set again change nothing: the rest of 100 ms stays. */
	kw_set_cg(W, "mark_%02d", 1, 4, 500);
	kw_step(W, 400, KW_FRAME(3), "the same values set again");
	/* The script sets Show in every frame of the key wait (FUNC 7153): the
	 * loop goes on while it stays set. */
	kw_show(W, true);
	kw_step(W, 300, KW_FRAME(3), "SetKeyWaitShow(true) while shown");
	kw_show(W, true);
	kw_step(W, 300, KW_FRAME(4), "and again");
	/* Hidden and shown again: back to the first frame, and the first
	 * update leaves it there; nothing is loaded again. */
	kw_show(W, false);
	KW_CHECK(kw_width(W, NULL) == 0, "a hidden loop CG is drawn");
	kw_show(W, true);
	KW_CHECK(kw_width(W, NULL) == KW_FRAME(1), "shown again: frame of size %d, want the first (%d)", kw_width(W, NULL), KW_FRAME(1));
	kw_step(W, 600, KW_FRAME(1), "the first update after showing again");
	kw_step(W, 600, KW_FRAME(2), "the second");
	KW_CHECK(kw_loads == 4, "showing again loaded CGs: %d loads, want 4", kw_loads);
	/* The rebuild happens in the update too, not only when drawing. */
	kw_show(W, false);
	kw_update_fn(700);
	kw_show(W, true);
	kw_update_fn(700);
	KW_CHECK(kw_width(W, NULL) == KW_FRAME(1), "hidden, updated, shown, updated: frame of size %d, want the first", kw_width(W, NULL));
	kw_step(W, 500, KW_FRAME(2), "and one frame later");
	/* Other numbers: the frames are loaded again, from the start number. */
	kw_log[0] = 0;
	kw_set_cg(W, "mark_%02d", 2, 2, 100);
	KW_CHECK(kw_width(W, NULL) == KW_FRAME(2) && !strcmp(kw_log, "mark_02;mark_03;"), "start 2, 2 frames: size %d, loaded %s",
		 kw_width(W, NULL), kw_log);
	kw_update_fn(1);
	kw_step(W, 100, KW_FRAME(3), "the second of two");
	kw_step(W, 100, KW_FRAME(2), "around");
	/* A time of 0 with frames: the first frame stays. */
	kw_set_cg(W, "mark_%02d", 3, 2, 0);
	kw_step(W, 1, KW_FRAME(3), "a time of 0, the first update");
	kw_step(W, 99999, KW_FRAME(3), "a time of 0");
	kw_step(W, 99999, KW_FRAME(3), "a time of 0, once more");
	/* One frame missing fails the whole loop CG, and the frames after it are
	 * not asked for (0x5b30f0). That there is no mark at all then is
	 * xsystem4's choice: the original keeps the frames loaded before the
	 * missing one and does not set its name, numbers and current frame
	 * (0x5b32b1 .. 0x5b32cc), and what it draws from that was not followed.
	 * The four CGs of the game's mark are all there. */
	kw_missing = 3;
	kw_log[0] = 0;
	kw_set_cg(W, "mark_%02d", 1, 4, 400);
	KW_CHECK(kw_width(W, NULL) == 0 && !strcmp(kw_log, "mark_01;mark_02;mark_03;"),
		 "a missing frame: size %d, asked for %s; want no mark, mark_01 to mark_03", kw_width(W, NULL), kw_log);
	kw_missing = 0;
	/* No frames (the count is 0 but another number is not): no mark, and
	 * the name is not taken as a single CG. */
	kw_log[0] = 0;
	kw_set_cg(W, "markB", 0, 0, 500);
	kw_update_fn(600);
	kw_update_fn(600);
	KW_CHECK(kw_width(W, NULL) == 0 && !kw_log[0], "a loop CG of 0 frames: size %d, asked for \"%s\"; want no mark, nothing", kw_width(W, NULL), kw_log);
	kw_set_cg(W, "markB", 0, 0, 0);
	KW_CHECK(kw_width(W, NULL) == KW_SINGLE, "back to a single CG: size %d, want %d", kw_width(W, NULL), KW_SINGLE);
	printf("KW3 a loop CG: %s\n", kw_failures ? "FAIL" : "ok");
}

/* --- KW4 --- */
#define KW_MSGWIN "\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda"							/* 信息窗口 */
#define KW_BLOCK "\xa5\xad\xa9\x60\xb4\xfd\xa4\xc1\xa5\xde\xa9\x60\xa5\xaf"				/* キー待ちマーク */
#define KW_CGNAME "\xa3\xc3\xa3\xc7\xc3\xfb"								/* ＣＧ名 */
#define KW_FLATNAME "\xa5\xd5\xa5\xe9\xa5\xc3\xa5\xc8\xc3\xfb"						/* フラット名 */
#define KW_START "\xd1\xad\xad\x68\xa3\xc3\xa3\xc7\xe9\x5f\xca\xbc\xb7\xac\xcc\x96"			/* 循環ＣＧ開始番號 */
#define KW_NR "\xd1\xad\xad\x68\xa3\xc3\xa3\xc7\xc3\xb6\x94\xb5"					/* 循環ＣＧ枚數 */
#define KW_TIME "\xd1\xad\xad\x68\xa3\xc3\xa3\xc7\xc7\xd0\x93\x51\x95\x72\xe9\x67"			/* 循環ＣＧ切換時間 */
#define KW_POS "\xd7\xf9\x98\xcb"									/* 座標 */

static struct ex_value kw_ilist(int n, const int *v)
{
	struct ex_list *l = xcalloc(1, sizeof(*l));
	l->nr_items = n;
	l->items = xcalloc(n, sizeof(*l->items));
	for (int i = 0; i < n; i++)
		l->items[i].value = (struct ex_value){ .type = EX_INT, .i = v[i] };
	return (struct ex_value){ .type = EX_LIST, .list = l };
}

/* The probe's ReadActivityFile: WinA with the whole block (as
 * AdvMessageWindow_event's, with other values), WinB without the block, WinC
 * with a block of the CG name alone and a position of two items. */
static bool kw_read_activity_file(struct string *name, possibly_unused struct string *filename, possibly_unused bool edit)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("KeyWaitActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("KeyWaitActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("\xa5\xeb\xa9\x60\xa5\xc8\xb2\xbf\xbc\xfe", BU_LAYOUT, 0, 3);
	struct ex_tree *kids = &top->children[0].children[1];
	kids->children[0] = bu_comp("WinA", KW_MSGWIN, 1, 0);
	struct ex_tree *block = &kids->children[0].children[0].children[1];
	*block = bu_branch(KW_BLOCK, 6);
	block->children[0] = bu_leaf(KW_POS, kw_ilist(3, (const int[]){ 930, 674, 3 }));
	block->children[1] = bu_sleaf(KW_CGNAME, "mark_%02d");
	block->children[2] = bu_sleaf(KW_FLATNAME, "fl");
	block->children[3] = bu_ileaf(KW_START, 1);
	block->children[4] = bu_ileaf(KW_NR, 4);
	block->children[5] = bu_ileaf(KW_TIME, 500);
	kids->children[1] = bu_comp("WinB", KW_MSGWIN, 0, 0);
	kids->children[2] = bu_comp("WinC", KW_MSGWIN, 1, 0);
	block = &kids->children[2].children[0].children[1];
	*block = bu_branch(KW_BLOCK, 2);
	block->children[0] = bu_sleaf(KW_CGNAME, "markB");
	block->children[1] = bu_leaf(KW_POS, kw_ilist(2, (const int[]){ 755, 148 }));
	bool ok = probe_load_activity_tree(name, e);
	ex_free(e);
	return ok;
}

static void kw_loader(void)
{
	lr_setup();
	lr_replace_hll("PartsEngine", "ReadActivityFile", (void *)kw_read_activity_file);
	int n = sf_str("KeyWait", -1), f = sf_str("KeyWait", -1);
	bool ok = BU_PE("ReadActivityFile", BU_I(n), BU_I(f), BU_I(0)).i;
	int a = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("WinA", -1))).i;
	int b = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("WinB", -1))).i;
	int c = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str("WinC", -1))).i;
	KW_CHECK(ok && parts_try_get(a) && parts_try_get(b) && parts_try_get(c), "the activity: loaded %d, parts %d, %d, %d", ok, a, b, c);
	if (!parts_try_get(a) || !parts_try_get(b) || !parts_try_get(c)) {
		printf("KW4 the pactex block: FAIL\n");
		return;
	}
	kw_expect_cg(a, "mark_%02d", 1, 4, 500, "WinA");
	kw_expect_pos(a, 930, 674, 3, "WinA");
	kw_expect_flat(a, "fl", "WinA");
	KW_CHECK(!BU_PE("IsKeyWaitShow", BU_I(a)).i, "a loaded window shows its mark");
	kw_expect_cg(b, "", 0, 0, 0, "WinB (no block)");
	kw_expect_pos(b, 0, 0, 0, "WinB (no block)");
	kw_expect_flat(b, "", "WinB (no block)");
	kw_expect_cg(c, "markB", 0, 0, 0, "WinC (the name alone)");
	kw_expect_pos(c, 755, 148, 0, "WinC (a position of two items)");
	kw_expect_flat(c, "", "WinC");
	printf("KW4 the pactex block: %s\n", kw_failures ? "FAIL" : "ok");
}

/* --- KW5 --- */
static void kw_engine_update(void)
{
	cn_gl_setup();
	lr_setup();
	if (!kw_hooks()) {
		printf("KW5 the engine's updates: FAIL\n");
		return;
	}
	const int W = 9730;
	parts_get(W);
	kw_set_cg(W, "mark_%02d", 1, 4, 500);
	kw_show(W, true);
	PE_Update(500, true);
	KW_CHECK(kw_width(W, NULL) == KW_FRAME(1), "the first PE_Update: frame of size %d, want the first (%d)", kw_width(W, NULL), KW_FRAME(1));
	PE_Update(500, true);
	KW_CHECK(kw_width(W, NULL) == KW_FRAME(2), "the second PE_Update(500): frame of size %d, want %d", kw_width(W, NULL), KW_FRAME(2));
	PE_UpdateParts(1000, false, true);
	KW_CHECK(kw_width(W, NULL) == KW_FRAME(4), "PE_UpdateParts(1000): frame of size %d, want %d", kw_width(W, NULL), KW_FRAME(4));
	printf("KW5 the engine's updates: %s\n", kw_failures ? "FAIL" : "ok");
}

/* --- KW6 --- */
static struct string *kw_format(const char *fmt, union vm_value v, enum string_format_type type)
{
	struct string *f = cstr_to_string(fmt);
	struct string *out = string_format(f, v, type);
	free_string(f);
	return out;
}
static void kw_format_is(const char *fmt, int v, const char *want)
{
	struct string *out = kw_format(fmt, (union vm_value){ .i = v }, STRFMT_INT);
	KW_CHECK(!strcmp(out->text, want), "\"%s\" of %d gives \"%s\", want \"%s\"", fmt, v, out->text, want);
	free_string(out);
}
/* The formatted number is `size` bytes of `fill` and then `last`. */
static void kw_format_wide(const char *fmt, union vm_value v, enum string_format_type type, int size, char fill, char last)
{
	struct string *out = kw_format(fmt, v, type);
	bool ok = out->size == size && (int)strlen(out->text) == size && size >= 2 && out->text[size - 1] == last;
	for (int i = 0; ok && i < size - 1; i++)
		ok = out->text[i] == fill;
	KW_CHECK(ok, "\"%s\" gives %d bytes ending in '%c', want %d of '%c' and a '%c'", fmt, out->size,
		 out->size ? out->text[out->size - 1] : '?', size - 1, fill, last);
	free_string(out);
}

static void kw_format_width(void)
{
	cn_gl_setup();
	lr_setup();
	if (!kw_hooks()) {
		printf("KW6 a wide number in the name: FAIL\n");
		return;
	}
	/* The mark's own path: the name asked for is "mark_", 509 spaces and
	 * the start number; there is no such CG and no mark. */
	const int W = 9740;
	parts_get(W);
	kw_set_cg(W, "mark_%600d", 1, 2, 500);
	kw_show(W, true);
	KW_CHECK(kw_width(W, NULL) == 0 && kw_loads == 1 && kw_last_len == 5 + 510,
		 "a width of 600 in the name: size %d, %d load(s), a name of %zu bytes; want no mark, 1, 515", kw_width(W, NULL), kw_loads,
		 kw_last_len);
	/* The number alone: a width of 510 at most, however it is written. */
	kw_format_wide("%600d", (union vm_value){ .i = 7 }, STRFMT_INT, 510, ' ', '7');
	kw_format_wide("%0600d", (union vm_value){ .i = 7 }, STRFMT_INT, 510, '0', '7');
	kw_format_wide("%99999999999d", (union vm_value){ .i = 7 }, STRFMT_INT, 510, ' ', '7');
	kw_format_wide("%511d", (union vm_value){ .i = 7 }, STRFMT_INT, 510, ' ', '7');
	kw_format_wide("%600.0f", (union vm_value){ .f = 7.0f }, STRFMT_FLOAT, 510, ' ', '7');
	/* a precision of 462 at most: "0." and the digits */
	struct string *out = kw_format("%.600f", (union vm_value){ .f = 0.5f }, STRFMT_FLOAT);
	KW_CHECK(out->size == 2 + 462 && !strncmp(out->text, "0.5000", 6), "\"%%.600f\" gives %d bytes (%.8s...), want 464 from 0.5000",
		 out->size, out->text);
	free_string(out);
	/* a zenkaku number takes two bytes a character: 255 characters */
	out = kw_format("%600D", (union vm_value){ .i = 7 }, STRFMT_INT);
	KW_CHECK(out->size == 510, "\"%%600D\" gives %d bytes, want 510", out->size);
	free_string(out);
	out = kw_format("%600F", (union vm_value){ .f = 1.5f }, STRFMT_FLOAT);
	KW_CHECK(out->size == 510 || out->size == 511, "\"%%600F\" gives %d bytes, want 510 (or 511 with the F of the SJIS rule)", out->size);
	free_string(out);
	/* Guards: widths that fitted before give what they gave. */
	kw_format_is("mark_%02d", 3, "mark_03");
	kw_format_is("%d", -45, "-45");
	kw_format_is("a%5db", 42, "a   42b");
	kw_format_is("%05d", 42, "00042");
	kw_format_is("%2d", 12345, "12345");
	kw_format_is("a%sb", 1, "a%sb");
	kw_format_wide("%300d", (union vm_value){ .i = 7 }, STRFMT_INT, 300, ' ', '7');
	kw_format_wide("%510d", (union vm_value){ .i = 7 }, STRFMT_INT, 510, ' ', '7');
	out = kw_format("%8.3f", (union vm_value){ .f = 1.5f }, STRFMT_FLOAT);
	KW_CHECK(!strcmp(out->text, "   1.500"), "\"%%8.3f\" of 1.5 gives \"%s\", want \"   1.500\"", out->text);
	free_string(out);
	out = kw_format("%3D", (union vm_value){ .i = 7 }, STRFMT_INT);
	KW_CHECK(out->size == 6, "\"%%3D\" of 7 gives %d bytes, want 6 (three zenkaku characters)", out->size);
	free_string(out);
	printf("KW6 a wide number in the name: %s\n", kw_failures ? "FAIL" : "ok");
}

static int kw_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		kw_failures = 0;
		alarm(120);
		fn();
		fflush(stdout);
		_exit(kw_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_key_wait(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "KW1", kw_properties }, { "KW2", kw_single }, { "KW3", kw_loop }, { "KW4", kw_loader },
		{ "KW5", kw_engine_update }, { "KW6", kw_format_width },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += kw_fork(cases[i].name, cases[i].fn);
	printf("KEY-WAIT %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}

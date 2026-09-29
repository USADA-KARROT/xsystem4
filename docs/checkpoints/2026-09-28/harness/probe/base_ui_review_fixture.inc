/* base-ui-review: the review findings on the base screen fix (6d39915) and
 * the frame pacing fix (44964f9).
 *
 *   BR1 A disabled button (SetButtonEnable false) swallows a v14 click: no
 *       MouseClick message for it and no whole-screen click either. The
 *       original ignores clicks on the greyed 成员/商店 (Wine reference
 *       d035/d036); the AIN click lambdas and SceneHome@Exit do not check
 *       Enable, so the engine must not send the click.
 *   BR2 The v14 click dispatch hit-tests the default state, as the hover
 *       does. A ＣＧ判定部件 detector only has a hit area in its normal state;
 *       the press switches it to the down state first, so testing the
 *       current state missed (the status page's 返回 could not be pressed).
 *   BR3 Array.At/First/Last on two-slot elements (0x10003, an interface):
 *       the AIN takes the returned element as two slots (X_MOV 4 2; X_ASSIGN
 *       2 into an interface local, e.g. SceneParentStack@Get), and Last is
 *       the last element, not the last slot. BR5 runs First/Last in real
 *       bytecode.
 *   BR4 Other two-slot Array calls: First(pred) and EraseAll walk elements,
 *       Concat, Reverse and Insert keep the element pairs. Real bytecode:
 *       Motion::PartsParamCollection@0 finds its TimeParam with First(pred).
 *   BR5 Real bytecode AnimateText@AdjustPos centres its characters (the
 *       banner and menu header titles were drawn about 73 px to the right).
 *   BR6 pactex 矩形部件 (25) and 構築部件 (26) states report their type, so
 *       CompParts(name, 25/26, 1) finds them (ThumbnailPage GetRect,
 *       StandView GetConstruction asserted).
 *   BR7 The engine's exits (vm_exit, VM errors) wait for a pending
 *       background screenshot write; sys_exit is _exit, so atexit never ran.
 * Each case runs in its own fork. Only functions that exist before the fix
 * are referenced. No game assets or GL are used.
 */
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "input.h"
#include "system4/cg.h"

static int br_failures;
#define BR_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); br_failures++; } } while (0)
#define BR_I(v) ((union vm_value){ .i = (v) })

/* Calls lib.fn through its AIN declaration and copies every result slot the
 * call left on the stack into out[]; returns their count. */
static int br_call(const char *libname, const char *fn, int nargs, int arg3, int nslots,
		const union vm_value *slots, union vm_value *out, int maxout)
{
	int lib = bu_lib(libname), f = sf_find(lib, fn, nargs);
	if (f < 0) { printf("BASE-UI-REVIEW %s.%s/%d not declared\n", libname, fn, nargs); exit(2); }
	int sp = stack_ptr;
	for (int i = 0; i < nslots; i++)
		stack_push(slots[i]);
	hll_call(lib, f, arg3);
	int n = stack_ptr - sp;
	for (int i = 0; i < n && i < maxout; i++)
		out[i] = stack[sp + i];
	stack_ptr = sp;
	return n;
}

static int br_member(int st, const char *name)
{
	for (int i = 0; i < ain->structures[st].nr_members; i++)
		if (!strcmp(ain->structures[st].members[i].name, name))
			return i;
	printf("BASE-UI-REVIEW member %s missing\n", name);
	exit(2);
}

static int br_vtoff(const char *struct_name, const char *iface_name)
{
	int st = ain_get_struct(ain, (char *)struct_name), is = ain_get_struct(ain, (char *)iface_name);
	assert(st >= 0 && is >= 0);
	for (int i = 0; i < ain->structures[st].nr_interfaces; i++)
		if (ain->structures[st].interfaces[i].struct_type == is)
			return ain->structures[st].interfaces[i].vtable_offset;
	printf("BASE-UI-REVIEW %s does not implement %s\n", struct_name, iface_name);
	exit(2);
}

/* A generic two-slot array with the given [object, second slot] pairs. */
static int br_pairs(int n, const int *pairs)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY, 2 * n);
	a->array.rank = 1;
	a->array.struct_type = 2;
	for (int i = 0; i < 2 * n; i++) {
		a->values[i].i = pairs[i];
		if (!(i & 1) && pairs[i] > 0)
			heap_ref(pairs[i]);
	}
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, a);
	return slot;
}

/* --- BR1 / BR2: the v14 click dispatch --- */

/* The v14 message queue accessors. The probe links without _PreLink, so take
 * them from the static export table after the v14 replacement. */
static int (*br_msg_type)(void), (*br_msg_parts)(void);
static void (*br_msg_pop)(void), (*br_msg_release)(void);

static void *br_pe_fn(const char *name)
{
	extern struct static_library lib_PartsEngine;
	for (int i = 0; lib_PartsEngine.functions[i].name; i++)
		if (!strcmp(lib_PartsEngine.functions[i].name, name))
			return lib_PartsEngine.functions[i].fun;
	return NULL;
}

static void br_input_setup(void)
{
	bu_postlink();
	extern void pe_v14_message_replace(void);
	pe_v14_message_replace();
	br_msg_type = (int (*)(void))br_pe_fn("GetMessageType");
	br_msg_parts = (int (*)(void))br_pe_fn("GetMessagePartsNumber");
	br_msg_pop = (void (*)(void))br_pe_fn("PopMessage");
	br_msg_release = (void (*)(void))br_pe_fn("ReleaseMessage");
	assert(br_msg_type && br_msg_parts && br_msg_pop && br_msg_release);
	sdl.w = 1280; sdl.h = 720;
	sdl.viewport = (SDL_Rect){ 0, 0, 1280, 720 };
	heap[global_page_slot].page->values[2].i = 0;	/* g_EndPartsBusyLoop */
	br_msg_release();
}

static struct parts *br_box(int no, int x, int y, int w, int h, bool all_states)
{
	struct parts *p = parts_get(no);
	PE_SetPos(no, x, y);
	for (int s = 1; s <= (all_states ? 3 : 1); s++)
		PE_SetPartsRectangleDetectionSize(no, w, h, s);
	PE_SetClickable(no, true);
	return p;
}

/* One press at (x, y) after a released frame; returns the number of queued
 * messages and the first one's type and parts number. */
static int br_click(int x, int y, int *type, int *no)
{
	extern bool key_state[];
	mouse_set_pos(x, y);
	PE_UpdateComponent(0);
	PE_BeginInput();
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	key_state[VK_LBUTTON] = true;
	PE_UpdateInputState(0);
	key_state[VK_LBUTTON] = false;
	PE_UpdateInputState(0);
	int n = 0;
	*type = -1; *no = -1;
	for (int t; (t = br_msg_type()) >= 0; br_msg_pop()) {
		if (!n++) {
			*type = t;
			*no = br_msg_parts();
		}
	}
	PE_EndInput();
	return n;
}

/* BR1 */
static void br_disabled_button(void)
{
	br_input_setup();
	const int B = 9801, E = 9802;
	br_box(B, 100, 100, 200, 80, true);
	br_box(E, 500, 100, 200, 80, true);
	int type, no;
	int n = br_click(600, 140, &type, &no);
	BR_CHECK(n == 1 && type == 4 && no == E, "enabled button: %d messages, first type %d parts %d; want 1, 4, %d", n, type, no, E);
	bu_call("PartsEngine", "SetButtonEnable", 2, 0, 2, (union vm_value[]){ BR_I(B), BR_I(0) });
	heap[global_page_slot].page->values[2].i = 0;
	n = br_click(150, 140, &type, &no);
	int busy = heap[global_page_slot].page->values[2].i;
	BR_CHECK(n == 0 && busy == 0, "disabled button: %d messages (first type %d parts %d), g_EndPartsBusyLoop %d; want none and 0",
		 n, type, no, busy);
	bu_call("PartsEngine", "SetButtonEnable", 2, 0, 2, (union vm_value[]){ BR_I(B), BR_I(1) });
	n = br_click(150, 140, &type, &no);
	BR_CHECK(n == 1 && type == 4 && no == B, "re-enabled button: %d messages, first type %d parts %d; want 1, 4, %d", n, type, no, B);
	/* A click beside every parts is still the whole-screen click. */
	heap[global_page_slot].page->values[2].i = 0;
	n = br_click(1000, 600, &type, &no);
	busy = heap[global_page_slot].page->values[2].i;
	BR_CHECK(n == 1 && type == 4 && no == 0 && busy == 1, "background: %d messages, type %d parts %d, busy %d; want 1, 4, 0, 1",
		 n, type, no, busy);
	printf("BR1 disabled button: %s\n", br_failures ? "FAIL" : "ok");
}

/* BR2 */
static void br_detector(void)
{
	br_input_setup();
	const int D = 9811;
	/* Normal state: the hit area (ＣＧ判定部件); on-cursor and down states:
	 * CG parts without a CG, as FooterButton's Detector. */
	struct parts *d = br_box(D, 1109, 653, 142, 50, false);
	parts_get_cg(d, 1);
	parts_get_cg(d, 2);
	int type, no;
	int n = br_click(1180, 678, &type, &no);
	BR_CHECK(n == 1 && type == 4 && no == D, "detector click: %d messages, first type %d parts %d; want 1, 4, %d", n, type, no, D);
	printf("BR2 detector click: %s\n", br_failures ? "FAIL" : "ok");
}

/* --- BR3: two-slot At/First/Last --- */

static void br_two_slot_returns(void)
{
	int st = ain_get_struct(ain, "CASPos");
	assert(st >= 0);
	int A = vm_construct_struct(st), B = vm_construct_struct(st);
	int arr = br_pairs(2, (int[]){ A, 5, B, 7 });
	union vm_value r[4];
	int refB = HEAP_REF(B);
	int n = br_call("Array", "Last", 1, 0x10003, 1, (union vm_value[]){ BR_I(arr) }, r, 4);
	BR_CHECK(n == 2 && r[0].i == B && r[1].i == 7 && HEAP_REF(B) == refB + 1,
		 "Last: %d slots [%d %d], ref(B) %d -> %d; want 2 slots [%d 7], one ref", n, r[0].i, n > 1 ? r[1].i : 0, refB, HEAP_REF(B), B);
	n = br_call("Array", "At", 2, 0x10003, 2, (union vm_value[]){ BR_I(arr), BR_I(1) }, r, 4);
	BR_CHECK(n == 2 && r[0].i == B && r[1].i == 7, "At(1): %d slots [%d %d]; want 2 [%d 7]", n, r[0].i, n > 1 ? r[1].i : 0, B);
	n = br_call("Array", "At", 2, 0x10003, 2, (union vm_value[]){ BR_I(arr), BR_I(2) }, r, 4);
	BR_CHECK(n == 2 && r[0].i == -1 && r[1].i == 0, "At(2) out of range: %d slots [%d %d]; want 2 [-1 0]", n, r[0].i, n > 1 ? r[1].i : 0);
	n = br_call("Array", "First", 1, 0x10003, 1, (union vm_value[]){ BR_I(arr) }, r, 4);
	BR_CHECK(n == 2 && r[0].i == A && r[1].i == 5, "First: %d slots [%d %d]; want 2 [%d 5]", n, r[0].i, n > 1 ? r[1].i : 0, A);
	int empty = br_pairs(0, NULL);
	n = br_call("Array", "Last", 1, 0x10003, 1, (union vm_value[]){ BR_I(empty) }, r, 4);
	BR_CHECK(n == 2 && r[0].i == -1 && r[1].i == 0, "Last(empty): %d slots [%d %d]; want 2 [-1 0]", n, r[0].i, n > 1 ? r[1].i : 0);
	/* One-slot references keep one slot. */
	int C = vm_construct_struct(st);
	struct page *one = alloc_page(ARRAY_PAGE, AIN_ARRAY, 2);
	one->array.rank = 1;
	one->values[0].i = A; one->values[1].i = C;
	heap_ref(A); heap_ref(C);
	int ones = heap_alloc_slot(VM_PAGE);
	heap_set_page(ones, one);
	n = br_call("Array", "Last", 1, 0x10000 + st, 1, (union vm_value[]){ BR_I(ones) }, r, 4);
	BR_CHECK(n == 1 && r[0].i == C, "Last (struct elements): %d slots [%d]; want 1 [%d]", n, r[0].i, C);
	printf("BR3 two-slot At/First/Last: %s\n", br_failures ? "FAIL" : "ok");
}

/* --- BR4: two-slot First(pred), EraseAll, Concat, Reverse, Insert --- */

static int br_time_param(int st, int time, int delay)
{
	int o = vm_construct_struct(st);
	heap[o].page->values[br_member(st, "Time")].i = time;
	heap[o].page->values[br_member(st, "Delay")].i = delay;
	return o;
}

static void br_two_slot_calls(void)
{
	int ts = ain_get_struct(ain, "Motion::TimeParam"), ss = ain_get_struct(ain, "Motion::StringParam");
	int cs = ain_get_struct(ain, "Motion::PartsParamCollection");
	int is_time = ain_get_function(ain, "Motion::PartsParamCollection@<lambda : Motion::PartsParamCollection@0(array<ref Motion::IParam>)(22, 36)>");
	int ctor = ain_get_function(ain, "Motion::PartsParamCollection@0");
	assert(ts >= 0 && ss >= 0 && cs >= 0 && is_time >= 0 && ctor >= 0);
	int tv = br_vtoff("Motion::TimeParam", "Motion::IParam"), sv = br_vtoff("Motion::StringParam", "Motion::IParam");
	int S1 = vm_construct_struct(ss), S2 = vm_construct_struct(ss);
	int T1 = br_time_param(ts, 150, 20), T2 = br_time_param(ts, 300, 40);
	int coll = vm_construct_struct(cs);
	union vm_value r[4];
	/* First(pred): the first TimeParam element, two slots. */
	int arr = br_pairs(3, (int[]){ S1, sv, T1, tv, T2, tv });
	int n = br_call("Array", "First", 2, 0x10003, 3, (union vm_value[]){ BR_I(arr), BR_I(coll), BR_I(is_time) }, r, 4);
	BR_CHECK(n == 2 && r[0].i == T1 && r[1].i == tv, "First(is TimeParam): %d slots [%d %d]; want 2 [%d %d]",
		 n, r[0].i, n > 1 ? r[1].i : 0, T1, tv);
	/* Real bytecode: the collection takes Time/Delay from its TimeParam. */
	int params = br_pairs(3, (int[]){ S1, sv, S2, sv, T1, tv });
	{
		int sp = stack_ptr, csp = call_stack_ptr; size_t ip = instr_ptr;
		stack_push(coll); stack_push(params);
		method_call(ctor, VM_RETURN);
		vm_execute();
		instr_ptr = ip;
		BR_CHECK(call_stack_ptr == csp && stack_ptr == sp, "PartsParamCollection@0: stack %d, calls %d", stack_ptr - sp, call_stack_ptr - csp);
		stack_ptr = sp;
	}
	int time = heap[coll].page->values[br_member(cs, "<Time>")].i;
	int delay = heap[coll].page->values[br_member(cs, "<DelayTime>")].i;
	BR_CHECK(time == 150 && delay == 20, "PartsParamCollection@0 Time/DelayTime %d/%d; want 150/20 (the default is 1000/0)", time, delay);
	/* EraseAll(pred) removes whole elements. */
	arr = br_pairs(4, (int[]){ S1, sv, T1, tv, S2, sv, T2, tv });
	int refT2 = HEAP_REF(T2);
	n = br_call("Array", "EraseAll", 2, 0x10003, 3, (union vm_value[]){ BR_I(arr), BR_I(coll), BR_I(is_time) }, r, 4);
	struct page *p = heap[arr].page;
	BR_CHECK(n == 1 && r[0].i == 1 && p->nr_vars == 4 && p->values[0].i == S1 && p->values[1].i == sv
		 && p->values[2].i == S2 && p->values[3].i == sv && HEAP_REF(T2) == refT2 - 1,
		 "EraseAll(is TimeParam): returned %d, %d slots [%d %d %d %d], ref(T2) %d -> %d; want true, [S1 sv S2 sv], one ref released",
		 r[0].i, p->nr_vars, p->nr_vars > 0 ? p->values[0].i : 0, p->nr_vars > 1 ? p->values[1].i : 0,
		 p->nr_vars > 2 ? p->values[2].i : 0, p->nr_vars > 3 ? p->values[3].i : 0, refT2, HEAP_REF(T2));
	/* Concat appends pairs and references only the objects. The second slot
	 * is a vtable offset (316 for ISpriteParts); here it equals a live heap
	 * slot, which must not gain a reference. */
	int live = vm_construct_struct(ss);
	int dst = br_pairs(1, (int[]){ S1, sv }), src = br_pairs(2, (int[]){ T1, live, S2, live });
	int refT1 = HEAP_REF(T1), refLive = HEAP_REF(live);
	br_call("Array", "Concat", 2, 0x10003, 2, (union vm_value[]){ BR_I(dst), BR_I(src) }, r, 4);
	p = heap[dst].page;
	n = br_call("Array", "Numof", 1, 0x10003, 1, (union vm_value[]){ BR_I(dst) }, r, 4);
	BR_CHECK(p->nr_vars == 6 && p->values[2].i == T1 && p->values[3].i == live && p->values[4].i == S2 && r[0].i == 3
		 && HEAP_REF(T1) == refT1 + 1 && HEAP_REF(live) == refLive,
		 "Concat: %d slots, Numof %d, ref(T1) %d -> %d, ref(second slot %d) %d -> %d; want 6 slots, 3, +1, unchanged",
		 p->nr_vars, r[0].i, refT1, HEAP_REF(T1), live, refLive, HEAP_REF(live));
	int fresh = bu_empty_generic_array();
	br_call("Array", "Concat", 2, 0x10003, 2, (union vm_value[]){ BR_I(fresh), BR_I(src) }, r, 4);
	br_call("Array", "Numof", 1, 0x10003, 1, (union vm_value[]){ BR_I(fresh) }, r, 4);
	BR_CHECK(r[0].i == 2 && heap[fresh].page->nr_vars == 4, "Concat into an X_A_INIT 0 array: Numof %d, %d slots; want 2, 4",
		 r[0].i, heap[fresh].page->nr_vars);
	/* Reverse keeps each pair together. */
	arr = br_pairs(3, (int[]){ S1, 1, T1, 2, S2, 3 });
	br_call("Array", "Reverse", 1, 0x10003, 1, (union vm_value[]){ BR_I(arr) }, r, 4);
	p = heap[arr].page;
	BR_CHECK(p->nr_vars == 6 && p->values[0].i == S2 && p->values[1].i == 3 && p->values[2].i == T1 && p->values[3].i == 2
		 && p->values[4].i == S1 && p->values[5].i == 1,
		 "Reverse: [%d %d %d %d %d %d]; want [S2 3 T1 2 S1 1]", p->values[0].i, p->values[1].i, p->values[2].i,
		 p->values[3].i, p->values[4].i, p->values[5].i);
	/* Insert puts a whole element at a logical index. */
	arr = br_pairs(2, (int[]){ S1, 1, S2, 3 });
	br_call("Array", "Insert", 3, 0x10003, 4, (union vm_value[]){ BR_I(arr), BR_I(1), BR_I(T1), BR_I(2) }, r, 4);
	p = heap[arr].page;
	n = br_call("Array", "Numof", 1, 0x10003, 1, (union vm_value[]){ BR_I(arr) }, r, 4);
	BR_CHECK(p->nr_vars == 6 && p->values[2].i == T1 && p->values[3].i == 2 && p->values[4].i == S2 && p->values[5].i == 3
		 && r[0].i == 3,
		 "Insert(1): %d slots [.. %d %d %d %d], Numof %d; want 6 [.. T1 2 S2 3], 3", p->nr_vars,
		 p->nr_vars > 2 ? p->values[2].i : 0, p->nr_vars > 3 ? p->values[3].i : 0,
		 p->nr_vars > 4 ? p->values[4].i : 0, p->nr_vars > 5 ? p->values[5].i : 0, r[0].i);
	printf("BR4 two-slot First(pred)/EraseAll/Concat/Reverse/Insert: %s\n", br_failures ? "FAIL" : "ok");
}

/* --- BR5: AnimateText@AdjustPos --- */

static void br_animate_text(void)
{
	bu_postlink();
	int wrap = ain_get_function(ain, "AFL_Parts_WrapSprite");
	int get_text = ain_get_function(ain, "parts::detail::CSpriteParts@GetText");
	int adjust = ain_get_function(ain, "AnimateText@AdjustPos");
	int ats = ain_get_struct(ain, "AnimateText");
	assert(wrap >= 0 && get_text >= 0 && adjust >= 0 && ats >= 0);
	int at = vm_construct_struct(ats);
	int mi = br_member(ats, "m_parts");
	int arr = heap[at].page->values[mi].i;
	if (arr <= 0 || !heap_index_valid(arr) || heap[arr].type != VM_PAGE || !heap[arr].page) {
		arr = bu_empty_generic_array();
		heap[at].page->values[mi].i = arr;
	}
	heap[at].page->values[br_member(ats, "m_span")].i = 0;
	enum { N = 4, W = 48 };
	for (int i = 0; i < N; i++) {
		int no = 9701 + i;
		struct parts *p = parts_get(no);
		for (int s = 0; s < PARTS_NR_STATES; s++) {
			p->states[s].common.w = W;
			p->states[s].common.h = 40;
		}
		int sp = stack_ptr; size_t ip = instr_ptr;
		stack_push(no); stack_push(-1); stack_push(1);	/* comment: none */
		function_call(wrap, VM_RETURN);
		vm_execute();
		instr_ptr = ip;
		assert(stack_ptr == sp + 2);
		int sprite = stack[sp].i;
		stack_ptr = sp;
		stack_push(sprite);
		method_call(get_text, VM_RETURN);
		vm_execute();
		instr_ptr = ip;
		assert(stack_ptr == sp + 2);
		int text = stack[sp].i, vt = stack[sp + 1].i;
		stack_ptr = sp;
		bu_call("Array", "Add", 2, 0x10003, 3, (union vm_value[]){ BR_I(heap[at].page->values[mi].i), BR_I(text), BR_I(vt) });
	}
	int sp = stack_ptr, csp = call_stack_ptr; size_t ip = instr_ptr;
	stack_push(at);
	method_call(adjust, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	BR_CHECK(call_stack_ptr == csp && stack_ptr == sp, "AdjustPos left stack %d, calls %d", stack_ptr - sp, call_stack_ptr - csp);
	stack_ptr = sp;
	/* totalWidth = 4*48 - 48/2 - 48/2 = 144; centres at -72, -24, 24, 72. */
	const int want[N] = { -72, -24, 24, 72 };
	for (int i = 0; i < N; i++) {
		int x = parts_get(9701 + i)->local.pos.x;
		BR_CHECK(x == want[i], "character %d at x %d, want %d", i, x, want[i]);
	}
	printf("BR5 AnimateText@AdjustPos: %s\n", br_failures ? "FAIL" : "ok");
}

/* --- BR6: 矩形部件 and 構築部件 states --- */

#define BR_RECT "\xbe\xd8\xd0\xce\xb2\xbf\xbc\xfe"                 /* 矩形部件 */
#define BR_CONS "\x98\x8b\xba\x42\xb2\xbf\xbc\xfe"                 /* 構築部件 */
#define BR_RECT_MODE "\xbe\xd8\xd0\xce\xc4\xa3\xca\xbd"             /* 矩形模式 */
#define BR_STEPS "\xca\xd6\xed\x98\xa5\xea\xa5\xb9\xa5\xc8"         /* 手順リスト */
#define BR_STEP1 "\xca\xd6\xed\x98" "1"                             /* 手順1 */
#define BR_COMMAND "\xa5\xb3\xa5\xde\xa5\xf3\xa5\xc9"               /* コマンド */
#define BR_CG_NAME "\xa3\xc3\xa3\xc7\xc3\xfb"                       /* ＣＧ名 */

static struct ex_tree br_point(const char *name, float x, float y)
{
	struct ex_list *l = xcalloc(1, sizeof(*l));
	l->nr_items = 2;
	l->items = xcalloc(2, sizeof(*l->items));
	l->items[0].value = (struct ex_value){ .type = EX_FLOAT, .f = x };
	l->items[1].value = (struct ex_value){ .type = EX_FLOAT, .f = y };
	return bu_leaf(name, (struct ex_value){ .type = EX_LIST, .list = l });
}

static const char *br_names[] = { "ThumbnailArea", "PlayerC", "Bg" };

static struct ex *br_resource(void)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("BaseUiReviewActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("BaseUiReviewActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("BaseUiReviewRoot", BU_LAYOUT, 0, 3);
	struct ex_tree *kids = &top->children[0].children[1];
	/* ThumbnailArea: a rect state with its four corners (矩形模式 1). */
	kids->children[0] = bu_comp("ThumbnailArea", BU_LOW, 1, 0);
	struct ex_tree *info = &kids->children[0].children[0];
	info->children[1] = bu_branch(BU_NORMAL, 6);
	struct ex_tree *st = &info->children[1];
	st->children[0] = bu_sleaf(BU_TYPE, BR_RECT);
	st->children[1] = bu_ileaf(BR_RECT_MODE, 1);
	st->children[2] = br_point("\xd7\xf3\xc9\xcf", 0, 0);        /* 左上 */
	st->children[3] = br_point("\xd3\xd2\xc9\xcf", 1068, 0);     /* 右上 */
	st->children[4] = br_point("\xd7\xf3\xcf\xc2", 0, 472);      /* 左下 */
	st->children[5] = br_point("\xd3\xd2\xcf\xc2", 1068, 472);   /* 右下 */
	/* PlayerC: a construction state with an empty step list. */
	kids->children[1] = bu_comp("PlayerC", BU_LOW, 1, 0);
	info = &kids->children[1].children[0];
	info->children[1] = bu_branch(BU_NORMAL, 2);
	info->children[1].children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	info->children[1].children[1] = bu_branch(BR_STEPS, 0);
	/* Bg: a construction state with a step that loads a CG. */
	kids->children[2] = bu_comp("Bg", BU_LOW, 1, 0);
	info = &kids->children[2].children[0];
	info->children[1] = bu_branch(BU_NORMAL, 2);
	info->children[1].children[0] = bu_sleaf(BU_TYPE, BR_CONS);
	info->children[1].children[1] = bu_branch(BR_STEPS, 1);
	struct ex_tree *step = &info->children[1].children[1].children[0];
	*step = bu_branch(BR_STEP1, 2);
	step->children[0] = bu_ileaf(BR_COMMAND, 2);
	step->children[1] = bu_sleaf(BR_CG_NAME, "BaseUiReviewMissingCg");
	return e;
}

static void br_rect_construction(void)
{
	bu_postlink();
	struct ex *e = br_resource();
	struct string *name = cstr_to_string("BaseUiReviewFixture");
	bool loaded = probe_load_activity_tree(name, e);
	free_string(name); ex_free(e);
	BR_CHECK(loaded, "synthetic activity did not load");
	int act = sf_str("BaseUiReviewFixture", -1);
	int nums[3];
	for (int i = 0; i < 3; i++)
		nums[i] = bu_number(act, br_names[i]);
	const int want[3] = { 25, 26, 26 };
	for (int i = 0; i < 3; i++) {
		int t = BU_PE("GetComponentType", BU_I(nums[i]), BU_I(1)).i;
		BR_CHECK(t == want[i], "GetComponentType(%s, 1) = %d, want %d", br_names[i], t, want[i]);
	}
	struct parts *rect = parts_get(nums[0]);
	BR_CHECK(rect->states[0].type == PARTS_RECT_DETECTION && rect->states[0].common.w == 1068
		 && rect->states[0].common.h == 472,
		 "ThumbnailArea state 1: type %d size %dx%d; want a rect detection state of 1068x472",
		 rect->states[0].type, rect->states[0].common.w, rect->states[0].common.h);
	BR_CHECK(parts_get(nums[1])->states[0].type == PARTS_CONSTRUCTION_PROCESS,
		 "PlayerC state 1: type %d, want a construction process", parts_get(nums[1])->states[0].type);
	/* The widget type setter of CRectParts / CConstructionParts keeps them. */
	BU_PE("SetComponentType", BU_I(nums[0]), BU_I(25), BU_I(1));
	BU_PE("SetComponentType", BU_I(nums[1]), BU_I(26), BU_I(1));
	BR_CHECK(BU_PE("GetComponentType", BU_I(nums[0]), BU_I(1)).i == 25 && rect->states[0].common.w == 1068
		 && BU_PE("GetComponentType", BU_I(nums[1]), BU_I(1)).i == 26,
		 "SetComponentType(25/26) changed the states");
	heap_unref(act);
	printf("BR6 rect/construction states: %s\n", br_failures ? "FAIL" : "ok");
}

/* --- BR7: exits wait for the screenshot write --- */

static bool br_png_complete(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return false;
	uint8_t tail[8] = { 0 };
	bool ok = !fseek(f, -8, SEEK_END) && fread(tail, 1, 8, f) == 8
		&& !memcmp(tail, "IEND\xae\x42\x60\x82", 8);
	fclose(f);
	return ok;
}

static void br_exit_child(const char *path, int how)
{
	/* Noise compresses slowly: the write outlasts the error path's stack
	 * trace, so an exit that does not wait loses the file. */
	enum { w = 2560, h = 1440 };
	uint8_t *px = xmalloc(w * h * 4);
	uint32_t seed = 12345;
	for (int i = 0; i < w * h * 4; i++) {
		seed = seed * 1103515245u + 12345u;
		px[i] = (i & 3) == 3 ? 0xff : seed >> 24;
	}
	gfx_write_pixels_async(px, w, h, path, ALCG_PNG);
	if (how == 0)
		vm_exit(0);
	instr_ptr = 0;	/* _vm_error names the instruction at instr_ptr */
	VM_ERROR("base-ui-review: intentional error after a background write");
}

static void br_exit_waits(void)
{
	const char *tmp = getenv("TMPDIR");
	char dir[PATH_MAX];
	snprintf(dir, sizeof(dir), "%s/xs4-base-ui-review-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	if (!mkdtemp(dir)) {
		BR_CHECK(0, "mkdtemp failed");
		return;
	}
	const char *what[2] = { "vm_exit", "VM_ERROR" };
	const int want_rc[2] = { 0, 1 };
	for (int how = 0; how < 2; how++) {
		int complete = 0, exited = 0;
		for (int k = 0; k < 3; k++) {
			char path[PATH_MAX];
			snprintf(path, sizeof(path), "%s/exit%d_%d.png", dir, how, k);
			fflush(stdout);
			pid_t pid = fork();
			if (pid == 0) {
				int devnull = open("/dev/null", O_WRONLY);
				if (devnull >= 0) { dup2(devnull, 2); dup2(devnull, 1); }
				br_exit_child(path, how);
				_exit(99);
			}
			int status = 0;
			waitpid(pid, &status, 0);
			exited += WIFEXITED(status) && WEXITSTATUS(status) == want_rc[how];
			complete += br_png_complete(path);
			unlink(path);
		}
		BR_CHECK(complete == 3 && exited == 3, "%s: %d of 3 background PNGs complete after the exit (%d of 3 exited with %d)",
			 what[how], complete, exited, want_rc[how]);
	}
	rmdir(dir);
	printf("BR7 exits wait for the screenshot write: %s\n", br_failures ? "FAIL" : "ok");
}

static int br_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		br_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(br_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_base_ui_review(void)
{
	int failed = 0;
	failed += br_fork("BR1", br_disabled_button);
	failed += br_fork("BR2", br_detector);
	failed += br_fork("BR3", br_two_slot_returns);
	failed += br_fork("BR4", br_two_slot_calls);
	failed += br_fork("BR5", br_animate_text);
	failed += br_fork("BR6", br_rect_construction);
	failed += br_fork("BR7", br_exit_waits);
	printf("BASE-UI-REVIEW %s: %d of 7 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 88 : 0;
}

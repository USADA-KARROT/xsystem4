/* array-literal: an array of multi-slot elements has all its slots, and only
 * the first slot of an element is a reference.
 *
 * Original (static disassembly). X_A_INIT (0x66e420) takes the type of the
 * variable it initializes (0x679830, vt+0x14; it must be an array), builds
 * the array page for that element type (0x679350) and stores the slots of
 * one element in the page (0x679432 -> 0x653420, page +0x48): 2 for an
 * interface or a wrap of one (object, vtable offset), the payload's slots
 * plus one for an option (0x653452). The immediate only chooses between
 * default-initializing the elements (non-zero, IVMArray vt+0x4c) and
 * leaving them (0, 0x67fee0, which sizes the data as elements * slots * 4
 * bytes). A literal is "PUSH n; X_A_INIT 0; X_DUP 1; PUSH 0; <elements>;
 * X_ASSIGN n * slots", and X_A_SIZE (0x66e860) is bytes / 4 / slots. The
 * page releases an element through its type (0x680270), so a vtable offset
 * or an option flag is never treated as a heap slot; the bytecode itself
 * retains only the object of an element (SP_INC).
 *
 * This VM sized a generic page as n * (immediate + 1) slots, so a literal
 * of two-slot elements got half its slots and the X_ASSIGN was cut
 * ("X_ASSIGN 38 past end of page"): FrameInfo@LoadSubParams loaded 9 of a
 * battle frame's 19 sub-parameters, and the 9 objects behind the cut kept
 * the reference the literal took. It also counted every slot of a generic
 * page as a reference in the page's teardown, in the A_REF copy and in
 * X_OP_SET, which is off by one on heap[offset] for a vtable offset above 1
 * that X_ASSIGN or the Array library stored.
 *
 *   AL1  The real FrameInfo@GetParams on a FrameInfo built by its own
 *        FrameInfo@2: 19 elements in 38 slots, each (the member object, 0),
 *        X_A_SIZE 19, no X_ASSIGN past the page, one reference per object
 *        while the array lives and none left when it is released. Every
 *        generic array page alive while the result lives is consistent (a
 *        page that calls a slot a value has a stride of 2 or 3, that
 *        struct_type and whole elements; no other page calls a slot a
 *        value).
 *   AL2  The real FrameInfo constructor with an EX key (FrameInfo@Load,
 *        LoadSubParams, GetParams) on the work copy's EX file: under
 *        LoadSubParams every one of the 19 member objects is entered twice
 *        (its key and its Load), the CG layer and effect collections among
 *        them, and nothing is cut. When the FrameInfo is released all 19
 *        objects go: no object of a sub-parameter's struct and no key
 *        string ("<frame key>.<name>") is left. Before, 9 objects and the
 *        argument strings of 10 skipped calls stayed.
 *   AL3  The real Header@2, DungeonSelector@2 and Footer@2 (user component
 *        parameter literals of 2, 3 and 1 elements): each element is the
 *        parameter object with one reference and the vtable offset its
 *        struct declares for IUserComponentParam; the heap is consistent
 *        as in AL1.
 *   AL4  The one three-slot literal: the real RankGauge@EndCurrentMotion
 *        with no parts (five none elements, X_ASSIGN 15) is not cut; and on
 *        the page its real X_A_INIT makes, elements stored by its real
 *        X_ASSIGN 15 whose second slot is the number of a live heap slot
 *        leave that slot's references alone through an A_REF copy and both
 *        teardowns, while the objects are retained and released. The
 *        function's X_ASSIGN 15 is counted, so "not cut" is not said of an
 *        instruction that did not run.
 *   AL5  The same for two-slot elements stored three ways: a literal (the
 *        real X_A_INIT and X_ASSIGN 4 of Header@2), Array.PushBack 0x10003
 *        (on the empty page X_A_INIT 0 makes and on a variable without a
 *        page) and X_OP_SET.
 *   AL6  A resume image (system.ResumeSave / ResumeLoad, both file
 *        formats) brings a multi-slot page back with its layout: after the
 *        load the literal's page still counts only its objects, and
 *        releasing it leaves the slot its offsets name alone. So does the
 *        page Array.PushBack 0x10003 makes for a variable without one,
 *        whose rank must be 1 for the image to keep it (the page it takes
 *        from the cache was left with rank 0). A generic page with
 *        struct_type 2 that nobody declared multi-slot and an array of the
 *        struct with index 2 come back as they were, every slot a
 *        reference.
 *   AL7  The three sized multi-slot X_A_INIT with immediate 1, which no
 *        X_ASSIGN follows. The real AdvTransition@2 leaves m_partsBg and
 *        m_partsMask (array<ICGParts>) with 2 elements in 4 slots each,
 *        the first slot of an element -1 (AdvTransition@0 reads it and
 *        DELETEs it before it stores the element); elements stored there
 *        with AdvTransition@0's own X_ASSIGN 2, whose second slot is the
 *        number of a live heap slot, leave that slot alone through an
 *        A_REF copy and the object's teardown. The real MatchingInfo@2
 *        leaves FrontWorker (array<option<wrap<Worker>>>) with 3 elements
 *        in 6 slots, each none: (-1, 1).
 *   AL8  The Array library gives a page its layout and keeps it: Insert
 *        0x10003 (into a variable without a page and into the empty page
 *        X_A_INIT 0 makes), Concat 0x10003 into a destination without a
 *        page, Alloc 0x30003 followed by X_OP_SET 65539, and Erase and
 *        PopBack 0x10003 on a page of three elements. Each result is
 *        checked like AL5's.
 *   G1   One-slot literals are what they were: the real
 *        BattleActionPanel::PanelType::GetNameList (four strings) and the
 *        vtable Header@2 stores (two ints).
 *   G2   An AIN before version 14 does not read the declaration: the same
 *        X_A_INIT gives immediate + 1 slots and no layout.
 *   G3   A page is multi-slot only when it was made so, never because of
 *        the number in array.struct_type: the real X_A_INIT of an
 *        array<parts::detail::SPartsUpdateData> (struct index 2) is a
 *        struct array whose every slot is a reference; hand-made generic
 *        pages with struct_type 2 and 3 keep every slot a reference; and so
 *        does the page Array.Concat makes for a destination without one,
 *        whose struct_type is left over from a cached page (here a real
 *        two-slot page that was just released, so the number is 2 and a
 *        layout that was not cleared would be inherited too).
 *   G4   A declaration that cannot be resolved keeps immediate + 1.
 *   G5   A one-slot element that stays generic takes its declared count
 *        whatever the immediate is: the real X_A_INIT 1 of
 *        elkeditor::detail::CEmitterTimeLine@2 on its member
 *        m_keyFrameLineList (11 ref structs) makes 11 slots, every one a
 *        reference, and no layout. (Immediate + 1 made 22.)
 *
 * Where the expectations are xsystem4's, not the original's:
 *   - The elements of a sized page are -1 in every slot before the literal
 *     is stored (natively unset for immediate 0). For a non-zero immediate
 *     the original default-initializes them (0x67f4a0 -> 0x67fe20 ->
 *     0x656970): an option to none, which is done (AL7: value slots -1,
 *     last slot the number of option layers, 0x656a44), an interface to
 *     (-1, 0) (0x6569b8), which is not: AL7's ICGParts elements start as
 *     (-1, -1). Only the first slot is read before the element is stored.
 *   - An empty page (size 0) keeps struct_type immediate + 1 and gets its
 *     stride and layout from the first multi-slot element the Array
 *     library stores; natively the page knows its element width from the
 *     start. Nothing can be read from an empty page, so only AL5 sees it.
 *   - AL2 does not ask for an empty heap after the release. What is left
 *     is printed and comes from elsewhere: the box this VM gives a wrap
 *     local that X_ASSIGN then overwrites (EX_GetNodeNameList and other
 *     functions, see ex-name-list); the CG layer object, which
 *     SelectableIndexArray<T>@Insert passes with A_REF (one reference) to
 *     Array.Insert, which retains it again; and one CASPosF that was left
 *     before this change as well. None of these is an array's doing. The
 *     counts are pinned as they are, 11 boxes and 9 other slots: they are
 *     known leaks, to be lowered when they are fixed. Six of the nine are
 *     the FrameInfoCgLayer and what stays with it (a string, three empty
 *     delegate pages and a CASPosF): they go when Array.Insert does not
 *     retain a one-slot value again, and they are new with this change
 *     only because the frame did not load its CG layers before. The other
 *     three (two empty strings and a CASPosF) were not traced.
 *   - AL6: the resume file has no field for the layout. It is carried in
 *     the upper half of the array record's root_rank, which this engine
 *     does not read back otherwise (rank_minus_1 is the rank); an image
 *     written before has 0 there and its pages come back under the older
 *     rule, the one their reference counts were taken with. The JSON
 *     format has its own "elem-slots" member.
 *   - G1's string literal leaves 5 slots, as before: a sized string array
 *     starts with an empty string per element that the literal's X_ASSIGN
 *     overwrites, and the local's first array slot is replaced the same
 *     way. Natively X_A_INIT 0 leaves the elements unset.
 *   - G3's hand-made and Concat pages: a generic page nobody declared
 *     multi-slot keeps the older rule, every slot a reference.
 *
 * What the cases do not reach:
 *   - Array.Where 0x10003 / 0x30002 (its result page gets the layout) has
 *     no case here; neither have the Array functions that do not follow
 *     the layout (see the comment above array_option_resize), which this
 *     AIN does not call with a multi-slot operand.
 *   - copy_page of a multi-slot page (a struct copied by value that holds
 *     one) and a declaration in a global or a ref array variable.
 *   - G3's Concat part needs the page cache to hand the released two-slot
 *     page back. When it does not, the page's struct_type is not 2, the
 *     line printed says so, and the part shows less (it still passes).
 *   - AL1's and AL3's heap scan cannot tell a one-slot page with a
 *     left-over struct_type 2 and an even number of slots from a two-slot
 *     page; G3 and AL6 are the cases for that.
 *   - Every vtable offset in the real literals (AL1 to AL3, AL7) is 0;
 *     offsets above 1 are the made-up ones of AL4 to AL8.
 *
 * Every case runs in a child process. Exit code 1 is a failed check, exit
 * code 2 the environment (no EX file in XS4_GAME, or the AIN lacks a
 * function, struct, member or instruction a case looks up). A signal, a
 * sanitizer abort or the instruction budget running out is that case
 * failing.
 */
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

static int al_failures;
#define AL_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); al_failures++; } } while (0)

static void al_collect(void)
{
	al_failures += da_failures + wc_failures + oa_failures + bu_failures + or_failures + xl_failures;
	da_failures = wc_failures = oa_failures = bu_failures = or_failures = xl_failures = 0;
}

/* --- watches --- */

/* X_ASSIGN that would write past its page, and the frames entered directly
 * under one function (with their object). */
static bool al_watch;
static int al_clamps, al_clamp_n, al_clamp_have;
static int al_count_n = -1, al_count;	/* X_ASSIGN al_count_n executed */
static int al_parent = -1, al_under_fn[128], al_under_this[128], al_nunder;

static void array_literal_probe_step(void)
{
	if (!al_watch || !call_stack_ptr)
		return;
	if (get_opcode(instr_ptr) == X_ASSIGN) {
		int n = get_argument(0);
		if (n == al_count_n)
			al_count++;
		if (n > 0 && stack_ptr >= n + 2) {
			int slot = stack[stack_ptr - n - 2].i, index = stack[stack_ptr - n - 1].i;
			if (da_live_page(slot) && index >= 0 && index + n > heap[slot].page->nr_vars) {
				if (!al_clamps) {
					al_clamp_n = n;
					al_clamp_have = heap[slot].page->nr_vars - index;
				}
				al_clamps++;
			}
		}
	}
	struct function_call *f = &call_stack[call_stack_ptr - 1];
	if (al_parent >= 0 && call_stack_ptr >= 2 && call_stack[call_stack_ptr - 2].fno == al_parent
	    && f->fno >= 0 && instr_ptr == ain->functions[f->fno].address && al_nunder < 128) {
		al_under_fn[al_nunder] = f->fno;
		al_under_this[al_nunder] = f->struct_page;
		al_nunder++;
	}
}

static void al_watch_start(int parent)
{
	al_watch = true;
	al_clamps = al_nunder = 0;
	al_parent = parent;
}

static void al_no_clamp(const char *what)
{
	AL_CHECK(al_clamps == 0, "%s: %d X_ASSIGN past the end of its page (the first stores %d slot(s) into %d)", what,
		al_clamps, al_clamp_n, al_clamp_have);
}

/* --- helpers --- */

static struct page *al_page(int slot)
{
	return da_live_page(slot) && heap[slot].page->type == ARRAY_PAGE ? heap[slot].page : NULL;
}

/* The page has `elements` elements of `stride` slots, and only the first
 * slot of each is a reference. */
static bool al_layout(const char *what, int slot, int stride, int elements)
{
	struct page *p = al_page(slot);
	bool ok = p && (p->a_type == AIN_ARRAY || p->a_type == AIN_REF_ARRAY) && p->nr_vars == stride * elements
		&& p->array.struct_type == stride && wc_size(slot) == elements;
	for (int i = 0; ok && i < p->nr_vars; i++)
		ok = (variable_type(p, i, NULL, NULL) == AIN_VOID) == (i % stride != 0);
	AL_CHECK(ok, "%s: want %d element(s) of %d slot(s), the first slot of each a reference and the others values",
		what, elements, stride);
	if (!ok && p) {
		printf("    %s: a_type=%d struct_type=%d slots=%d X_A_SIZE=%d types:", what, p->a_type, p->array.struct_type,
		       p->nr_vars, wc_size(slot));
		for (int i = 0; i < p->nr_vars && i < 8; i++)
			printf(" %d", variable_type(p, i, NULL, NULL));
		printf("\n");
	}
	return ok;
}

static int al_fn_args(const char *name, int nargs)
{
	for (int i = 0; i < ain->nr_functions; i++)
		if (ain->functions[i].nr_args == nargs && !strcmp(ain->functions[i].name, name))
			return i;
	printf("ARRAY-LITERAL function %s with %d argument(s) not declared\n", name, nargs);
	exit(2);
}

/* A fresh local page of fno with nothing in it. */
static int al_local(int fno)
{
	struct ain_function *f = &ain->functions[fno];
	struct page *lp = alloc_page(LOCAL_PAGE, fno, f->nr_vars);
	lp->local.struct_ptr = -1;
	for (int i = 0; i < f->nr_vars; i++)
		lp->values[i].i = -1;
	return heap_alloc_page(lp);
}

/* The real X_A_INIT at `at` on (page, var) with this size. The variable
 * owns the new array; the result is borrowed. */
static int al_init(int page, int var, int size, uint32_t at)
{
	size_t ip = instr_ptr;
	int sp = stack_ptr;
	stack_push(page);
	stack_push(var);
	stack_push(size);
	instr_ptr = at;
	execute_instruction(X_A_INIT);
	instr_ptr = ip;
	int slot = stack_pop().i;
	AL_CHECK(stack_ptr == sp && da_live_page(page) && heap[page].page->values[var].i == slot,
		"X_A_INIT did not leave its array in the variable");
	return slot;
}

/* The array X_A_INIT (the instruction at `at`) makes for local `var` of
 * fno, alone: one reference, the local page gone. */
static int al_init_local(int fno, int var, int size, uint32_t at)
{
	int lp = al_local(fno);
	int slot = al_init(lp, var, size, at);
	heap_ref(slot);
	heap_unref(lp);
	return slot;
}

/* The real X_ASSIGN at `at` (n slots) into the array from slot `index`. */
static void al_assign_at(uint32_t at, int arr, int index, int n, const int *vals)
{
	size_t ip = instr_ptr;
	int sp = stack_ptr;
	stack_push(arr);
	stack_push(index);
	for (int i = 0; i < n; i++)
		stack_push(vals[i]);
	instr_ptr = at;
	execute_instruction(X_ASSIGN);
	instr_ptr = ip;
	AL_CHECK(stack_ptr == sp + n, "X_ASSIGN %d left %d slot(s)", n, stack_ptr - sp);
	stack_ptr = sp;
}

static void al_assign(uint32_t at, int arr, int n, const int *vals)
{
	al_assign_at(at, arr, 0, n, vals);
}

/* The real A_REF of an array: a copy the caller owns. */
static int al_copy(int arr)
{
	int sp = stack_ptr;
	stack_push(arr);
	execute_instruction(A_REF);
	int copy = stack_pop().i;
	AL_CHECK(stack_ptr == sp && copy != arr && al_page(copy), "A_REF did not make a new array");
	return copy;
}

static int al_local_var(int fno, const char *name)
{
	for (int i = 0; i < ain->functions[fno].nr_vars; i++)
		if (!strcmp(ain->functions[fno].vars[i].name, name))
			return i;
	printf("ARRAY-LITERAL %s has no variable %s\n", ain->functions[fno].name, name);
	exit(2);
}

/* --- AL1: FrameInfo@GetParams --- */

enum { AL_PARAMS = 19 };
static void al_scan_heap(const char *tag);

/* A FrameInfo with its members constructed, and the 19 sub-parameter
 * members in declaration order (they follow m_exBaseKey). */
static int al_frame_info(int *member)
{
	int st = sp_sno("FrameInfo"), first = sp_mem(st, "BgLighting"), last = sp_mem(st, "Effects");
	if (last - first != AL_PARAMS - 1) {
		printf("ARRAY-LITERAL FrameInfo does not have 19 sub-parameters from BgLighting to Effects\n");
		exit(2);
	}
	int obj = alloc_struct(st);
	int sp = or_guard_push();
	vm_call(da_fn("FrameInfo@2"), obj);
	or_guard_pop("FrameInfo@2", sp, 0, NULL);
	for (int i = 0; i < AL_PARAMS; i++)
		member[i] = heap[obj].page->values[first + i].i;
	return obj;
}

static void al_get_params(void)
{
	int member[AL_PARAMS], ref[AL_PARAMS], good = 0;
	int obj = al_frame_info(member);
	for (int i = 0; i < AL_PARAMS; i++) {
		if (da_live_page(member[i]) && heap[member[i]].page->type == STRUCT_PAGE)
			good++;
		ref[i] = da_live_page(member[i]) ? HEAP_REF(member[i]) : -1;
	}
	if (good != AL_PARAMS) {
		printf("  FAIL: FrameInfo@2 constructed %d of 19 sub-parameters\n", good);
		fflush(stdout);
		_exit(1);
	}
	size_t base = live_slots();
	int r[1] = { -1 };
	al_watch_start(-1);
	int sp = or_guard_push();
	vm_call(da_fn("FrameInfo@GetParams"), obj);
	or_guard_pop("FrameInfo@GetParams", sp, 1, r);
	al_watch = false;
	al_collect();
	int arr = r[0], same = 0, held = 0;
	struct page *p = al_page(arr);
	printf("AL1 GetParams: %d slot(s), X_A_SIZE %d, %d X_ASSIGN past the page\n", p ? p->nr_vars : -1,
	       p ? wc_size(arr) : -1, al_clamps);
	al_no_clamp("GetParams");
	if (al_layout("GetParams", arr, 2, AL_PARAMS)) {
		for (int i = 0; i < AL_PARAMS; i++) {
			if (p->values[2 * i].i == member[i] && p->values[2 * i + 1].i == 0)
				same++;
			if (HEAP_REF(member[i]) == ref[i] + 1)
				held++;
		}
	}
	AL_CHECK(same == AL_PARAMS, "%d of 19 elements are (the member object, 0)", same);
	AL_CHECK(held == AL_PARAMS, "%d of 19 member objects have one more reference while the array lives", held);
	AL_CHECK(p && HEAP_REF(arr) == 1, "the result is not the caller's alone");
	al_scan_heap("AL1");
	if (p)
		heap_unref(arr);
	int back = 0;
	for (int i = 0; i < AL_PARAMS; i++)
		if (HEAP_REF(member[i]) == ref[i])
			back++;
	printf("AL1 after release: %d of 19 member objects have the references they had, %+ld live slot(s)\n", back,
	       (long)live_slots() - (long)base);
	AL_CHECK(back == AL_PARAMS, "%d of 19 member objects kept a reference of the array", AL_PARAMS - back);
	AL_CHECK(live_slots() == base, "%+ld live slot(s) after the array was released", (long)live_slots() - (long)base);
}

/* --- AL2: a whole frame --- */

static bool al_wrap_box(size_t slot)
{
	return heap[slot].type == VM_PAGE && heap[slot].page && heap[slot].page->type == STRUCT_PAGE
		&& heap[slot].page->index == -1 && heap[slot].page->nr_vars == 1;
}

/* The slots that appeared since xl_mark, by kind. */
static void al_show_left(void)
{
	int shown = 0;
	for (size_t i = 2; i < heap_size && shown < 40; i++) {
		if (HEAP_REF(i) <= 0 || (i < xl_mark_size && xl_mark_map[i]))
			continue;
		shown++;
		if (heap[i].type == VM_STRING) {
			printf("    left: slot %zu ref %d string of %d byte(s)\n", i, HEAP_REF(i), heap[i].s ? heap[i].s->size : -1);
			continue;
		}
		struct page *p = heap[i].page;
		if (!p)
			printf("    left: slot %zu ref %d page (none)\n", i, HEAP_REF(i));
		else if (p->type == STRUCT_PAGE && p->index >= 0 && p->index < ain->nr_structures)
			printf("    left: slot %zu ref %d struct %s\n", i, HEAP_REF(i), ain->structures[p->index].name);
		else if (p->type == LOCAL_PAGE && p->index >= 0 && p->index < ain->nr_functions)
			printf("    left: slot %zu ref %d local page of %s\n", i, HEAP_REF(i), ain->functions[p->index].name);
		else
			printf("    left: slot %zu ref %d page type=%d index=%d n=%d\n", i, HEAP_REF(i), p->type, p->index, p->nr_vars);
	}
}

/* Every generic array page: a page that calls a slot a value has one
 * stride, 2 or 3, equal to its struct_type, and whole elements. */
static void al_scan_heap(const char *tag)
{
	int generic = 0, multi = 0, bad = 0;
	for (size_t i = 2; i < heap_size; i++) {
		if (HEAP_REF(i) <= 0 || heap[i].type != VM_PAGE || !heap[i].page || heap[i].page->type != ARRAY_PAGE)
			continue;
		struct page *p = heap[i].page;
		if (p->a_type != AIN_ARRAY && p->a_type != AIN_REF_ARRAY) {
			for (int k = 0; k < p->nr_vars && k < 4; k++)
				if (p->array.rank <= 1 && variable_type(p, k, NULL, NULL) == AIN_VOID) {
					bad++;
					break;
				}
			continue;
		}
		generic++;
		int stride = 0;
		for (int k = 1; k < p->nr_vars; k++)
			if (variable_type(p, k, NULL, NULL) != AIN_VOID) {
				stride = k;
				break;
			}
		if (p->nr_vars < 2 || variable_type(p, 1, NULL, NULL) != AIN_VOID)
			continue;	/* every slot a reference */
		if (!stride)
			stride = p->nr_vars;
		multi++;
		bool ok = (stride == 2 || stride == 3) && p->array.struct_type == stride && p->nr_vars % stride == 0;
		for (int k = 0; ok && k < p->nr_vars; k++)
			ok = (variable_type(p, k, NULL, NULL) == AIN_VOID) == (k % stride != 0);
		if (!ok && bad++ < 3)
			printf("    slot %zu: a_type=%d struct_type=%d slots=%d stride=%d\n", i, p->a_type, p->array.struct_type,
			       p->nr_vars, stride);
	}
	printf("%s generic array pages alive: %d, with a multi-slot layout: %d, inconsistent: %d\n", tag, generic, multi, bad);
	AL_CHECK(multi > 0 && bad == 0, "%s: %d multi-slot page(s), %d inconsistent", tag, multi, bad);
}

static void al_whole_frame(void)
{
	struct ex *e = or_real_ex();
	const char *key = XL_MOTION "." XL_CHARA "." XL_IDLE "." XL_F000;
	struct ex_value *node = ex_get(e, key);
	if (!node || node->type != EX_TREE) {
		printf("ARRAY-LITERAL the EX file has no frame at the key AL2 reads\n");
		exit(2);
	}
	int ctor = al_fn_args("FrameInfo@0", 2), sub = da_fn("FrameInfo@LoadSubParams");
	int layers = da_fn("FrameInfoCgLayerCollection@Load"), effects = da_fn("FrameInfoEffectCollection@Load");
	int names = da_fn("EX_GetNodeNameList"), exnames = da_fn("EX_GetEXNameList");
	int st = sp_sno("FrameInfo"), first = sp_mem(st, "BgLighting");

	size_t base = live_slots();
	xl_mark();
	unsigned long layers0 = calls[layers], effects0 = calls[effects], boxes0 = calls[names] + calls[exnames];
	int obj = alloc_struct(st);
	int args[2] = { 0, sf_str(key, -1) };
	al_watch_start(sub);
	int sp = or_guard_push();
	or_method(ctor, obj, 2, args);
	or_guard_pop("FrameInfo@0(index, key)", sp, 0, NULL);
	al_watch = false;
	al_parent = -1;
	al_collect();
	AL_CHECK(HEAP_REF(args[1]) == 1, "the key has %d reference(s), want the caller's one", HEAP_REF(args[1]));
	heap_unref(args[1]);

	int member[AL_PARAMS], twice = 0, never = 0;
	unsigned seq[AL_PARAMS];
	for (int i = 0; i < AL_PARAMS; i++) {
		member[i] = heap[obj].page->values[first + i].i;
		seq[i] = da_live_page(member[i]) ? heap[member[i]].seq : 0;
		int n = 0;
		for (int k = 0; k < al_nunder; k++)
			if (al_under_this[k] == member[i])
				n++;
		if (n == 2)
			twice++;
		if (n == 0)
			never++;
	}
	printf("AL2 under LoadSubParams: %d frame(s) entered; %d of 19 sub-parameters entered twice, %d never; "
	       "CG layers loaded %lu time(s), effects %lu; %d X_ASSIGN past the page\n", al_nunder, twice, never,
	       calls[layers] - layers0, calls[effects] - effects0, al_clamps);
	AL_CHECK(twice == AL_PARAMS, "%d of 19 sub-parameters were asked for their key and loaded", twice);
	AL_CHECK(calls[layers] - layers0 == 1 && calls[effects] - effects0 == 1,
		"the CG layer and effect collections were loaded %lu and %lu time(s), want 1 each",
		calls[layers] - layers0, calls[effects] - effects0);
	al_no_clamp("the frame");

	int kind[AL_PARAMS];
	for (int i = 0; i < AL_PARAMS; i++)
		kind[i] = da_live_page(member[i]) ? heap[member[i]].page->index : -1;
	heap_unref(obj);
	int gone = 0, boxes = 0, params = 0, keys = 0, other = 0;
	size_t keylen = strlen(key);
	for (int i = 0; i < AL_PARAMS; i++)
		if (member[i] > 1 && (HEAP_REF(member[i]) <= 0 || heap[member[i]].seq != seq[i]))
			gone++;
	for (size_t i = 2; i < heap_size; i++) {
		if (HEAP_REF(i) <= 0 || (i < xl_mark_size && xl_mark_map[i]))
			continue;
		if (al_wrap_box(i)) {
			boxes++;
			continue;
		}
		other++;
		if (heap[i].type == VM_STRING && heap[i].s && (size_t)heap[i].s->size > keylen
		    && !strncmp(heap[i].s->text, key, keylen) && heap[i].s->text[keylen] == '.')
			keys++;
		if (heap[i].type == VM_PAGE && heap[i].page && heap[i].page->type == STRUCT_PAGE)
			for (int k = 0; k < AL_PARAMS; k++)
				if (heap[i].page->index == kind[k]) {
					params++;
					break;
				}
	}
	unsigned long listed = calls[names] + calls[exnames] - boxes0;
	printf("AL2 after release: %d of 19 sub-parameter objects gone; left: %d sub-parameter object(s), %d key string(s), "
	       "%d wrap box(es) (%lu name list call(s)), %d other slot(s) in all, %+ld live\n", gone, params, keys, boxes,
	       listed, other, (long)live_slots() - (long)base);
	AL_CHECK(gone == AL_PARAMS, "%d of 19 sub-parameter objects outlived the FrameInfo", AL_PARAMS - gone);
	AL_CHECK(params == 0, "%d object(s) of a sub-parameter's struct were left", params);
	AL_CHECK(keys == 0, "%d key string(s) of a sub-parameter were left", keys);
	/* known leaks that are not an array's (see the header) */
	AL_CHECK(boxes == 11 && other == 9, "%d wrap box(es) and %d other slot(s) were left; the known ones are 11 and 9", boxes,
		other);
	al_show_left();
}

/* --- AL3: user component parameters --- */

static int al_iface_offset(int object, const char *iface)
{
	int s = heap[object].page->index, target = ain_get_struct(ain, (char *)iface);
	if (s < 0 || s >= ain->nr_structures)
		return -1;
	for (int i = 0; i < ain->structures[s].nr_interfaces; i++)
		if (ain->structures[s].interfaces[i].struct_type == target)
			return ain->structures[s].interfaces[i].vtable_offset;
	return -1;
}

static void al_component(const char *name, int want)
{
	char init[160];
	snprintf(init, sizeof init, "%s@2", name);
	int st = sp_sno(name), fno = da_fn(init), mem = sp_mem(st, "<Params>");
	int obj = alloc_struct(st);
	al_watch_start(-1);
	int sp = or_guard_push();
	vm_call(fno, obj);
	or_guard_pop(init, sp, 0, NULL);
	al_watch = false;
	al_collect();
	int arr = heap[obj].page->values[mem].i, good = 0;
	struct page *p = al_page(arr);
	printf("AL3 %s: %d slot(s), X_A_SIZE %d, %d X_ASSIGN past the page", init, p ? p->nr_vars : -1,
	       p ? wc_size(arr) : -1, al_clamps);
	al_no_clamp(init);
	if (al_layout(init, arr, 2, want)) {
		for (int i = 0; i < want; i++) {
			int o = p->values[2 * i].i, off = p->values[2 * i + 1].i;
			bool ok = da_live_page(o) && heap[o].page->type == STRUCT_PAGE && HEAP_REF(o) == 1
				&& off >= 0 && off == al_iface_offset(o, "IUserComponentParam");
			printf("; element %d: %s, offset %d", i,
			       da_live_page(o) && heap[o].page->type == STRUCT_PAGE
			       ? ain->structures[heap[o].page->index].name : "not an object", off);
			for (int j = 0; ok && j < i; j++)
				ok = p->values[2 * j].i != o;
			if (ok)
				good++;
		}
	}
	printf("\n");
	AL_CHECK(good == want, "%s: %d of %d elements are a parameter object with one reference and its vtable offset",
		init, good, want);
	al_scan_heap(init);
}

static void al_components(void)
{
	al_component("Header", 2);
	al_component("DungeonSelector", 3);
	al_component("Footer", 1);
}

/* --- AL4: three-slot elements --- */

static void al_three_slots(void)
{
	/* the whole function, on a gauge without parts */
	da_parts_init();
	int st = sp_sno("RankGauge"), fno = da_fn("RankGauge@EndCurrentMotion");
	static const char *const opt[] = { "m_uiCgRank", "m_uiHGauge", "m_uiAltBase", "m_uiAltValue", "m_uiAltArrow" };
	int gauge = alloc_struct(st);
	for (int i = 0; i < 5; i++) {
		int m = sp_mem(st, opt[i]);
		if (ss_type_slot_count(&ain->structures[st].members[m].type) != 3) {
			printf("ARRAY-LITERAL RankGauge.%s is not a three-slot option\n", opt[i]);
			exit(2);
		}
		heap[gauge].page->values[m].i = -1;
		heap[gauge].page->values[m + 1].i = -1;
		heap[gauge].page->values[m + 2].i = 1;
	}
	al_count_n = 15;
	al_count = 0;
	al_watch_start(-1);
	int sp = or_guard_push();
	vm_call(fno, gauge);
	or_guard_pop("RankGauge@EndCurrentMotion", sp, 0, NULL);
	al_watch = false;
	al_count_n = -1;
	al_collect();
	printf("AL4 RankGauge@EndCurrentMotion without parts: X_ASSIGN 15 ran %d time(s), %d X_ASSIGN past the page\n", al_count,
	       al_clamps);
	AL_CHECK(al_count == 1, "RankGauge@EndCurrentMotion ran its X_ASSIGN 15 %d time(s), want 1", al_count);
	al_no_clamp("RankGauge@EndCurrentMotion");

	/* its X_A_INIT and X_ASSIGN 15 on their own */
	uint32_t init = oa_find("RankGauge@EndCurrentMotion", X_A_INIT, 0, 0);
	uint32_t assign = oa_find("RankGauge@EndCurrentMotion", X_ASSIGN, 15, 0);
	int arr = al_init_local(fno, al_local_var(fno, "parts"), 5, init);
	al_layout("X_A_INIT 0 of five option<wrap<iwrap<IParts>>>", arr, 3, 5);
	int a = oa_obj(), b = oa_obj(), ka = sf_str("offset a", -1), kb = sf_str("offset b", -1);
	/* (a, ka, 0), none, (b, kb, 0), none, none: the literal retains a and b */
	int v[15] = { a, ka, 0, -1, -1, 1, b, kb, 0, -1, -1, 1, -1, -1, 1 };
	heap_ref(a);
	heap_ref(b);
	al_assign(assign, arr, 15, v);
	struct page *p = al_page(arr);
	AL_CHECK(p && p->nr_vars == 15 && p->values[0].i == a && p->values[1].i == ka
		&& p->values[2].i == 0 && p->values[5].i == 1 && p->values[6].i == b && p->values[7].i == kb
		&& p->values[14].i == 1, "X_ASSIGN 15 did not store the five elements");
	AL_CHECK(HEAP_REF(a) == 2 && HEAP_REF(ka) == 1 && HEAP_REF(kb) == 1, "after the literal: object %d, offsets %d %d",
		HEAP_REF(a), HEAP_REF(ka), HEAP_REF(kb));
	int copy = al_copy(arr);
	printf("AL4 three-slot elements: object references %d (copy alive), the slot named by the offset %d\n", HEAP_REF(a),
	       HEAP_REF(ka));
	AL_CHECK(HEAP_REF(a) == 3 && HEAP_REF(b) == 3, "A_REF: the objects have %d and %d references, want 3", HEAP_REF(a),
		HEAP_REF(b));
	AL_CHECK(HEAP_REF(ka) == 1 && HEAP_REF(kb) == 1, "A_REF retained the slot a vtable offset names (%d, %d)",
		HEAP_REF(ka), HEAP_REF(kb));
	al_layout("the A_REF copy", copy, 3, 5);
	heap_unref(copy);
	heap_unref(arr);
	AL_CHECK(HEAP_REF(a) == 1 && HEAP_REF(b) == 1, "after both arrays went the objects have %d and %d references",
		HEAP_REF(a), HEAP_REF(b));
	AL_CHECK(HEAP_REF(ka) == 1 && HEAP_REF(kb) == 1 && !strcmp(xl_text(ka), "offset a"),
		"a teardown released the slot a vtable offset names (%d, %d)", HEAP_REF(ka), HEAP_REF(kb));
	al_collect();
}

/* --- AL5: two-slot elements, stored three ways --- */

/* arr holds one element (obj, key) and owns one reference to obj; key is
 * the number of a live string slot. A copy and both teardowns must leave
 * that string alone. */
static void al_companion(const char *what, int arr, int obj, int key)
{
	struct page *p = al_page(arr);
	bool stored = p && p->nr_vars == 2 && p->values[0].i == obj && p->values[1].i == key;
	AL_CHECK(stored, "%s: the element is not (object, offset)", what);
	if (!stored)
		return;
	al_layout(what, arr, 2, 1);
	int ro = HEAP_REF(obj), rk = HEAP_REF(key);
	int copy = al_copy(arr);
	bool copied = HEAP_REF(obj) == ro + 1 && HEAP_REF(key) == rk;
	heap_unref(copy);
	bool back = HEAP_REF(obj) == ro && HEAP_REF(key) == rk;
	heap_unref(arr);
	bool gone = HEAP_REF(obj) == ro - 1 && HEAP_REF(key) == rk && !strcmp(xl_text(key), "offset");
	printf("AL5 %s: copy %s, copy released %s, array released %s\n", what, copied ? "ok" : "WRONG", back ? "ok" : "WRONG",
	       gone ? "ok" : "WRONG");
	AL_CHECK(copied && back && gone, "%s: the slot the offset names has %d reference(s) (had %d), the object %d (had %d)",
		what, HEAP_REF(key), rk, HEAP_REF(obj), ro);
}

static void al_two_slots(void)
{
	int key = sf_str("offset", -1);

	/* 1: the literal of Header@2, one element short: X_ASSIGN 4 is its own */
	int hfn = da_fn("Header@2");
	uint32_t init = oa_find("Header@2", X_A_INIT, 0, 1), assign = oa_find("Header@2", X_ASSIGN, 4, 0);
	int arr = al_init_local(hfn, 0, 2, init);
	al_layout("X_A_INIT 0 of two wrap<iwrap<IUserComponentParam>>", arr, 2, 2);
	int a = oa_obj(), b = oa_obj();
	int v[4] = { a, key, b, key };
	heap_ref(a);
	heap_ref(b);
	al_assign(assign, arr, 4, v);
	struct page *p = al_page(arr);
	AL_CHECK(p && p->nr_vars == 4 && p->values[0].i == a && p->values[1].i == key && p->values[2].i == b
		&& p->values[3].i == key, "X_ASSIGN 4 did not store two elements");
	int copy = al_copy(arr);
	bool copied = HEAP_REF(a) == 3 && HEAP_REF(b) == 3 && HEAP_REF(key) == 1;
	heap_unref(copy);
	heap_unref(arr);
	bool gone = HEAP_REF(a) == 1 && HEAP_REF(b) == 1 && HEAP_REF(key) == 1;
	printf("AL5 literal of two elements: copy %s, both arrays released %s\n", copied ? "ok" : "WRONG", gone ? "ok" : "WRONG");
	AL_CHECK(copied && gone, "literal: objects %d %d, the slot the offset names %d", HEAP_REF(a), HEAP_REF(b), HEAP_REF(key));
	heap_unref(a);
	heap_unref(b);

	/* 2: PushBack 0x10003 on the empty page X_A_INIT 0 makes for such a variable */
	int o = oa_obj();
	arr = al_init_local(hfn, 0, 0, init);
	int s[3] = { arr, o, key };
	int left = oa_hll("PushBack", 0, 0x10003, 3, s, NULL, 0);
	AL_CHECK(left == 0, "PushBack 0x10003 left %d slot(s)", left);
	al_companion("PushBack 0x10003 on an empty page", arr, o, key);
	heap_unref(o);

	/* 3: PushBack 0x10003 on a variable that has no page yet */
	o = oa_obj();
	arr = heap_alloc_slot(VM_PAGE);
	heap_set_page(arr, NULL);
	s[0] = arr;
	s[1] = o;
	left = oa_hll("PushBack", 0, 0x10003, 3, s, NULL, 0);
	AL_CHECK(left == 0, "PushBack 0x10003 left %d slot(s)", left);
	al_companion("PushBack 0x10003 without a page", arr, o, key);
	heap_unref(o);

	/* 4: X_OP_SET of both slots of an element of a sized page */
	o = oa_obj();
	arr = al_init_local(hfn, 0, 1, init);
	int e[2] = { o, key };
	oa_op_set(oa_find_any(X_OP_SET, 65538), arr, 0, 2, e);
	al_companion("X_OP_SET", arr, o, key);
	heap_unref(o);

	AL_CHECK(HEAP_REF(key) == 1 && !strcmp(xl_text(key), "offset"), "the string has %d reference(s) at the end", HEAP_REF(key));
	heap_unref(key);
	al_collect();
}

/* --- G1: one-slot literals --- */

static void al_guard_one_slot(void)
{
	size_t base = live_slots();
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	al_watch_start(-1);
	int n = da_callfunc(da_fn("BattleActionPanel::PanelType::GetNameList"), 0, NULL, r);
	al_watch = false;
	al_collect();
	struct page *p = n == 1 ? al_page(r[0].i) : NULL;
	static const char *const want[] = { "Invalid", "Skill", "Item", "PlayerStock" };
	bool ok = p && p->a_type == AIN_ARRAY_STRING && p->nr_vars == 4 && al_clamps == 0;
	for (int i = 0; ok && i < 4; i++)
		ok = !strcmp(xl_text(p->values[i].i), want[i]) && variable_type(p, i, NULL, NULL) == AIN_STRING;
	printf("G1 GetNameList: %d string(s)\n", p ? p->nr_vars : -1);
	AL_CHECK(ok, "the string literal is not four strings");
	if (p)
		heap_unref(r[0].i);
	/* as before this change: the four empty strings X_A_INIT gave the
	 * elements and the local's first array slot stay */
	AL_CHECK(live_slots() == base + 5, "%+ld live slot(s), was +5", (long)live_slots() - (long)base);

	/* the int literal Header@2 stores in <vtable> */
	int st = sp_sno("Header"), obj = alloc_struct(st);
	al_watch_start(-1);
	vm_call(da_fn("Header@2"), obj);
	al_watch = false;
	al_collect();
	p = al_page(heap[obj].page->values[sp_mem(st, "<vtable>")].i);
	ok = p && p->a_type == AIN_ARRAY_INT && p->nr_vars == 2 && p->values[0].i > 0 && p->values[1].i > 0
		&& variable_type(p, 1, NULL, NULL) == AIN_INT;
	printf("G1 Header@2 <vtable>: %d int(s)\n", p ? p->nr_vars : -1);
	AL_CHECK(ok, "the int literal is not two ints");
}

/* --- G2, G4: no declaration --- */

static void al_guard_undeclared(void)
{
	int st = sp_sno("activityeditor::detail::CGridView"), member = sp_mem(st, "m_vLine");
	uint32_t init = oa_find("EXHelper::GetNodes", X_A_INIT, 0, 0);

	/* an AIN before version 14 */
	int owner = alloc_struct(st), version = ain->version;
	ain->version = 13;
	int slot = wc_init_member(owner, member, 2, init);
	ain->version = version;
	struct page *p = al_page(slot);
	printf("G2 version 13: %d slot(s), struct_type %d\n", p ? p->nr_vars : -1, p ? p->array.struct_type : -1);
	AL_CHECK(p && p->nr_vars == 2 && p->array.struct_type == 1 && variable_type(p, 1, NULL, NULL) != AIN_VOID,
		"version 13: X_A_INIT 0 of two elements is not two one-slot elements, each a reference");
	heap_unref(owner);

	/* a declaration without an element type */
	owner = alloc_struct(st);
	struct ain_type *t = &ain->structures[st].members[member].type;
	struct ain_type *elem = t->array_type;
	t->array_type = NULL;
	slot = wc_init_member(owner, member, 2, init);
	t->array_type = elem;
	p = al_page(slot);
	printf("G4 no element type: %d slot(s), struct_type %d\n", p ? p->nr_vars : -1, p ? p->array.struct_type : -1);
	AL_CHECK(p && p->nr_vars == 2 && p->array.struct_type == 1 && variable_type(p, 1, NULL, NULL) != AIN_VOID,
		"an undeclared element: X_A_INIT 0 of two elements is not two one-slot elements");
	heap_unref(owner);
	al_collect();
}

/* --- G3: struct_type 2 and 3 are not a layout --- */

static bool al_all_refs(struct page *p)
{
	for (int i = 0; i < p->nr_vars; i++)
		if (variable_type(p, i, NULL, NULL) == AIN_VOID)
			return false;
	return true;
}

static void al_guard_struct_index(void)
{
	/* array<parts::detail::SPartsUpdateData>: struct index 2 */
	int target = ain_get_struct(ain, "parts::detail::SPartsUpdateData");
	int fno = da_fn("parts::detail::CallPartsUpdateEvent"), var = al_local_var(fno, "list");
	struct ain_type *t = &ain->functions[fno].vars[var].type;
	if (target != 2 || t->data != AIN_ARRAY || !t->array_type || t->array_type->data != AIN_STRUCT
	    || t->array_type->struc != 2) {
		printf("ARRAY-LITERAL no array of the struct with index 2 where G3 looks for one\n");
		exit(2);
	}
	int arr = al_init_local(fno, var, 3, oa_find("EXHelper::GetNodes", X_A_INIT, 0, 0));
	struct page *p = al_page(arr);
	printf("G3 array of struct 2: a_type %d, struct_type %d, %d slot(s), X_A_SIZE %d\n", p ? (int)p->a_type : -1,
	       p ? p->array.struct_type : -1, p ? p->nr_vars : -1, p ? wc_size(arr) : -1);
	AL_CHECK(p && p->a_type == AIN_ARRAY_STRUCT && p->array.struct_type == 2 && p->nr_vars == 3 && wc_size(arr) == 3
		&& al_all_refs(p) && variable_type(p, 1, NULL, NULL) == AIN_STRUCT,
		"an array of the struct with index 2 is not three struct references");
	heap_unref(arr);

	/* generic pages that only carry the number */
	for (int stride = 2; stride <= 3; stride++) {
		int o[6];
		struct page *g = alloc_page(ARRAY_PAGE, AIN_ARRAY, 6);
		g->array.rank = 1;
		g->array.struct_type = stride;
		for (int i = 0; i < 6; i++)
			g->values[i].i = o[i] = oa_obj();
		int slot = heap_alloc_page(g);
		bool refs = al_all_refs(g);
		heap_unref(slot);
		int freed = 0;
		for (int i = 0; i < 6; i++)
			if (HEAP_REF(o[i]) == 0)
				freed++;
		printf("G3 hand-made generic page with struct_type %d: %s, %d of 6 objects released with it\n", stride,
		       refs ? "every slot a reference" : "SOME SLOTS ARE VALUES", freed);
		AL_CHECK(refs && freed == 6, "a generic page with struct_type %d is treated as multi-slot", stride);
	}

	/* Array.Concat into a destination without a page: the new page takes its
	 * struct_type from whatever page of that size was freed last. Here that
	 * is a real two-slot page (one element of a user component parameter
	 * array), so both the number 2 and the page's layout are left over. */
	int o[2] = { oa_obj(), oa_obj() };
	int hfn = da_fn("Header@2");
	uint32_t hinit = oa_find("Header@2", X_A_INIT, 0, 1);
	heap_unref(al_init_local(hfn, 0, 1, hinit));
	struct page *src = alloc_page(ARRAY_PAGE, AIN_ARRAY, 0);
	src->array.rank = 1;
	src->array.struct_type = 1;
	int ssrc = heap_alloc_page(src);
	for (int i = 0; i < 2; i++) {
		int s[2] = { ssrc, o[i] };
		oa_hll("PushBack", 0, 0x10002, 2, s, NULL, 0);
	}
	/* the pushes may have taken the cached page: leave such a page again */
	heap_unref(al_init_local(hfn, 0, 1, hinit));
	int dst = heap_alloc_slot(VM_PAGE);
	heap_set_page(dst, NULL);
	int c[2] = { dst, ssrc };
	oa_hll("Concat", 0, 0x10002, 2, c, NULL, 0);
	al_collect();
	p = al_page(dst);
	printf("G3 Array.Concat into a variable without a page: %d slot(s), struct_type %d%s\n", p ? p->nr_vars : -1,
	       p ? p->array.struct_type : -1, p && p->array.struct_type == 2 ? " (left over)" : " (not the left-over 2: the case is weaker)");
	AL_CHECK(p && p->nr_vars == 2 && p->values[0].i == o[0] && p->values[1].i == o[1], "Concat did not copy two elements");
	AL_CHECK(p && al_all_refs(p), "the page Concat made treats a slot as a value (struct_type %d)", p ? p->array.struct_type : -1);
	AL_CHECK(HEAP_REF(o[0]) == 3 && HEAP_REF(o[1]) == 3, "the objects have %d and %d references, want 3", HEAP_REF(o[0]),
		HEAP_REF(o[1]));
	heap_unref(dst);
	heap_unref(ssrc);
	AL_CHECK(HEAP_REF(o[0]) == 1 && HEAP_REF(o[1]) == 1, "after both arrays went the objects have %d and %d references",
		HEAP_REF(o[0]), HEAP_REF(o[1]));
}

/* --- AL6: resume images --- */

static void al_resume_round(const char *format)
{
	int hfn = da_fn("Header@2"), cfn = da_fn("parts::detail::CallPartsUpdateEvent");
	uint32_t init = oa_find("Header@2", X_A_INIT, 0, 1), assign = oa_find("Header@2", X_ASSIGN, 4, 0);
	int key = sf_str("offset", -1), a = oa_obj(), b = oa_obj();
	/* the literal: (a, key) (b, key) */
	int lit = al_init_local(hfn, 0, 2, init);
	int v[4] = { a, key, b, key };
	heap_ref(a);
	heap_ref(b);
	al_assign(assign, lit, 4, v);
	/* a generic page that only carries the number 2 */
	int o[4];
	struct page *g = alloc_page(ARRAY_PAGE, AIN_ARRAY, 4);
	g->array.rank = 1;
	g->array.struct_type = 2;
	for (int i = 0; i < 4; i++)
		g->values[i].i = o[i] = oa_obj();
	int plain = heap_alloc_page(g);
	/* an array of the struct with index 2 */
	int structs = al_init_local(cfn, al_local_var(cfn, "list"), 2, oa_find("EXHelper::GetNodes", X_A_INIT, 0, 0));
	/* the array PushBack 0x10003 makes for a variable without a page: (po,
	 * key). The page it gets is one the cache was just given with rank 0
	 * (or a new one, zeroed), and an image writes rank 0 as "no array". */
	struct page *stale = alloc_page(ARRAY_PAGE, AIN_ARRAY, 2);
	stale->array.rank = 0;
	free_page(stale);
	int po = oa_obj(), pushed = heap_alloc_slot(VM_PAGE);
	heap_set_page(pushed, NULL);
	int push[3] = { pushed, po, key };
	oa_hll("PushBack", 0, 0x10003, 3, push, NULL, 0);
	AL_CHECK(al_page(pushed) && al_page(pushed)->array.rank == 1, "%s: the page PushBack made has rank %d, want 1", format,
		al_page(pushed) ? al_page(pushed)->array.rank : -1);
	al_collect();

	char file[64];
	snprintf(file, sizeof file, "al-resume-%s.dat", format);
	/* An image is taken inside a function: enter one (it is not run) and
	 * leave the two values system.ResumeSave has on the stack. */
	int csp = call_stack_ptr;
	function_call(da_fn("BattleActionPanel::PanelType::GetNameList"), VM_RETURN);
	stack_push(0);
	stack_push(0);
	int ok = vm_save_image("array-literal", file);
	stack_pop();
	stack_pop();
	AL_CHECK(call_stack_ptr == csp + 1, "call stack %d before the image", call_stack_ptr);
	if (!ok) {
		printf("  FAIL: the %s image was not written\n", format);
		fflush(stdout);
		_exit(1);
	}
	vm_load_image("array-literal", file);

	struct page *p = al_page(lit);
	bool same = p && p->nr_vars == 4 && p->values[0].i == a && p->values[1].i == key && p->values[2].i == b
		&& p->values[3].i == key;
	AL_CHECK(same, "%s: the literal did not come back", format);
	al_layout("the literal after the load", lit, 2, 2);
	struct page *pg = al_page(plain), *ps = al_page(structs);
	bool plain_ok = pg && pg->nr_vars == 4 && pg->array.struct_type == 2 && al_all_refs(pg);
	bool structs_ok = ps && ps->a_type == AIN_ARRAY_STRUCT && ps->array.struct_type == 2 && ps->nr_vars == 2
		&& al_all_refs(ps);
	AL_CHECK(plain_ok, "%s: the undeclared generic page came back multi-slot", format);
	AL_CHECK(structs_ok, "%s: the array of the struct with index 2 came back changed", format);
	struct page *pp = al_page(pushed);
	bool pushed_ok = pp && pp->nr_vars == 2 && pp->values[0].i == po && pp->values[1].i == key;
	AL_CHECK(pushed_ok, "%s: the array PushBack made for a variable without a page did not come back (%d slot(s))", format,
		pp ? pp->nr_vars : -1);
	if (pushed_ok)
		pushed_ok = al_layout("the pushed array after the load", pushed, 2, 1);
	bool pushed_released = false;
	if (pushed_ok) {
		AL_CHECK(HEAP_REF(po) == 2, "%s: the pushed object has %d reference(s) after the load, want 2", format, HEAP_REF(po));
		heap_unref(pushed);
		pushed_released = HEAP_REF(po) == 1 && HEAP_REF(key) == 1 && !strcmp(xl_text(key), "offset");
		AL_CHECK(pushed_released, "%s: releasing the loaded pushed array left its object %d and the slot its offset names %d",
			format, HEAP_REF(po), HEAP_REF(key));
	}
	AL_CHECK(HEAP_REF(a) == 2 && HEAP_REF(b) == 2 && HEAP_REF(key) == 1 && !strcmp(xl_text(key), "offset"),
		"%s: references after the load: objects %d %d, the slot the offsets name %d", format, HEAP_REF(a), HEAP_REF(b),
		HEAP_REF(key));
	heap_unref(lit);
	bool released = HEAP_REF(a) == 1 && HEAP_REF(b) == 1 && HEAP_REF(key) == 1 && !strcmp(xl_text(key), "offset");
	heap_unref(plain);
	int freed = 0;
	for (int i = 0; i < 4; i++)
		if (HEAP_REF(o[i]) == 0)
			freed++;
	printf("AL6 %s image: literal %s, its release %s; pushed array %s, its release %s; undeclared page %s, %d of 4 objects "
	       "released with it; struct array %s\n", format, same ? "back" : "LOST", released ? "balanced" : "WRONG",
	       pushed_ok ? "back" : "LOST", pushed_released ? "balanced" : "WRONG", plain_ok ? "as before" : "CHANGED", freed,
	       structs_ok ? "as before" : "CHANGED");
	AL_CHECK(released, "%s: releasing the loaded literal left objects %d %d and the slot its offsets name %d", format,
		HEAP_REF(a), HEAP_REF(b), HEAP_REF(key));
	AL_CHECK(freed == 4, "%s: %d of 4 objects of the undeclared page were released", format, freed);
}

static void al_resume(void)
{
	sp_setup_dir();
	config.save_format = SAVE_FORMAT_RSM;
	al_resume_round("rsm");
	config.save_format = SAVE_FORMAT_JSON;
	al_resume_round("json");
	sp_remove_dir();
}

/* --- AL7: sized, immediate 1, no literal --- */

static void al_default_init(void)
{
	size_t base = live_slots();
	int key = sf_str("offset", -1);

	/* AdvTransition@2: m_partsBg and m_partsMask, 2 ICGParts each */
	int st = sp_sno("AdvTransition"), member[2] = { sp_mem(st, "m_partsBg"), sp_mem(st, "m_partsMask") };
	if (ain->structures[st].destructor > 0) {
		printf("ARRAY-LITERAL AdvTransition has a destructor\n");
		exit(2);
	}
	uint32_t assign = oa_find("AdvTransition@0", X_ASSIGN, 2, 0);
	int obj = alloc_struct(st), o[4] = { -1, -1, -1, -1 }, good = 0;
	al_watch_start(-1);
	int sp = or_guard_push();
	vm_call(da_fn("AdvTransition@2"), obj);
	or_guard_pop("AdvTransition@2", sp, 0, NULL);
	al_watch = false;
	al_collect();
	for (int m = 0; m < 2; m++) {
		const char *name = m ? "AdvTransition.m_partsMask" : "AdvTransition.m_partsBg";
		int arr = heap[obj].page->values[member[m]].i;
		struct page *p = al_page(arr);
		printf("AL7 %s: %d slot(s), X_A_SIZE %d, struct_type %d, elements", name, p ? p->nr_vars : -1,
		       p ? wc_size(arr) : -1, p ? p->array.struct_type : -1);
		for (int i = 0; p && i + 1 < p->nr_vars && i < 4; i += 2)
			printf(" (%d, %d)", p->values[i].i, p->values[i + 1].i);
		printf("\n");
		if (!al_layout(name, arr, 2, 2))
			continue;
		AL_CHECK(p->values[0].i == -1 && p->values[2].i == -1, "%s: the first slot of a default element is not -1", name);
		/* as AdvTransition@0 does: X_ASSIGN 2 for each element, the object
		 * retained (SP_INC) */
		for (int i = 0; i < 2; i++) {
			int v[2] = { o[2 * m + i] = oa_obj(), key };
			heap_ref(v[0]);
			al_assign_at(assign, arr, 2 * i, 2, v);
		}
		AL_CHECK(p->values[0].i == o[2 * m] && p->values[1].i == key && p->values[2].i == o[2 * m + 1]
			&& p->values[3].i == key, "%s: X_ASSIGN 2 did not store the two elements", name);
		good++;
	}
	if (good == 2) {
		int copy = al_copy(heap[obj].page->values[member[0]].i);
		bool copied = HEAP_REF(o[0]) == 3 && HEAP_REF(o[1]) == 3 && HEAP_REF(key) == 1;
		heap_unref(copy);
		heap_unref(obj);
		bool gone = HEAP_REF(key) == 1 && !strcmp(xl_text(key), "offset");
		for (int i = 0; i < 4; i++)
			gone = gone && HEAP_REF(o[i]) == 1;
		printf("AL7 AdvTransition with four elements stored: copy %s, the object released %s\n", copied ? "ok" : "WRONG",
		       gone ? "ok" : "WRONG");
		AL_CHECK(copied && gone, "AdvTransition: objects %d %d %d %d, the slot the offsets name %d", HEAP_REF(o[0]),
			HEAP_REF(o[1]), HEAP_REF(o[2]), HEAP_REF(o[3]), HEAP_REF(key));
		for (int i = 0; i < 4; i++)
			heap_unref(o[i]);
	}

	/* MatchingInfo@2: FrontWorker, 3 option<wrap<Worker>> */
	st = sp_sno("MatchingInfo");
	if (ain->structures[st].destructor > 0) {
		printf("ARRAY-LITERAL MatchingInfo has a destructor\n");
		exit(2);
	}
	obj = alloc_struct(st);
	sp = or_guard_push();
	vm_call(da_fn("MatchingInfo@2"), obj);
	or_guard_pop("MatchingInfo@2", sp, 0, NULL);
	al_collect();
	int arr = heap[obj].page->values[sp_mem(st, "FrontWorker")].i, none = 0;
	struct page *p = al_page(arr);
	printf("AL7 MatchingInfo.FrontWorker: %d slot(s), X_A_SIZE %d, struct_type %d, elements", p ? p->nr_vars : -1,
	       p ? wc_size(arr) : -1, p ? p->array.struct_type : -1);
	for (int i = 0; p && i + 1 < p->nr_vars && i < 6; i += 2)
		printf(" (%d, %d)", p->values[i].i, p->values[i + 1].i);
	printf("\n");
	if (al_layout("MatchingInfo.FrontWorker", arr, 2, 3))
		for (int i = 0; i < 3; i++)
			if (p->values[2 * i].i == -1 && p->values[2 * i + 1].i == 1)
				none++;
	AL_CHECK(none == 3, "%d of 3 default option elements are none, (-1, 1)", none);
	heap_unref(obj);
	heap_unref(key);
	AL_CHECK(live_slots() == base, "%+ld live slot(s) at the end", (long)live_slots() - (long)base);
	al_collect();
}

/* --- AL8: the Array library gives the layout and keeps it --- */

static int al_no_page(void)
{
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, NULL);
	return slot;
}

static void al_library(void)
{
	int key = sf_str("offset", -1), hfn = da_fn("Header@2");
	uint32_t init = oa_find("Header@2", X_A_INIT, 0, 1);

	/* 1: Insert 0x10003 into a variable without a page */
	int o = oa_obj(), arr = al_no_page();
	int in[4] = { arr, 0, o, key };
	int left = oa_hll("Insert", 0, 0x10003, 4, in, NULL, 0);
	AL_CHECK(left == 0, "Insert 0x10003 left %d slot(s)", left);
	al_companion("Insert 0x10003 without a page", arr, o, key);
	heap_unref(o);

	/* 2: Insert 0x10003 into the empty page X_A_INIT 0 makes */
	o = oa_obj();
	in[0] = arr = al_init_local(hfn, 0, 0, init);
	in[2] = o;
	left = oa_hll("Insert", 0, 0x10003, 4, in, NULL, 0);
	AL_CHECK(left == 0, "Insert 0x10003 left %d slot(s)", left);
	al_companion("Insert 0x10003 on an empty page", arr, o, key);
	heap_unref(o);

	/* 3: Concat 0x10003 into a destination without a page */
	o = oa_obj();
	int src = al_no_page(), dst = al_no_page();
	int s[3] = { src, o, key }, c[2] = { dst, src };
	oa_hll("PushBack", 0, 0x10003, 3, s, NULL, 0);
	left = oa_hll("Concat", 0, 0x10003, 2, c, NULL, 0);
	AL_CHECK(left == 0 && HEAP_REF(o) == 3 && HEAP_REF(key) == 1, "Concat 0x10003 left %d slot(s), object %d, offset slot %d",
		left, HEAP_REF(o), HEAP_REF(key));
	heap_unref(src);
	al_companion("Concat 0x10003 into a variable without a page", dst, o, key);
	heap_unref(o);

	/* 4: Alloc 0x30003 (2 none elements), then X_OP_SET 65539 of element 1 */
	o = oa_obj();
	arr = al_no_page();
	int al[2] = { arr, 2 }, e[3] = { o, key, 0 };
	oa_hll("Alloc", 0, 0x30003, 2, al, NULL, 0);
	if (al_layout("Alloc 0x30003", arr, 3, 2)) {
		oa_op_set(oa_find_any(X_OP_SET, 0x10003), arr, 3, 3, e);
		struct page *p = al_page(arr);
		bool stored = p && p->nr_vars == 6 && p->values[3].i == o && p->values[4].i == key && p->values[5].i == 0
			&& HEAP_REF(o) == 2 && HEAP_REF(key) == 1;
		int copy = al_copy(arr);
		bool copied = HEAP_REF(o) == 3 && HEAP_REF(key) == 1;
		al_layout("the A_REF copy of the Alloc 0x30003 page", copy, 3, 2);
		heap_unref(copy);
		heap_unref(arr);
		bool gone = HEAP_REF(o) == 1 && HEAP_REF(key) == 1 && !strcmp(xl_text(key), "offset");
		printf("AL8 Alloc 0x30003 and X_OP_SET 65539: stored %s, copy %s, both arrays released %s\n", stored ? "ok" : "WRONG",
		       copied ? "ok" : "WRONG", gone ? "ok" : "WRONG");
		AL_CHECK(stored && copied && gone, "Alloc 0x30003: the object has %d reference(s), the slot the offset names %d",
			HEAP_REF(o), HEAP_REF(key));
	}
	heap_unref(o);

	/* 5: Erase and PopBack 0x10003 on three elements */
	int oo[3], out[1] = { -999 };
	arr = al_no_page();
	for (int i = 0; i < 3; i++) {
		int ps[3] = { arr, oo[i] = oa_obj(), key };
		oa_hll("PushBack", 0, 0x10003, 3, ps, NULL, 0);
	}
	al_layout("three PushBack 0x10003", arr, 2, 3);
	int er[3] = { arr, 1, 1 };
	left = oa_hll("Erase", 0, 0x10003, 3, er, out, 1);
	AL_CHECK(left == 1 && HEAP_REF(oo[1]) == 1 && HEAP_REF(key) == 1, "Erase 0x10003 left %d slot(s), object %d, offset slot %d",
		left, HEAP_REF(oo[1]), HEAP_REF(key));
	bool erased = al_layout("after Erase 0x10003", arr, 2, 2);
	left = oa_hll("PopBack", 0, 0x10003, 1, &arr, NULL, 0);
	AL_CHECK(left == 0 && HEAP_REF(oo[2]) == 1 && HEAP_REF(key) == 1, "PopBack 0x10003 left %d slot(s), object %d, offset slot %d",
		left, HEAP_REF(oo[2]), HEAP_REF(key));
	bool popped = al_layout("after PopBack 0x10003", arr, 2, 1);
	printf("AL8 Erase and PopBack 0x10003: layout after Erase %s, after PopBack %s\n", erased ? "kept" : "LOST",
	       popped ? "kept" : "LOST");
	al_companion("the element left after Erase and PopBack", arr, oo[0], key);
	for (int i = 0; i < 3; i++)
		heap_unref(oo[i]);

	AL_CHECK(HEAP_REF(key) == 1 && !strcmp(xl_text(key), "offset"), "the string has %d reference(s) at the end", HEAP_REF(key));
	heap_unref(key);
	al_collect();
}

/* --- G5: a one-slot element that stays generic, immediate 1 --- */

static void al_guard_generic_one_slot(void)
{
	int st = sp_sno("elkeditor::detail::CEmitterTimeLine"), member = sp_mem(st, "m_keyFrameLineList");
	struct ain_type *t = &ain->structures[st].members[member].type;
	if (t->data != AIN_ARRAY || !t->array_type || t->array_type->data != AIN_REF_STRUCT
	    || ss_type_slot_count(t->array_type) != 1 || ain->structures[st].destructor > 0) {
		printf("ARRAY-LITERAL no array of ref structs where G5 looks for one\n");
		exit(2);
	}
	int owner = alloc_struct(st);
	int slot = wc_init_member(owner, member, 11, wc_init_instruction("elkeditor::detail::CEmitterTimeLine@2", 1));
	struct page *p = al_page(slot);
	bool unset = p != NULL;
	for (int i = 0; unset && i < p->nr_vars; i++)
		unset = p->values[i].i == -1;
	printf("G5 X_A_INIT 1 of 11 ref structs: a_type %d, %d slot(s), struct_type %d, X_A_SIZE %d\n", p ? (int)p->a_type : -1,
	       p ? p->nr_vars : -1, p ? p->array.struct_type : -1, p ? wc_size(slot) : -1);
	AL_CHECK(p && p->a_type == AIN_ARRAY && p->nr_vars == 11 && p->array.struct_type == 1 && wc_size(slot) == 11
		&& al_all_refs(p) && unset, "X_A_INIT 1 of 11 one-slot generic elements is not 11 reference slots of -1");
	heap_unref(owner);
	al_collect();
}

static int test_array_literal(void)
{
	static const struct { const char *name; void (*run)(void); } cases[] = {
		{ "AL1", al_get_params }, { "AL2", al_whole_frame }, { "AL3", al_components },
		{ "AL4", al_three_slots }, { "AL5", al_two_slots }, { "AL6", al_resume },
		{ "AL7", al_default_init }, { "AL8", al_library },
		{ "G1", al_guard_one_slot }, { "G2+G4", al_guard_undeclared }, { "G3", al_guard_struct_index },
		{ "G5", al_guard_generic_one_slot },
	};
	int failed = 0, environment = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			al_failures = da_failures = wc_failures = oa_failures = bu_failures = or_failures = xl_failures = 0;
			cases[i].run();
			al_collect();
			fflush(stdout);
			_exit(al_failures ? 1 : 0);
		}
		int st = 0;
		if (pid < 0 || waitpid(pid, &st, 0) != pid) {
			printf("%s ENVIRONMENT (fork/wait failed)\n", cases[i].name);
			environment++;
		} else if (WIFSIGNALED(st)) {
			printf("%s FAIL (terminated by signal %d)\n", cases[i].name, WTERMSIG(st));
			failed++;
		} else if (WEXITSTATUS(st) == 0) {
			printf("%s ok\n", cases[i].name);
		} else if (WEXITSTATUS(st) == 2) {
			printf("%s ENVIRONMENT (no EX file, or the AIN lacks something this case looks up)\n", cases[i].name);
			environment++;
		} else {
			printf("%s FAIL (exit %d)\n", cases[i].name, WEXITSTATUS(st));
			failed++;
		}
	}
	printf("ARRAY-LITERAL %s: %d of %zu cases failed, %d environment error(s)\n",
		failed || environment ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]), environment);
	return environment ? 2 : failed ? 1 : 0;
}

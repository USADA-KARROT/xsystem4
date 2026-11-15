/* wrap-local-init: a v14 local variable declared as a wrap starts as -1, as
 * in the original, and not as a box this VM makes for it; nothing is left
 * when the bytecode's first store overwrites it.
 *
 * Original (static disassembly of the unpacked executable):
 *   - A call makes the local page with 0x679970, which writes every
 *     variable's initial value with 0x656b10: string, struct, delegate and
 *     array are -1; the other types go to 0x656970, where a wrap (type 82,
 *     branch 0x6569b8) is -1 in its first slot and 0 in the others (the slot
 *     count of 0x653420), like the ref types and iface. No object is made.
 *   - X_ASSIGN (0x66f080) stores the values as they are, without a
 *     reference count. A wrap local given its value at the declaration and
 *     the element variable of a foreach are stored without a DELETE before
 *     (there is nothing to release); a function result kept in an anonymous
 *     local is DELETEd before each store (DELETE skips -1, 0x668032).
 *   - Returning releases what the slots hold then, by declared type
 *     (0x6570f0 -> 0x656cf0 -> 0x656c10; -1 is skipped).
 * This VM (before the change) gave each wrap local a one-value STRUCT_PAGE
 * box (variable_initval) in _function_call and alloc_scenario_page; a store
 * without a DELETE left that box with one reference and no owner, one per
 * call of such a function. Since ff77c18 the first slot of a value option
 * local is -1 in the same two places, for the same reason.
 *
 * "Empty boxes" below are the slots of the shape of this VM's box: a live
 * VM_PAGE, STRUCT_PAGE, index -1, one value, -1 inside. variable_initval
 * makes new ones; a copied or restored box has the same shape and would be
 * counted too.
 *
 * Cases (each in a child process). In parentheses: what the bytecode does,
 * read from the disassembly, not checked. "Checks": what is tested. Only WL1
 * to WL4 and WL5's HasValue first check the declared types they rely on; the
 * others check values only (the WL6 and WL7 tables go by each declared type).
 *   WL1  parts::detail::AddProcessList with an empty list, 8 calls (the
 *        element variable p of its foreach is stored before the loop
 *        without a DELETE; the loop body, which holds the only call and the
 *        HLL behind it, does not run). Checks: no live slot and no empty
 *        box is left; the list keeps its reference count.
 *   WL2  OptionalExtensions::HasValue<string>, 4 calls with a value and 4
 *        with none (the named local v is given its value at the
 *        declaration, if-let). Checks: 4 results 1 and 4 results 0; no live
 *        slot and no empty box is left; the string and both holders keep
 *        their reference counts.
 *   WL3  backlog::detail::SBackLogUnitModelManager@StopVoice over a list of
 *        three models, 5 calls: 4, each followed by setting member 2 of the
 *        three models back to 1, then a fifth whose result is read (a
 *        foreach that runs, element variable [list page, index]; no call
 *        inside). Checks: the stacks are back after each of the first 4;
 *        after the fifth, member 2 of all three models is 0; no live slot
 *        and no empty box is left by the 5 calls; the models and the list
 *        keep their reference counts.
 *   WL4  activityeditor::detail::SHsvColor@Color::get, 4 calls (function
 *        results kept in anonymous wrap locals, DELETEd before each store;
 *        a control: nothing was left before the change either). Checks:
 *        local 0 is an anonymous wrap; the 4 results are CASColor objects
 *        and alike; no live slot and no empty box is left.
 *   WL5  Entry values, read after the call made the frame and before its
 *        first instruction (the frame then runs): HasValue<ISpriteParts&>,
 *        given an option with no value, has local 2 (wrap<iwrap<>>) and its
 *        second slot local 3 = [-1, 0]; AddProcessList, given an empty
 *        list, has p (local 4, wrap<ref T>) and local 5 = [-1, 0], local 3
 *        (the foreach index) 0 and local 6 (the foreach list) -1. Only these
 *        locals are read.
 *   WL6  The scenario page (alloc_scenario_page). First case: the page made
 *        for AddProcessList is its own (7 values) with p = [-1, 0], and
 *        releasing it leaves no live slot. Second case: a page for each
 *        function, by name. alloc_scenario_page takes the first function of
 *        a name, so a function with no name or no variable, and one whose
 *        name leads to a function already checked, is skipped (14512 of
 *        37742 here; WL7 goes by function number). On each page every wrap
 *        variable must be -1, a void variable right after a wrap 0, and
 *        every other variable must have the kind of initial value
 *        variable_initval gives its type (wl_expected: 0, -1, a live string,
 *        a slot with no page or an empty delegate page; value options -1),
 *        compared by kind, not by value; releasing the pages leaves no live
 *        slot.
 *   WL7  The same table for the frame a call makes (_function_call, the
 *        locals after the arguments), for every function with a local after
 *        its arguments (the others, 26373 here, are skipped). The frames
 *        are taken down without running (argument slots set to -1 first);
 *        the call stack and the live slots must be back.
 *   WL8  With the AIN reported as version 13, p of AddProcessList is a box
 *        on a call frame and on a scenario page. Back at version 14,
 *        variable_initval(AIN_WRAP) itself still makes a box; its other
 *        callers (global and array element wraps) use it but are not run
 *        here.
 *   WL9  Returns. AddProcessList with no list (-1), 4 calls (it returns
 *        before p is stored; p is not read at the return). Checks: no live
 *        slot and no empty box is left. HasValue<ISpriteParts&> with a
 *        value, 4 calls (v holds the object when it returns). Checks: 4
 *        results 1; the object and the holder are back to their reference
 *        counts; no live slot and no empty box is left.
 */

static int wl_fn(const char *name)
{
	int f = ain_get_function(ain, (char *)name);
	if (f < 0) {
		printf("WRAP-LOCAL function %s not declared\n", name);
		exit(2);
	}
	return f;
}

static bool wl_is_box(size_t i)
{
	if (HEAP_REF(i) <= 0 || heap[i].type != VM_PAGE)
		return false;
	struct page *p = heap[i].page;
	return p && p->type == STRUCT_PAGE && p->index == -1 && p->nr_vars == 1 && p->values[0].i == -1;
}

static long wl_boxes(void)
{
	long n = 0;
	for (size_t i = 2; i < heap_size; i++)
		if (wl_is_box(i))
			n++;
	return n;
}

/* CALLFUNC fno with the given argument slots; returns the number of result
 * slots (copied to ret, up to 2). */
static int wl_call(int fno, int nargs, const int *args, union vm_value *ret)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	if (call_stack_ptr != csp) {
		printf("  call of %s left the call stack at %d (was %d)\n", ain->functions[fno].name, call_stack_ptr, csp);
		exit(1);
	}
	int n = stack_ptr - sp;
	for (int i = 0; i < n && i < 2; i++)
		ret[i] = stack[sp + i];
	stack_ptr = sp;
	return n;
}

/* An empty one-dimensional array page. */
static int wl_empty_list(void)
{
	struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY, 0);
	p->array.struct_type = -1;
	p->array.rank = 1;
	return heap_alloc_page(p);
}

/* A holder page for a wrap<option<T>> argument: [values..., flag]; an int
 * page, so that releasing it releases nothing it holds. */
static int wl_holder(int n, const int *v)
{
	struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
	p->array.struct_type = -1;
	p->array.rank = 1;
	for (int i = 0; i < n; i++)
		p->values[i].i = v[i];
	return heap_alloc_page(p);
}

/* --- WL1 --- */

static int wl1_add_process_list(void)
{
	int fno = wl_fn("parts::detail::AddProcessList");
	struct ain_function *f = &ain->functions[fno];
	SP_CHECK(f->nr_args == 3 && f->nr_vars == 7 && f->vars[4].type.data == AIN_WRAP && f->vars[5].type.data == AIN_VOID,
		 "AddProcessList is not (int, ref array, int) with the wrap local p at 4");
	int list = wl_empty_list(), list_ref = HEAP_REF(list);
	size_t live0 = live_slots();
	long boxes0 = wl_boxes();
	for (int k = 0; k < 8; k++) {
		int args[] = { 0, list, 0 };
		union vm_value r[2];
		SP_CHECK(wl_call(fno, 3, args, r) == 0, "AddProcessList returned a value");
	}
	long dlive = (long)live_slots() - (long)live0, dbox = wl_boxes() - boxes0;
	printf("WL1 AddProcessList x8 with an empty list: %+ld live slot(s), %+ld empty box(es); list ref %d -> %d\n",
	       dlive, dbox, list_ref, HEAP_REF(list));
	SP_CHECK(dbox == 0, "%ld empty wrap box(es) left by 8 calls (the box of p, overwritten by its first store)", dbox);
	SP_CHECK(dlive == 0, "%+ld live slot(s) after 8 calls", dlive);
	SP_CHECK(HEAP_REF(list) == list_ref, "the list has %d reference(s), had %d", HEAP_REF(list), list_ref);
	return 0;
}

/* --- WL2 --- */

static int wl2_has_value_string(void)
{
	int fno = wl_fn("OptionalExtensions::HasValue<string>");
	struct ain_function *f = &ain->functions[fno];
	SP_CHECK(f->nr_args == 2 && f->nr_vars == 3 && f->vars[2].type.data == AIN_WRAP,
		 "HasValue<string> is not (wrap<option<string>>) with the wrap local v at 2");
	int str = sp_str("wl2"), str_ref = HEAP_REF(str);
	int some_v[] = { str, 0 }, none_v[] = { -1, 1 };
	int some = wl_holder(2, some_v), none = wl_holder(2, none_v);
	int some_ref = HEAP_REF(some), none_ref = HEAP_REF(none);
	size_t live0 = live_slots();
	long boxes0 = wl_boxes();
	int got_some = 0, got_none = 0;
	for (int k = 0; k < 4; k++) {
		union vm_value r[2];
		int a1[] = { some, 0 }, a2[] = { none, 0 };
		SP_CHECK(wl_call(fno, 2, a1, r) == 1, "HasValue returned no value");
		got_some += r[0].i == 1;
		SP_CHECK(wl_call(fno, 2, a2, r) == 1, "HasValue returned no value");
		got_none += r[0].i == 0;
	}
	long dlive = (long)live_slots() - (long)live0, dbox = wl_boxes() - boxes0;
	printf("WL2 HasValue<string> x4 with a value, x4 with none: %d true, %d false; %+ld live slot(s), %+ld empty box(es); "
	       "string ref %d -> %d\n", got_some, got_none, dlive, dbox, str_ref, HEAP_REF(str));
	SP_CHECK(got_some == 4 && got_none == 4, "results %d/4 true and %d/4 false", got_some, got_none);
	SP_CHECK(dbox == 0, "%ld empty wrap box(es) left by 8 calls (the box of v, overwritten by its declaration)", dbox);
	SP_CHECK(dlive == 0, "%+ld live slot(s) after 8 calls", dlive);
	SP_CHECK(HEAP_REF(str) == str_ref && HEAP_REF(some) == some_ref && HEAP_REF(none) == none_ref,
		 "string/holders have %d/%d/%d reference(s), had %d/%d/%d", HEAP_REF(str), HEAP_REF(some), HEAP_REF(none),
		 str_ref, some_ref, none_ref);
	return 0;
}

/* --- WL3 --- */

static int wl3_stop_voice(void)
{
	int fno = wl_fn("backlog::detail::SBackLogUnitModelManager@StopVoice");
	struct ain_function *f = &ain->functions[fno];
	SP_CHECK(f->nr_args == 0 && f->nr_vars == 4 && f->vars[1].type.data == AIN_WRAP && f->vars[2].type.data == AIN_VOID,
		 "StopVoice has no wrap local model at 1");
	int mst = sp_sno("backlog::detail::SBackLogUnitModelManager"), est = sp_sno("backlog::detail::SBackLogUnitModel");
	SP_CHECK(ain->structures[est].nr_members > 2 && (ain->structures[est].members[2].type.data == AIN_INT
		 || ain->structures[est].members[2].type.data == AIN_BOOL), "the model's member 2 is not an int or bool");
	int mgr = alloc_struct(mst);
	int listslot = sp_get(mgr, "m_list");
	SP_CHECK(listslot > 1 && heap[listslot].type == VM_PAGE, "m_list has no array slot");
	int model[3], mref[3];
	for (int i = 0; i < 3; i++) {
		model[i] = alloc_struct(est);
		heap[model[i]].page->values[2].i = 1;
		heap_ref(model[i]); /* the list's reference */
	}
	sp_arr_set(listslot, model, 3, false);
	/* one slot per element, as X_A_INIT with immediate 0 leaves a generic
	 * page (X_A_SIZE reads struct_type as the element width) */
	heap[listslot].page->array.struct_type = 1;
	for (int i = 0; i < 3; i++)
		mref[i] = HEAP_REF(model[i]);
	int list_ref = HEAP_REF(listslot);
	size_t live0 = live_slots();
	long boxes0 = wl_boxes();
	for (int k = 0; k < 4; k++) {
		int sp = stack_ptr, csp = call_stack_ptr;
		size_t ip = instr_ptr;
		stack_push(mgr);
		method_call(fno, VM_RETURN);
		vm_execute();
		instr_ptr = ip;
		SP_CHECK(call_stack_ptr == csp && stack_ptr == sp, "StopVoice left the stacks at %d/%d", call_stack_ptr, stack_ptr - sp);
		for (int i = 0; i < 3; i++)
			heap[model[i]].page->values[2].i = 1;
	}
	/* one more call to read what it did */
	int sp = stack_ptr;
	size_t ip = instr_ptr;
	stack_push(mgr);
	method_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	stack_ptr = sp;
	int zeroed = 0, kept = 0;
	for (int i = 0; i < 3; i++) {
		zeroed += heap[model[i]].page->values[2].i == 0;
		kept += HEAP_REF(model[i]) == mref[i];
	}
	long dlive = (long)live_slots() - (long)live0, dbox = wl_boxes() - boxes0;
	printf("WL3 StopVoice x5 over 3 models: %d member(s) set to 0, %d model(s) keep their count; %+ld live slot(s), "
	       "%+ld empty box(es); list ref %d -> %d\n", zeroed, kept, dlive, dbox, list_ref, HEAP_REF(listslot));
	SP_CHECK(zeroed == 3, "%d of 3 models had member 2 set to 0", zeroed);
	SP_CHECK(dbox == 0, "%ld empty wrap box(es) left by 5 calls (the box of model, overwritten before the loop)", dbox);
	SP_CHECK(dlive == 0, "%+ld live slot(s) after 5 calls", dlive);
	SP_CHECK(kept == 3 && HEAP_REF(listslot) == list_ref, "models/list changed their reference counts");
	return 0;
}

/* --- WL4 --- */

static int wl4_result_temporaries(void)
{
	int fno = wl_fn("activityeditor::detail::SHsvColor@Color::get");
	struct ain_function *f = &ain->functions[fno];
	SP_CHECK(f->nr_vars >= 1 && f->vars[0].type.data == AIN_WRAP && !strncmp(f->vars[0].name, "<dummy", 6),
		 "Color::get has no anonymous wrap local at 0");
	int obj = sp_new("activityeditor::detail::SHsvColor");
	int cst = sp_sno("CASColor");
	size_t live0 = live_slots();
	long boxes0 = wl_boxes();
	int first[4] = { 0 }, same = 0, colors = 0;
	for (int k = 0; k < 4; k++) {
		call_method0(fno, obj);
		int r = stack_pop().i;
		bool ok = da_live_page(r) && heap[r].page->type == STRUCT_PAGE && heap[r].page->index == cst
			&& heap[r].page->nr_vars >= 4;
		colors += ok;
		if (ok) {
			int c[4];
			for (int i = 0; i < 4; i++)
				c[i] = heap[r].page->values[i].i;
			if (k == 0)
				memcpy(first, c, sizeof c);
			same += !memcmp(first, c, sizeof c);
		}
		heap_unref(r);
	}
	long dlive = (long)live_slots() - (long)live0, dbox = wl_boxes() - boxes0;
	printf("WL4 Color::get x4: %d CASColor result(s) (%d,%d,%d,%d), %d alike; %+ld live slot(s), %+ld empty box(es)\n",
	       colors, first[0], first[1], first[2], first[3], same, dlive, dbox);
	SP_CHECK(colors == 4 && same == 4, "%d/4 results are CASColor objects, %d/4 alike", colors, same);
	SP_CHECK(dbox == 0 && dlive == 0, "%+ld empty box(es), %+ld live slot(s) after 4 calls", dbox, dlive);
	return 0;
}

/* --- WL5 --- */

/* Make the frame of fno (arguments pushed), check it with check(), then run it. */
static int wl_frame(int fno, int nargs, const int *args, int (*check)(struct page *))
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	function_call(fno, VM_RETURN);
	int rc = check(heap[call_stack[call_stack_ptr - 1].page_slot].page);
	vm_execute();
	instr_ptr = ip;
	stack_ptr = sp;
	if (call_stack_ptr != csp)
		return 1;
	return rc;
}

static int wl5_sprite_entry(struct page *p)
{
	printf("WL5 HasValue<ISpriteParts&> entry: local 2 = %d, local 3 = %d%s\n", p->values[2].i, p->values[3].i,
	       p->values[2].i > 1 && wl_is_box(p->values[2].i) ? " (a box)" : "");
	SP_CHECK(p->values[2].i == -1 && p->values[3].i == 0, "local [2, 3] = [%d, %d], want [-1, 0]", p->values[2].i,
		 p->values[3].i);
	return 0;
}

static int wl5_process_entry(struct page *p)
{
	printf("WL5 AddProcessList entry: locals 3..6 = %d, %d, %d, %d%s\n", p->values[3].i, p->values[4].i, p->values[5].i,
	       p->values[6].i, p->values[4].i > 1 && wl_is_box(p->values[4].i) ? " (local 4 is a box)" : "");
	SP_CHECK(p->values[4].i == -1 && p->values[5].i == 0, "p = [%d, %d], want [-1, 0]", p->values[4].i, p->values[5].i);
	SP_CHECK(p->values[3].i == 0 && p->values[6].i == -1, "foreach index %d (want 0), list %d (want -1)", p->values[3].i,
		 p->values[6].i);
	return 0;
}

static int wl5_entry_values(void)
{
	int sprite = wl_fn("OptionalExtensions::HasValue<ISpriteParts&>");
	struct ain_function *f = &ain->functions[sprite];
	SP_CHECK(f->nr_args == 2 && f->nr_vars == 4 && f->vars[2].type.data == AIN_WRAP && f->vars[3].type.data == AIN_VOID,
		 "HasValue<ISpriteParts&> has no two-slot wrap local at 2");
	int none_v[] = { -1, 0, 1 };
	int none = wl_holder(3, none_v);
	int a1[] = { none, 0 };
	if (wl_frame(sprite, 2, a1, wl5_sprite_entry))
		return 1;
	int list = wl_empty_list();
	int a2[] = { 0, list, 0 };
	return wl_frame(wl_fn("parts::detail::AddProcessList"), 3, a2, wl5_process_entry);
}

/* --- WL6 / WL7: tables --- */

enum wl_kind { WK_ZERO, WK_MINUS1, WK_BOX, WK_STRING, WK_NULLPAGE, WK_DELEGATE, WK_OTHER };
static const char *wl_kind_name[] = { "0", "-1", "box", "string", "null-page", "delegate", "other" };

static enum wl_kind wl_kind_of(int v)
{
	if (v == -1)
		return WK_MINUS1;
	if (v == 0)
		return WK_ZERO;
	if (v < 2 || !heap_index_valid(v) || HEAP_REF(v) <= 0)
		return WK_OTHER;
	if (heap[v].type == VM_STRING)
		return WK_STRING;
	if (heap[v].type != VM_PAGE)
		return WK_OTHER;
	struct page *p = heap[v].page;
	if (!p)
		return WK_NULLPAGE;
	if (p->type == DELEGATE_PAGE && p->nr_vars == 0)
		return WK_DELEGATE;
	if (wl_is_box(v))
		return WK_BOX;
	return WK_OTHER;
}

/* What a local of this type starts as: a wrap -1 (the box below version 14);
 * every other type what variable_initval gives it (value options -1). */
static enum wl_kind wl_expected(const struct ain_type *t)
{
	if (ain_option_type_is_value(t))
		return WK_MINUS1;
	switch (t->data) {
	case AIN_WRAP:
		return ain->version >= 14 ? WK_MINUS1 : WK_BOX;
	case AIN_STRING:
		return WK_STRING;
	case AIN_STRUCT:
	case AIN_REF_TYPE:
	case AIN_IFACE:
		return WK_MINUS1;
	case AIN_ARRAY_TYPE:
	case AIN_ARRAY:
		return WK_NULLPAGE;
	case AIN_DELEGATE:
		return WK_DELEGATE;
	case AIN_FUNC_TYPE:
		if (ain->version < 11)
			return WK_ZERO;
		return WK_NULLPAGE;
	case AIN_IFACE_WRAP:
	case AIN_OPTION:
		return WK_NULLPAGE;
	default:
		return WK_ZERO;
	}
}

struct wl_table { long funcs, skipped, wraps, wrap_ok, two_slot, two_slot_ok, others, other_ok, shown; long kinds[7]; };

static void wl_table_page(struct wl_table *t, const char *label, int fno, struct page *p, int from)
{
	struct ain_function *f = &ain->functions[fno];
	for (int i = from; i < f->nr_vars && i < p->nr_vars; i++) {
		enum wl_kind want = wl_expected(&f->vars[i].type), got = wl_kind_of(p->values[i].i);
		bool ok = want == got;
		if (f->vars[i].type.data == AIN_WRAP) {
			t->wraps++;
			t->wrap_ok += ok;
			t->kinds[got]++;
			if (i + 1 < f->nr_vars && f->vars[i + 1].type.data == AIN_VOID) {
				t->two_slot++;
				t->two_slot_ok += p->values[i + 1].i == 0;
				if (p->values[i + 1].i != 0 && t->shown++ < 5)
					printf("  %s mismatch: function %d %s var %d (companion of a wrap) = %d, want 0\n", label, fno,
					       f->name, i + 1, p->values[i + 1].i);
			}
		} else {
			t->others++;
			t->other_ok += ok;
		}
		if (!ok && t->shown++ < 5)
			printf("  %s mismatch: function %d %s var %d %s (type %d) = %d (%s), want %s\n", label, fno, f->name, i,
			       f->vars[i].name, f->vars[i].type.data, p->values[i].i, wl_kind_name[got], wl_kind_name[want]);
	}
}

static int wl_table_report(const char *label, struct wl_table *t, long dlive)
{
	printf("%s %ld function(s) (%ld skipped): %ld wrap variable(s), %ld as expected (-1 %ld, box %ld, other %ld); "
	       "%ld two-slot wrap(s), %ld companion(s) 0; %ld other variable(s), %ld as before; %+ld live slot(s) after\n",
	       label, t->funcs, t->skipped, t->wraps, t->wrap_ok, t->kinds[WK_MINUS1], t->kinds[WK_BOX],
	       t->wraps - t->kinds[WK_MINUS1] - t->kinds[WK_BOX], t->two_slot, t->two_slot_ok, t->others, t->other_ok, dlive);
	SP_CHECK(t->funcs > 0 && t->wraps > 0, "no function or no wrap variable seen");
	SP_CHECK(t->wrap_ok == t->wraps, "%ld of %ld wrap variable(s) did not start as expected", t->wraps - t->wrap_ok,
		 t->wraps);
	SP_CHECK(t->two_slot_ok == t->two_slot, "%ld two-slot wrap companion(s) are not 0", t->two_slot - t->two_slot_ok);
	SP_CHECK(t->other_ok == t->others, "%ld other variable(s) changed their initial value", t->others - t->other_ok);
	SP_CHECK(dlive == 0, "%+ld live slot(s) after the pages were released", dlive);
	return 0;
}

static int wl6_process_page(void)
{
	int fno = wl_fn("parts::detail::AddProcessList");
	size_t live0 = live_slots();
	int slot = alloc_scenario_page(ain->functions[fno].name);
	struct page *p = heap[slot].page;
	SP_CHECK(p && p->index == fno && p->nr_vars == 7, "the scenario page is not AddProcessList's");
	printf("WL6 scenario page of AddProcessList: values 0..6 = %d %d %d %d %d %d %d%s\n", p->values[0].i, p->values[1].i,
	       p->values[2].i, p->values[3].i, p->values[4].i, p->values[5].i, p->values[6].i,
	       p->values[4].i > 1 && wl_is_box(p->values[4].i) ? " (p is a box)" : "");
	bool ok = p->values[4].i == -1 && p->values[5].i == 0;
	heap_unref(slot);
	SP_CHECK(ok, "p is not [-1, 0] on the scenario page");
	SP_CHECK(live_slots() == live0, "%+ld live slot(s) after the page was released", (long)live_slots() - (long)live0);
	return 0;
}

static int wl6_scenario_table(void)
{
	struct wl_table t = { 0 };
	size_t live0 = live_slots();
	unsigned char *seen = xcalloc(ain->nr_functions, 1);
	for (int fno = 0; fno < ain->nr_functions; fno++) {
		struct ain_function *f = &ain->functions[fno];
		if (!f->name || !f->name[0] || f->nr_vars <= 0) {
			t.skipped++;
			continue;
		}
		int slot = alloc_scenario_page(f->name);
		struct page *p = heap[slot].page;
		int g = p ? p->index : -1;
		if (g < 0 || g >= ain->nr_functions || seen[g]) {
			heap_unref(slot);
			t.skipped++;
			continue;
		}
		seen[g] = 1;
		t.funcs++;
		wl_table_page(&t, "WL6", g, p, 0);
		heap_unref(slot);
	}
	free(seen);
	return wl_table_report("WL6 scenario pages:", &t, (long)live_slots() - (long)live0);
}

/* The frame _function_call makes, taken down without running: the argument
 * slots are set to -1 first (nothing was passed), then the page released. */
static int wl_make_frame(int fno)
{
	size_t ip = instr_ptr;
	int slot = _function_call(fno, VM_RETURN);
	instr_ptr = ip;
	return slot;
}

static void wl_drop_frame(int fno, int slot)
{
	call_stack_ptr--;
	struct page *p = heap[slot].page;
	for (int i = 0; p && i < ain->functions[fno].nr_args && i < p->nr_vars; i++)
		p->values[i].i = -1;
	heap_unref(slot);
}

static int wl7_call_table(void)
{
	struct wl_table t = { 0 };
	size_t live0 = live_slots();
	int csp = call_stack_ptr;
	for (int fno = 0; fno < ain->nr_functions; fno++) {
		struct ain_function *f = &ain->functions[fno];
		if (f->nr_vars <= f->nr_args) {
			t.skipped++;
			continue;
		}
		int slot = wl_make_frame(fno);
		if (slot < 0) {
			t.skipped++;
			continue;
		}
		t.funcs++;
		wl_table_page(&t, "WL7", fno, heap[slot].page, f->nr_args);
		wl_drop_frame(fno, slot);
	}
	SP_CHECK(call_stack_ptr == csp, "the call stack is at %d, was %d", call_stack_ptr, csp);
	return wl_table_report("WL7 call frames:", &t, (long)live_slots() - (long)live0);
}

/* --- WL8 --- */

static int wl8_versions(void)
{
	int fno = wl_fn("parts::detail::AddProcessList");
	int saved = ain->version;
	ain->version = 13;
	int frame = wl_make_frame(fno);
	int fv = heap[frame].page->values[4].i;
	bool frame_box = fv > 1 && wl_is_box(fv);
	int scen = alloc_scenario_page(ain->functions[fno].name);
	int sv = heap[scen].page->values[4].i;
	bool scen_box = sv > 1 && wl_is_box(sv);
	ain->version = saved;
	wl_drop_frame(fno, frame);
	heap_unref(scen);
	union vm_value g = variable_initval(AIN_WRAP);
	bool init_box = g.i > 1 && wl_is_box(g.i);
	printf("WL8 version 13: call frame p = %d (%s), scenario page p = %d (%s); version 14 variable_initval(wrap) = %d (%s)\n",
	       fv, frame_box ? "a box" : "no box", sv, scen_box ? "a box" : "no box", g.i, init_box ? "a box" : "no box");
	if (g.i > 1)
		heap_unref(g.i);
	SP_CHECK(frame_box && scen_box, "below version 14 a wrap local no longer gets its box");
	SP_CHECK(init_box, "variable_initval no longer makes a box for a wrap");
	return 0;
}

/* --- WL9 --- */

static int wl9_returns(void)
{
	int fno = wl_fn("parts::detail::AddProcessList");
	size_t live0 = live_slots();
	long boxes0 = wl_boxes();
	for (int k = 0; k < 4; k++) {
		int args[] = { 0, -1, 0 };
		union vm_value r[2];
		SP_CHECK(wl_call(fno, 3, args, r) == 0, "AddProcessList returned a value");
	}
	long dlive = (long)live_slots() - (long)live0, dbox = wl_boxes() - boxes0;
	printf("WL9 AddProcessList x4 with no list (returns before p is stored): %+ld live slot(s), %+ld empty box(es)\n", dlive,
	       dbox);
	SP_CHECK(dlive == 0 && dbox == 0, "%+ld live slot(s), %+ld box(es) after early returns", dlive, dbox);

	int sprite = wl_fn("OptionalExtensions::HasValue<ISpriteParts&>");
	int obj = alloc_struct(sp_sno("CASPosF")), obj_ref = HEAP_REF(obj);
	int some_v[] = { obj, 0, 0 };
	int some = wl_holder(3, some_v), some_ref = HEAP_REF(some);
	live0 = live_slots();
	boxes0 = wl_boxes();
	int yes = 0;
	for (int k = 0; k < 4; k++) {
		int args[] = { some, 0 };
		union vm_value r[2];
		SP_CHECK(wl_call(sprite, 2, args, r) == 1, "HasValue returned no value");
		yes += r[0].i == 1;
	}
	dlive = (long)live_slots() - (long)live0;
	dbox = wl_boxes() - boxes0;
	printf("WL9 HasValue<ISpriteParts&> x4 with a value (v holds the object at the return): %d true; object ref %d -> %d; "
	       "%+ld live slot(s), %+ld empty box(es)\n", yes, obj_ref, HEAP_REF(obj), dlive, dbox);
	SP_CHECK(yes == 4, "%d/4 true", yes);
	SP_CHECK(HEAP_REF(obj) == obj_ref && HEAP_REF(some) == some_ref, "object/holder have %d/%d reference(s), had %d/%d",
		 HEAP_REF(obj), HEAP_REF(some), obj_ref, some_ref);
	SP_CHECK(dlive == 0 && dbox == 0, "%+ld live slot(s), %+ld box(es) after 4 calls", dlive, dbox);
	return 0;
}

static int test_wrap_local_init(void)
{
	sp_setup_dir();
	sp_run("WL1 foreach element, empty list, 8 calls", wl1_add_process_list);
	sp_run("WL2 named wrap given at its declaration", wl2_has_value_string);
	sp_run("WL3 foreach element over 3 models", wl3_stop_voice);
	sp_run("WL4 result temporaries (control)", wl4_result_temporaries);
	sp_run("WL5 two-slot wraps at entry", wl5_entry_values);
	sp_run("WL6 scenario page of AddProcessList", wl6_process_page);
	sp_run("WL6 scenario pages of every function", wl6_scenario_table);
	sp_run("WL7 call frames of every function", wl7_call_table);
	sp_run("WL8 below version 14 and other callers", wl8_versions);
	sp_run("WL9 returns", wl9_returns);
	sp_remove_dir();
	printf("WRAP-LOCAL-INIT summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}

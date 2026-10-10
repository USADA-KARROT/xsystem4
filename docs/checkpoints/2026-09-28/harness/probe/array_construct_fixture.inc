/* array-construct: Array.Alloc and Array.Realloc make struct elements the
 * way the original does, 2026-10-10.
 *
 * The original (read from the disassembly, not run):
 *   - Array.Alloc (HLL case 0, 0x644342 -> 0x647470 -> the array's vtable
 *     +0x4c, 0x67f4a0) empties the array first (+0x54, 0x67f5a0 -> 0x67ec50:
 *     the elements released from the last to the first) and makes it anew
 *     (0x67fe20 with 1): the buffer at its full size, then every element its
 *     default in index order (0x656970 with 1).
 *   - Array.Realloc (case 1, 0x644383 -> +0x50, 0x67f4d0) does nothing for
 *     the same length, makes the new tail's defaults after the array has
 *     grown (0x680170 with the index and 1), and releases the elements a
 *     shorter array loses, the last one first (0x680270), before shrinking.
 *   - The default of a struct element (jump table 0x656aa4: 13 -> 0x656a12)
 *     is 0x679b30(struct, 1): a new object with its no-argument constructor
 *     run, or with its members' defaults when the struct has none.
 * xsystem4 gave an unconstructed object with one reference too many, kept
 * the old elements over Alloc and released nothing in a shorter Realloc.
 *
 * AC1 Alloc: constructed elements, owned by the array alone; the array is
 *     emptied first; nothing is left behind.
 * AC2 Realloc: the same length, longer, shorter, and the order in which a
 *     shorter array's elements go.
 * AC3 a struct without a constructor gets its members' defaults.
 * AC4 a constructor that changes the array it is being made for (the probe
 *     steps in at the constructor's first instruction): EmplaceBack on the
 *     same array from inside EmplaceBack, Array.Free from inside Alloc, the
 *     array destroyed with another one put in its heap slot, and a
 *     destructor that frees the array Alloc is emptying.
 * AC5 the game's own bytecode: CASEffectKeyCancelChecker@Init (Alloc 14).
 * AC6 the game's own bytecode: MobViewCollection@0 (Alloc 7 of MobView,
 *     whose constructor makes a CG parts and starts its walk).
 * AC7 guards: version 13, an int array, a string array, the operand of a
 *     wrap or reference element, an array without a page and a generic page
 *     all keep what they did.
 *
 * Requires base_ui, base_ui_review, delegate_args, layer_root and
 * backlog_text fixtures. */

static int ac_failures;
#define AC_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ac_failures++; } } while (0)

/* The watched function: the struct page of each entry, and an action taken
 * once at the first instruction of entry number `skip`. */
static struct {
	int fn;
	size_t addr;
	int skip;
	void (*action)(void);
	int array;
	int seq[16], nseq;
} ac;

static void array_construct_probe_step(void)
{
	if (ac.fn <= 0 || call_stack_ptr <= 0)
		return;
	struct function_call *f = &call_stack[call_stack_ptr-1];
	if (f->fno != ac.fn || instr_ptr != ac.addr)
		return;
	if (ac.nseq < 16)
		ac.seq[ac.nseq++] = f->struct_page;
	if (!ac.action)
		return;
	if (ac.skip > 0) {
		ac.skip--;
		return;
	}
	void (*action)(void) = ac.action;
	ac.action = NULL;
	action();
}

static void ac_watch(int fn, int array, int skip, void (*action)(void))
{
	memset(&ac, 0, sizeof(ac));
	ac.fn = fn;
	ac.addr = fn > 0 ? ain->functions[fn].address : 0;
	ac.array = array;
	ac.skip = skip;
	ac.action = action;
}

static void ac_alloc(int array, int n, int arg3)
{
	bu_call("Array", "Alloc", 2, arg3, 2, (union vm_value[]){ BU_I(array), BU_I(n) });
}
static void ac_realloc(int array, int n, int arg3)
{
	bu_call("Array", "Realloc", 2, arg3, 2, (union vm_value[]){ BU_I(array), BU_I(n) });
}
static void ac_free(int array)
{
	bu_call("Array", "Free", 1, 2, 1, (union vm_value[]){ BU_I(array) });
}
static int ac_new_array(enum ain_data_type type, int st)
{
	union vm_value dim = { .i = 0 };
	return bt_array_slot(alloc_array(1, &dim, type, st, false));
}
static bool ac_same(int slot, uint32_t seq)
{
	return slot > 0 && heap_index_valid(slot) && heap[slot].seq == seq;
}
static int ac_elem(int array, int i)
{
	struct page *a = da_live_page(array) ? heap[array].page : NULL;
	return a && i >= 0 && i < a->nr_vars ? a->values[i].i : -1;
}
static int ac_numof(int array)
{
	return da_live_page(array) && heap[array].page ? heap[array].page->nr_vars : -1;
}
static int ac_member(int obj, int member)
{
	return da_live_page(obj) && member < heap[obj].page->nr_vars ? heap[obj].page->values[member].i : -12345;
}

/* --- AC1 --- */
static void ac_case_alloc(void)
{
	lr_setup();
	int st = bt_struct("CASFont"), ctor = ain->structures[st].constructor, size_m = br_member(st, "Size");
	int fonts = ac_new_array(AIN_ARRAY_STRUCT, st);
	/* once before counting, for whatever the first CASFont makes for good */
	ac_alloc(fonts, 1, 2);
	ac_alloc(fonts, 0, 2);
	size_t live = live_slots();
	unsigned long made = calls[ctor];
	ac_alloc(fonts, 3, 2);
	struct page *a = heap[fonts].page;
	AC_CHECK(a && a->nr_vars == 3 && a->a_type == AIN_ARRAY_STRUCT && a->array.struct_type == st,
		 "Alloc(3): %d element(s), page type %d of struct %d", a ? a->nr_vars : -1, a ? (int)a->a_type : -1, a ? a->array.struct_type : -1);
	AC_CHECK(calls[ctor] == made + 3, "Alloc(3) ran CASFont@0 %lu time(s), want 3 (0x656a12 -> 0x679b30 with 1)", calls[ctor] - made);
	for (int i = 0; i < 3; i++) {
		int e = ac_elem(fonts, i);
		/* CASFont@0 -> CASFont@2: Size 16 */
		AC_CHECK(da_live_page(e) && ac_member(e, size_m) == 16, "element %d (slot %d) has Size %d, want 16 (its constructor)", i, e, ac_member(e, size_m));
		AC_CHECK(da_live_page(e) && HEAP_REF(e) == 1, "element %d has %d reference(s), want 1 (the array's)", i, da_live_page(e) ? HEAP_REF(e) : -1);
		AC_CHECK(i == 0 || e != ac_elem(fonts, i - 1), "elements %d and %d are one object", i - 1, i);
	}
	/* Alloc again: the array is emptied first (0x67f4a0 -> +0x54), nothing
	 * is kept. */
	int e0 = ac_elem(fonts, 0), e1 = ac_elem(fonts, 1);
	if (!da_live_page(e0) || !da_live_page(e1))
		return;
	uint32_t s1 = heap[e1].seq;
	heap_ref(e0);
	heap[e0].page->values[size_m].i = 99;
	made = calls[ctor];
	ac_alloc(fonts, 2, 2);
	AC_CHECK(ac_numof(fonts) == 2 && ac_elem(fonts, 0) != e0 && ac_member(ac_elem(fonts, 0), size_m) == 16,
		 "Alloc(2) over 3 elements: %d element(s), [0] is %s with Size %d; want a new object with 16", ac_numof(fonts),
		 ac_elem(fonts, 0) == e0 ? "the old object" : "another object", ac_member(ac_elem(fonts, 0), size_m));
	AC_CHECK(calls[ctor] == made + 2, "Alloc(2) over 3 elements ran CASFont@0 %lu time(s), want 2", calls[ctor] - made);
	AC_CHECK(HEAP_REF(e0) == 1, "the old element 0 has %d reference(s) besides none: want the probe's only", HEAP_REF(e0));
	AC_CHECK(!ac_same(e1, s1), "the old element 1 was not released");
	heap_unref(e0);
	ac_alloc(fonts, 0, 2);
	a = heap[fonts].page;
	AC_CHECK(a && a->nr_vars == 0 && a->a_type == AIN_ARRAY_STRUCT && a->array.struct_type == st,
		 "Alloc(0): %d element(s), page type %d of struct %d", a ? a->nr_vars : -1, a ? (int)a->a_type : -1, a ? a->array.struct_type : -1);
	AC_CHECK(live_slots() == live, "after Alloc(0) %zu slot(s) are live, %zu before the elements were made", live_slots(), live);
	printf("AC1 Alloc constructs, empties first, owns alone: %s\n", ac_failures ? "FAIL" : "ok");
}

/* --- AC2 --- */
static void ac_case_realloc(void)
{
	lr_setup();
	int st = bt_struct("CASFont"), ctor = ain->structures[st].constructor, size_m = br_member(st, "Size");
	int fonts = ac_new_array(AIN_ARRAY_STRUCT, st);
	ac_alloc(fonts, 1, 2);
	ac_alloc(fonts, 0, 2);
	size_t live = live_slots();
	/* an array without elements: Realloc is Alloc (0x67f4d3) */
	unsigned long made = calls[ctor];
	ac_realloc(fonts, 2, 2);
	int a0 = ac_elem(fonts, 0), a1 = ac_elem(fonts, 1);
	AC_CHECK(ac_numof(fonts) == 2 && calls[ctor] == made + 2 && ac_member(a0, size_m) == 16 && ac_member(a1, size_m) == 16,
		 "Realloc(2) of an empty array: %d element(s), CASFont@0 ran %lu time(s), Sizes %d and %d", ac_numof(fonts), calls[ctor] - made,
		 ac_member(a0, size_m), ac_member(a1, size_m));
	if (!da_live_page(a0) || !da_live_page(a1))
		return;
	heap[a0].page->values[size_m].i = 77;
	/* longer: the leading elements stay, the tail is constructed */
	made = calls[ctor];
	ac_realloc(fonts, 4, 2);
	AC_CHECK(ac_numof(fonts) == 4 && ac_elem(fonts, 0) == a0 && ac_elem(fonts, 1) == a1 && ac_member(a0, size_m) == 77,
		 "Realloc(4): %d element(s), [0] %d (was %d, Size %d, want 77), [1] %d (was %d)", ac_numof(fonts), ac_elem(fonts, 0), a0,
		 ac_member(a0, size_m), ac_elem(fonts, 1), a1);
	AC_CHECK(calls[ctor] == made + 2, "Realloc(4) of 2 ran CASFont@0 %lu time(s), want 2 (0x67f530)", calls[ctor] - made);
	for (int i = 2; i < 4; i++) {
		int e = ac_elem(fonts, i);
		AC_CHECK(da_live_page(e) && ac_member(e, size_m) == 16 && HEAP_REF(e) == 1,
			 "new element %d (slot %d): Size %d, %d reference(s); want 16 and 1", i, e, ac_member(e, size_m), da_live_page(e) ? HEAP_REF(e) : -1);
	}
	/* the same length: nothing (0x67f50b) */
	made = calls[ctor];
	int t2 = ac_elem(fonts, 2), t3 = ac_elem(fonts, 3);
	ac_realloc(fonts, 4, 2);
	AC_CHECK(ac_numof(fonts) == 4 && calls[ctor] == made && ac_elem(fonts, 2) == t2 && ac_elem(fonts, 3) == t3,
		 "Realloc(4) of 4: %d element(s), CASFont@0 ran %lu time(s)", ac_numof(fonts), calls[ctor] - made);
	/* shorter: the removed elements are released (0x67f554 -> 0x680270) */
	if (!da_live_page(t2) || !da_live_page(t3))
		return;
	uint32_t s1 = heap[a1].seq, s2 = heap[t2].seq;
	heap_ref(t3);
	ac_realloc(fonts, 1, 2);
	AC_CHECK(ac_numof(fonts) == 1 && ac_elem(fonts, 0) == a0 && ac_member(a0, size_m) == 77, "Realloc(1): %d element(s), [0] %d (was %d)",
		 ac_numof(fonts), ac_elem(fonts, 0), a0);
	AC_CHECK(HEAP_REF(t3) == 1, "the removed element 3 has %d reference(s), want the probe's only", HEAP_REF(t3));
	AC_CHECK(!ac_same(a1, s1) && !ac_same(t2, s2), "the removed elements 1 and 2 were not released (%d, %d)", ac_same(a1, s1), ac_same(t2, s2));
	heap_unref(t3);
	ac_realloc(fonts, 0, 2);
	AC_CHECK(ac_numof(fonts) == 0 && live_slots() == live, "Realloc(0): %d element(s), %zu slot(s) live, %zu before", ac_numof(fonts),
		 live_slots(), live);

	/* The order: a struct with a destructor. Its destructor's struct pages
	 * are those of the elements from the last to the first, for Realloc
	 * (0x67f554 counts down) and for Alloc (0x67ec50). */
	int rst = bt_struct("textfile::detail::CTextReader"), dtor = ain->structures[rst].destructor;
	AC_CHECK(dtor > 0, "CTextReader has no destructor (%d): this case wants a struct with one", dtor);
	if (dtor <= 0)
		return;
	int readers = ac_new_array(AIN_ARRAY_STRUCT, rst);
	for (int round = 0; round < 2; round++) {
		ac_alloc(readers, 3, 2);
		int e[3] = { ac_elem(readers, 0), ac_elem(readers, 1), ac_elem(readers, 2) };
		ac_watch(dtor, readers, 0, NULL);
		if (round == 0)
			ac_realloc(readers, 1, 2);
		else
			ac_alloc(readers, 0, 2);
		int want = round == 0 ? 2 : 3;
		AC_CHECK(ac.nseq == want && ac.seq[0] == e[2] && ac.seq[1] == e[1] && (want == 2 || ac.seq[2] == e[0]),
			 "%s of [%d, %d, %d] ran %d destructor(s), on %d, %d, %d; want %d, the last element first",
			 round == 0 ? "Realloc(1)" : "Alloc(0)", e[0], e[1], e[2], ac.nseq, ac.nseq > 0 ? ac.seq[0] : -1, ac.nseq > 1 ? ac.seq[1] : -1,
			 ac.nseq > 2 ? ac.seq[2] : -1, want);
		ac_watch(0, 0, 0, NULL);
		ac_alloc(readers, 0, 2);
	}
	printf("AC2 Realloc keeps, constructs the tail, releases from the back: %s\n", ac_failures ? "FAIL" : "ok");
}

/* --- AC3 --- */
static void ac_case_plain(void)
{
	lr_setup();
	/* A struct without a constructor: 0x679b30 gives its members their
	 * defaults (0x679c2c). */
	int timer_st = bt_struct("CASTimerImp");
	AC_CHECK(ain->structures[timer_st].constructor < 0, "CASTimerImp has a constructor: this case wants a struct without one");
	int timers = ac_new_array(AIN_ARRAY_STRUCT, timer_st);
	ac_alloc(timers, 2, 2);
	for (int i = 0; i < 2; i++) {
		int t = ac_elem(timers, i);
		bool ok = da_live_page(t) && heap[t].page->type == STRUCT_PAGE && heap[t].page->index == timer_st
			&& heap[t].page->nr_vars == ain->structures[timer_st].nr_members && HEAP_REF(t) == 1;
		AC_CHECK(ok, "Alloc(2) of CASTimerImp: element %d is slot %d with %d reference(s)", i, t, da_live_page(t) ? HEAP_REF(t) : -1);
		for (int m = 0; ok && m < heap[t].page->nr_vars; m++)
			AC_CHECK(heap[t].page->values[m].i == 0, "element %d member %d is %d, want 0", i, m, heap[t].page->values[m].i);
	}
	/* the first struct of the AIN with a string member and neither a
	 * constructor nor an interface: the string is an empty one */
	int str_st = -1, str_member = -1;
	for (int i = 0; i < ain->nr_structures && str_st < 0; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->constructor >= 0 || s->nr_interfaces || s->is_interface)
			continue;
		for (int m = 0; m < s->nr_members && str_st < 0; m++) {
			if (s->members[m].type.data == AIN_STRING) {
				str_st = i;
				str_member = m;
			}
		}
	}
	AC_CHECK(str_st >= 0, "the AIN has no struct with a string member and no constructor");
	if (str_st >= 0) {
		int strs = ac_new_array(AIN_ARRAY_STRUCT, str_st);
		ac_realloc(strs, 2, 2);
		for (int i = 0; i < 2; i++) {
			int text = ac_member(ac_elem(strs, i), str_member);
			AC_CHECK(text > 0 && heap_index_valid(text) && heap[text].type == VM_STRING && !strcmp(bu_str(text), ""),
				 "Realloc(2) of struct %d: element %d's string member %d is slot %d ('%s'), want an empty string", str_st, i,
				 str_member, text, bu_str(text));
		}
	}
	printf("AC3 a struct without a constructor gets its defaults: %s\n", ac_failures ? "FAIL" : "ok");
}

/* --- AC4 --- */
static int ac_inner = -1;
static void ac_act_emplace(void)
{
	ac_inner = bt_emplace_back(ac.array, 2);
	if (da_live_page(ac_inner))
		heap_unref(ac_inner); /* the wrap the call returned */
}
static void ac_act_free(void)
{
	ac_free(ac.array);
}

/* The probe gives its array up, and makes arrays until one lands in the heap
 * slot the first one had: another array of the same struct, with two
 * elements. */
static int ac_other = -1, ac_other_elem[2];
static void ac_act_replace(void)
{
	int target = ac.array, st = heap[target].page->array.struct_type;
	heap_unref(target);
	int extra[64], n = 0;
	ac_other = -1;
	while (n < 64) {
		int s = ac_new_array(AIN_ARRAY_STRUCT, st);
		if (s == target) {
			ac_other = s;
			break;
		}
		extra[n++] = s;
	}
	for (int i = 0; i < n; i++)
		heap_unref(extra[i]);
	if (ac_other > 0) {
		ac_alloc(ac_other, 2, 2);
		ac_other_elem[0] = ac_elem(ac_other, 0);
		ac_other_elem[1] = ac_elem(ac_other, 1);
	}
}

static void ac_case_reenter(void)
{
	lr_setup();
	int st = bt_struct("CASFont"), ctor = ain->structures[st].constructor, size_m = br_member(st, "Size");
	int fonts = ac_new_array(AIN_ARRAY_STRUCT, st);
	ac_alloc(fonts, 1, 2);
	ac_alloc(fonts, 0, 2);
	size_t live = live_slots();
	/* EmplaceBack whose element's constructor emplaces into the same
	 * array. Natively the array is one longer before the constructor runs
	 * (0x67f50f), so the inner element comes after the outer one. Before,
	 * the outer call put its own page back over the inner one's and freed
	 * a page twice. */
	ac_watch(ctor, fonts, 0, ac_act_emplace);
	int outer = bt_emplace_back(fonts, 2);
	ac_watch(0, 0, 0, NULL);
	AC_CHECK(ac_numof(fonts) == 2 && ac_elem(fonts, 0) == outer && ac_elem(fonts, 1) == ac_inner && outer != ac_inner,
		 "EmplaceBack inside EmplaceBack: %d element(s) [%d, %d]; the outer call returned %d, the inner one %d", ac_numof(fonts),
		 ac_elem(fonts, 0), ac_elem(fonts, 1), outer, ac_inner);
	AC_CHECK(da_live_page(outer) && HEAP_REF(outer) == 2 && ac_member(outer, size_m) == 16,
		 "the outer element: %d reference(s), Size %d; want 2 (the array's and the result's) and 16", da_live_page(outer) ? HEAP_REF(outer) : -1,
		 ac_member(outer, size_m));
	AC_CHECK(da_live_page(ac_inner) && HEAP_REF(ac_inner) == 1 && ac_member(ac_inner, size_m) == 16,
		 "the inner element: %d reference(s), Size %d; want 1 and 16", da_live_page(ac_inner) ? HEAP_REF(ac_inner) : -1,
		 ac_member(ac_inner, size_m));
	if (da_live_page(outer))
		heap_unref(outer);
	ac_alloc(fonts, 0, 2);
	AC_CHECK(live_slots() == live, "after the nested EmplaceBack %zu slot(s) are live, %zu before", live_slots(), live);

	/* Alloc(3) whose second element's constructor frees the array: the
	 * first element goes with the array, the second has no place and is
	 * released, the third is not made (natively its index check fails,
	 * 0x680185). */
	unsigned long made = calls[ctor];
	ac_watch(ctor, fonts, 1, ac_act_free);
	ac_alloc(fonts, 3, 2);
	ac_watch(0, 0, 0, NULL);
	AC_CHECK(ac_numof(fonts) == 0 && calls[ctor] == made + 2, "Array.Free inside Alloc(3): %d element(s), CASFont@0 ran %lu time(s); want 0 and 2",
		 ac_numof(fonts), calls[ctor] - made);
	AC_CHECK(live_slots() == live, "after the Free inside Alloc %zu slot(s) are live, %zu before", live_slots(), live);
	/* and the array still works */
	ac_alloc(fonts, 2, 2);
	AC_CHECK(ac_numof(fonts) == 2 && ac_member(ac_elem(fonts, 1), size_m) == 16, "Alloc(2) afterwards: %d element(s)", ac_numof(fonts));

	/* Alloc(3) whose second element's constructor destroys the array, and
	 * the heap slot is another array by the time it returns: that array is
	 * not the one the call was made on (the slot's allocation sequence
	 * says so) and stays as it is. */
	int doomed = ac_new_array(AIN_ARRAY_STRUCT, st);
	made = calls[ctor];
	ac_watch(ctor, doomed, 1, ac_act_replace);
	ac_alloc(doomed, 3, 2);
	ac_watch(0, 0, 0, NULL);
	AC_CHECK(ac_other == doomed, "the probe could not get the freed array's heap slot again (%d, was %d)", ac_other, doomed);
	if (ac_other == doomed) {
		int o0 = ac_elem(ac_other, 0), o1 = ac_elem(ac_other, 1);
		/* outer: two constructors (the third is not made); inner: two */
		AC_CHECK(ac_numof(ac_other) == 2 && calls[ctor] == made + 4, "the array now in the slot has %d element(s), CASFont@0 ran %lu time(s); want 2 and 4",
			 ac_numof(ac_other), calls[ctor] - made);
		AC_CHECK(o0 == ac_other_elem[0] && o1 == ac_other_elem[1], "its elements are %d and %d, were %d and %d when it was made", o0, o1,
			 ac_other_elem[0], ac_other_elem[1]);
		AC_CHECK(da_live_page(o0) && da_live_page(o1) && o0 != o1 && HEAP_REF(o0) == 1 && HEAP_REF(o1) == 1
			 && ac_member(o0, size_m) == 16 && ac_member(o1, size_m) == 16,
			 "its elements are %d (%d reference(s)) and %d (%d): want two objects of its own, one reference each", o0,
			 da_live_page(o0) ? HEAP_REF(o0) : -1, o1, da_live_page(o1) ? HEAP_REF(o1) : -1);
		heap_unref(ac_other);
	}
	ac_alloc(fonts, 0, 2);
	AC_CHECK(live_slots() == live, "after the array was replaced under Alloc %zu slot(s) are live, %zu before", live_slots(), live);

	/* Alloc over elements whose destructor frees the array. Natively the
	 * array is emptied before it is made anew (0x67f4a0), so the new
	 * elements are made all the same. Here the new page is in place while
	 * the old elements go: the destructor empties that page, and no
	 * element is constructed for an array that has no place for it. The
	 * outcome is xsystem4's own; what is kept is that nothing is made in
	 * vain and nothing is left. */
	int rst = bt_struct("textfile::detail::CTextReader");
	int rctor = ain->structures[rst].constructor, rdtor = ain->structures[rst].destructor;
	int readers = ac_new_array(AIN_ARRAY_STRUCT, rst);
	ac_alloc(readers, 1, 2);
	ac_alloc(readers, 0, 2);
	size_t live_r = live_slots();
	ac_alloc(readers, 2, 2);
	made = calls[rctor];
	ac_watch(rdtor, readers, 0, ac_act_free);
	ac_alloc(readers, 2, 2);
	int destructed = ac.nseq;
	ac_watch(0, 0, 0, NULL);
	AC_CHECK(destructed == 2 && calls[rctor] == made && ac_numof(readers) == 0,
		 "Array.Free in a destructor under Alloc(2): %d destructor(s), CTextReader@0 ran %lu time(s), %d element(s); want 2, 0 and 0",
		 destructed, calls[rctor] - made, ac_numof(readers));
	AC_CHECK(live_slots() == live_r, "after it %zu slot(s) are live, %zu before", live_slots(), live_r);
	printf("AC4 a constructor that changes its own array: %s\n", ac_failures ? "FAIL" : "ok");
}

/* --- AC5 --- */
static void ac_case_key_cancel(void)
{
	lr_setup();
	int cst = bt_struct("input::detail::CASEffectKeyCancelChecker"), kst = bt_struct("input::detail::CASEffectKeyCancelKey");
	int kctor = ain->structures[kst].constructor, key_m = br_member(kst, "m_nKey");
	AC_CHECK(kctor > 0, "CASEffectKeyCancelKey has no constructor (%d)", kctor);
	int obj = vm_construct_struct(cst);
	unsigned long made = calls[kctor];
	/* m_keyList.Alloc(14), then m_keyList[i].Set(key, flag) for each */
	int inited = lr_method("input::detail::CASEffectKeyCancelChecker@Init", obj, 1, (int[]){ 1 });
	AC_CHECK(inited == 1, "CASEffectKeyCancelChecker@Init returned %d, want true", inited);
	int list = ac_member(obj, br_member(cst, "m_keyList"));
	AC_CHECK(ac_numof(list) == 14, "m_keyList has %d element(s), want 14", ac_numof(list));
	AC_CHECK(calls[kctor] == made + 14, "CASEffectKeyCancelChecker@Init ran CASEffectKeyCancelKey@0 %lu time(s), want 14", calls[kctor] - made);
	static const int keys[14] = { 1, 2, 4, 13, 27, 32, 65, 83, 90, 33, 8, 36, 38, 40 };
	for (int i = 0; i < 14 && i < ac_numof(list); i++) {
		int e = ac_elem(list, i);
		AC_CHECK(ac_member(e, key_m) == keys[i], "m_keyList[%d].m_nKey is %d, want %d", i, ac_member(e, key_m), keys[i]);
		AC_CHECK(da_live_page(e) && HEAP_REF(e) == 1, "m_keyList[%d] has %d reference(s), want 1", i, da_live_page(e) ? HEAP_REF(e) : -1);
	}
	printf("AC5 CASEffectKeyCancelChecker@Init: %s\n", ac_failures ? "FAIL" : "ok");
}

/* --- AC6 --- */
static void ac_case_mob(void)
{
	lr_setup();
	int cst = bt_struct("MobViewCollection"), vst = bt_struct("MobView");
	int vctor = ain->structures[vst].constructor;
	int reset = ci_fn("MobView@Reset"), run = ci_fn("MobView@Run");
	unsigned long made = calls[vctor], resets = calls[reset], runs = calls[run];
	/* MobViewCollection@0: a rect parts, m_views.Alloc(7), then Parent and Z
	 * of each view. MobView@0: AFL_Parts_CreateCG, Reset (the walk height,
	 * the direction, one of the passer-by CGs), Run (a motion across the
	 * screen whose end calls Reset and Run again). */
	int obj = vm_construct_struct(cst);
	int views = ac_member(obj, br_member(cst, "m_views"));
	AC_CHECK(ac_numof(views) == 7, "m_views has %d element(s), want 7", ac_numof(views));
	AC_CHECK(calls[vctor] == made + 7 && calls[reset] == resets + 7 && calls[run] == runs + 7,
		 "MobViewCollection@0 ran MobView@0 %lu, Reset %lu and Run %lu time(s); want 7 each", calls[vctor] - made, calls[reset] - resets,
		 calls[run] - runs);
	int cg_m = br_member(vst, "m_partsCg"), timer_m = br_member(vst, "m_timer"), height_m = br_member(vst, "m_walkHeight");
	for (int i = 0; i < 7 && i < ac_numof(views); i++) {
		int v = ac_elem(views, i);
		AC_CHECK(da_live_page(v) && heap[v].page->type == STRUCT_PAGE && heap[v].page->index == vst, "m_views[%d] is no MobView (slot %d)", i, v);
		AC_CHECK(da_live_page(ac_member(v, cg_m)), "m_views[%d] has no CG parts object (m_partsCg %d): its constructor did not run", i,
			 ac_member(v, cg_m));
		AC_CHECK(da_live_page(ac_member(v, timer_m)), "m_views[%d] has no m_timer (%d)", i, ac_member(v, timer_m));
		/* MobView@Reset: m_walkHeight = 5 + RAND(10) */
		int h = ac_member(v, height_m);
		AC_CHECK(h >= 5 && h <= 15, "m_views[%d].m_walkHeight is %d, want 5 .. 15", i, h);
	}
	printf("AC6 MobViewCollection@0 makes its seven MobView: %s\n", ac_failures ? "FAIL" : "ok");
}

/* --- AC7 --- */
static void ac_case_guards(void)
{
	lr_setup();
	int st = bt_struct("CASFont"), ctor = ain->structures[st].constructor, size_m = br_member(st, "Size");
	/* version 13: no constructor, the old elements stay (two tests keep
	 * this, array_value_struct's of the version and vm_construct_struct's
	 * own; the check of the kept element tells the first) */
	int fonts = ac_new_array(AIN_ARRAY_STRUCT, st);
	ac_alloc(fonts, 1, 2);
	int e0 = ac_elem(fonts, 0);
	unsigned long made = calls[ctor];
	int version = ain->version;
	ain->version = 13;
	ac_alloc(fonts, 2, 2);
	ac_realloc(fonts, 3, 2);
	ain->version = version;
	AC_CHECK(ac_numof(fonts) == 3 && ac_elem(fonts, 0) == e0 && calls[ctor] == made && ac_member(ac_elem(fonts, 1), size_m) == 0
		 && ac_member(ac_elem(fonts, 2), size_m) == 0,
		 "v13 Alloc(2), Realloc(3): %d element(s), [0] %d (was %d), CASFont@0 ran %lu time(s), Sizes %d and %d; want the old element, 0 and 0",
		 ac_numof(fonts), ac_elem(fonts, 0), e0, calls[ctor] - made, ac_member(ac_elem(fonts, 1), size_m), ac_member(ac_elem(fonts, 2), size_m));
	/* an int array keeps its values over Alloc and Realloc, as before */
	int ints = ac_new_array(AIN_ARRAY_INT, -1);
	ac_alloc(ints, 3, 1);
	for (int i = 0; i < 3 && i < ac_numof(ints); i++)
		heap[ints].page->values[i].i = 5 + i;
	ac_realloc(ints, 5, 1);
	ac_alloc(ints, 6, 1);
	AC_CHECK(ac_numof(ints) == 6 && ac_elem(ints, 0) == 5 && ac_elem(ints, 2) == 7 && ac_elem(ints, 3) == 0 && ac_elem(ints, 5) == 0
		 && heap[ints].page->a_type == AIN_ARRAY_INT,
		 "an int array after Realloc(5), Alloc(6): %d element(s) [%d, .., %d, %d, .., %d]; want 5, 7, 0, 0", ac_numof(ints), ac_elem(ints, 0),
		 ac_elem(ints, 2), ac_elem(ints, 3), ac_elem(ints, 5));
	/* a string array has operand 2 as well: no struct is made of it */
	int strs = ac_new_array(AIN_ARRAY_STRING, -1);
	made = calls[ctor];
	ac_alloc(strs, 2, 2);
	ac_realloc(strs, 3, 2);
	AC_CHECK(ac_numof(strs) == 3 && heap[strs].page->a_type == AIN_ARRAY_STRING && heap[strs].page->array.struct_type == -1,
		 "a string array: %d element(s), page type %d", ac_numof(strs), (int)heap[strs].page->a_type);
	for (int i = 0; i < ac_numof(strs); i++)
		AC_CHECK(ac_elem(strs, i) <= 0 || heap[ac_elem(strs, i)].type != VM_PAGE, "a string array: element %d is a page (slot %d)", i, ac_elem(strs, i));
	/* the operand of a wrap or reference element (0x10002): natively such
	 * an element starts as -1 (0x6569b8); it keeps the unconstructed object
	 * it was given here */
	int refs = ac_new_array(AIN_ARRAY_STRUCT, st);
	made = calls[ctor];
	ac_alloc(refs, 2, 0x10002);
	ac_realloc(refs, 3, 0x10002);
	AC_CHECK(ac_numof(refs) == 3 && calls[ctor] == made, "operand 0x10002: %d element(s), CASFont@0 ran %lu time(s); want 3 and 0", ac_numof(refs),
		 calls[ctor] - made);
	for (int i = 0; i < ac_numof(refs); i++)
		AC_CHECK(da_live_page(ac_elem(refs, i)) && ac_member(ac_elem(refs, i), size_m) == 0,
			 "operand 0x10002: element %d is slot %d with Size %d; want the unconstructed object it was before", i, ac_elem(refs, i),
			 ac_member(ac_elem(refs, i), size_m));
	/* no page yet: nothing says what the elements are */
	int none = heap_alloc_slot(VM_PAGE);
	heap[none].page = NULL;
	made = calls[ctor];
	ac_alloc(none, 2, 2);
	AC_CHECK(ac_numof(none) == 2 && ac_elem(none, 0) == 0 && ac_elem(none, 1) == 0 && calls[ctor] == made,
		 "Alloc(2) on no array: %d element(s) [%d, %d]; want two 0", ac_numof(none), ac_elem(none, 0), ac_elem(none, 1));
	/* a generic page keeps its element stride where a struct page keeps
	 * its struct: it is not taken for an array of that struct */
	struct page *g = alloc_page(ARRAY_PAGE, AIN_ARRAY, 0);
	g->array.rank = 1;
	g->array.struct_type = st;
	int gen = bt_array_slot(g);
	made = calls[ctor];
	ac_alloc(gen, 2, 2);
	AC_CHECK(ac_numof(gen) == 2 && calls[ctor] == made, "a generic page: %d element(s), CASFont@0 ran %lu time(s); want 2 and 0", ac_numof(gen),
		 calls[ctor] - made);
	/* (the older code takes the stride for a struct index and allocates
	 * that struct: kept as it was) */
	for (int i = 0; i < ac_numof(gen); i++)
		AC_CHECK(da_live_page(ac_elem(gen, i)) && ac_member(ac_elem(gen, i), size_m) == 0,
			 "a generic page: element %d is slot %d with Size %d; want what it was before, an unconstructed object", i,
			 ac_elem(gen, i), ac_member(ac_elem(gen, i), size_m));
	printf("AC7 guards: %s\n", ac_failures ? "FAIL" : "ok");
}

static int ac_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		ac_failures = 0;
		alarm(120);
		fn();
		fflush(stdout);
		_exit(ac_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_array_construct(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "AC1", ac_case_alloc }, { "AC2", ac_case_realloc }, { "AC3", ac_case_plain }, { "AC4", ac_case_reenter },
		{ "AC5", ac_case_key_cancel }, { "AC6", ac_case_mob }, { "AC7", ac_case_guards },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += ac_fork(cases[i].name, cases[i].fn);
	printf("ARRAY-CONSTRUCT %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}

/* v14 primitive Array.Alloc: native 0x67f4a0 clears before 0x67fe20
 * initializes; 0x6569a9 writes zero for int/float/bool, while release
 * 0x656c50 never interprets their bits as an object handle. Uses AC helpers.
 * SetPosList below is real game bytecode; its arguments are synthetic. */
static void ap_scalar(enum ain_data_type type)
{
	int array = ac_new_array(type, -1);
	heap[array].page->a_type = type; // exercise ref-container metadata too
	heap[array].page->array.struct_type = 37; // ignored for scalars, but preserved
	heap[array].page->array.elem_slots = 1;
	int witness = heap_alloc_slot(VM_PAGE);
	heap[witness].page = NULL;
	uint32_t seq = heap_get_seq(witness);
	heap_ref(witness); // observe one erroneous unref without crashing cleanup
	ac_alloc(array, 4, 1);
	for (int round = 0; round < 4; round++) {
		struct page *old = heap[array].page;
		for (int i = 0; i < old->nr_vars; i++)
			old->values[i].i = witness; // float bits are a positive subnormal
		static const int sizes[] = { 2, 2, 5, 0 };
		int n = sizes[round];
		ac_alloc(array, n, 1);
		struct page *a = heap[array].page;
		AC_CHECK(ac_same(witness, seq) && HEAP_REF(witness) == 2,
			"type %d round %d interpreted scalar bits as a heap reference", type, round);
		AC_CHECK(a && a->nr_vars == n && a->a_type == type && a->array.rank == 1
			&& a->array.struct_type == 37 && a->array.elem_slots == 1,
			"type %d round %d changed layout", type, round);
		for (int i = 0; a && i < a->nr_vars; i++)
			AC_CHECK(a->values[i].i == 0, "type %d round %d index %d kept bits %d", type, round, i, a->values[i].i);
	}
	if (!ac_failures) {
		heap_unref(array);
		heap_unref(witness);
		heap_unref(witness);
		AC_CHECK(!ac_same(witness, seq), "witness survived its two actual owners");
	}
}
static void ap_int(void) { lr_setup(); ap_scalar(AIN_ARRAY_INT); }
static void ap_float(void) { lr_setup(); ap_scalar(AIN_ARRAY_FLOAT); }
static void ap_bool(void) { lr_setup(); ap_scalar(AIN_ARRAY_BOOL); }
static void ap_refs(void)
{
	lr_setup();
	ap_scalar(AIN_REF_ARRAY_INT);
	ap_scalar(AIN_REF_ARRAY_FLOAT);
	ap_scalar(AIN_REF_ARRAY_BOOL);
}
static void ap_realloc(void)
{
	lr_setup();
	static const enum ain_data_type types[] = { AIN_ARRAY_INT, AIN_ARRAY_FLOAT, AIN_ARRAY_BOOL };
	for (int t = 0; t < 3; t++) {
		int a = ac_new_array(types[t], -1), w = heap_alloc_slot(VM_PAGE);
		heap[w].page = NULL;
		uint32_t seq = heap_get_seq(w);
		ac_alloc(a, 3, 1);
		for (int i = 0; i < 3; i++) heap[a].page->values[i].i = w;
		ac_realloc(a, 1, 1);
		AC_CHECK(ac_numof(a) == 1 && ac_elem(a, 0) == w && ac_same(w, seq) && HEAP_REF(w) == 1,
			"Realloc shrink type %d changed the kept value or released bits", types[t]);
		ac_realloc(a, 4, 1);
		AC_CHECK(ac_numof(a) == 4 && ac_elem(a, 0) == w && ac_elem(a, 1) == 0 && ac_elem(a, 3) == 0,
			"Realloc growth type %d did not preserve prefix and zero tail", types[t]);
		ac_alloc(a, -1, 1);
		AC_CHECK(ac_numof(a) == 4 && ac_elem(a, 0) == w, "negative Alloc no longer keeps existing behavior");
		if (!ac_failures) { heap_unref(a); heap_unref(w); }
	}
}
static void ap_guards(void)
{
	lr_setup();
	for (int k = 0; k < 6; k++) {
		struct page *p = alloc_page(ARRAY_PAGE, k == 2 ? AIN_ARRAY : AIN_ARRAY_INT, 1);
		p->array.rank = k == 3 ? 2 : k == 4 ? 0 : 1;
		p->array.struct_type = -1;
		p->array.elem_slots = k == 5 ? 2 : 0;
		p->values[0].i = -7; // no references; never tear down malformed ranks
		int a = bt_array_slot(p), version = ain->version;
		if (k == 0) ain->version = 13;
		ac_alloc(a, 2, k == 1 ? 0 : 1);
		ain->version = version;
		AC_CHECK(ac_numof(a) == 2 && ac_elem(a, 0) == -7 && ac_elem(a, 1) == 0,
			"guard %d changed legacy prefix", k);
	}
	int a = heap_alloc_slot(VM_PAGE);
	heap[a].page = NULL;
	ac_alloc(a, 2, 1);
	AC_CHECK(ac_numof(a) == 2 && ac_elem(a, 0) == 0 && ac_elem(a, 1) == 0, "no-page fallback changed");
}
static void ap_positions(void)
{
	lr_setup();
	int process = alloc_struct(bt_struct("CASConstructionProcess"));
	int pos_st = bt_struct("CASPos"), list = ac_new_array(AIN_ARRAY_STRUCT, pos_st);
	ac_alloc(list, 3, 2);
	int witness = heap_alloc_slot(VM_PAGE);
	heap[witness].page = NULL;
	uint32_t seq = heap_get_seq(witness);
	heap_ref(witness);
	for (int i = 0; i < 3; i++) {
		struct page *p = heap[ac_elem(list, i)].page;
		p->values[0].i = 21 + i;
		p->values[1].i = i == 2 ? witness : -20 - i;
	}
	lr_method("CASConstructionProcess@SetPosList", process, 1, (int[]){ list });
	int out = ac_member(process, br_member(bt_struct("CASConstructionProcess"), "ArrayPos"));
	AC_CHECK(ac_numof(out) == 6 && ac_elem(out, 5) == witness, "real SetPosList did not store three points");
	ac_realloc(list, 1, 2);
	lr_method("CASConstructionProcess@SetPosList", process, 1, (int[]){ list });
	AC_CHECK(ac_numof(out) == 2 && ac_elem(out, 0) == 21 && ac_elem(out, 1) == -20, "real SetPosList did not store the shorter list");
	AC_CHECK(ac_same(witness, seq) && HEAP_REF(witness) == 2, "real SetPosList released the old coordinate as an object");
	printf("AP7 real SetPosList: points 3 -> 1, witness refs=%d\n", ac_same(witness, seq) ? HEAP_REF(witness) : 0);
}
/* GameContext@0 is real game bytecode: its initializer GameContext@2 makes
 * m_menuState, an array<MenuState> (an enum), with X_A_INIT, and the
 * constructor then calls Array.Alloc, operand 1, with MenuType::Numof().
 * X_A_INIT gives an array of enums the concrete int page (as of ints and
 * bools), so this Alloc takes the primitive path too. Natively an enum
 * element is the same scalar: the release table 0x656c88 sends type 92,
 * like 10, 11 and 47, to the no-op 0x656c50, and the default is 0
 * (0x6569a9). The Allocs after the first are synthetic calls of the same
 * shape: the game never calls Alloc on this array again.
 * An object the initializer did not run on keeps a generic page for this
 * member (alloc_struct; printed, not asserted), which Alloc leaves to the
 * older path. */
static void ap_enum(void)
{
	lr_setup();
	int st = bt_struct("GameContext"), member = br_member(st, "m_menuState");
	int gc = alloc_struct(st), raw = ac_member(gc, member);
	printf("AP8 note: before GameContext@2 m_menuState is a page of a_type %d\n",
		da_live_page(raw) && heap[raw].page ? (int)heap[raw].page->a_type : -1);
	lr_method("GameContext@0", gc, 0, NULL);
	int menu = ac_member(gc, member);
	struct page *a = da_live_page(menu) ? heap[menu].page : NULL;
	int n = a ? a->nr_vars : -1;
	/* MenuType::Numof() itself, the script function the constructor calls */
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	function_call(nm_fn("MenuType::Numof"), VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	int numof = stack_ptr == sp + 1 && call_stack_ptr == csp ? stack[sp].i : -1;
	stack_ptr = sp;
	AC_CHECK(numof > 2, "MenuType::Numof() returned %d", numof);
	AC_CHECK(a && a->type == ARRAY_PAGE && a->a_type == AIN_ARRAY_INT && a->array.rank == 1 && n == numof,
		"GameContext@0 left m_menuState as a_type %d, rank %d, %d elements; want an int page of MenuType::Numof() = %d",
		a ? (int)a->a_type : -1, a ? a->array.rank : -1, n, numof);
	for (int i = 0; a && i < n; i++)
		AC_CHECK(a->values[i].i == 0, "GameContext@0: MenuState %d starts as %d, want 0", i, a->values[i].i);
	if (ac_failures)
		return;
	int witness = heap_alloc_slot(VM_PAGE);
	heap[witness].page = NULL;
	uint32_t seq = heap_get_seq(witness);
	heap_ref(witness); // observe one erroneous unref without crashing cleanup
	const int sizes[] = { n - 1, n - 1, n + 2, 0 };
	for (int round = 0; round < 4; round++) {
		struct page *old = heap[menu].page;
		for (int i = 0; i < old->nr_vars; i++)
			old->values[i].i = witness; // an enum value that is a live heap slot's number
		ac_alloc(menu, sizes[round], 1);
		a = heap[menu].page;
		AC_CHECK(ac_same(witness, seq) && HEAP_REF(witness) == 2,
			"enum round %d interpreted enum bits as a heap reference", round);
		AC_CHECK(a && a->nr_vars == sizes[round] && a->a_type == AIN_ARRAY_INT && a->array.rank == 1,
			"enum round %d changed layout", round);
		for (int i = 0; a && i < a->nr_vars; i++)
			AC_CHECK(a->values[i].i == 0, "enum round %d index %d kept bits %d", round, i, a->values[i].i);
	}
	if (!ac_failures) {
		heap_unref(gc);
		heap_unref(witness);
		heap_unref(witness);
		AC_CHECK(!ac_same(witness, seq), "witness survived its two actual owners");
	}
}
static int test_array_primitive(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "AP1-int", ap_int }, { "AP2-float", ap_float }, { "AP3-bool", ap_bool },
		{ "AP4-ref-containers", ap_refs }, { "AP5-realloc", ap_realloc },
		{ "AP6-guards", ap_guards }, { "AP7-SetPosList", ap_positions },
		{ "AP8-enum", ap_enum },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++) failed += ac_fork(cases[i].name, cases[i].fn);
	printf("ARRAY-PRIMITIVE %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}

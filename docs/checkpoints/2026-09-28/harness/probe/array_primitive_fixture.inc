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
static int test_array_primitive(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "AP1-int", ap_int }, { "AP2-float", ap_float }, { "AP3-bool", ap_bool },
		{ "AP4-ref-containers", ap_refs }, { "AP5-realloc", ap_realloc },
		{ "AP6-guards", ap_guards }, { "AP7-SetPosList", ap_positions },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++) failed += ac_fork(cases[i].name, cases[i].fn);
	printf("ARRAY-PRIMITIVE %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}

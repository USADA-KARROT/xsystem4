/* Spring-working cards: real AIN allocation and selector paths, no game assets.
 *
 * Native X_A_INIT 0x66e420 chooses initialized allocation for arg != 0
 * (0x67fe20 -> 0x656970); the argument is not an element-slot count.
 * Both paths allocate size logical elements. Struct initialization reaches
 * 0x679b30 / 0x679be8 and invokes the real constructor.
 * Native delegate returns use nested type metadata (0x6537e0): wrap<struct>
 * has one slot, wrap<int> has two; an interface keeps its two-slot value.
 *
 * WC1 isolates the actual LocalGame@2 instruction, so unrelated LocalGame
 * constructors cannot read EX data. WC2 isolates the actual fixed-size float
 * array initialization in the global initializer. WC3 uses the
 * original CustomerCollection constructor, Shuffle, GetOrdered and selector.
 * Existing DA1/DA2 provide the interface delegate controls in WC5.
 */
static int wc_failures;
static bool wc_watch_select;
static int wc_select_function, wc_push_function;
static unsigned wc_push_calls, wc_bad_push_stack;

#define WC_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); wc_failures++; } } while (0)

static void working_cards_probe_step(void)
{
	if (!wc_watch_select || !call_stack_ptr || get_opcode(instr_ptr) != CALLHLL)
		return;
	struct function_call *f = &call_stack[call_stack_ptr - 1];
	if (f->fno != wc_select_function || get_argument(0) != bu_lib("Array")
	    || get_argument(1) != wc_push_function)
		return;
	wc_push_calls++;
	/* This call consumes only [result array, Customer reference]. A spurious
	 * second delegate return used to sit on top and shift both arguments. */
	if (stack_ptr != f->base_sp + 2)
		wc_bad_push_stack++;
}

static struct page *wc_page(int slot)
{
	return da_live_page(slot) ? heap[slot].page : NULL;
}

static int wc_size(int slot)
{
	stack_push(slot);
	execute_instruction(X_A_SIZE);
	return stack_pop().i;
}

static uint32_t wc_init_instruction(const char *name, int argument)
{
	int fno = da_fn(name);
	for (uint32_t ip = ain->functions[fno].address; ip + 6 <= ain->code_size;
	     ip += instruction_width(get_opcode(ip))) {
		enum opcode op = get_opcode(ip);
		if (op == X_A_INIT && LittleEndian_getDW(ain->code, ip + 2) == argument)
			return ip;
		if (op == ENDFUNC)
			break;
	}
	printf("WORKING-CARDS missing X_A_INIT %d in %s\n", argument, name);
	exit(2);
}

static int wc_init_member(int object, int member, int size, uint32_t instruction)
{
	/* Match the real bytecode's DELETE before X_A_INIT. */
	int old = heap[object].page->values[member].i;
	if (old > 0 && heap_index_valid(old))
		heap_unref(old);
	heap[object].page->values[member].i = -1;
	uint32_t ip = instr_ptr;
	int sp = stack_ptr;
	stack_push(object);
	stack_push(member);
	stack_push(size);
	instr_ptr = instruction;
	execute_instruction(X_A_INIT);
	instr_ptr = ip;
	int slot = stack_pop().i;
	WC_CHECK(stack_ptr == sp && heap[object].page->values[member].i == slot,
		"X_A_INIT did not preserve stack/target ownership");
	return slot;
}

static void wc_rival_construction(void)
{
	size_t baseline = live_slots();
	int st = sp_sno("LocalGame"), worker_st = sp_sno("WorkerCollection");
	int owner = alloc_struct(st), member = sp_mem(st, "m_rivalShops");
	int ctor = da_fn("WorkerCollection@0");
	unsigned long before = calls[ctor];
	uint32_t instruction = wc_init_instruction("LocalGame@2", 1);
	int slot = wc_init_member(owner, member, 2, instruction);
	struct page *p = wc_page(slot);
	WC_CHECK(p && p->type == ARRAY_PAGE && p->nr_vars == 2 && wc_size(slot) == 2,
		"two rival shops: physical slots %d, logical size %d; want 2/2",
		p ? p->nr_vars : -1, wc_size(slot));
	WC_CHECK(calls[ctor] - before == 2, "WorkerCollection constructors: %lu, want 2", calls[ctor] - before);
	int live = 0;
	if (p) {
		for (int i = 0; i < p->nr_vars && i < 2; i++) {
			struct page *shop = wc_page(p->values[i].i);
			if (!shop || shop->type != STRUCT_PAGE || shop->index != worker_st)
				continue;
			live++;
			int id = shop->values[sp_mem(worker_st, "m_workers")].i;
			struct page *idp = wc_page(id);
			WC_CHECK(idp && idp->type == STRUCT_PAGE && idp->index == sp_sno("IdArray<string, Worker>"),
				"rival %d has no constructed IdArray", i);
			if (idp && idp->type == STRUCT_PAGE && idp->index == sp_sno("IdArray<string, Worker>")) {
				int list = sp_get(id, "m_list");
				struct page *lp = wc_page(list);
				WC_CHECK(lp && lp->type == ARRAY_PAGE && lp->nr_vars == 0,
					"rival %d lacks the constructor's empty worker list", i);
			}
		}
		if (live == 2)
			WC_CHECK(p->values[0].i != p->values[1].i, "rival shops alias the same object");
	}
	WC_CHECK(live == 2, "initialized rival objects: %d, want 2", live);
	if (p && live == 2) {
		int old_shops[2] = { p->values[0].i, p->values[1].i };
		heap_ref(slot); /* a second owner keeps the old array across reinitialization */
		int fresh = wc_init_member(owner, member, 2, instruction);
		struct page *fp = wc_page(fresh);
		WC_CHECK(fresh != slot && HEAP_REF(slot) == 1 && wc_page(slot) == p,
			"reinitialization lost the held old array");
		WC_CHECK(fp && fp->nr_vars == 2 && calls[ctor] - before == 4,
			"reinitialization did not construct two new shops");
		if (fp && fp->nr_vars == 2)
			WC_CHECK(fp->values[0].i != old_shops[0] && fp->values[1].i != old_shops[1],
				"reinitialized shops reused live old objects");
		heap_unref(slot);
		WC_CHECK(!da_live_page(old_shops[0]) && !da_live_page(old_shops[1]),
			"dropping the held old array did not release its shops");
	}
	heap_unref(owner);
	WC_CHECK(live_slots() == baseline, "rival teardown live slots %zu, want %zu", live_slots(), baseline);
	printf("  WC1 X_A_INIT 1 @%x: %d live rival shops; reinitialize/teardown checked\n", instruction, live);
}

static void wc_primitive_construction(void)
{
	size_t baseline = live_slots();
	uint32_t instruction = wc_init_instruction("0", 1);
	int checked = 0;
	for (int g = 153; g <= 154; g++) {
		const struct ain_type *t = &ain->globals[g].type;
		WC_CHECK(t->data == AIN_ARRAY && t->array_type && t->array_type->data == AIN_FLOAT,
			"vibration table global %d no longer declares array<float>", g);
		int slot = wc_init_member(global_page_slot, g, 1024, instruction);
		struct page *p = wc_page(slot);
		WC_CHECK(p && p->a_type == AIN_ARRAY_FLOAT && p->nr_vars == 1024 && wc_size(slot) == 1024,
			"float table %d: type/physical/logical %d/%d/%d, want float/1024/1024", g,
			p ? p->a_type : -1, p ? p->nr_vars : -1, wc_size(slot));
		if (p && p->nr_vars == 1024) for (int i = 0; i < 1024; i++) {
			WC_CHECK(p->values[i].f == 0.0f, "float table %d[%d] not zero", g, i);
			checked += p->values[i].f == 0.0f;
		}
		heap[global_page_slot].page->values[g].i = -1;
		heap_unref(slot);
	}
	const struct {
		const char *structure, *function, *member;
		int length, data_type;
	} cases[] = {
		{ "elkeditor::detail::CEmitterKey", "elkeditor::detail::CEmitterKey@2", "m_paramList", 5, AIN_ARRAY_FLOAT },
		{ "activityeditor::detail::SSampleParts", "activityeditor::detail::SSampleParts@2", "m_typeList", 3, AIN_ARRAY_INT },
		{ "Player", "Player@2", "m_passiveSkillAvailable", 2, AIN_ARRAY_INT },
	};
	for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		int st = sp_sno(cases[c].structure), owner = alloc_struct(st);
		int member = sp_mem(st, cases[c].member);
		int slot = wc_init_member(owner, member, cases[c].length, wc_init_instruction(cases[c].function, 1));
		struct page *p = wc_page(slot);
		WC_CHECK(p && p->a_type == cases[c].data_type && p->nr_vars == cases[c].length
			&& wc_size(slot) == cases[c].length, "%s initialization type/length mismatch", cases[c].function);
		if (p && p->nr_vars == cases[c].length) for (int i = 0; i < p->nr_vars; i++)
			WC_CHECK(p->values[i].i == 0, "%s[%d] not zero", cases[c].member, i);
		heap_unref(owner);
	}
	/* Isolate ERROR's array<string> allocation; do not execute ERROR or
	 * its unrelated UI/error-reporting body. A LOCAL_PAGE supplies the real
	 * function's declaration exactly as the VM's call frame would. */
	int error_fn = da_fn("_system::detail::ERROR");
	int local = heap_alloc_page(alloc_page(LOCAL_PAGE, error_fn, ain->functions[error_fn].nr_vars));
	int strings = wc_init_member(local, 2, 2, wc_init_instruction("_system::detail::ERROR", 1));
	struct page *strp = wc_page(strings);
	WC_CHECK(strp && strp->a_type == AIN_ARRAY_STRING && strp->nr_vars == 2 && wc_size(strings) == 2,
		"ERROR's local array<string> initialization type/length mismatch");
	if (strp && strp->nr_vars == 2) for (int i = 0; i < 2; i++) {
		int s = strp->values[i].i;
		WC_CHECK(s > 1 && heap_index_valid(s) && HEAP_REF(s) == 1 && heap[s].type == VM_STRING
			&& heap[s].s && heap[s].s->size == 0, "initialized string %d is not an owned empty string", i);
	}
	heap_unref(local);
	WC_CHECK(live_slots() == baseline, "primitive teardown live slots %zu, want %zu", live_slots(), baseline);
	printf("  WC2 real X_A_INIT 1 global float tables: %d zero floats; teardown checked\n", checked);
}

static void wc_customer_order(void)
{
	size_t baseline = live_slots();
	int c = sp_new("CustomerCollection");
	int list = sp_get(sp_get(c, "m_customer"), "m_list");
	int customers[4];
	for (int i = 0; i < 4; i++)
		customers[i] = alloc_struct(sp_sno("Customer"));
	sp_arr_set(list, customers, 4, true);
	call_method0(da_fn("CustomerCollection@Count::get"), c);
	WC_CHECK(stack_pop().i == 4, "the source collection does not contain 4 customers");
	call_method0(da_fn("CustomerCollection@Shuffle"), c);
	int index_slot = sp_get(c, "m_shuffledIndex");
	struct page *indices = wc_page(index_slot);
	WC_CHECK(indices && indices->nr_vars == 4 && wc_size(index_slot) == 4,
		"Shuffle did not produce four indices");
	int order[4] = {-1, -1, -1, -1}, mask = 0;
	if (indices && indices->nr_vars == 4) for (int i = 0; i < 4; i++) {
		order[i] = indices->values[i].i;
		if (order[i] >= 0 && order[i] < 4)
			mask |= 1 << order[i];
	}
	WC_CHECK(mask == 15, "Shuffle indices are not a permutation of 0..3");
	wc_select_function = da_fn("ArrayExtensions::Select<Customer&, int>");
	wc_push_function = sf_find(bu_lib("Array"), "PushBack", 2);
	int selector = da_fn("CustomerCollection@<lambda : CustomerCollection@GetOrdered()(55, 39)>");
	int source_refs[4];
	for (int i = 0; i < 4; i++)
		source_refs[i] = HEAP_REF(customers[i]);
	unsigned long entered = calls[selector];
	wc_push_calls = wc_bad_push_stack = 0;
	wc_watch_select = true;
	call_method0(da_fn("CustomerCollection@GetOrdered"), c);
	wc_watch_select = false;
	int result = stack_pop().i;
	struct page *r = wc_page(result);
	WC_CHECK(calls[selector] - entered == 4 && wc_push_calls == 4,
		"selector/push calls %lu/%u, want 4/4", calls[selector] - entered, wc_push_calls);
	WC_CHECK(wc_bad_push_stack == 0, "%u selector pushes carried extra stack values", wc_bad_push_stack);
	WC_CHECK(r && r->nr_vars == 4 && wc_size(result) == 4,
		"GetOrdered result physical/logical %d/%d, want 4/4", r ? r->nr_vars : -1, wc_size(result));
	if (r && r->nr_vars == 4 && mask == 15) for (int i = 0; i < 4; i++)
		WC_CHECK(r->values[i].i == customers[order[i]] && da_live_page(r->values[i].i),
			"ordered customer %d lost identity: %d, want %d", i, r->values[i].i, customers[order[i]]);
	printf("  WC3 Shuffle [%d,%d,%d,%d] -> %d customers, %lu selectors, %u malformed pushes\n",
		order[0], order[1], order[2], order[3], r ? r->nr_vars : -1,
		calls[selector] - entered, wc_bad_push_stack);
	heap_unref(result);
	for (int i = 0; i < 4; i++)
		WC_CHECK(da_live_page(customers[i]) && HEAP_REF(customers[i]) == source_refs[i],
			"dropping GetOrdered result changed source customer %d ref %d, want %d",
			i, HEAP_REF(customers[i]), source_refs[i]);
	heap_unref(c);
	for (int i = 0; i < 4; i++)
		WC_CHECK(!da_live_page(customers[i]), "source customer %d survived collection teardown", i);
	WC_CHECK(!da_live_page(c) && !da_live_page(list) && !da_live_page(index_slot) && !da_live_page(result),
		"collection/list/index/result survived their last owner");
	/* GetOrdered leaves one pre-existing unused local placeholder, an
	 * anonymous empty wrap box (a separate VM local-initialization issue;
	 * an empty-source GetOrdered with zero callbacks leaves it too). Do not
	 * hide extra retained customers inside a blanket allowance. Until the
	 * arg-ownership change an empty delegate page was left as well and this
	 * check pinned both: GetOrdered hands its lambda (DG_NEW_FROM_METHOD)
	 * to ArrayExtensions::Select as a delegate argument, which
	 * function_call retained again, so the delegate page stayed, emptied
	 * when its target, the collection, was released. Natively a delegate
	 * argument is stored without a retain (0x657430, table 0x657508) and
	 * released with the callee's page (0x656c10, table 0x656c88). */
	int delegate_placeholders = 0, wrap_placeholders = 0;
	for (size_t i = 2; i < heap_size; i++) if (HEAP_REF(i) > 0) {
		struct page *p = heap[i].type == VM_PAGE ? heap[i].page : NULL;
		if (p && HEAP_REF(i) == 1 && p->type == DELEGATE_PAGE && p->nr_vars == 0)
			delegate_placeholders++;
		if (p && HEAP_REF(i) == 1 && p->type == STRUCT_PAGE && p->index == -1
		    && p->nr_vars == 1 && p->values[0].i == -1)
			wrap_placeholders++;
	}
	WC_CHECK(live_slots() == baseline + 1 && delegate_placeholders == 0 && wrap_placeholders == 1,
		"teardown leaves %zu live slots (%d empty delegate/%d empty wrap), want only the baseline wrap placeholder",
		live_slots() - baseline, delegate_placeholders, wrap_placeholders);
}

static void wc_delegate_shapes(void)
{
	int structs = 0, interfaces = 0;
	for (int i = 0; i < ain->nr_delegates; i++) {
		struct ain_type *t = &ain->delegates[i].return_type;
		if (t->data != AIN_WRAP || !t->array_type)
			continue;
		if (t->array_type->data == AIN_STRUCT) {
			structs++;
			WC_CHECK(delegate_return_slots(t) == 1, "%s wrap<struct> return is not 1 slot", ain->delegates[i].name);
		} else if (t->array_type->data == AIN_IFACE_WRAP) {
			interfaces++;
			WC_CHECK(delegate_return_slots(t) == 2, "%s wrapped interface return is not 2 slots", ain->delegates[i].name);
		}
	}
	WC_CHECK(structs == 26 && interfaces == 18, "AIN wrap return declarations %d/%d, want 26/18", structs, interfaces);
	/* This AIN has no primitive-wrap delegate return declaration. The next
	 * check is a synthetic metadata control, not a claimed bytecode caller.
	 * Native 0x6537e0 selects two slots for nested int/float/bool/enum. */
	struct ain_type inner = { .data = AIN_INT, .struc = -1 };
	struct ain_type wrapped = { .data = AIN_WRAP, .struc = -1, .array_type = &inner };
	WC_CHECK(delegate_return_slots(&wrapped) == 2, "synthetic wrap<int> lost its second slot");
	inner.data = AIN_FLOAT;
	WC_CHECK(delegate_return_slots(&wrapped) == 2, "synthetic wrap<float> lost its second slot");
	inner.data = AIN_BOOL;
	WC_CHECK(delegate_return_slots(&wrapped) == 2, "synthetic wrap<bool> lost its second slot");
	int version = ain->version;
	ain->version = 13;
	WC_CHECK(delegate_return_slots(&wrapped) == 2, "legacy unrecognized wrap binding changed");
	int customer_dg = da_delegate("DG_Func<ref int, Customer&>");
	WC_CHECK(delegate_return_slots(&ain->delegates[customer_dg].return_type) == 2,
		"v14's plain-struct change leaked into the legacy wrap path");
	ain->version = version;
	printf("  WC4 %d plain-struct / %d interface declarations; synthetic primitive and legacy controls\n", structs, interfaces);
}

static void wc_interface_control(void)
{
	da_failures = 0;
	da_lambda();
	WC_CHECK(da_failures == 0, "real Tutorial selector two-slot return regressed (%d checks)", da_failures);
	/* The standalone delegate-args mode also runs DA2. Keeping the original
	 * end-to-end Select here catches blanket WRAP -> one-slot changes. */
	da_select();
	WC_CHECK(da_failures == 0, "real interface Select regressed (%d checks)", da_failures);
	printf("  WC5 real interface delegate and four-element Select controls\n");
}

static void wc_generic_unchanged(void)
{
	size_t baseline = live_slots();
	int sentinel = heap_alloc_page(alloc_page(STRUCT_PAGE, -1, 0));
	unsigned seq = heap[sentinel].seq;
	int st = sp_sno("activityeditor::detail::CGridView"), owner = alloc_struct(st);
	int member = sp_mem(st, "m_vLine");
	int slot = wc_init_member(owner, member, 1,
		wc_init_instruction("activityeditor::detail::CGridView@0", 0));
	struct page *p = wc_page(slot);
	/* m_vLine is array<wrap<iwrap<IParts>>>: an element is (object, vtable
	 * offset), two slots. The original gives the page its element width
	 * from the declaration (0x679432 -> 0x653420, page +0x48) and sizes a
	 * sized array as elements * width (0x67fee0); the immediate 0 only
	 * says "do not default-initialize". So one element is two slots and
	 * X_A_SIZE is 1. (Until the array-literal change this page had one
	 * slot, immediate + 1, and this check pinned that.) The -1 in both
	 * slots is xsystem4's: natively the elements are left unset. The size
	 * 1 is this case's: the script has size 0 there (CGridView@0 pushes
	 * 0), and an empty page keeps immediate + 1 (see array-literal). */
	WC_CHECK(p && p->a_type == AIN_ARRAY && p->array.struct_type == 2
		&& p->nr_vars == 2 && wc_size(slot) == 1
		&& p->values[0].i == -1 && p->values[1].i == -1,
		"X_A_INIT 0 of one interface element is not one two-slot element");
	/* No nested-option array is initialized by this AIN. Supply a synthetic
	 * declaration only, keep the real opcode and production allocator. The
	 * page is two slots as before, now because the declaration says so:
	 * every option layer together adds one slot to the one-slot payload
	 * (0x653452). The immediate is 1, so the element is default-initialized
	 * (0x67f4a0 -> 0x67fe20 -> 0x656970) to none: its value slot -1 and its
	 * last slot the number of option layers (0x656a44: 0x653610 + 1), 2
	 * here. (Until the array-literal change the element started as
	 * (-1, -1) and this check pinned that.) */
	struct ain_type original = ain->structures[st].members[member].type;
	struct ain_type payload = { .data = AIN_STRUCT, .struc = sp_sno("CASColor") };
	struct ain_type wrap = { .data = AIN_WRAP, .struc = payload.struc, .array_type = &payload };
	struct ain_type inner = { .data = AIN_OPTION, .struc = -1, .array_type = &wrap };
	struct ain_type outer = { .data = AIN_OPTION, .struc = -1, .array_type = &inner };
	ain->structures[st].members[member].type.array_type = &outer;
	slot = wc_init_member(owner, member, 1, wc_init_instruction("LocalGame@2", 1));
	ain->structures[st].members[member].type = original;
	p = wc_page(slot);
	WC_CHECK(p && p->a_type == AIN_ARRAY && p->array.struct_type == 2 && p->nr_vars == 2
		&& p->values[0].i == -1 && p->values[1].i == 2,
		"X_A_INIT 1 of one nested-option element is not one two-slot none, (-1, 2)");
	heap_unref(owner);
	WC_CHECK(da_live_page(sentinel) && heap[sentinel].seq == seq && HEAP_REF(sentinel) == 1,
		"generic initialization/teardown changed the unrelated sentinel");
	heap_unref(sentinel);
	WC_CHECK(live_slots() == baseline, "generic control teardown leaked slots");
	printf("  WC6 a sized interface array has two slots per element; the synthetic nested-option element is none in two slots; sentinel retained\n");
}

static int test_working_cards(void)
{
	void (*cases[])(void) = { wc_rival_construction, wc_primitive_construction,
		wc_customer_order, wc_delegate_shapes, wc_interface_control, wc_generic_unchanged };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			wc_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(wc_failures ? 1 : 0);
		}
		int st = 0;
		bool ok = pid > 0 && waitpid(pid, &st, 0) == pid
			&& WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("WC%u %s\n", i + 1, ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("WORKING-CARDS %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS", failed,
		sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}

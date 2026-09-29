/* save-fixes: regressions found by review of 173ff1d (2026-09-29).
 *
 * F1 corrupt struct-definition count: a valid zlib body whose nsd is larger
 *    than the remaining bytes must make DeserializeStruct return false with
 *    the destination untouched, not dereference NULL in the reader cleanup.
 * F2 option<int> loaded by DeserializeStruct and then overwritten by real
 *    game bytecode (WorkerHistory@SetIncome, X_OP_SET) must not unref the
 *    unrelated slot whose number equals the old integer, nor ref the new one.
 * F3 X_OP_SET of option<int> followed by deleting the struct keeps the refs
 *    of the slot whose number equals the payload unchanged (no leak).
 * F4 copying a struct holding option<int> keeps the integer (no deep copy of
 *    the page whose number equals it).
 * F5 vm_construct_struct of a struct without a STRT constructor initializes
 *    its members the native way (0x679b30(i,0) -> 0x656970(m,0)): struct
 *    members are objects, strings empty strings, delegates delegates, options
 *    none.
 * F6 the same constructor-less path fills a first member named <vtable> with
 *    the struct's method list (0x679c9e); Player and BattleSkill are rebuilt
 *    by the loader and dispatch through it.
 * F7 a 3-slot option (option<wrap<iwrap<T>>>) defaults to none as
 *    (-1, -1, 1), not (-1, 1, 0) (0x656a44).
 * F8 a local option<int> owns no heap slot: calling LocalSave@Available
 *    repeatedly must not leave slots whose page is NULL.
 * Functions only present after the fix are not referenced directly, so this
 * file also compiles against the pre-fix revision for before-check.sh.
 */


static bool ain_option_type_is_value_probe(const struct ain_type *t)
{
	if (!t || t->data != AIN_OPTION)
		return false;
	while (t && (t->data == AIN_OPTION || t->data == AIN_UNKNOWN_TYPE_87))
		t = t->array_type;
	return t && (t->data == AIN_INT || t->data == AIN_FLOAT || t->data == AIN_BOOL
		     || t->data == AIN_ENUM || t->data == AIN_ENUM2 || t->data == AIN_LONG_INT);
}

static int sf_opt_mem(const char *sname)
{
	/* first option<int> member of <sname> (its flag is the next slot) */
	int s = sp_sno(sname);
	for (int i = 0; i + 1 < ain->structures[s].nr_members; i++) {
		struct ain_type *t = &ain->structures[s].members[i].type;
		if (t->data == AIN_OPTION && t->array_type && t->array_type->data == AIN_INT)
			return i;
	}
	printf("  %s has no option<int> member\n", sname);
	exit(2);
}

static int sf_nsd(void)
{
	int wh = sp_new("WorkerHistory");
	int m = sf_opt_mem("WorkerHistory");
	heap[wh].page->values[m].i = 4321;
	heap[wh].page->values[m + 1].i = 0;
	SP_CHECK(sp_sys("SerializeStruct", "T_NSD.asd", wh) == 1, "save");
	size_t len = 0;
	uint8_t zhdr[2];
	uint8_t *raw = sp_read_raw("T_NSD.asd", &len, zhdr);
	SP_CHECK(raw, "read raw");
	struct sp_body b = { .raw = raw, .len = len };
	SP_CHECK(sp_body_parse(&b), "parse body");
	sp_put_le(raw + b.sd_off, 0x100000);
	sp_write_raw("T_NSD.asd", raw, len);
	free(raw);
	int dest = sp_new("WorkerHistory");
	heap[dest].page->values[m].i = 77;
	heap[dest].page->values[m + 1].i = 0;
	int r = sp_sys("DeserializeStruct", "T_NSD.asd", dest);
	SP_CHECK(r == 0, "corrupt nsd: DeserializeStruct returned %d (want 0)", r);
	SP_CHECK(heap[dest].page->values[m].i == 77, "destination changed to %d", heap[dest].page->values[m].i);
	heap_unref(dest);
	heap_unref(wh);
	return 0;
}

static int sf_loaded_then_setincome(void)
{
	int victim = sp_str("victim-A");
	int victim2 = sp_str("victim-B");
	int m = sf_opt_mem("WorkerHistory");
	int wh = sp_new("WorkerHistory");
	heap[wh].page->values[m].i = victim;   /* some(<victim's slot number>) as a plain int */
	heap[wh].page->values[m + 1].i = 0;
	SP_CHECK(sp_sys("SerializeStruct", "T_WH.asd", wh) == 1, "save");
	int wh2 = sp_new("WorkerHistory");
	SP_CHECK(sp_sys("DeserializeStruct", "T_WH.asd", wh2) == 1, "load");
	SP_CHECK(heap[wh2].page->values[m].i == victim && heap[wh2].page->values[m + 1].i == 0,
		 "loaded (%d,%d)", heap[wh2].page->values[m].i, heap[wh2].page->values[m + 1].i);
	SP_CHECK(heap[victim].ref == 1, "victim ref %d after load", heap[victim].ref);
	int fno = ain_get_function(ain, "WorkerHistory@SetIncome");
	SP_CHECK(fno >= 0, "WorkerHistory@SetIncome not found");
	size_t saved = instr_ptr;
	stack_push(wh2);
	stack_push(victim2);
	method_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = saved;
	SP_CHECK(heap[victim].ref == 1, "victim ref %d after SetIncome (0 = released by the game's X_OP_SET)", heap[victim].ref);
	SP_CHECK(heap[victim2].ref == 1, "victim2 ref %d after SetIncome (2 = int payload ref'd)", heap[victim2].ref);
	heap_unref(wh2);
	heap_unref(wh);
	SP_CHECK(heap[victim].ref == 1 && heap[victim2].ref == 1, "refs %d/%d after deleting the structs",
		 heap[victim].ref, heap[victim2].ref);
	heap_unref(victim);
	heap_unref(victim2);
	return 0;
}

static int sf_xopset_delete(void)
{
	int fno = ain_get_function(ain, "CAS3DLayer@PreparePluginNumber");
	size_t addr = 0;
	for (size_t a = fno >= 0 ? ain->functions[fno].address : 0;
	     fno >= 0 && a + 6 < ain->code_size && a < ain->functions[fno].address + 200; a++) {
		uint16_t op = ain->code[a] | ain->code[a + 1] << 8;
		int32_t arg = (int32_t)(ain->code[a + 2] | ain->code[a + 3] << 8 | ain->code[a + 4] << 16
					| (uint32_t)ain->code[a + 5] << 24);
		if (op == X_OP_SET && arg == 1) { addr = a; break; }
	}
	SP_CHECK(addr, "no X_OP_SET 1 in CAS3DLayer@PreparePluginNumber");
	int m = sf_opt_mem("WorkerHistory");
	int wh = sp_new("WorkerHistory");
	int victim = sp_str("victim-C");
	size_t saved = instr_ptr;
	int sp0 = stack_ptr;
	instr_ptr = addr;
	stack_push(wh); stack_push(m); stack_push(victim); stack_push(0);
	execute_instruction(X_OP_SET);
	stack_ptr = sp0;
	instr_ptr = saved;
	SP_CHECK(heap[wh].page->values[m].i == victim, "payload %d", heap[wh].page->values[m].i);
	SP_CHECK(heap[victim].ref == 1, "victim ref %d after X_OP_SET of an int payload", heap[victim].ref);
	heap_unref(wh);
	SP_CHECK(heap[victim].ref == 1, "victim ref %d after deleting the struct", heap[victim].ref);
	heap_unref(victim);
	return 0;
}

static int sf_copy(void)
{
	int m = sf_opt_mem("WorkerHistory");
	int wh = sp_new("WorkerHistory");
	int victim = sp_str("victim-D");
	heap[wh].page->values[m].i = victim;
	heap[wh].page->values[m + 1].i = 0;
	size_t live0 = sp_nonzero_slots();
	int copy = vm_copy_page(heap[wh].page);
	SP_CHECK(heap[copy].page->values[m].i == victim, "copy payload %d (want %d)", heap[copy].page->values[m].i, victim);
	SP_CHECK(heap[victim].ref == 1, "victim ref %d after copy", heap[victim].ref);
	heap_unref(copy);
	SP_CHECK(sp_nonzero_slots() == live0, "live slots %zu -> %zu after copy+delete", live0, sp_nonzero_slots());
	heap_unref(wh);
	SP_CHECK(heap[victim].ref == 1, "victim ref %d after deleting the original", heap[victim].ref);
	heap_unref(victim);
	return 0;
}

static int sf_default_members(void)
{
	int (*construct)(int) = (int (*)(int))sp_sym("vm_construct_struct");
	SP_CHECK(construct, "vm_construct_struct not present");
	int sno = sp_sno("BattleContext");
	SP_CHECK(ain->structures[sno].constructor <= 0, "BattleContext has a constructor now; pick another struct");
	size_t live0 = sp_nonzero_slots();
	int bc = construct(sno);
	SP_CHECK(bc > 0 && heap[bc].page, "construct failed");
	const char *objs[] = { "m_actions", "m_history", "m_result" };
	for (int i = 0; i < 3; i++) {
		int s = sp_get(bc, objs[i]);
		SP_CHECK(s > 0 && heap_index_valid(s) && heap[s].type == VM_PAGE && heap[s].page
			 && heap[s].page->type == STRUCT_PAGE, "%s = %d (want a struct)", objs[i], s);
	}
	int str = sp_get(bc, "<BackgroundId>");
	SP_CHECK(str > 0 && heap_index_valid(str) && heap[str].type == VM_STRING && heap[str].s
		 && heap[str].s->size == 0, "<BackgroundId> = %d (want an empty string)", str);
	const char *dgs[] = { "<OnPopStockEvent>", "<OnChangePlayerEvent>", "<OnSwapPlayerEvent>" };
	for (int i = 0; i < 3; i++) {
		int d = sp_get(bc, dgs[i]);
		SP_CHECK(d > 0 && heap_index_valid(d) && heap[d].page && heap[d].page->type == DELEGATE_PAGE,
			 "%s = %d (want a delegate)", dgs[i], d);
	}
	int mm = sp_mem(sno, "MusicId");
	SP_CHECK(heap[bc].page->values[mm].i == -1 && heap[bc].page->values[mm + 1].i == 1,
		 "MusicId = (%d,%d) (want none = (-1,1))", heap[bc].page->values[mm].i, heap[bc].page->values[mm + 1].i);
	heap_unref(bc);
	SP_CHECK(sp_nonzero_slots() == live0, "live slots %zu -> %zu after deleting", live0, sp_nonzero_slots());
	return 0;
}


static int sf_vtable_of(const char *sname)
{
	int (*construct)(int) = (int (*)(int))sp_sym("vm_construct_struct");
	SP_CHECK(construct, "vm_construct_struct not present");
	int sno = sp_sno(sname);
	struct ain_struct *st = &ain->structures[sno];
	SP_CHECK(st->constructor <= 0, "%s has a constructor now", sname);
	SP_CHECK(st->nr_members > 0 && !strcmp(st->members[0].name, "<vtable>") && st->nr_vmethods > 0,
		 "%s has no <vtable> member", sname);
	int obj = construct(sno);
	SP_CHECK(obj > 0 && heap[obj].page, "construct %s failed", sname);
	int arr = heap[obj].page->values[0].i;
	SP_CHECK(arr > 0 && heap_index_valid(arr) && heap[arr].page, "%s.<vtable> = %d (want an array)", sname, arr);
	struct page *a = heap[arr].page;
	SP_CHECK(a->nr_vars == st->nr_vmethods, "%s.<vtable> has %d entries (want %d)", sname, a->nr_vars, st->nr_vmethods);
	for (int k = 0; k < st->nr_vmethods; k++)
		SP_CHECK(a->values[k].i == st->vmethods[k], "%s.<vtable>[%d] = %d (want %d)", sname, k, a->values[k].i, st->vmethods[k]);
	printf("  %s.<vtable>: %d entries\n", sname, a->nr_vars);
	heap_unref(obj);
	return 0;
}

static int sf_vtable(void)
{
	size_t live0 = sp_nonzero_slots();
	if (sf_vtable_of("Player") || sf_vtable_of("BattleSkill"))
		return 1;
	SP_CHECK(sp_nonzero_slots() == live0, "live slots %zu -> %zu", live0, sp_nonzero_slots());
	return 0;
}

static int sf_option3(void)
{
	int (*construct)(int) = (int (*)(int))sp_sym("vm_construct_struct");
	SP_CHECK(construct, "vm_construct_struct not present");
	int sno = sp_sno("PlayerAction");
	SP_CHECK(ain->structures[sno].constructor <= 0, "PlayerAction has a constructor now");
	int m = sp_mem(sno, "m_innerAction");
	struct ain_type *t = &ain->structures[sno].members[m].type;
	SP_CHECK(t->data == AIN_OPTION && t->array_type && t->array_type->data == AIN_WRAP,
		 "m_innerAction is not option<wrap<...>>");
	int obj = construct(sno);
	SP_CHECK(obj > 0 && heap[obj].page, "construct failed");
	union vm_value *v = heap[obj].page->values;
	SP_CHECK(v[m].i == -1 && v[m + 1].i == -1 && v[m + 2].i == 1,
		 "m_innerAction = (%d,%d,%d) (want none = (-1,-1,1))", v[m].i, v[m + 1].i, v[m + 2].i);
	heap_unref(obj);
	return 0;
}

static size_t sf_null_page_slots(void)
{
	size_t n = 0;
	for (size_t i = 2; i < heap_size; i++)
		if (heap[i].ref > 0 && heap[i].type == VM_PAGE && !heap[i].page)
			n++;
	return n;
}

static int sf_local_option(void)
{
	int fno = ain_get_function(ain, "LocalSave@Available");
	SP_CHECK(fno >= 0, "LocalSave@Available not found");
	SP_CHECK(ain->functions[fno].nr_vars > 0 && ain_option_type_is_value_probe(&ain->functions[fno].vars[0].type),
		 "LocalSave@Available var 0 is not option<int>");
	int ls = sp_new("LocalSave");
	size_t before = sf_null_page_slots();
	for (int k = 0; k < 4; k++) {
		size_t saved = instr_ptr;
		int sp0 = stack_ptr;
		stack_push(ls);
		method_call(fno, VM_RETURN);
		vm_execute();
		instr_ptr = saved;
		stack_ptr = sp0;
	}
	size_t after = sf_null_page_slots();
	SP_CHECK(after == before, "slots with a NULL page %zu -> %zu after 4 calls (a local option<int> leaks one per call)",
		 before, after);
	heap_unref(ls);
	return 0;
}

static int test_save_fixes(void)
{
	sp_setup_dir();
	sp_run("F1 corrupt struct-definition count", sf_nsd);
	sp_run("F2 loaded option<int> then SetIncome", sf_loaded_then_setincome);
	sp_run("F3 X_OP_SET option<int> then delete", sf_xopset_delete);
	sp_run("F4 copy struct with option<int>", sf_copy);
	sp_run("F5 default members without a ctor", sf_default_members);
	sp_run("F6 <vtable> of ctor-less structs", sf_vtable);
	sp_run("F7 3-slot option defaults to none", sf_option3);
	sp_run("F8 local option<int> owns no slot", sf_local_option);
	sp_remove_dir();
	printf("SAVE-FIXES summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}

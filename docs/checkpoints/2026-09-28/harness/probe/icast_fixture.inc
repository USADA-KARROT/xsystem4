/* icast: X_ICAST and X_TO_STR 47 against the original handlers (2026-10-08).
 *
 * X_ICAST target (0x66c540) reads the object at TOS without popping it and
 * pushes two slots: [object, vtable offset, 0] when the object's struct is the
 * target (0x66c727, offset 0) or lists it in its interface pair table
 * (0x66c752), and [-1, 0, 1] otherwise (0x66c6bc rewrites TOS to -1, then
 * push 0, push 1). The game tests the third slot with X_MOV 2 1; POP; PUSH 1;
 * GTE (157 sites) or keeps [object, offset] for an interface variable after a
 * POP (380 sites). Before the fix the VM pushed [object, object, offset]:
 * every cast to an interface that is not the struct's first one was reported
 * as failed, and a stored interface value carried the object slot instead of
 * the vtable offset.
 *
 * X_TO_STR 47 (0x66c1a6) is the literal "true"/"false" (0x7e3c28/0x7e3c30);
 * the VM printed "1"/"0".
 *
 * All cases run real bytecode: IC1-IC4 single-step the real X_ICAST
 * instruction (and the PUSH 1; GTE that follows it) found in the game's own
 * function bodies; IC5 runs parts::detail::CSpriteParts@Get#1 end to end on
 * the CSpriteParts an AdvStand constructor creates (the path backlog's
 * CBackLogView@GetFont and every "GetX(state) as ISprite" use); IC6 runs
 * CASAnchor@ToString.
 *   IC1 CCGParts as ISprite (second interface): [obj, vtoff>=1, 0]; GTE -> 0
 *   IC2 CASAnchor as ISprite (no such interface): [-1, 0, 1]; GTE -> 1
 *   IC3 null as ISprite: [-1, 0, 1] (the original logs nothing for -1; the
 *       warning counter is private to vm.c, so that is not asserted here)
 *   IC4 CUserComponentParts as CUserComponentParts (same type): [obj, 0, 0]
 *   IC7 a string slot as ISprite, IC8 an array page as ISprite: not a struct
 *       page (0x67ac30 checks the page type at 0x67ac8e) -> [-1, 0, 1] after
 *       the "構造体ページの取得に失敗" warning
 *   IC5 CSpriteParts@Get#1(state): wrap<iwrap<ISprite>> = [CCGParts, vtoff]
 *   IC9 the returned interface value is used: ISprite declares no method, so
 *       the same object is cast with the real X_ICAST ICGParts of
 *       CCopyProperty@CopyPropertySpriteCG ([obj, 0, 0]) and ICGParts'
 *       Core::get is called through <vtable>[offset + k] (the shape of every
 *       interface call: X_REF 2; ...; PUSH k; ADD; X_REF 1; CALLMETHOD)
 *   IC6 CASAnchor@ToString with Top and Right set: "(true, false, false, true)"
 * Uses ia_fn/ia_run from iface_arg_fixture.inc (include order). */
static int ic_failures;
#define IC_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ic_failures++; } } while (0)

/* ain_get_function rewrites "name#n" in place, so it needs a writable copy. */
static int ic_fn(const char *name)
{
	char *copy = strdup(name);
	int f = ain_get_function(ain, copy);
	free(copy);
	if (f < 0) { printf("ICAST %s not declared\n", name); exit(2); }
	return f;
}

/* Address of the first <opcode operand> in fno's body, or 0. A lambda body
 * embedded in the function (its own FUNC/ENDFUNC) is skipped over. */
static size_t ic_find(int fno, int opcode, int operand)
{
	int depth = 0;
	for (size_t ip = ain->functions[fno].address; ip + 2 <= ain->code_size; ) {
		int op = get_opcode(ip);
		if (op == FUNC) depth++;
		else if (op == ENDFUNC) { if (depth-- == 0) break; }
		else if (depth == 0 && op == opcode
			 && (operand < 0 || LittleEndian_getDW(ain->code, ip + 2) == operand))
			return ip;
		ip += instruction_width(op);
	}
	return 0;
}

/* Execute the single instruction at ip (what vm_execute does per step). */
static void ic_step(size_t ip)
{
	instr_ptr = ip;
	execute_instruction(get_opcode(ip));
}

static int ic_vtoff(int struct_type, int iface)
{
	struct ain_struct *s = &ain->structures[struct_type];
	if (struct_type == iface) return 0;
	for (int i = 0; i < s->nr_interfaces; i++)
		if (s->interfaces[i].struct_type == iface)
			return s->interfaces[i].vtable_offset;
	return -1;
}

/* X_ICAST at ip with obj at TOS, then the real PUSH 1; GTE that follows it in
 * the game. Reports the three pushed slots and the GTE result. */
static void ic_cast_case(const char *name, size_t ip, int obj, int want0, int want1, int want2)
{
	int sp = stack_ptr;
	stack_push(obj);
	ic_step(ip);
	int n = stack_ptr - sp;
	int s0 = n > 0 ? stack[sp].i : -99, s1 = n > 1 ? stack[sp + 1].i : -99, s2 = n > 2 ? stack[sp + 2].i : -99;
	IC_CHECK(n == 3 && s0 == want0 && s1 == want1 && s2 == want2,
		 "%s: X_ICAST left %d slot(s) [%d, %d, %d], want [%d, %d, %d]", name, n, s0, s1, s2, want0, want1, want2);
	size_t next = ip + instruction_width(X_ICAST);
	bool has_test = n == 3 && get_opcode(next) == PUSH && LittleEndian_getDW(ain->code, next + 2) == 1
		&& get_opcode(next + instruction_width(PUSH)) == GTE;
	if (has_test) {
		ic_step(next);
		ic_step(next + instruction_width(PUSH));
		/* [obj, vtoff, flag] -> PUSH 1 -> GTE pops two, pushes one: [obj, vtoff, flag >= 1] */
		int flag = stack_ptr == sp + 3 ? stack[sp + 2].i : -99;
		IC_CHECK(flag == (want2 >= 1), "%s: PUSH 1; GTE on the flag gave %d, want %d", name, flag, want2 >= 1);
	}
	printf("%s: [%d, %d, %d]%s\n", name, s0, s1, s2, has_test ? " flag tested" : "");
	stack_ptr = sp;
}

static void ic_single_step(void)
{
	int cc = ain_get_struct(ain, "parts::detail::CCGParts");
	int isprite = ain_get_struct(ain, "ISprite");
	int anchor = ain_get_struct(ain, "CASAnchor");
	int ucp = ain_get_struct(ain, "parts::detail::CUserComponentParts");
	assert(cc >= 0 && isprite >= 0 && anchor >= 0 && ucp >= 0);
	size_t ip_sprite = ic_find(ic_fn("parts::detail::CSpriteParts@Get#1"), X_ICAST, isprite);
	size_t ip_same = ic_find(ic_fn("CUserComponentSet@Set"), X_ICAST, ucp);
	IC_CHECK(ip_sprite && ip_same, "X_ICAST ISprite at 0x%zx, X_ICAST CUserComponentParts at 0x%zx", ip_sprite, ip_same);
	if (ic_failures) return;
	int vtoff = ic_vtoff(cc, isprite);
	IC_CHECK(vtoff >= 1, "CCGParts lists ISprite at vtable offset %d (want >= 1: ICGParts comes first)", vtoff);
	int obj = alloc_struct(cc), other = alloc_struct(anchor), same = alloc_struct(ucp);
	ic_cast_case("IC1 CCGParts as ISprite", ip_sprite, obj, obj, vtoff, 0);
	ic_cast_case("IC2 CASAnchor as ISprite", ip_sprite, other, -1, 0, 1);
	ic_cast_case("IC3 null as ISprite", ip_sprite, -1, -1, 0, 1);
	ic_cast_case("IC4 CUserComponentParts as itself", ip_same, same, same, 0, 0);
	/* Not a struct page: a string slot (heap type VM_STRING) and an array page. */
	int str_slot = heap_alloc_slot(VM_STRING);
	heap[str_slot].s = make_string("ISprite", 7);
	struct page *arr = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 2);
	arr->array.rank = 1; arr->array.struct_type = -1;
	int arr_slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(arr_slot, arr);
	ic_cast_case("IC7 string slot as ISprite", ip_sprite, str_slot, -1, 0, 1);
	ic_cast_case("IC8 array page as ISprite", ip_sprite, arr_slot, -1, 0, 1);
	heap_unref(obj); heap_unref(other); heap_unref(same); heap_unref(str_slot); heap_unref(arr_slot);
}

/* Runs fno as CALLFUNC (obj < 0) or CALLMETHOD and copies the returned slots. */
static int ic_call(int fno, int obj, int nargs, const int *args, int *ret, int maxret)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	if (obj >= 0) stack_push(obj);
	for (int i = 0; i < nargs; i++) stack_push(args[i]);
	if (obj >= 0) method_call(fno, VM_RETURN); else function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	int n = stack_ptr - sp;
	for (int i = 0; i < n && i < maxret; i++) ret[i] = stack[sp + i].i;
	IC_CHECK(call_stack_ptr == csp, "%s: call stack %d != %d", ia_fname(fno), call_stack_ptr, csp);
	IC_CHECK(n >= 1 && n <= maxret, "%s: returned %d slot(s), want 1..%d", ia_fname(fno), n, maxret);
	stack_ptr = sp;
	return n;
}

static void ic_get_sprite(void)
{
	probe_init_parts_table();
	extern struct static_library lib_PartsEngine;
	for (int i = 0; lib_PartsEngine.functions[i].name; i++) {
		if (!strcmp(lib_PartsEngine.functions[i].name, "_PostLink")) {
			((void (*)(void))lib_PartsEngine.functions[i].fun)();
			break;
		}
	}
	int st = ain_get_struct(ain, "AdvStand"), sprite_type = ain_get_struct(ain, "parts::detail::CSpriteParts");
	int cc = ain_get_struct(ain, "parts::detail::CCGParts"), isprite = ain_get_struct(ain, "ISprite");
	assert(st >= 0 && sprite_type >= 0 && cc >= 0 && isprite >= 0);
	int get1 = ic_fn("parts::detail::CSpriteParts@Get#1");
	int stand = vm_construct_struct(st);	/* NEW AdvStand: AFL_Parts_CreateSprite into m_parent */
	assert(stand > 0 && heap[stand].page && heap[stand].page->nr_vars == 6);
	int sprite = heap[stand].page->values[3].i;
	IC_CHECK(sprite > 0 && heap_index_valid(sprite) && heap[sprite].page && heap[sprite].page->index == sprite_type,
		 "IC5 AdvStand constructor left m_parent=%d", sprite);
	if (ic_failures) return;
	int vtoff = ic_vtoff(cc, isprite);
	/* Make state 1 a CG: GetCG#1(state) runs NEW CCGParts(number, state, sprite),
	 * whose constructor calls parts::detail::SetComponentType(number, 19, state);
	 * Get#1 then dispatches on GetComponentType == 19 to "GetCG(state) as ISprite". */
	int getcg = ic_fn("parts::detail::CSpriteParts@GetCG#1");
	int cg[2] = { -99, -99 }, state = 1;
	ic_call(getcg, sprite, 1, &state, cg, 2);
	IC_CHECK(cg[0] > 0 && heap_index_valid(cg[0]) && heap[cg[0]].page && heap[cg[0]].page->index == cc,
		 "IC5 GetCG#1(1) returned [%d, %d], want a CCGParts", cg[0], cg[1]);
	if (cg[0] > 0 && heap_index_valid(cg[0])) heap_unref(cg[0]);
	struct ain_type *rt = &ain->functions[get1].return_type;
	printf("IC5 Get#1 return type data=%d struc=%d inner=%d -> %d slot(s)\n", rt->data, rt->struc,
	       rt->array_type ? rt->array_type->data : -1, ain_return_slots_type(rt));
	int ret[2] = { -99, -99 }, arg = 1;
	ic_call(get1, sprite, 1, &arg, ret, 2);
	bool is_cg = ret[0] > 0 && heap_index_valid(ret[0]) && heap[ret[0]].type == VM_PAGE
		&& heap[ret[0]].page && heap[ret[0]].page->index == cc;
	IC_CHECK(is_cg && ret[1] == vtoff,
		 "IC5 CSpriteParts@Get#1(1) returned [%d, %d]: want [a CCGParts page, %d] (got %s)",
		 ret[0], ret[1], vtoff,
		 ret[0] == -1 ? "null: the cast was reported as failed" :
		 is_cg ? "the object, but the second slot is not the ISprite vtable offset" : "not a CCGParts");
	printf("IC5 CSpriteParts@Get#1(1) -> [%d, %d] (CCGParts %s, ISprite vtoff %d)\n", ret[0], ret[1], is_cg ? "yes" : "no", vtoff);
	if (is_cg) {
		/* IC9: an interface call through a cast value. ISprite has no methods,
		 * so cast the same CCGParts with the real X_ICAST ICGParts and call
		 * ICGParts' Core::get through <vtable>[offset + k]. */
		int icg = ain_get_struct(ain, "ICGParts");
		size_t ip_icg = ic_find(ic_fn("activityeditor::detail::CCopyProperty@CopyPropertySpriteCG"), X_ICAST, icg);
		int core_get = ic_fn("parts::detail::CCGParts@Core::get");
		IC_CHECK(icg >= 0 && ip_icg, "IC9 X_ICAST ICGParts not found");
		if (icg >= 0 && ip_icg) {
			int sp = stack_ptr;
			stack_push(ret[0]);
			ic_step(ip_icg);
			int off = stack_ptr == sp + 3 ? stack[sp + 1].i : -99, flag = stack_ptr == sp + 3 ? stack[sp + 2].i : -99;
			stack_ptr = sp;
			IC_CHECK(off == 0 && flag == 0, "IC9 CCGParts as ICGParts gave [%d, %d]", off, flag);
			int vt = heap[ret[0]].page->values[0].i, k = -1;
			if (off == 0 && vt > 0 && heap_index_valid(vt) && heap[vt].page)
				for (int i = off; i < heap[vt].page->nr_vars; i++)
					if (heap[vt].page->values[i].i == core_get) { k = i - off; break; }
			IC_CHECK(k >= 0, "IC9 CCGParts@Core::get not in <vtable> from offset %d", off);
			if (k >= 0) {
				int r2[2] = { -99, -99 };
				ic_call(core_get, ret[0], 0, NULL, r2, 2);
				bool ok = r2[0] > 0 && heap_index_valid(r2[0]) && heap[r2[0]].type == VM_PAGE && heap[r2[0]].page;
				IC_CHECK(ok, "IC9 Core::get through <vtable>[%d + %d] gave [%d, %d]", off, k, r2[0], r2[1]);
				printf("IC9 ICGParts cast [%d, %d, %d]; <vtable>[%d + %d] = CCGParts@Core::get -> [%d, %d] %s\n",
				       ret[0], off, flag, off, k, r2[0], r2[1], ok ? "ok" : "bad");
				if (ok) heap_unref(r2[0]);
			}
		}
		heap_unref(ret[0]);
	}
	heap_unref(stand);
}

static void ic_to_str(void)
{
	int anchor = ain_get_struct(ain, "CASAnchor");
	int fno = ic_fn("CASAnchor@ToString");
	assert(anchor >= 0 && ain->structures[anchor].nr_members == 4);
	int obj = alloc_struct(anchor);
	heap[obj].page->values[0].i = 1;	/* Top */
	heap[obj].page->values[3].i = 1;	/* Right */
	int ret[1] = { -99 };
	ic_call(fno, obj, 0, NULL, ret, 1);
	const char *text = ret[0] > 0 && heap_index_valid(ret[0]) && heap[ret[0]].type == VM_STRING && heap[ret[0]].s
		? heap[ret[0]].s->text : "(not a string)";
	IC_CHECK(!strcmp(text, "(true, false, false, true)"), "IC6 CASAnchor@ToString = \"%s\", want \"(true, false, false, true)\"", text);
	printf("IC6 CASAnchor@ToString -> %s\n", text);
	heap_unref(obj);
}

static int ic_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		ic_failures = 0;
		fn();
		fflush(stdout);
		_exit(ic_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	printf("%s %s%s\n", name, ok ? "PASS" : "FAIL", WIFSIGNALED(status) ? " (crashed)" : "");
	fflush(stdout);
	return ok ? 0 : 1;
}

static int test_icast(void)
{
	int failed = 0;
	failed += ic_run("IC1-IC4, IC7-IC8 X_ICAST single-step on real instructions", ic_single_step);
	failed += ic_run("IC5, IC9 CSpriteParts@Get#1 end to end and an interface call through the value", ic_get_sprite);
	failed += ic_run("IC6 X_TO_STR 47 through CASAnchor@ToString", ic_to_str);
	printf("ICAST summary: %d/3 group(s) failed\n", failed);
	return failed ? 91 : 0;
}

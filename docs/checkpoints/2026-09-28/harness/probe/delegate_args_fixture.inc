/* delegate-args: a delegate call copies exactly one stack slot into each
 * argument variable of the callee, as a CALLFUNC does.
 *
 * In v14 a two-slot argument (interface [object, vtable offset], option
 * [value, discriminant], ref int [page, index], ...) occupies two variables:
 * the argument itself and a void companion that holds its second slot. The
 * delegate's nr_arguments counts the companion too, and DG_CALLBEGIN leaves
 * exactly delegate_param_slots() == nr_arguments slots on the stack.
 * The original (0x66dce0, the DG_CALL handler) takes nr_arguments values off
 * the stack (vector sized by [delegate+0x4c], copy loop at 0x66df46) and
 * hands them to 0x66a0e0 -> 0x657430, which stores value i in variable i for
 * i < the callee's nr_args, retaining by the type of variable i; a void
 * companion is stored as is. Upstream xsystem4 did the same
 * (values[i] = stack_peek(nr_arguments + 1 - i)).
 *
 * Before the fix delegate_call copied both slots of a two-slot argument and
 * then visited the companion variable as one more argument: every later
 * variable received the next slot, and the last one read past the arguments
 * into the delegate page slot DG_CALLBEGIN put above them. So:
 *   - the first local after the arguments was overwritten with the delegate
 *     page slot (its initial value leaked), and a DELETE/.LOCALDELETE or the
 *     page teardown then released the delegate page;
 *   - arguments after a two-slot argument were retained by the type of the
 *     previous variable: a wrap after an option was stored as "void" (no
 *     retain, but released on return) and the int after it was retained as a
 *     wrap (heap_ref on an integer).
 *
 * Cases (each in its own fork):
 *   DA0 Every delegate of the AIN has delegate_param_slots() == nr_arguments,
 *       i.e. the one-slot-per-variable copy covers exactly what DG_CALLBEGIN
 *       pushed. (Holds before the fix too; guards the assumption.)
 *   DA1 DG_Func<ref IRectParts, IParts&>(IRectParts) invoking the real lambda
 *       <lambda : Tutorial::MoveSceneParent(ICGParts&, bool)(31, 79)> (the
 *       selector Tutorial::MoveSceneParent hands to ArrayExtensions::Select)
 *       with a real rect from AFL_Parts_CreateRect. Right after the frame is
 *       set up, local 2 (<dummy : IRectParts@Core::get>, wrap) must still hold
 *       its own initial value, not the delegate page slot. After the call the
 *       delegate page must be alive with its refcount unchanged, the rect's
 *       refcount unchanged and the returned IParts pair non-null.
 *   DA2 The real ArrayExtensions::Select<IParts&, IRectParts&> over four real
 *       rects, handed a new delegate page of that lambda the way
 *       Tutorial::MoveSceneParent does (DG_NEW_FROM_METHOD, CALLFUNC): the
 *       selector must run four times and all four results must be live IParts.
 *   DA3 DG_Function<int, Worker&?, SpecialCustomer&, int>
 *       (option<wrap<Worker>>, void, wrap<SpecialCustomer>, int), the delegate
 *       SpecialCustomerEvent@GetIncome builds from EX, invoking the only
 *       function of that signature (EX "control/.../income/..." income
 *       function, found by signature) with no worker: it must return
 *       (Income + 1) * 50 * -2, leave the borrowed SpecialCustomer's refcount
 *       unchanged, and must not retain the int argument as a heap slot (the
 *       int is set to the slot of an unrelated live object to observe it).
 *
 * Only static VM functions that exist before the fix are used (delegate_call,
 * function_call, delegate_param_slots, vm_execute), so before-check.sh can
 * build it. Needs no GL or assets; DA1/DA2 register PartsEngine like IA4.
 */
#include <sys/wait.h>
#include <unistd.h>
extern void probe_init_parts_table(void);

static int da_failures;
#define DA_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); da_failures++; } } while (0)

static int da_fn(const char *name)
{
	int f = ain_get_function(ain, (char *)name);
	if (f < 0) { printf("DELEGATE-ARGS function %s not declared\n", name); exit(2); }
	return f;
}

static int da_delegate(const char *name)
{
	for (int i = 0; i < ain->nr_delegates; i++)
		if (!strcmp(ain->delegates[i].name, name))
			return i;
	printf("DELEGATE-ARGS delegate %s not declared\n", name);
	exit(2);
}

static bool da_live_page(int slot)
{
	return slot > 0 && heap_index_valid(slot) && HEAP_REF(slot) > 0 && heap[slot].type == VM_PAGE && heap[slot].page;
}

static void da_parts_init(void)
{
	probe_init_parts_table();
	extern struct static_library lib_PartsEngine;
	for (int i = 0; lib_PartsEngine.functions[i].name; i++) {
		if (!strcmp(lib_PartsEngine.functions[i].name, "_PostLink")) {
			((void (*)(void))lib_PartsEngine.functions[i].fun)();
			break;
		}
	}
}

/* CALLFUNC fno; copies up to 2 result slots to ret and returns how many there were. */
static int da_callfunc(int fno, int nargs, const int *args, union vm_value *ret)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	for (int i = 0; i < nargs; i++)
		stack_push(args[i]);
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	DA_CHECK(call_stack_ptr == csp, "%s: call stack %d != %d", ain->functions[fno].name, call_stack_ptr, csp);
	int n = stack_ptr - sp;
	for (int i = 0; i < n && i < 2; i++)
		ret[i] = stack[sp + i];
	stack_ptr = sp;
	return n;
}

/* AFL_Parts_CreateRect(0, none): a real CRectParts as IRectParts [object, vtable offset]. */
static void da_create_rect(int *obj, int *vtoff)
{
	int args[] = { 0, -1, 1 };	/* partsNumber 0 = free system number, option<string> comment = none */
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	int n = da_callfunc(da_fn("AFL_Parts_CreateRect"), 3, args, r);
	*obj = r[0].i;
	*vtoff = r[1].i;
	DA_CHECK(n == 2 && da_live_page(*obj), "AFL_Parts_CreateRect returned %d slot(s) [%d, %d]", n, *obj, *vtoff);
}

/* The stack DG_CALLBEGIN leaves: [argument slots, dg page, index 0, return placeholders].
 * delegate_call returns to instr_ptr + width(DG_CALL); point that at VM_RETURN.
 * inspect (optional) sees the callee's local page after the arguments were copied. */
static void da_dgcall(int dg, int dgslot, int nslots, const int *slots, void (*inspect)(struct page *),
		      union vm_value *ret, int *nret)
{
	int ret_slots = delegate_return_slots(&ain->delegates[dg].return_type);
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	for (int i = 0; i < nslots; i++)
		stack_push(slots[i]);
	stack_push(dgslot);
	stack_push(0);
	for (int i = 0; i < ret_slots; i++)
		stack_push(0);
	instr_ptr = VM_RETURN - instruction_width(DG_CALL);
#ifdef DELEGATE_CALL_STATE_IN_PAGE	/* v14: DG_CALLBEGIN also starts the call in the delegate (0x66daf3) */
	delegate_call_begin(heap_get_delegate_page(dgslot));
#endif
	delegate_call(dg, 0);
	DA_CHECK(call_stack_ptr == csp + 1, "%s: delegate_call did not enter the callee (csp %d)", ain->delegates[dg].name, call_stack_ptr);
	if (call_stack_ptr == csp + 1) {
		if (inspect)
			inspect(heap[call_stack[call_stack_ptr-1].page_slot].page);
		vm_execute();
	}
	instr_ptr = ip;
	DA_CHECK(call_stack_ptr == csp && stack_ptr == sp + nslots + 2 + ret_slots && stack[sp + nslots + 1].i == 1,
		 "%s frame: csp=%d/%d sp=%d/%d index=%d", ain->delegates[dg].name, call_stack_ptr, csp,
		 stack_ptr, sp + nslots + 2 + ret_slots, stack_ptr > sp + nslots + 1 ? stack[sp + nslots + 1].i : -99);
	*nret = ret_slots;
	for (int i = 0; i < ret_slots && i < 2; i++)
		ret[i] = stack_ptr >= sp + nslots + 2 + ret_slots ? stack[sp + nslots + 2 + i] : (union vm_value){ .i = -99 };
	stack_ptr = sp;
}

static void da_slots_match(void)
{
	int bad = 0, two_slot = 0;
	for (int d = 0; d < ain->nr_delegates; d++) {
		struct ain_function_type *dg = &ain->delegates[d];
		bool has2 = false;
		for (int i = 0; i < dg->nr_arguments; i++)
			has2 |= delegate_arg_is_2slot(&dg->variables[i].type);
		two_slot += has2;
		if (delegate_param_slots(dg) != dg->nr_arguments) {
			if (bad++ < 5)
				printf("  delegate %d %s: %d stack slots, %d argument variables\n",
				       d, dg->name, delegate_param_slots(dg), dg->nr_arguments);
		}
	}
	printf("DA0 %d delegates, %d with a two-slot argument, %d slot/variable mismatches\n", ain->nr_delegates, two_slot, bad);
	DA_CHECK(bad == 0, "DA0 %d delegate(s) whose DG_CALLBEGIN slot count differs from nr_arguments", bad);
}

static int da_lambda_fno, da_selector_dg, da_seen_local2, da_seen_local3;
static bool da_local2_fresh;

static void da_lambda_setup(void)
{
	da_parts_init();
	da_lambda_fno = da_fn("<lambda : Tutorial::MoveSceneParent(ICGParts&, bool)(31, 79)>");
	da_selector_dg = da_delegate("DG_Func<ref IRectParts, IParts&>");
	struct ain_function *f = &ain->functions[da_lambda_fno];
	struct ain_function_type *dg = &ain->delegates[da_selector_dg];
	assert(f->nr_args == 2 && f->nr_vars == 4 && f->vars[0].type.data == AIN_IFACE && f->vars[1].type.data == AIN_VOID
	       && f->vars[2].type.data == AIN_WRAP && f->vars[3].type.data == AIN_VOID);
	assert(dg->nr_arguments == 2 && dg->variables[0].type.data == AIN_IFACE && dg->variables[1].type.data == AIN_VOID
	       && dg->return_type.data == AIN_WRAP);
}

static void da_inspect_lambda(struct page *local)
{
	da_seen_local2 = local && local->nr_vars > 2 ? local->values[2].i : -99;
	da_seen_local3 = local && local->nr_vars > 3 ? local->values[3].i : -99;
	/* _function_call gives a wrap local a fresh one-value STRUCT_PAGE (variable_initval). */
	da_local2_fresh = da_live_page(da_seen_local2) && HEAP_REF(da_seen_local2) == 1
		&& heap[da_seen_local2].page->type == STRUCT_PAGE && heap[da_seen_local2].page->index == -1;
}

/* The returned IParts pair is owned by the caller (SP_INC in the lambda). */
static bool da_release_iparts(union vm_value *r)
{
	bool ok = da_live_page(r[0].i) && r[1].i >= 0;
	if (ok)
		heap_unref(r[0].i);
	return ok;
}

static void da_lambda(void)
{
	da_lambda_setup();
	int obj, vtoff;
	da_create_rect(&obj, &vtoff);
	if (da_failures)
		return;
	unsigned obj_seq = heap[obj].seq;
	int obj_ref = HEAP_REF(obj);
	int dgslot = heap_alloc_page(delegate_new_from_method(-1, da_lambda_fno));
	unsigned dg_seq = heap[dgslot].seq;
	int slots[] = { obj, vtoff };
	union vm_value r[2];
	int nret = 0;
	da_seen_local2 = da_seen_local3 = -99;
	da_dgcall(da_selector_dg, dgslot, 2, slots, da_inspect_lambda, r, &nret);
	printf("DA1 lambda locals at entry: [2]=%d [3]=%d (delegate page slot %d); returned [%d, %d]\n",
	       da_seen_local2, da_seen_local3, dgslot, r[0].i, r[1].i);
	DA_CHECK(da_seen_local2 != dgslot && da_local2_fresh,
		 "DA1 local 2 <dummy : IRectParts@Core::get> = %d at entry, want its own fresh wrap page%s", da_seen_local2,
		 da_seen_local2 == dgslot ? " (it is the delegate page slot)" : "");
	bool dg_alive = da_live_page(dgslot) && heap[dgslot].seq == dg_seq;
	DA_CHECK(dg_alive && HEAP_REF(dgslot) == 1,
		 "DA1 delegate page slot %d: ref=%d (want 1)%s", dgslot, HEAP_REF(dgslot), dg_alive ? "" : ", released");
	DA_CHECK(heap[obj].seq == obj_seq && HEAP_REF(obj) == obj_ref,
		 "DA1 borrowed rect ref=%d (want %d)", HEAP_REF(obj), obj_ref);
	DA_CHECK(nret == 2 && da_release_iparts(r), "DA1 returned IParts [%d, %d] is not a live object", r[0].i, r[1].i);
	if (dg_alive)
		heap_unref(dgslot);
}

static void da_select(void)
{
	da_lambda_setup();
	enum { N = 4 };
	int objs[N], vtoffs[N];
	for (int i = 0; i < N; i++)
		da_create_rect(&objs[i], &vtoffs[i]);
	if (da_failures)
		return;
	/* ref array<wrap<iwrap<IRectParts>>>: a generic array of two-slot elements (X_A_INIT 1). */
	struct page *arr = alloc_page(ARRAY_PAGE, AIN_ARRAY, 2 * N);
	arr->array.struct_type = 2;
	for (int i = 0; i < N; i++) {
		arr->values[2*i].i = objs[i];
		arr->values[2*i+1].i = vtoffs[i];
		heap_ref(objs[i]);	/* the array's own reference */
	}
	int arr_slot = heap_alloc_page(arr);
	/* Tutorial::MoveSceneParent: PUSH -1, PUSH fno, DG_NEW_FROM_METHOD, CALLFUNC Select.
	 * The new delegate page is handed to Select; nothing else holds it. */
	int dgslot = heap_alloc_page(delegate_new_from_method(-1, da_lambda_fno));
	int select = da_fn("ArrayExtensions::Select<IParts&, IRectParts&>");
	unsigned long entered = calls[da_lambda_fno];
	int args[] = { arr_slot, dgslot };
	union vm_value r[2] = {{ .i = -1 }, { .i = -1 }};
	int n = da_callfunc(select, 2, args, r);
	entered = calls[da_lambda_fno] - entered;
	int res = n >= 1 ? r[0].i : -1;
	struct page *rp = da_live_page(res) ? heap[res].page : NULL;
	int live = 0, count = rp ? rp->nr_vars : -1;
	if (rp) {
		int stride = rp->nr_vars == 2 * N ? 2 : 1;
		for (int i = 0; i < rp->nr_vars; i += stride)
			live += da_live_page(rp->values[i].i);
	}
	printf("DA2 Select over %d rects: selector entered %lu time(s); result slot %d with %d value(s), %d live IParts\n",
	       N, entered, res, count, live);
	DA_CHECK(entered == N, "DA2 the selector ran %lu time(s) for %d elements (the delegate page was released mid-loop)", entered, N);
	DA_CHECK(rp && live == N, "DA2 Select over %d rects produced %d live IParts (result slot %d, %d value(s))", N, live, res, count);
}

static void da_income(void)
{
	int dg = da_delegate("DG_Function<int, Worker&?, SpecialCustomer&, int>");
	struct ain_function_type *d = &ain->delegates[dg];
	assert(d->nr_arguments == 4 && d->variables[0].type.data == AIN_OPTION && d->variables[1].type.data == AIN_VOID
	       && d->variables[2].type.data == AIN_WRAP && d->variables[3].type.data == AIN_INT);
	int sc_type = ain_get_struct(ain, "SpecialCustomer"), worker_type = ain_get_struct(ain, "Worker");
	assert(sc_type >= 0 && worker_type >= 0);
	/* The EX income function has the delegate's exact signature; it is the only one. */
	int fno = -1, matches = 0;
	for (int i = 0; i < ain->nr_functions; i++) {
		struct ain_function *f = &ain->functions[i];
		if (f->nr_args != 4 || f->return_type.data != AIN_INT)
			continue;
		const struct ain_type *t0 = &f->vars[0].type, *t2 = &f->vars[2].type;
		if (t0->data == AIN_OPTION && t0->array_type && t0->array_type->data == AIN_WRAP
		    && t0->array_type->array_type && t0->array_type->array_type->struc == worker_type
		    && f->vars[1].type.data == AIN_VOID && t2->data == AIN_WRAP && t2->array_type
		    && t2->array_type->struc == sc_type && f->vars[3].type.data == AIN_INT) {
			fno = i;
			matches++;
		}
	}
	DA_CHECK(matches == 1, "DA3 %d function(s) with the delegate's signature", matches);
	if (matches != 1)
		return;
	int customer = alloc_struct(sc_type);
	assert(customer > 0 && heap[customer].page && heap[customer].page->nr_vars >= 5);
	heap[customer].page->values[4].i = 3;	/* Income */
	int bystander = alloc_struct(sc_type);	/* an unrelated live object; its slot is passed as the int */
	unsigned cseq = heap[customer].seq, bseq = heap[bystander].seq;
	int cref = HEAP_REF(customer), bref = HEAP_REF(bystander);
	int dgslot = heap_alloc_page(delegate_new_from_method(-1, fno));
	/* [option<wrap<Worker>> none = (-1, 1), wrap<SpecialCustomer>, matchCount] */
	int slots[] = { -1, 1, customer, bystander };
	union vm_value r[2];
	int nret = 0;
	da_dgcall(dg, dgslot, 4, slots, NULL, r, &nret);
	printf("DA3 income fno %d returned %d; customer ref %d -> %d, int argument slot ref %d -> %d\n",
	       fno, r[0].i, cref, HEAP_REF(customer), bref, HEAP_REF(bystander));
	DA_CHECK(nret == 1 && r[0].i == -400, "DA3 income = %d, want (3 + 1) * 50 * -2 = -400", r[0].i);
	bool alive = heap[customer].seq == cseq && HEAP_REF(customer) > 0;
	DA_CHECK(alive && HEAP_REF(customer) == cref,
		 "DA3 borrowed SpecialCustomer ref=%d (want %d)%s", HEAP_REF(customer), cref, alive ? "" : ", released");
	DA_CHECK(heap[bystander].seq == bseq && HEAP_REF(bystander) == bref,
		 "DA3 the int argument was retained as a heap slot: slot %d ref %d (want %d)", bystander, HEAP_REF(bystander), bref);
	heap_unref(dgslot);
}

static int da_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		da_failures = 0;
		fn();
		fflush(stdout);
		_exit(da_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	printf("%s %s%s\n", name, ok ? "PASS" : "FAIL", WIFSIGNALED(status) ? " (crashed)" : "");
	fflush(stdout);
	return ok ? 0 : 1;
}

static int test_delegate_args(void)
{
	int failed = 0;
	failed += da_run("DA0 delegate stack slots equal argument variables", da_slots_match);
	failed += da_run("DA1 interface argument: lambda locals and delegate page", da_lambda);
	failed += da_run("DA2 ArrayExtensions::Select with the Tutorial selector", da_select);
	failed += da_run("DA3 option argument followed by wrap and int (income function)", da_income);
	printf("DELEGATE-ARGS summary: %d/4 case(s) failed\n", failed);
	return failed ? 84 : 0;
}

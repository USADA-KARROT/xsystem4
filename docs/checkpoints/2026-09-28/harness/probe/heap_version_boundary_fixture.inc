/* heap-version-boundary (2026-10-14).
 *
 * Synthetic regression for the conservative v11 format boundary. Upstream's
 * older VM is the reference for the <11 path; v11-v13 preserve this branch's
 * behavior at f23883c (before C), and v14 keeps its existing ownership rules. No v11-v13
 * game is available, and these cases do not establish native semantics or
 * prove the output of every older compiler. All declarations, instructions,
 * text and pages below are generated here, not copied from a game.
 *
 * Requires v13_heap_fixture.inc for the synthetic slot/string helpers.
 */

static int hb_failures;
#define HB_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); hb_failures++; } } while (0)

static const int hb_versions[] = { 5, 10, 11, 12, 13, 14 };

struct hb_program {
	struct ain *saved_ain;
	uint8_t *saved_flags;
	size_t saved_ip;
	struct ain model;
	struct ain_function funcs[2];
	struct ain_variable vars[2];
	uint8_t code[32], flags[2];
};

static void hb_emit(struct hb_program *p, unsigned address, enum opcode op, unsigned arg)
{
	p->code[address] = op & 0xff;
	p->code[address + 1] = op >> 8;
	for (unsigned i = 0; i < 4; i++)
		p->code[address + 2 + i] = (arg >> (8 * i)) & 0xff;
}

static void hb_begin(struct hb_program *p, int version, enum ain_data_type type)
{
	memset(p, 0, sizeof(*p));
	p->saved_ain = ain;
	p->saved_flags = func_flags;
	p->saved_ip = instr_ptr;
	for (int f = 0; f < 2; f++) {
		p->vars[f] = (struct ain_variable) { .name = "value", .type = AIN_MKTYPE(type, -1, 0) };
		p->funcs[f] = (struct ain_function) {
			.name = f ? "synthetic_callee" : "synthetic_owner", .address = 14,
			.return_type = AIN_VOID_TYPE, .nr_args = 1, .nr_vars = 1, .vars = &p->vars[f],
		};
	}
	p->model = (struct ain) {
		.version = version, .code = p->code, .code_size = sizeof(p->code),
		.nr_functions = 2, .functions = p->funcs,
	};
	hb_emit(p, 0, X_REF, 1);
	hb_emit(p, 6, CALLFUNC, 1);
	hb_emit(p, 14, RETURN, 0);
	ain = &p->model;
	func_flags = p->flags;
	function_call_args_borrowed = false;
}

static void hb_end(struct hb_program *p)
{
	ain = p->saved_ain;
	func_flags = p->saved_flags;
	instr_ptr = p->saved_ip;
}

static int hb_refs(int slot, uint32_t seq)
{
	return vh_alive(slot, seq) ? HEAP_REF(slot) : 0;
}

/* Cleanup follows assertions; never normalize counts before observing them. */
static void hb_release(int slot, uint32_t seq)
{
	while (vh_alive(slot, seq))
		heap_unref(slot);
}

static void hb_step(unsigned address)
{
	instr_ptr = address;
	execute_instruction(get_opcode(address));
}

/* HB1: M1's exact causal path. X_REF borrows the caller's only string slot;
 * CALLFUNC must retain it, and RETURN releases only the callee's ownership. */
static void hb_xref_string(void)
{
	for (int version = 11; version <= 14; version++) {
		struct hb_program p;
		hb_begin(&p, version, AIN_STRING);
		size_t live = live_slots();
		int sp = stack_ptr, csp = call_stack_ptr;
		int s = vh_string("caller-string");
		uint32_t seq = heap[s].seq;
		struct page *owner = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 1);
		owner->array.struct_type = -1;
		owner->array.rank = 1;
		owner->values[0].i = s;
		int caller = heap_alloc_page(owner);
		stack_push(0x12345);
		stack_push(caller);
		stack_push(0);
		hb_step(0);
		HB_CHECK(stack_ptr == sp + 2 && stack_peek(0).i == s && hb_refs(s, seq) == 1,
			"v%d X_REF must leave the caller's one reference and push its slot", version);
		hb_step(6);
		int local = call_stack[call_stack_ptr - 1].page_slot;
		uint32_t lseq = heap[local].seq;
		HB_CHECK(call_stack_ptr == csp + 1 && instr_ptr == 14 && stack_ptr == sp + 1
			&& heap[local].page->values[0].i == s && heap[s].ref == 2,
			"v%d CALLFUNC: string count word %#x, want 2; stack delta %d, want 1",
			version, (unsigned)heap[s].ref, stack_ptr - sp);
		hb_step(14);
		HB_CHECK(call_stack_ptr == csp && instr_ptr == 12 && stack_ptr == sp + 1 && stack_peek(0).i == 0x12345
			&& !vh_alive(local, lseq), "v%d RETURN did not restore the caller's stack/frame or free the local page", version);
		HB_CHECK(owner->values[0].i == s && hb_refs(s, seq) == 1 && heap[s].ref == 1
			&& heap[s].type == VM_STRING && heap[s].s && !strcmp(heap[s].s->text, "caller-string"),
			"v%d RETURN lost the caller's string (same-generation refs %d, want 1)", version, hb_refs(s, seq));
		stack_pop();
		/* On a failing old VM, do not finalize a dead slot again. */
		if (!vh_alive(s, seq))
			owner->values[0].i = -1;
		heap_unref(caller);
		HB_CHECK(!vh_alive(s, seq) && live_slots() == live, "v%d caller teardown left live slots", version);
		hb_end(&p);
	}
	puts("HB1 synthetic X_REF -> CALLFUNC -> RETURN: v11/12/13/14 preserve caller string, generation and stack");
}

/* HB2: functype initialization, both finalizers, and initialized array teardown. */
static void hb_functype(void)
{
	for (unsigned k = 0; k < sizeof(hb_versions) / sizeof(hb_versions[0]); k++) {
		int version = hb_versions[k];
		struct hb_program p;
		hb_begin(&p, version, AIN_FUNC_TYPE);
		size_t live = live_slots();
		bool counted = version >= 11;
		union vm_value v = variable_initval(AIN_FUNC_TYPE);
		if (counted) {
			HB_CHECK(v.i > 1 && heap_index_valid(v.i) && heap[v.i].type == VM_PAGE && !heap[v.i].page
				&& HEAP_REF(v.i) == 1 && live_slots() == live + 1,
				"v%d functype initial value must be one empty page slot (got %d)", version, v.i);
		} else {
			HB_CHECK(v.i == 0 && live_slots() == live, "v%d functype initial value must be function number 0", version);
		}
		variable_fini(v, AIN_FUNC_TYPE, true);
		HB_CHECK(live_slots() == live, "v%d functype initial value leaked on finalization", version);
		int w = vh_witness();
		uint32_t seq = heap[w].seq;
		heap_ref(w);
		heap_ref(w);
		variable_fini((union vm_value) { .i = w }, AIN_FUNC_TYPE, true);
		HB_CHECK(hb_refs(w, seq) == (counted ? 2 : 3), "v%d functype destructor finalizer: refs %d", version, hb_refs(w, seq));
		variable_fini((union vm_value) { .i = w }, AIN_FUNC_TYPE, false);
		HB_CHECK(hb_refs(w, seq) == (counted ? 1 : 3), "v%d functype exit finalizer: refs %d", version, hb_refs(w, seq));
		hb_release(w, seq);
		union vm_value dim = { .i = 3 };
		struct page *a = alloc_array(1, &dim, AIN_ARRAY_FUNC_TYPE, -1, false);
		bool good = a && a->nr_vars == 3;
		for (int i = 0; good && i < 3; i++) {
			int n = a->values[i].i;
			good = counted ? n > 1 && heap_index_valid(n) && HEAP_REF(n) == 1 && heap[n].type == VM_PAGE && !heap[n].page
				: n == 0;
		}
		HB_CHECK(good && live_slots() == live + (counted ? 3 : 0), "v%d functype array initial values/counts mismatch", version);
		heap_unref(heap_alloc_page(a));
		HB_CHECK(live_slots() == live, "v%d functype array teardown left live slots", version);
		hb_end(&p);
	}
	puts("HB2 functype init/fini and array teardown: v5/10 scalar, v11/12/13/14 counted");
}

/* Assignment uses distinct old/new generations. For v14 X_ASSIGN, prepare
 * ownership explicitly: release old, retain new for the receiving page, and
 * reuse old's slot as an unrelated witness before executing the instruction. */
static void hb_assignment_case(int version, enum opcode op, enum ain_data_type type, bool self)
{
	struct hb_program p;
	hb_begin(&p, version, type);
	hb_emit(&p, 0, op, 1);
	size_t live = live_slots();
	int sp = stack_ptr;
	int old = type == AIN_STRING ? vh_string("old") : vh_witness();
	uint32_t old_seq = heap[old].seq;
	int value = self ? old : type == AIN_STRING ? vh_string("new") : vh_witness();
	uint32_t value_seq = heap[value].seq;
	struct page *owner = alloc_page(LOCAL_PAGE, 0, 1);
	owner->local.struct_ptr = -1;
	owner->values[0].i = old;
	int owner_slot = heap_alloc_page(owner), recycled = -1;
	uint32_t recycled_seq = 0;
	bool prepared = op == X_ASSIGN && version >= 14;
	bool counted = op == X_ASSIGN ? version < 14 : version >= 11;
	if (prepared) {
		/* Retain first permits the same-slot form too. */
		heap_ref(value);
		heap_unref(old);
		if (!self) {
			recycled = vh_witness();
			recycled_seq = heap[recycled].seq;
			HB_CHECK(recycled == old && recycled_seq != old_seq, "v14 X_ASSIGN setup did not reuse old's released slot");
		}
	}
	int before_new = hb_refs(value, value_seq), before_old = hb_refs(old, old_seq);
	stack_push(0x2468);
	stack_push(owner_slot);
	stack_push(0);
	stack_push(value);
	hb_step(0);
	int want_new = before_new + (counted && !self ? 1 : 0);
	int want_old = before_old - (counted && !self ? 1 : 0);
	int result_sp = stack_ptr;
	int result = stack_pop().i, sentinel = stack_pop().i;
	HB_CHECK(result_sp == sp + 2 && result == value && sentinel == 0x2468
		&& owner->values[0].i == value && hb_refs(value, value_seq) == want_new && hb_refs(old, old_seq) == want_old,
		"v%d op %d type %d self %d: new/old refs %d/%d, want %d/%d; assignment/stack mismatch",
		version, op, type, self, hb_refs(value, value_seq), hb_refs(old, old_seq), want_new, want_old);
	if (recycled >= 0)
		HB_CHECK(hb_refs(recycled, recycled_seq) == 1, "v14 X_ASSIGN released the unrelated occupant of old's slot");
	heap_unref(owner_slot);
	int want_after = want_new - (type == AIN_FUNC_TYPE && version < 11 ? 0 : 1);
	HB_CHECK(hb_refs(value, value_seq) == want_after,
		"v%d op %d type %d self %d: page teardown left %d new-value refs, want %d",
		version, op, type, self, hb_refs(value, value_seq), want_after);
	if (!self)
		HB_CHECK(hb_refs(old, old_seq) == want_old,
			"v%d op %d type %d: page teardown changed old-value refs", version, op, type);
	hb_release(value, value_seq);
	if (!self)
		hb_release(old, old_seq);
	if (recycled >= 0)
		hb_release(recycled, recycled_seq);
	HB_CHECK(stack_ptr == sp && live_slots() == live, "v%d assignment cleanup stack/live-slot mismatch", version);
	hb_end(&p);
}

static void hb_assign(void)
{
	static const enum ain_data_type types[] = { AIN_STRING, AIN_STRUCT, AIN_DELEGATE, AIN_FUNC_TYPE, AIN_ARRAY_INT };
	for (unsigned k = 0; k < sizeof(hb_versions) / sizeof(hb_versions[0]); k++)
		for (unsigned t = 0; t < sizeof(types) / sizeof(types[0]); t++)
			for (int self = 0; self < 2; self++) {
				hb_assignment_case(hb_versions[k], ASSIGN, types[t], self);
				hb_assignment_case(hb_versions[k], F_ASSIGN, types[t], self);
			}
	puts("HB3 ASSIGN/F_ASSIGN old/new references, self-assignment, generations and page teardown across v5/10/11/12/13/14");
}

static void hb_xassign(void)
{
	static const enum ain_data_type types[] = { AIN_STRING, AIN_STRUCT, AIN_DELEGATE, AIN_FUNC_TYPE, AIN_ARRAY_INT };
	for (int version = 11; version <= 14; version++)
		for (unsigned t = 0; t < sizeof(types) / sizeof(types[0]); t++)
			for (int self = 0; self < 2; self++)
				hb_assignment_case(version, X_ASSIGN, types[t], self);
	puts("HB4 X_ASSIGN v11/12/13 retain/release (including functype); v14 transfers prepared ownership; no pre-v11 X instruction");
}

/* HB5: callee entry and RETURN, including marked strings and borrowed guards.
 * Generic array/wrap cases at older version numbers guard this branch's
 * existing dispatch only; they are not claims about those file formats. */
static void hb_arguments(void)
{
	static const struct { enum ain_data_type type; const char *name; bool value; bool old_array; } types[] = {
		{ AIN_STRUCT, "struct", true, false }, { AIN_DELEGATE, "delegate", true, false },
		{ AIN_ARRAY, "generic-array", true, false }, { AIN_ARRAY_INT, "int-array", true, true },
		{ AIN_ARRAY_STRUCT, "struct-array", true, true },
		{ AIN_REF_STRUCT, "ref-struct", false, false }, { AIN_REF_ARRAY_INT, "ref-array", false, false },
		{ AIN_WRAP, "wrap", false, false }, { AIN_STRING, "string", false, false },
	};
	for (unsigned k = 0; k < sizeof(hb_versions) / sizeof(hb_versions[0]); k++) {
		int version = hb_versions[k];
		for (unsigned t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
			for (int borrowed = 0; borrowed < 2; borrowed++) {
				for (int marked = 0; marked <= (types[t].type == AIN_STRING); marked++) {
					struct hb_program p;
					hb_begin(&p, version, types[t].type);
					size_t live = live_slots();
					int sp = stack_ptr, csp = call_stack_ptr;
					stack_push(0x1357);
					int w;
					if (marked) {
						stack_push_string(make_string("temporary", 9));
						w = stack_peek(0).i;
					} else {
						w = types[t].type == AIN_STRING ? vh_string("owned") : vh_witness();
						stack_push(w);
					}
					uint32_t seq = heap[w].seq;
					int want = 2;
					if (types[t].type == AIN_STRING)
						want = marked || version < 11 ? 1 : 2;
					else if (types[t].value)
						want = version < 11 ? 1 : version < 14 || types[t].old_array ? 2 : 1 + borrowed;
					function_call_args_borrowed = borrowed;
					hb_step(6);
					int local = call_stack[call_stack_ptr - 1].page_slot;
					uint32_t lseq = heap[local].seq;
					HB_CHECK(heap[w].ref == want && stack_ptr == sp + 1 && call_stack_ptr == csp + 1
						&& !function_call_args_borrowed && heap[local].page->values[0].i == w,
						"v%d %s borrowed=%d marked=%d: entry count word %#x, want %d, or bad frame/stack",
						version, types[t].name, borrowed, marked, (unsigned)heap[w].ref, want);
					hb_step(14);
					int result_sp = stack_ptr, sentinel = stack_pop().i;
					HB_CHECK(hb_refs(w, seq) == want - 1 && !vh_alive(local, lseq) && call_stack_ptr == csp
						&& instr_ptr == 12 && result_sp == sp + 1 && sentinel == 0x1357,
						"v%d %s borrowed=%d marked=%d: return refs %d, want %d, or bad stack/frame",
						version, types[t].name, borrowed, marked, hb_refs(w, seq), want - 1);
					hb_release(w, seq);
					HB_CHECK(live_slots() == live && stack_ptr == sp, "v%d %s cleanup left slots/stack", version, types[t].name);
					hb_end(&p);
				}
			}
		}
	}
	puts("HB5 value arguments, ref-struct/ref-array/wrap guards and marked/unmarked strings: entry, return and stack counts");
}

/* HB6: exit_unref's VM_STRING payload may be NULL, with or without TEMP. */
static void hb_null_string(void)
{
	for (int marked = 0; marked < 2; marked++) {
		size_t live = live_slots();
		int s = heap_alloc_slot(VM_STRING);
		heap[s].s = NULL;
		uint32_t seq = heap[s].seq;
		if (marked)
			heap[s].ref |= HEAP_TEMP_FLAG;
		exit_unref(s);
		HB_CHECK(!vh_alive(s, seq) && heap[s].ref == 0, "NULL string marked=%d was not released", marked);
		int reused = heap_alloc_slot(VM_STRING);
		heap[reused].s = NULL;
		HB_CHECK(reused == s && heap[reused].seq != seq, "NULL string marked=%d slot not returned to free list", marked);
		heap_unref(reused);
		HB_CHECK(live_slots() == live, "NULL string marked=%d left live slots", marked);
	}
	puts("HB6 exit_unref frees plain and TEMP-marked NULL string slots");
}

static int test_heap_version_boundary(void)
{
	static const struct { const char *name; void (*run)(void); } cases[] = {
		{ "HB1", hb_xref_string }, { "HB2", hb_functype }, { "HB3", hb_assign },
		{ "HB4", hb_xassign }, { "HB5", hb_arguments }, { "HB6", hb_null_string },
	};
	int failed = 0, environment = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			hb_failures = 0;
			cases[i].run();
			fflush(stdout);
			_exit(hb_failures ? 1 : 0);
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
		} else {
			printf("%s FAIL (exit %d)\n", cases[i].name, WEXITSTATUS(st));
			failed++;
		}
	}
	printf("HEAP-VERSION-BOUNDARY %s: %d of %zu cases failed, %d environment error(s)\n",
		failed || environment ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]), environment);
	return environment ? 2 : failed ? 1 : 0;
}

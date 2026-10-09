/* delegate-cursor: a v14 delegate call keeps its place while its handlers
 * change the delegate.
 *
 * The original keeps the call in the delegate object: a cursor (+0x2c) and a
 * limit (+0x30).
 *   DG_CALLBEGIN 0x66daf3-0x66db08: cursor = 0, limit = number of entries.
 *   DG_CALL 0x66df92 -> 0x652a30: while cursor < limit, call entry `cursor`
 *       and add one to the cursor.
 *   remove 0x652330 (0x652394-0x65239f): an index below the cursor takes the
 *       cursor down by one; the limit goes down by one in any case.
 *   add 0x652210: removes the same entry first (0x65225b), appends, leaves
 *       the limit alone: an entry added during the call is not called by it.
 *   DG_PLUSA 0x652710: removes each entry of the source (0x652830), appends.
 *   clear 0x652620 (0x6526d4, 0x6526db): cursor and limit 0, which ends the
 *       call. DG_ASSIGN (0x66fc27) and Set (0x6522a0) clear first.
 *   an object's entries dropped when it is freed 0x652970: the same remove.
 * The fields belong to the delegate: a nested call of the same delegate
 * starts them over and the outer call ends with the inner one.
 *
 * Every call here is the game's own parts::detail::CallErasingLayerEvent
 * (.GLOBALREF g_dgPartsErasingLayerEvent, DG_CALLBEGIN, DG_CALL, JUMP) on
 * the game's global delegate. The handlers are one empty void(int) function
 * of the game, entered for different objects (entries differ by object); a
 * step hook notes the object when the handler starts and then changes the
 * delegate with the VM's own DG_PLUSA, DG_MINUSA, DG_CLEAR and DG_ASSIGN
 * instructions. Sequences below name the objects A, B, C...
 *
 *   DC2  A removes itself (the entry just called): A B C; before, B was
 *        skipped (LR10 of layer-root is this with the real windows).
 *   DC3  A removes C, an entry after the cursor: A B. A removes B, the
 *        entry at the cursor: A C (a cursor that moved for an index equal
 *        to it would call A again).
 *   DC4  B removes A, an entry before the cursor that is not B: A B C.
 *   DC5  A adds D: A B C, and the next call A B C D. A adds itself: it
 *        moves to the end and the call still ends, A B C with B C A left.
 *        A removes and adds itself: the same.
 *   DC6  A clears the delegate: A. A clears and adds D, E: still A, and
 *        the next call D E.
 *   DC7  A assigns another delegate [D, E]: A, and the next call D E.
 *   DC8  A's object loses its last owner while A runs and is freed when A
 *        returns, which drops its entry: A B C. B frees A's object: A B C.
 *   DC9  A calls the same delegate again: A A B C, the outer call ends
 *        with the inner one.
 *   DC11 outside a call, adding an entry that is there moves it to the end.
 *   DC13 the fields: equal after a call; a copy of the page and a new page
 *        from the cache start at 0 (nothing is left for an image to load);
 *        they are not in the array metadata, so an array page made from
 *        what was a delegate keeps the rank it had.
 * Guards, which pass before the change as well:
 *   DC1  three handlers are called in order, twice.
 *   DC10 outside a call: removing an entry, adding a new one and removing
 *        one that is not there give what they gave.
 *   DC12 an engine before v14: adding an entry that is there changes
 *        nothing, removing one leaves the fields alone, the index picks
 *        entries by position.
 * Each case runs in its own fork. No game assets or GL are used.
 */

static int dc_failures;
#define DC_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); dc_failures++; } } while (0)
#define DC_MAX 6

static struct {
	bool active;
	int fn;				/* the handler: an empty void(int) */
	size_t fn_addr;
	int g;				/* g_dgPartsErasingLayerEvent */
	int st;				/* a plain struct for the handlers' objects */
	int obj[DC_MAX];		/* objects A.. */
	int nseq, seq[32];		/* the objects the handler ran for */
	void (*action)(int call_no);
} dc;

static int dc_slot(void) { return heap[global_page_slot].page->values[dc.g].i; }
static struct page *dc_page(void) { return heap_get_delegate_page(dc_slot()); }

/* Runs a delegate instruction on the global: [global page, variable, source]. */
static void dc_op(enum opcode op, int src)
{
	int sp = stack_ptr;
	stack_push(global_page_slot);
	stack_push(dc.g);
	if (op != DG_CLEAR)
		stack_push(src);
	execute_instruction(op);
	stack_ptr = sp;		/* the instruction leaves the source for DG_POP */
}

static void dc_entry_op(enum opcode op, int k)
{
	int tmp = heap_alloc_page(delegate_new_from_method_env(dc.obj[k], dc.fn, 0));
	dc_op(op, tmp);
	heap_unref(tmp);
}
static void dc_plus(int k) { dc_entry_op(DG_PLUSA, k); }
static void dc_minus(int k) { dc_entry_op(DG_MINUSA, k); }

static char dc_letter(int obj)
{
	for (int k = 0; k < DC_MAX; k++)
		if (dc.obj[k] == obj)
			return 'A' + k;
	return '?';
}

static const char *dc_seq(void)
{
	static char buf[40];
	int n = 0;
	for (int i = 0; i < dc.nseq; i++)
		buf[n++] = dc_letter(dc.seq[i]);
	buf[n] = '\0';
	return buf;
}

static const char *dc_entries(void)
{
	static char buf[40];
	struct page *p = dc_page();
	int n = 0;
	for (int i = 0; p && i + 2 < p->nr_vars && n < 39; i += 3)
		buf[n++] = dc_letter(p->values[i].i);
	buf[n] = '\0';
	return buf;
}

static void delegate_cursor_probe_step(void)
{
	if (!dc.active || call_stack_ptr <= 0)
		return;
	struct function_call *f = &call_stack[call_stack_ptr-1];
	if (f->fno != dc.fn || instr_ptr != dc.fn_addr)
		return;
	int n = dc.nseq;
	if (n < 32)
		dc.seq[dc.nseq++] = f->struct_page;
	if (dc.action)
		dc.action(n);
}

/* The game's CallErasingLayerEvent; returns the handlers' objects in order. */
static const char *dc_run(void (*action)(int))
{
	dc.nseq = 0;
	dc.action = action;
	dc.active = true;
	ci_call("parts::detail::CallErasingLayerEvent", 1, (int[]){ 4711 });
	dc.active = false;
	return dc_seq();
}

/* An empty delegate holding new objects A.. (n of them as entries). */
static void dc_reset(int n)
{
	dc_op(DG_CLEAR, 0);
	for (int k = 0; k < DC_MAX; k++) {
		if (dc.obj[k] > 0)		/* -1: a case gave the object up */
			heap_unref(dc.obj[k]);
		dc.obj[k] = alloc_struct(dc.st);
	}
	for (int k = 0; k < n; k++)
		dc_plus(k);
}

static void dc_setup(int n)
{
	ci_setup();
	memset(&dc, 0, sizeof(dc));
	dc.g = ci_global("parts::detail::g_dgPartsErasingLayerEvent");
	dc.fn = dc.st = -1;
	for (int f = 0; f < ain->nr_functions && dc.fn < 0; f++) {
		struct ain_function *fn = &ain->functions[f];
		if (fn->nr_args == 1 && fn->nr_vars == 1 && fn->vars[0].type.data == AIN_INT
				&& fn->return_type.data == AIN_VOID && !fn->is_lambda && !strchr(fn->name, '@')
				&& fn->address < ain->code_size
				&& (get_opcode(fn->address) & ~OPTYPE_MASK) == RETURN)
			dc.fn = f;
	}
	for (int s = 0; s < ain->nr_structures && dc.st < 0; s++) {
		struct ain_struct *st = &ain->structures[s];
		bool plain = st->constructor < 0 && st->destructor < 0 && st->nr_interfaces == 0
			&& st->nr_vmethods == 0 && st->nr_members >= 1 && st->nr_members <= 4;
		for (int m = 0; plain && m < st->nr_members; m++)
			plain = st->members[m].type.data == AIN_INT || st->members[m].type.data == AIN_BOOL;
		if (plain)
			dc.st = s;
	}
	struct ain_type *t = &ain->globals[dc.g].type;
	struct ain_function_type *dg = t->struc >= 0 && t->struc < ain->nr_delegates ? &ain->delegates[t->struc] : NULL;
	if (dc.fn < 0 || dc.st < 0 || t->data != AIN_DELEGATE || !dg || dg->nr_arguments != 1
			|| dg->variables[0].type.data != AIN_INT || dg->return_type.data != AIN_VOID) {
		printf("DELEGATE-CURSOR setup: handler %d, struct %d, global type %d\n", dc.fn, dc.st, t->data);
		exit(2);
	}
	dc.fn_addr = ain->functions[dc.fn].address;
	dc_reset(n);
}

#define DC_SEQ(got, want, what) do { const char *g_ = (got); \
	DC_CHECK(!strcmp(g_, want), "%s: called %s, want %s", what, g_, want); } while (0)
#define DC_LEFT(want, what) DC_CHECK(!strcmp(dc_entries(), want), "%s: the delegate holds %s, want %s", what, dc_entries(), want)

/* --- DC1 (guard) --- */

static void dc_in_order(void)
{
	dc_setup(3);
	DC_SEQ(dc_run(NULL), "ABC", "three handlers");
	DC_SEQ(dc_run(NULL), "ABC", "called again");
	DC_LEFT("ABC", "after two calls");
	printf("DC1 handlers in order: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC2 --- */

static void dc_act_remove_self(int n) { if (n == 0) dc_minus(0); }

static void dc_remove_self(void)
{
	dc_setup(3);
	DC_SEQ(dc_run(dc_act_remove_self), "ABC", "A removes itself");
	DC_LEFT("BC", "A removed itself");
	DC_SEQ(dc_run(NULL), "BC", "the next call");
	printf("DC2 a handler removes itself: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC3 --- */

static void dc_act_remove_last(int n) { if (n == 0) dc_minus(2); }
static void dc_act_remove_next(int n) { if (n == 0) dc_minus(1); }

static void dc_remove_after(void)
{
	dc_setup(3);
	DC_SEQ(dc_run(dc_act_remove_last), "AB", "A removes C");
	DC_LEFT("AB", "A removed C");
	dc_reset(3);
	DC_SEQ(dc_run(dc_act_remove_next), "AC", "A removes B, the entry at the cursor");
	DC_LEFT("AC", "A removed B");
	printf("DC3 removing an entry that is still to come: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC4 --- */

static void dc_act_second_removes_first(int n) { if (n == 1) dc_minus(0); }

static void dc_remove_before(void)
{
	dc_setup(3);
	DC_SEQ(dc_run(dc_act_second_removes_first), "ABC", "B removes A");
	DC_LEFT("BC", "B removed A");
	printf("DC4 removing another entry before the cursor: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC5 --- */

static void dc_act_add_new(int n) { if (n == 0) dc_plus(3); }
static void dc_act_add_self(int n) { if (n == 0) dc_plus(0); }
static void dc_act_remove_add_self(int n) { if (n == 0) { dc_minus(0); dc_plus(0); } }

static void dc_add(void)
{
	dc_setup(3);
	DC_SEQ(dc_run(dc_act_add_new), "ABC", "A adds D");
	DC_LEFT("ABCD", "A added D");
	DC_SEQ(dc_run(NULL), "ABCD", "the call after A added D");
	dc_reset(3);
	DC_SEQ(dc_run(dc_act_add_self), "ABC", "A adds itself");
	DC_LEFT("BCA", "A added itself (0x652210: removed, then appended)");
	dc_reset(3);
	DC_SEQ(dc_run(dc_act_remove_add_self), "ABC", "A removes and adds itself");
	DC_LEFT("BCA", "A removed and added itself");
	dc_reset(1);
	DC_SEQ(dc_run(dc_act_remove_add_self), "A", "the only handler removes and adds itself");
	DC_LEFT("A", "the only handler removed and added itself");
	printf("DC5 adding during a call: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC6 --- */

static void dc_act_clear(int n) { if (n == 0) dc_op(DG_CLEAR, 0); }
static void dc_act_clear_add(int n) { if (n == 0) { dc_op(DG_CLEAR, 0); dc_plus(3); dc_plus(4); } }

static void dc_clear(void)
{
	dc_setup(3);
	DC_SEQ(dc_run(dc_act_clear), "A", "A clears the delegate");
	DC_LEFT("", "A cleared the delegate");
	dc_reset(3);
	DC_SEQ(dc_run(dc_act_clear_add), "A", "A clears the delegate and adds D, E");
	DC_LEFT("DE", "A cleared the delegate and added D, E");
	DC_SEQ(dc_run(NULL), "DE", "the call after that");
	printf("DC6 clearing during a call: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC7 --- */

static int dc_other;
static void dc_act_assign(int n) { if (n == 0) dc_op(DG_ASSIGN, dc_other); }

static void dc_assign(void)
{
	dc_setup(3);
	struct page *other = delegate_new_from_method_env(dc.obj[3], dc.fn, 0);
	other = delegate_append(other, dc.obj[4], dc.fn);
	dc_other = heap_alloc_page(other);
	DC_SEQ(dc_run(dc_act_assign), "A", "A assigns [D, E]");
	DC_LEFT("DE", "A assigned [D, E]");
	DC_SEQ(dc_run(NULL), "DE", "the call after the assignment");
	heap_unref(dc_other);
	printf("DC7 assigning during a call: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC8 --- */

/* The harness holds the only reference besides the frame's pin (a v14
 * delegate does not own its objects). */
static void dc_act_drop_own(int n) { if (n == 0) heap_unref(dc.obj[0]); }
static void dc_act_second_drops_first(int n) { if (n == 1) heap_unref(dc.obj[0]); }

static void dc_object_freed(void)
{
	dc_setup(3);
	int a = dc.obj[0];
	unsigned seq = heap[a].seq;
	DC_SEQ(dc_run(dc_act_drop_own), "ABC", "A's object is freed when A returns");
	DC_LEFT("BC", "A's object was freed");
	DC_CHECK(HEAP_REF(a) <= 0 || heap[a].seq != seq, "A's object is still alive (ref %d)", HEAP_REF(a));
	dc.obj[0] = -1;
	dc_reset(3);
	DC_SEQ(dc_run(dc_act_second_drops_first), "ABC", "B frees A's object");
	DC_LEFT("BC", "B freed A's object");
	dc.obj[0] = -1;
	printf("DC8 an object freed during a call: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC9 --- */

static void dc_act_nest(int n)
{
	if (n != 0)
		return;
	ci_call("parts::detail::CallErasingLayerEvent", 1, (int[]){ 4712 });
}

static void dc_nested(void)
{
	dc_setup(3);
	DC_SEQ(dc_run(dc_act_nest), "AABC", "A calls the delegate again");
	DC_LEFT("ABC", "after the nested call");
	DC_SEQ(dc_run(NULL), "ABC", "the call after the nested one");
	printf("DC9 a nested call of the same delegate: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC10 (guard) --- */

static void dc_outside(void)
{
	dc_setup(3);
	dc_minus(1);
	DC_LEFT("AC", "B removed");
	dc_plus(1);
	DC_LEFT("ACB", "B added again");
	dc_minus(5);
	DC_LEFT("ACB", "removing an entry that is not there");
	dc_plus(3);
	DC_LEFT("ACBD", "D added");
	DC_SEQ(dc_run(NULL), "ACBD", "the call");
	dc_op(DG_CLEAR, 0);
	DC_LEFT("", "cleared");
	DC_SEQ(dc_run(NULL), "", "the call of an empty delegate");
	printf("DC10 outside a call: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC11 --- */

static void dc_add_again(void)
{
	dc_setup(3);
	dc_plus(0);
	DC_LEFT("BCA", "adding A again (0x652210)");
	DC_SEQ(dc_run(NULL), "BCA", "the call");
	printf("DC11 adding an entry that is there: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC12 (guard) --- */

static void dc_legacy(void)
{
	dc_setup(3);
	int a = dc.obj[0], b = dc.obj[1], c = dc.obj[2];
	ain->version = 13;
	struct page *p = delegate_new_from_method(a, dc.fn);
	p = delegate_append(p, b, dc.fn);
	p = delegate_append(p, c, dc.fn);
#ifdef DELEGATE_CALL_STATE_IN_PAGE
	p->dg.cursor = 2;
	p->dg.limit = 3;
#endif
	p = delegate_append(p, a, dc.fn);
	DC_CHECK(p->nr_vars == 9 && p->values[0].i == a && p->values[3].i == b && p->values[6].i == c,
		 "v13: adding an entry that is there: %d entries, first %c", p->nr_vars / 3, dc_letter(p->values[0].i));
	delegate_erase(p, a, dc.fn);
	DC_CHECK(p->nr_vars == 6 && p->values[0].i == b && p->values[3].i == c, "v13: after erasing A: %d entries", p->nr_vars / 3);
#ifdef DELEGATE_CALL_STATE_IN_PAGE
	DC_CHECK(p->dg.cursor == 2 && p->dg.limit == 3, "v13: erasing moved the call fields to %d/%d", p->dg.cursor, p->dg.limit);
#endif
	int obj = -1, fun = -1;
	DC_CHECK(delegate_get(p, 1, &obj, &fun) && obj == c && fun == dc.fn && !delegate_get(p, 2, &obj, &fun),
		 "v13: the index does not pick entries by position (got %c)", dc_letter(obj));
	DC_CHECK(delegate_numof(p) == 2, "v13: %d entries, want 2", delegate_numof(p));
	free_page(p);
	ain->version = 14;
	printf("DC12 engines before v14: %s\n", dc_failures ? "FAIL" : "ok");
}

/* --- DC13 --- */

static void dc_fields(void)
{
	dc_setup(3);
#ifdef DELEGATE_CALL_STATE_IN_PAGE
	dc_run(NULL);
	struct page *p = dc_page();
	DC_CHECK(p && p->dg.cursor == 3 && p->dg.limit == 3, "after a call: cursor %d, limit %d; want 3, 3",
		 p ? p->dg.cursor : -1, p ? p->dg.limit : -1);
	dc_minus(0);
	DC_CHECK(p->dg.cursor == 2 && p->dg.limit == 2, "after removing A: cursor %d, limit %d; want 2, 2", p->dg.cursor, p->dg.limit);
	dc_plus(3);
	p = dc_page();
	DC_CHECK(p->dg.cursor == 2 && p->dg.limit == 2 && p->nr_vars == 9, "after adding D: cursor %d, limit %d, %d entries; want 2, 2, 3",
		 p->dg.cursor, p->dg.limit, p->nr_vars / 3);
	/* DG_COPY, A_REF: a copy is a delegate of its own */
	struct page *copy = copy_page(p);
	DC_CHECK(copy->nr_vars == 9 && copy->dg.cursor == 0 && copy->dg.limit == 0, "a copy: %d entries, cursor %d, limit %d; want 3, 0, 0",
		 copy->nr_vars / 3, copy->dg.cursor, copy->dg.limit);
	/* a page from the cache (what loading an image and DG_NEW get) */
	copy->dg.cursor = 5;
	copy->dg.limit = 7;
	free_page(copy);		/* its entries hold no environment */
	struct page *fresh = alloc_page(DELEGATE_PAGE, 0, 9);
	DC_CHECK(fresh == copy && fresh->dg.cursor == 0 && fresh->dg.limit == 0, "a new page%s: cursor %d, limit %d; want 0, 0",
		 fresh == copy ? " from the cache" : " (not the cached one)", fresh->dg.cursor, fresh->dg.limit);
	/* The fields are the delegate's own: a page that was a delegate and
	 * comes back from the cache as an array (alloc_page leaves a cached
	 * page's array metadata to its next user) has the struct type and
	 * rank it had, not the cursor and limit of a finished call. */
	fresh->array.struct_type = 77;
	fresh->array.rank = 1;
	fresh->values[0].i = dc.obj[0]; fresh->values[1].i = dc.fn;
	fresh->values[3].i = dc.obj[1]; fresh->values[4].i = dc.fn;
	fresh->values[6].i = dc.obj[2]; fresh->values[7].i = dc.fn;
	delegate_call_begin(fresh);
	int o_, f_, e_, calls = 0;
	while (delegate_call_next(fresh, &o_, &f_, &e_))
		calls++;
	DC_CHECK(calls == 3 && fresh->dg.cursor == 3 && fresh->dg.limit == 3, "a call of a bare page: %d entries, cursor %d, limit %d",
		 calls, fresh->dg.cursor, fresh->dg.limit);
	free_page(fresh);
	struct page *array = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 9);
	DC_CHECK(array == fresh && array->array.struct_type == 77 && array->array.rank == 1,
		 "an array page made from what was a delegate%s: struct type %d, rank %d; want 77, 1 (not the delegate's cursor and limit)",
		 array == fresh ? "" : " (not the cached page)", array->array.struct_type, array->array.rank);
	free_page(array);
	dc_op(DG_CLEAR, 0);
	DC_CHECK(dc_page() && dc_page()->dg.cursor == 0 && dc_page()->dg.limit == 0, "after clearing: cursor %d, limit %d",
		 dc_page() ? dc_page()->dg.cursor : -1, dc_page() ? dc_page()->dg.limit : -1);
#else
	DC_CHECK(false, "the call is not kept in the delegate page");
#endif
	printf("DC13 the call fields: %s\n", dc_failures ? "FAIL" : "ok");
}

static int dc_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		dc_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(dc_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_delegate_cursor(void)
{
	static const struct { const char *name; void (*fn)(void); } cases[] = {
		{ "DC1", dc_in_order }, { "DC2", dc_remove_self }, { "DC3", dc_remove_after },
		{ "DC4", dc_remove_before }, { "DC5", dc_add }, { "DC6", dc_clear }, { "DC7", dc_assign },
		{ "DC8", dc_object_freed }, { "DC9", dc_nested }, { "DC10", dc_outside },
		{ "DC11", dc_add_again }, { "DC12", dc_legacy }, { "DC13", dc_fields },
	};
	int failed = 0, n = sizeof(cases) / sizeof(cases[0]);
	for (int i = 0; i < n; i++)
		failed += dc_fork(cases[i].name, cases[i].fn);
	printf("DELEGATE-CURSOR %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}

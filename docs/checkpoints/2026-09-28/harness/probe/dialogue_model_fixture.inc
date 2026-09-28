/* Synthetic text in the real CMessageText array shape. Exercise the original
 * AIN declarations and ffi for the model's Free -> EmplaceBack -> At#1 cycle.
 * No dialogue/assets are embedded; no UI/global message services are started. */
static bool dm_live_struct(int slot, int type)
{
	return slot > 1 && heap_index_valid(slot) && HEAP_REF(slot) > 0
		&& heap[slot].type == VM_PAGE && heap[slot].page
		&& heap[slot].page->type == STRUCT_PAGE && heap[slot].page->index == type;
}

static int dm_append_read(int lib, int append, int at, int array, int type,
			  const char *stage, const char *text)
{
	int sp = stack_ptr;
	stack_push(array); hll_call(lib, append, 2);
	int slots = stack_ptr - sp;
	struct page *a = heap[array].page;
	int obj = slots == 1 ? stack[sp].i : -1;
	bool shape = slots == 1 && dm_live_struct(obj, type)
		&& a && a->type == ARRAY_PAGE && a->nr_vars == 1
		&& a->values[0].i == obj;
	printf("DIALOGUE-MODEL %s append slots=%d type=%d struct=%d size=%d object_ok=%d %s\n",
		stage, slots, a ? a->a_type : -1, a ? a->array.struct_type : -1,
		a ? a->nr_vars : -1, dm_live_struct(obj, type), shape ? "ok" : "FAIL");
	fflush(stdout);
	/* Stop a bad result before treating an array reference as a struct. */
	if (!shape) {
		if (slots == 2 && stack[sp].i == array) heap_unref(array);
		stack_ptr = sp;
		return -1;
	}
	stack_pop();
	struct page *m = heap[obj].page;
	assert(m->nr_vars == 7 && ain->structures[type].members[4].type.data == AIN_STRING);
	/* v14 alloc_struct leaves strings null until the bytecode initializes them. */
	if (m->values[4].i > 1 && heap_index_valid(m->values[4].i)
	    && heap[m->values[4].i].type == VM_STRING)
		heap_string_assign(m->values[4].i, make_string(text, strlen(text)));
	else
		m->values[4].i = sf_str(text, -1);
	stack_push(array); stack_push(0); hll_call(lib, at, 2);
	int read_slots = stack_ptr - sp;
	int read = read_slots == 1 ? stack[sp].i : -1;
	bool ok = read_slots == 1 && read == obj && dm_live_struct(read, type)
		&& !strcmp(fc_text(heap[read].page->values[4].i), text);
	printf("DIALOGUE-MODEL %s At#1 slots=%d same=%d text_len=%zu refs=%d %s\n",
		stage, read_slots, read == obj, strlen(text), HEAP_REF(obj), ok ? "ok" : "FAIL");
	stack_ptr = sp;
	if (!ok) return -1;
	heap_unref(read); /* Release At's owner; retain EmplaceBack's external owner. */
	return obj;
}

static int dm_array_query(int lib, int function, int array)
{
	int sp = stack_ptr;
	stack_push(array); hll_call(lib, function, 2);
	assert(stack_ptr == sp + 1);
	return stack_pop().i;
}

static bool dm_empty_queries(int lib, int array, int held)
{
	int value_find = fo_find_fn(lib, "Find", 4, 0, 0);
	int pred_find = fo_find_fn(lib, "Find", 4, 1, 0);
	int count = fo_find_fn(lib, "Count", 2, 1, 0);
	int numof = fo_find_fn(lib, "Numof", 2, 1, 0);
	assert(value_find >= 0 && pred_find >= 0 && count >= 0 && numof >= 0);
	/* Empty arrays cannot match values or execute callbacks. In particular,
	 * retaining a struct descriptor must not trigger a new unsupported-value
	 * or invalid-callback VM_ERROR where the old null page returned early. */
	unsigned long before_steps = total_steps;
	int sp = stack_ptr;
	stack_push(array); stack_push(0); stack_push(5); stack_push(held);
	hll_call(lib, value_find, 2);
	assert(stack_ptr == sp + 1);
	int found_value = stack_pop().i;
	stack_push(array); stack_push(0); stack_push(5); stack_push(-1); stack_push(-1);
	hll_call(lib, pred_find, 2);
	assert(stack_ptr == sp + 1);
	int found_predicate = stack_pop().i;
	stack_push(array); stack_push(-1); stack_push(-1); hll_call(lib, count, 2);
	assert(stack_ptr == sp + 1);
	int matches_count = stack_pop().i;
	stack_push(array); stack_push(-1); stack_push(-1); hll_call(lib, numof, 2);
	assert(stack_ptr == sp + 1);
	int matches_numof = stack_pop().i;
	bool ok = found_value == -1 && found_predicate == -1
		&& matches_count == 0 && matches_numof == 0
		&& total_steps == before_steps && HEAP_REF(held) == 1;
	printf("DIALOGUE-MODEL empty queries find_value=%d find_pred=%d count=%d numof=%d callback_steps=%lu held_refs=%d %s\n",
		found_value, found_predicate, matches_count, matches_numof,
		total_steps - before_steps, HEAP_REF(held), ok ? "ok" : "FAIL");
	return ok;
}

static int dm_clear_cycle(const char *operation)
{
	int lib = fo_find_lib("Array"), type = ain_get_struct(ain, "message::detail::CMessageText");
	assert(lib >= 0 && type >= 0);
	int free_fn = fo_find_fn(lib, operation, 1, 0, 0);
	int append = fo_find_fn(lib, "EmplaceBack", 1, 0, 0);
	int at = fo_find_fn(lib, "At", 2, 0, 1);
	int numof = fo_find_fn(lib, "Numof", 1, 0, 0);
	int empty = fo_find_fn(lib, "Empty", 1, 0, 0);
	assert(free_fn >= 0 && append >= 0 && at >= 0 && numof >= 0 && empty >= 0);
	union vm_value dimension = { .i = 0 };
	int array = heap_alloc_page(alloc_array(1, &dimension, AIN_ARRAY_STRUCT, type, false));
	printf("DIALOGUE-MODEL operation=%s\n", operation);
	int held = dm_append_read(lib, append, at, array, type, "before-clear", "synthetic-before");
	if (held < 0) { puts("DIALOGUE-MODEL FAIL initial typed control"); return 86; }
	int sp = stack_ptr;
	for (int round = 0; round < 2; round++) {
		stack_push(array); hll_call(lib, free_fn, 2);
		assert(stack_ptr == sp);
		int n = dm_array_query(lib, numof, array), e = dm_array_query(lib, empty, array);
		bool holder = dm_live_struct(held, type) && HEAP_REF(held) == 1
			&& !strcmp(fc_text(heap[held].page->values[4].i), "synthetic-before");
		printf("DIALOGUE-MODEL clear%d numof=%d empty=%d held_refs=%d held_text=%d %s\n",
			round + 1, n, e, HEAP_REF(held), holder, n == 0 && e && holder ? "ok" : "FAIL");
		if (n != 0 || !e || !holder) { puts("DIALOGUE-MODEL FAIL clear/holder contract"); return 86; }
		if (!dm_empty_queries(lib, array, held)) { puts("DIALOGUE-MODEL FAIL empty query contract"); return 86; }
	}
	int after = dm_append_read(lib, append, at, array, type, "after-clear-twice", "synthetic-after");
	if (after < 0) {
		heap_unref(held);
		heap_unref(array);
		printf("DIALOGUE-MODEL FAIL %s loses typed append/read contract\n", operation);
		return 86;
	}
	heap_unref(after);
	heap_unref(held);
	heap_unref(array);
	assert(stack_ptr == sp);
	printf("DIALOGUE-MODEL %s typed append/read and external owner PASS\n", operation);
	return 0;
}

static int test_dialogue_model(void)
{
	int free_rc = dm_clear_cycle("Free"), clear_rc = dm_clear_cycle("Clear");
	int lib = fo_find_lib("Array"), failures = !!free_rc + !!clear_rc;
	const char *names[] = {"Free", "Clear"};
	for (unsigned op = 0; op < 2; op++) {
		int fn = fo_find_fn(lib, names[op], 1, 0, 0), sp = stack_ptr;
		/* Scalar typed pages and their element owners must survive the same
		 * empty transition without turning strings into primitive integers. */
		for (int is_string = 0; is_string < 2; is_string++) {
			union vm_value dimension = { .i = 2 };
			int type = is_string ? AIN_ARRAY_STRING : AIN_ARRAY_INT;
			int a = heap_alloc_page(alloc_array(1, &dimension, type, -1, false));
			int holder = -1;
			if (is_string) {
				holder = heap[a].page->values[0].i;
				heap_string_assign(holder, make_string("held-text", 9));
				heap_ref(holder);
			} else {
				heap[a].page->values[0].i = 42;
				heap[a].page->values[1].i = 17;
			}
			stack_push(a); hll_call(lib, fn, is_string ? 2 : 1);
			assert(stack_ptr == sp);
			struct page *p = heap[a].page;
			bool typed = p && p->type == ARRAY_PAGE && p->a_type == type
				&& p->array.rank == 1 && p->array.struct_type == -1 && p->nr_vars == 0;
			bool owner = !is_string || (HEAP_REF(holder) == 1 && !strcmp(fc_text(holder), "held-text"));
			printf("DIALOGUE-MODEL %s %s typed_empty=%d owner=%d %s\n", names[op],
				is_string ? "string" : "int", typed, owner, typed && owner ? "ok" : "FAIL");
			failures += !(typed && owner);
			if (holder > 1) heap_unref(holder);
			heap_unref(a);
		}
		/* Compatibility checks only: a null page has no declaration to retain;
		 * the pre-v14 engine convention remains Free=NULL, Clear=int[0]. */
		for (int legacy = 0; legacy < 2; legacy++) {
			union vm_value dimension = { .i = 1 };
			int a = heap_alloc_page(legacy ? alloc_array(1, &dimension, AIN_ARRAY_INT, -1, false) : NULL);
			int version = ain->version;
			if (legacy) ain->version = 13;
			stack_push(a); hll_call(lib, fn, 1);
			ain->version = version;
			assert(stack_ptr == sp);
			struct page *p = heap[a].page;
			bool ok = op == 0 ? !p : p && p->type == ARRAY_PAGE && p->a_type == AIN_ARRAY_INT && p->nr_vars == 0;
			printf("DIALOGUE-MODEL %s %s compatibility=%d %s\n", names[op],
				legacy ? "v13-guard" : "null", ok, ok ? "ok" : "FAIL");
			failures += !ok;
			heap_unref(a);
		}
	}
	if (failures) { puts("DIALOGUE-MODEL FAIL typed clear/reuse cycle"); return 86; }
	puts("DIALOGUE-MODEL PASS typed append/read survives repeated Free/Clear; external element owner survives");
	return 0;
}

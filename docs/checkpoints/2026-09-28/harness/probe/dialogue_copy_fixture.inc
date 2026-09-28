/* Original Array.ShallowCopy helper 0x658d40 shares reference elements and
 * retains them for the returned array. Real AIN declaration + real ffi;
 * all text is synthetic. This does not claim a GUI root cause. */
static bool dc_object_ok(int object, int type, const char *text)
{
	if (object <= 1 || !heap_index_valid(object) || HEAP_REF(object) <= 0
	    || heap[object].type != VM_PAGE || !heap[object].page)
		return false;
	struct page *p = heap[object].page;
	return p->type == STRUCT_PAGE && p->index == type && p->nr_vars == 7
		&& !strcmp(fc_text(p->values[4].i), text);
}

static int dc_copy(int lib, int fn, int source)
{
	int sp = stack_ptr;
	stack_push(source); hll_call(lib, fn, 2);
	assert(stack_ptr == sp + 1);
	return stack_pop().i;
}

static bool dc_live_array(int slot, int count)
{
	return slot > 1 && heap_index_valid(slot) && HEAP_REF(slot) > 0
		&& heap[slot].type == VM_PAGE && heap[slot].page
		&& heap[slot].page->type == ARRAY_PAGE && heap[slot].page->nr_vars == count;
}

static int dc_owner_order(int lib, int copy_fn, int at, int type, bool source_first)
{
	const char *text = "synthetic-shared-text";
	union vm_value dimension = { .i = 1 };
	int source = heap_alloc_page(alloc_array(1, &dimension, AIN_ARRAY_STRUCT, type, false));
	int object = alloc_struct(type);
	assert(ain->structures[type].nr_members == 7 && ain->structures[type].members[4].type.data == AIN_STRING);
	heap[object].page->values[4].i = sf_str(text, -1);
	/* Transfer the only object owner to the source array. No external holder. */
	heap[source].page->values[0].i = object;
	assert(HEAP_REF(object) == 1);
	int copy = dc_copy(lib, copy_fn, source);
	bool same = copy != source && dc_live_array(copy, 1)
		&& heap[copy].page->values[0].i == object;
	bool retained = same && HEAP_REF(object) == 2 && dc_object_ok(object, type, text);
	printf("DIALOGUE-COPY %s copy same_object=%d refs=%d want=2 %s\n",
		source_first ? "source-first" : "copy-first", same, HEAP_REF(object), retained ? "ok" : "FAIL");
	fflush(stdout);
	/* Preserve semantic before evidence without dereferencing freed objects. */
	if (!retained) return 86;
	int survivor = source_first ? copy : source;
	heap_unref(source_first ? source : copy);
	bool alive = HEAP_REF(object) == 1 && dc_object_ok(object, type, text);
	printf("DIALOGUE-COPY %s survivor refs=%d text_ok=%d %s\n",
		source_first ? "source-first" : "copy-first", HEAP_REF(object), alive, alive ? "ok" : "FAIL");
	if (!alive) return 86;
	int sp = stack_ptr;
	stack_push(survivor); stack_push(0); hll_call(lib, at, 2);
	assert(stack_ptr == sp + 1);
	int read = stack_pop().i;
	bool readable = read == object && HEAP_REF(object) == 2 && dc_object_ok(read, type, text);
	printf("DIALOGUE-COPY %s At#1 same_object=%d text_ok=%d refs=%d %s\n",
		source_first ? "source-first" : "copy-first", read == object, readable,
		HEAP_REF(object), readable ? "ok" : "FAIL");
	if (!readable) return 86;
	heap_unref(read);
	heap_unref(survivor);
	bool released = HEAP_REF(object) == 0;
	printf("DIALOGUE-COPY %s last owner released=%d %s\n",
		source_first ? "source-first" : "copy-first", released, released ? "ok" : "FAIL");
	return released ? 0 : 86;
}

static int dc_string_order(int lib, int copy_fn, int at, bool source_first)
{
	const char *text = "synthetic-shared-string";
	int source = fo_make_string_array(&text, 1);
	int element = heap[source].page->values[0].i;
	assert(HEAP_REF(element) == 1);
	int copy = dc_copy(lib, copy_fn, source);
	bool same = copy != source && dc_live_array(copy, 1)
		&& heap[copy].page->values[0].i == element;
	bool retained = same && HEAP_REF(element) == 2 && !strcmp(fc_text(element), text);
	printf("DIALOGUE-COPY string %s same_object=%d refs=%d want=2 %s\n",
		source_first ? "source-first" : "copy-first", same, HEAP_REF(element), retained ? "ok" : "FAIL");
	fflush(stdout);
	if (!retained) return 86;
	int survivor = source_first ? copy : source;
	heap_unref(source_first ? source : copy);
	bool alive = HEAP_REF(element) == 1 && !strcmp(fc_text(element), text);
	if (!alive) { puts("DIALOGUE-COPY string survivor FAIL"); return 86; }
	int sp = stack_ptr;
	stack_push(survivor); stack_push(0); hll_call(lib, at, 2);
	assert(stack_ptr == sp + 1);
	int read = stack_pop().i;
	bool readable = read == element && HEAP_REF(element) == 2 && !strcmp(fc_text(read), text);
	printf("DIALOGUE-COPY string %s At#1 same_object=%d text_ok=%d refs=%d %s\n",
		source_first ? "source-first" : "copy-first", read == element, readable,
		HEAP_REF(element), readable ? "ok" : "FAIL");
	if (!readable) return 86;
	heap_unref(read); heap_unref(survivor);
	bool released = HEAP_REF(element) == 0;
	printf("DIALOGUE-COPY string %s last owner released=%d %s\n",
		source_first ? "source-first" : "copy-first", released, released ? "ok" : "FAIL");
	return released ? 0 : 86;
}

static int test_dialogue_copy(void)
{
	int lib = fo_find_lib("Array"), type = ain_get_struct(ain, "message::detail::CMessageText");
	assert(lib >= 0 && type >= 0);
	int copy_fn = fo_find_fn(lib, "ShallowCopy", 1, 0, 1);
	int at = fo_find_fn(lib, "At", 2, 0, 1);
	assert(copy_fn >= 0 && at >= 0);
	if (dc_owner_order(lib, copy_fn, at, type, false)) {
		puts("DIALOGUE-COPY FAIL copy must own its shared struct element"); return 86;
	}
	if (dc_owner_order(lib, copy_fn, at, type, true)) {
		puts("DIALOGUE-COPY FAIL source teardown must preserve copied element"); return 86;
	}
	/* Native element descriptor 12 (string), like descriptor 13 (struct),
	 * reaches the retain branch 0x658e33 -> heap_ref 0x67f2e1. */
	if (dc_string_order(lib, copy_fn, at, false) || dc_string_order(lib, copy_fn, at, true)) {
		puts("DIALOGUE-COPY FAIL copied string must have an independent owner"); return 86;
	}
	/* Empty/null inputs must return a usable empty array. Avoid imposing a
	 * new descriptor representation on the legacy empty fallback. */
	for (int null_source = 0; null_source < 2; null_source++) {
		union vm_value dimension = { .i = 0 };
		int source = heap_alloc_page(null_source ? NULL : alloc_array(1, &dimension, AIN_ARRAY_STRUCT, type, false));
		int copy = dc_copy(lib, copy_fn, source);
		bool ok = copy != source && dc_live_array(copy, 0);
		printf("DIALOGUE-COPY %s usable_empty=%d %s\n", null_source ? "null" : "typed-empty", ok, ok ? "ok" : "FAIL");
		if (!ok) return 86;
		heap_unref(copy); heap_unref(source);
	}
	puts("DIALOGUE-COPY PASS shared struct/string survives either array teardown; no deep copy; empty/null compatible");
	return 0;
}

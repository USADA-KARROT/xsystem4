/* title-review: the second review of 6d39915..ce5c75a
 * (research/gui-visual/base-ui.md §11).
 *
 *   TR1 A v14 method delegate does not own its object (original 0x6522c0 ->
 *       0x652210 retains only a lambda's environment and records the
 *       delegate in the object; 0x681f70 drops the entries when the object
 *       is freed). Real bytecode SceneTutorial@RegisterEvent adds its own
 *       methods to its buttons' click events; the scene must still be freed
 *       when its owner lets go, and its entries must leave the buttons'
 *       events. Before: the delegates kept the scene alive (SceneTitle's and
 *       SceneTutorial's layers were never erased, the title buttons stayed
 *       clickable under the opening).
 *   TR2 What a destructor lets go of is destroyed before its next
 *       instruction (reference counting in the original): a nested
 *       destructor runs inside the outer one, not after it.
 *   TR3 RemoveController and ReleaseActivity return the erased parts'
 *       delegate indices in their wrap<array<int>> out-list (original
 *       0x53d500 -> 0x5386c0 reads parts +0x88, the field SetEventID
 *       writes; 0x540050 -> 0x55e400 reads it with 0x56d620). EraseLayer and
 *       activity::detail::Release hand the list to
 *       CPartsMessageManager@ReleaseFunctionSetList. Before: the v14 list
 *       stayed empty, DeletedEvent never fired, and a scene opened a second
 *       time reused released components.
 *   TR4 A_REF copies an array (original 0x6794d0 -> 0x67ffb0). Real
 *       bytecode ArrayExtensions::GetReverse<IRectParts&> (two-slot, used by
 *       TitleCharacterView@FadeIn), GetReverse<ActionTarget&> (one-slot wrap)
 *       and GetSort<string> must leave their source alone. Before: A_REF
 *       shared the page and the title characters were reversed in place.
 *   TR5 The v14 click dispatch picks its target as the hover does: a
 *       clickable system parts (1000001000 and up, InputDisabler and dialog
 *       blockers) takes the click; a shown parts that only blocks the cursor
 *       keeps it from the parts behind and it becomes the whole-screen click.
 *   TR6 A hover state with nothing to show (an empty ＣＧ部件 state) keeps
 *       the normal state instead of blanking the parts.
 *   TR7 Freeing a CParts page does not release the parts numbered like its
 *       <vtable> array's heap slot (a port hack read member 0 as the number).
 * Each case runs in its own fork. Only functions that exist before the fix
 * are referenced. No game assets or GL are used.
 */

static int tr_failures;
#define TR_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); tr_failures++; } } while (0)
#define TR_I(v) ((union vm_value){ .i = (v) })

static int tr_fn(const char *name)
{
	int f = ain_get_function(ain, (char *)name);
	if (f < 0) { printf("TITLE-REVIEW function %s missing\n", name); exit(2); }
	return f;
}

static int tr_struct(const char *name)
{
	int s = ain_get_struct(ain, (char *)name);
	if (s < 0) { printf("TITLE-REVIEW struct %s missing\n", name); exit(2); }
	return s;
}

static struct page *tr_delegate(int slot)
{
	return slot > 1 && heap_index_valid(slot) && heap[slot].type == VM_PAGE
		&& heap[slot].page && heap[slot].page->type == DELEGATE_PAGE ? heap[slot].page : NULL;
}

/* Calls a function with one one-slot argument and returns its one-slot result. */
static int tr_call1(int fno, int arg)
{
	int sp = stack_ptr, csp = call_stack_ptr;
	size_t ip = instr_ptr;
	stack_push(arg);
	function_call(fno, VM_RETURN);
	vm_execute();
	instr_ptr = ip;
	assert(call_stack_ptr == csp && stack_ptr == sp + 1);
	int r = stack[sp].i;
	stack_ptr = sp;
	return r;
}

/* --- TR1 --- */

static void tr_weak_target(void)
{
	bu_postlink();
	((void (*)(void))br_pe_fn("_PreLink"))();
	int st = tr_struct("SceneTutorial");
	int fb = tr_struct("FooterButton");
	int reg = tr_fn("SceneTutorial@RegisterEvent");
	int m_prev = br_member(st, "m_uiPrev"), m_click = br_member(fb, "<OnClickEvent>");
	int tut = alloc_struct(st);
	int prev = heap[tut].page->values[m_prev].i;
	TR_CHECK(prev > 1 && heap_index_valid(prev) && heap[prev].page && heap[prev].page->index == fb,
		 "m_uiPrev is not a FooterButton (%d)", prev);
	heap_ref(prev); /* the harness keeps the button */
	/* SceneContext@1 erases its layer when the scene goes away. */
	int (*add_controller)(int) = (int (*)(int))br_pe_fn("AddController");
	int sc = heap[tut].page->values[br_member(st, "m_scene")].i;
	int layer = add_controller(-1);
	heap[sc].page->values[br_member(tr_struct("SceneContext"), "<LayerPartsNumber>")].i = layer;
	int ref_before = HEAP_REF(tut);
	call_method0(reg, tut);
	struct page *dg = tr_delegate(heap[prev].page->values[m_click].i);
	TR_CHECK(dg && dg->nr_vars == 3 && dg->values[0].i == tut,
		 "OnClickEvent after RegisterEvent: %d entries, target %d; want 1 entry targeting %d",
		 dg ? dg->nr_vars / 3 : -1, dg && dg->nr_vars ? dg->values[0].i : 0, tut);
	TR_CHECK(HEAP_REF(tut) == ref_before, "RegisterEvent's four delegates hold the scene: ref %d -> %d",
		 ref_before, HEAP_REF(tut));
	heap_unref(tut); /* the scene runner lets go */
	TR_CHECK(HEAP_REF(tut) <= 0, "scene still alive after its owner let go (ref %d)", HEAP_REF(tut));
	dg = tr_delegate(heap[prev].page->values[m_click].i);
	TR_CHECK(dg && dg->nr_vars == 0, "the freed scene's entry is still in the button's event (%d entries)",
		 dg ? dg->nr_vars / 3 : -1);
	heap_unref(prev);
	printf("TR1 weak delegate target: %s\n", tr_failures ? "FAIL" : "ok");
}

/* --- TR2 --- */

static struct {
	bool armed, done;
	int outer, inner, dtor;
	unsigned long nested_calls;
	int inner_ref_after;
} tr_nested;

static void title_review_probe_step(void)
{
	if (!tr_nested.armed || tr_nested.done || !call_stack_ptr)
		return;
	struct function_call *top = &call_stack[call_stack_ptr - 1];
	if (top->fno != tr_nested.dtor || top->struct_page != tr_nested.outer)
		return;
	/* The outer destructor lets go of the inner object's last reference. */
	tr_nested.done = true;
	unsigned long before = calls[tr_nested.dtor];
	heap_unref(tr_nested.inner);
	tr_nested.nested_calls = calls[tr_nested.dtor] - before;
	tr_nested.inner_ref_after = HEAP_REF(tr_nested.inner);
}

static void tr_nested_free(void)
{
	int st = tr_struct("backlog::detail::CBackLogUnit");
	int dtor = tr_fn("backlog::detail::CBackLogUnit@1");
	TR_CHECK(ain->structures[st].destructor == dtor, "CBackLogUnit destructor is %d, want %d",
		 ain->structures[st].destructor, dtor);
	int outer = alloc_struct(st), inner = alloc_struct(st);
	tr_nested = (__typeof__(tr_nested)){ .armed = true, .outer = outer, .inner = inner, .dtor = dtor };
	unsigned long before = calls[dtor];
	heap_unref(outer);
	TR_CHECK(tr_nested.done, "the outer destructor did not run");
	TR_CHECK(tr_nested.nested_calls == 1 && tr_nested.inner_ref_after <= 0,
		 "inside the outer destructor the inner object was not destroyed: %lu nested destructor calls, ref %d",
		 tr_nested.nested_calls, tr_nested.inner_ref_after);
	TR_CHECK(calls[dtor] - before == 2, "%lu destructor calls in all, want 2", calls[dtor] - before);
	tr_nested.armed = false;
	printf("TR2 nested destruction: %s\n", tr_failures ? "FAIL" : "ok");
}

/* --- TR3 --- */

static int tr_int_array(void)
{
	struct page *a = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0);
	a->array.rank = 1;
	a->array.struct_type = -1;
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, a);
	return slot;
}

static bool tr_list_is(int slot, int n, const int *want)
{
	struct page *a = heap[slot].page;
	int got = a ? a->nr_vars : 0;
	if (got != n)
		return false;
	for (int i = 0; i < n; i++) {
		bool found = false;
		for (int j = 0; j < n; j++)
			found |= a->values[j].i == want[i];
		if (!found)
			return false;
	}
	return true;
}

static void tr_erase_lists(void)
{
	bu_postlink();
	/* The probe links without _PreLink, which replaces RemoveController
	 * with its v14 (wrap<array<int>> out-list) version; run it and call
	 * that. The activity functions are bound by _PostLink (bu_postlink). */
	((void (*)(void))br_pe_fn("_PreLink"))();
	void (*remove_controller)(int, int) = (void (*)(int, int))br_pe_fn("RemoveController");
	int (*add_controller)(int) = (int (*)(int))br_pe_fn("AddController");
	assert(remove_controller && add_controller);
	/* RemoveController */
	int id = add_controller(-1);
	const int A = 9601, B = 9602;
	parts_get(A);
	parts_get(B);
	PE_SetDelegateIndex(A, 37);
	PE_SetDelegateIndex(B, 41);
	int list = tr_int_array();
	remove_controller(list, id);
	struct page *l = heap[list].page;
	TR_CHECK(!parts_try_get(A) && !parts_try_get(B), "the controller's parts were not released");
	TR_CHECK(tr_list_is(list, 2, (int[]){ 37, 41 }), "RemoveController list: %d entries (%d %d); want the delegate indices 37 41",
		 l ? l->nr_vars : 0, l && l->nr_vars > 0 ? l->values[0].i : 0, l && l->nr_vars > 1 ? l->values[1].i : 0);
	/* ReleaseActivity */
	int name = heap_alloc_string(make_string("TR3", 3));
	int pname = heap_alloc_string(make_string("Item", 4));
	const int C = 9603;
	parts_get(C);
	PE_SetDelegateIndex(C, 53);
	TR_CHECK(bu_call("PartsEngine", "CreateActivity", 1, 0, 1, (union vm_value[]){ TR_I(name) }).i
		 && bu_call("PartsEngine", "AddActivityParts", 3, 0, 3, (union vm_value[]){ TR_I(name), TR_I(pname), TR_I(C) }).i,
		 "activity setup failed");
	int list2 = tr_int_array();
	TR_CHECK(bu_call("PartsEngine", "ReleaseActivity", 2, 0, 2, (union vm_value[]){ TR_I(name), TR_I(list2) }).i,
		 "ReleaseActivity failed");
	l = heap[list2].page;
	TR_CHECK(!parts_try_get(C), "the activity's parts were not released");
	TR_CHECK(tr_list_is(list2, 1, (int[]){ 53 }), "ReleaseActivity list: %d entries (%d); want the delegate index 53",
		 l ? l->nr_vars : 0, l && l->nr_vars ? l->values[0].i : 0);
	printf("TR3 erase lists hold delegate indices: %s\n", tr_failures ? "FAIL" : "ok");
}

/* --- TR4 --- */

static void tr_array_copies(void)
{
	int ps = tr_struct("CASPos");
	/* two-slot: GetReverse<IRectParts&> */
	int A = vm_construct_struct(ps), B = vm_construct_struct(ps), C = vm_construct_struct(ps);
	int src = br_pairs(3, (int[]){ A, 0, B, 1, C, 0 });
	int refs[3] = { HEAP_REF(A), HEAP_REF(B), HEAP_REF(C) };
	int res = tr_call1(tr_fn("ArrayExtensions::GetReverse<IRectParts&>"), src);
	struct page *s = heap[src].page, *r = res > 1 && heap_index_valid(res) ? heap[res].page : NULL;
	TR_CHECK(s->nr_vars == 6 && s->values[0].i == A && s->values[1].i == 0 && s->values[2].i == B
		 && s->values[4].i == C, "GetReverse<IRectParts&> changed its source: [%d %d %d %d %d %d]",
		 s->values[0].i, s->values[1].i, s->values[2].i, s->values[3].i, s->values[4].i, s->values[5].i);
	TR_CHECK(res != src && r && r->nr_vars == 6 && r->values[0].i == C && r->values[2].i == B
		 && r->values[3].i == 1 && r->values[4].i == A,
		 "GetReverse<IRectParts&> result: slot %d (source %d)", res, src);
	if (res != src)
		heap_unref(res);
	TR_CHECK(HEAP_REF(A) == refs[0] && HEAP_REF(B) == refs[1] && HEAP_REF(C) == refs[2],
		 "element refs after freeing the result: %d %d %d, want %d %d %d",
		 HEAP_REF(A), HEAP_REF(B), HEAP_REF(C), refs[0], refs[1], refs[2]);
	/* one-slot wrap: GetReverse<ActionTarget&> */
	struct page *one = alloc_page(ARRAY_PAGE, AIN_ARRAY, 3);
	one->array.rank = 1;
	one->array.struct_type = 1;
	int D = vm_construct_struct(ps), E = vm_construct_struct(ps), F = vm_construct_struct(ps);
	one->values[0].i = D; one->values[1].i = E; one->values[2].i = F;
	heap_ref(D); heap_ref(E); heap_ref(F);
	int src1 = heap_alloc_slot(VM_PAGE);
	heap_set_page(src1, one);
	int res1 = tr_call1(tr_fn("ArrayExtensions::GetReverse<ActionTarget&>"), src1);
	s = heap[src1].page;
	r = res1 > 1 && heap_index_valid(res1) ? heap[res1].page : NULL;
	TR_CHECK(s->values[0].i == D && s->values[2].i == F, "GetReverse<ActionTarget&> changed its source: [%d %d %d]",
		 s->values[0].i, s->values[1].i, s->values[2].i);
	TR_CHECK(res1 != src1 && r && r->nr_vars == 3 && r->values[0].i == F && r->values[2].i == D,
		 "GetReverse<ActionTarget&> result: slot %d (source %d)", res1, src1);
	if (res1 != src1)
		heap_unref(res1);
	/* strings: GetSort<string> */
	const char *words[] = { "pear", "apple", "fig" };
	struct page *sp = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 3);
	sp->array.rank = 1;
	sp->array.struct_type = -1;
	for (int i = 0; i < 3; i++)
		sp->values[i].i = heap_alloc_string(make_string(words[i], strlen(words[i])));
	int src2 = heap_alloc_slot(VM_PAGE);
	heap_set_page(src2, sp);
	int res2 = tr_call1(tr_fn("ArrayExtensions::GetSort<string>"), src2);
	s = heap[src2].page;
	r = res2 > 1 && heap_index_valid(res2) ? heap[res2].page : NULL;
	TR_CHECK(!strcmp(bu_str(s->values[0].i), "pear") && !strcmp(bu_str(s->values[2].i), "fig"),
		 "GetSort<string> changed its source: %s %s %s", bu_str(s->values[0].i), bu_str(s->values[1].i),
		 bu_str(s->values[2].i));
	TR_CHECK(res2 != src2 && r && r->nr_vars == 3 && !strcmp(bu_str(r->values[0].i), "apple")
		 && !strcmp(bu_str(r->values[2].i), "pear"), "GetSort<string> result: slot %d (source %d)", res2, src2);
	printf("TR4 A_REF copies arrays: %s\n", tr_failures ? "FAIL" : "ok");
}

/* --- TR5 --- */

static void tr_click_targets(void)
{
	br_input_setup();
	int type, no;
	/* a clickable full-screen system blocker in front of a button */
	const int B = 9901, S = 1000001500;
	br_box(B, 100, 100, 200, 80, true);
	br_box(S, 0, 0, 1280, 720, true);
	PE_SetZ(S, 50);
	int n = br_click(150, 140, &type, &no);
	TR_CHECK(n == 1 && type == 4 && no == S, "system blocker: %d messages, first type %d parts %d; want 1, 4, %d",
		 n, type, no, S);
	PE_ReleaseParts(S);
	/* a shown parts that only blocks the cursor (not clickable, オン指針透過 0) */
	const int N = 9902;
	br_box(N, 50, 50, 400, 300, true);
	PE_SetClickable(N, false);
	PE_SetZ(N, 50);
	heap[global_page_slot].page->values[2].i = 0;
	n = br_click(150, 140, &type, &no);
	TR_CHECK(n == 1 && type == 4 && no == 0, "cursor blocker: %d messages, first type %d parts %d; want 1, 4, 0 (whole screen)",
		 n, type, no);
	/* the same parts passing the cursor lets the click through */
	PE_SetPassCursor(N, true);
	n = br_click(150, 140, &type, &no);
	TR_CHECK(n == 1 && type == 4 && no == B, "cursor-passing parts: %d messages, first type %d parts %d; want 1, 4, %d",
		 n, type, no, B);
	printf("TR5 click targets: %s\n", tr_failures ? "FAIL" : "ok");
}

/* --- TR6 --- */

static int tr_hover_state(struct parts *p, int x, int y)
{
	mouse_set_pos(x, y);
	PE_UpdateComponent(0);
	PE_BeginInput();
	PE_UpdateInputState(0);
	int state = p->state;
	PE_EndInput();
	return state;
}

static void tr_hover_empty_state(void)
{
	br_input_setup();
	const int H = 9921, K = 9922;
	/* Image0 of SceneTutorial: normal state shown, on-cursor state a ＣＧ部件
	 * without a CG, not clickable, blocks the cursor. */
	struct parts *h = br_box(H, 100, 100, 200, 200, false);
	PE_SetClickable(H, false);
	parts_get_cg(h, 1);
	parts_get_cg(h, 2);
	int state = tr_hover_state(h, 150, 150);
	TR_CHECK(state == 0, "empty on-cursor state entered (state %d); want the normal state kept", state);
	/* an on-cursor state with a CG is still entered */
	struct parts *k = br_box(K, 500, 100, 200, 200, false);
	PE_SetClickable(K, false);
	struct parts_cg *cg = parts_get_cg(k, 1);
	cg->name = make_string("probe", 5);
	state = tr_hover_state(k, 550, 150);
	TR_CHECK(state == 1, "on-cursor state with a CG not entered (state %d)", state);
	state = tr_hover_state(k, 1000, 600);
	TR_CHECK(state == 0, "state after the cursor left: %d", state);
	printf("TR6 empty hover state: %s\n", tr_failures ? "FAIL" : "ok");
}

/* --- TR7 --- */

static void tr_cparts_teardown(void)
{
	bu_postlink();
	int cs = tr_struct("parts::detail::CParts");
	int c = vm_construct_struct(cs);	/* <vtable> filled, not attached */
	TR_CHECK(heap_index_valid(c), "no CParts (%d)", c);
	int vtable = heap[c].page->values[0].i;
	TR_CHECK(vtable > 1 && heap_index_valid(vtable) && heap[vtable].page && heap[vtable].page->type == ARRAY_PAGE
		 && heap[vtable].page->nr_vars > 35, "CParts member 0 is not a filled <vtable> array (%d)", vtable);
	TR_CHECK(heap[c].page->values[br_member(cs, "<AutoRelease>")].i == 0, "AutoRelease set");
	/* An unrelated parts whose number happens to equal that heap slot. */
	parts_get(vtable);
	heap_unref(c);
	TR_CHECK(parts_try_get(vtable) != NULL, "parts %d (numbered like the freed <vtable> slot) was released", vtable);
	printf("TR7 CParts teardown: %s\n", tr_failures ? "FAIL" : "ok");
}

static int tr_fork(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		tr_failures = 0;
		alarm(60);
		fn();
		fflush(stdout);
		_exit(tr_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok)
		printf("%s FAILED (%s %d)\n", name, WIFEXITED(status) ? "exit" : "signal",
		       WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
	return ok ? 0 : 1;
}

static int test_title_review(void)
{
	int failed = 0;
	failed += tr_fork("TR1", tr_weak_target);
	failed += tr_fork("TR2", tr_nested_free);
	failed += tr_fork("TR3", tr_erase_lists);
	failed += tr_fork("TR4", tr_array_copies);
	failed += tr_fork("TR5", tr_click_targets);
	failed += tr_fork("TR6", tr_hover_empty_state);
	failed += tr_fork("TR7", tr_cparts_teardown);
	printf("TITLE-REVIEW %s: %d of 7 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 89 : 0;
}

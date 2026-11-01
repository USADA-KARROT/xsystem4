/* G8 consumer: synthetic wire data, original AIN dispatcher/callback bytecode.
 * Include after global_init_fixture.inc and base_ui_review_fixture.inc.
 * Declare/call key_consumer_probe_step() from the existing probe_step hook.
 * The hook only observes; no instructions, callbacks or results are replaced.
 * Only the five message-source HLL functions are controlled in child processes.
 * This checks consumption ABI, not native event production/timing.
 */
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

static int kc_failures;
#define KC_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); kc_failures++; } } while (0)
static struct {
	bool active;
	int key_sink, click_sink;
	int parts, unique, vars[3], nvars, reads[3], pops;
	int seen, receiver, nargs, args[3];
} kc;

static void key_consumer_probe_step(void)
{
	if (!kc.active || !call_stack_ptr) return;
	struct function_call *f = &call_stack[call_stack_ptr - 1];
	if ((f->fno != kc.key_sink && f->fno != kc.click_sink)
			|| instr_ptr != ain->functions[f->fno].address) return;
	struct page *p = heap[f->page_slot].page;
	kc.seen++;
	kc.receiver = f->struct_page;
	kc.nargs = ain->functions[f->fno].nr_args;
	KC_CHECK(kc.pops == 1, "callback before exactly one Pop: %d", kc.pops);
	for (int i = 0; i < kc.nargs && i < 3; i++) kc.args[i] = p->values[i].i;
}

static int kc_parts(void) { KC_CHECK(!kc.pops, "parts read after Pop"); return kc.parts; }
static int kc_index(void) { KC_CHECK(!kc.pops, "index read after Pop"); return 0; }
static int kc_unique(void) { KC_CHECK(!kc.pops, "unique read after Pop"); return kc.unique; }
static int kc_var(int i)
{
	KC_CHECK(!kc.pops && i >= 0 && i < kc.nvars, "wire read index=%d nvars=%d pops=%d", i, kc.nvars, kc.pops);
	if (i < 0 || i >= kc.nvars || i >= 3) return -777;
	kc.reads[i]++;
	return kc.vars[i];
}
static void kc_pop(void) { kc.pops++; KC_CHECK(kc.pops == 1, "duplicate Pop"); }

static void kc_override(const char *name, void *fn)
{
	int found = 0;
	for (int i = 0; i < ain->nr_libraries; i++) {
		if (strcmp(ain->libraries[i].name, "PartsEngine") || !libraries[i]) continue;
		for (int j = 0; j < ain->libraries[i].nr_functions; j++)
			if (!strcmp(ain->libraries[i].functions[j].name, name)) {
				struct hll_function *binding = &libraries[i][j];
				if (!binding->fun || !binding->return_type || binding->cif.rtype != binding->return_type
						|| binding->nr_args != (unsigned)ain->libraries[i].functions[j].nr_arguments
						|| (binding->nr_args && !binding->args)) {
					printf("KEY-CONSUMER HLL %s has no prepared FFI binding\n", name); exit(2);
				}
				binding->fun = fn;
				found++;
			}
	}
	if (found != 1) { printf("KEY-CONSUMER missing/ambiguous HLL %s\n", name); exit(2); }
}

/* Discover an original empty void(int,...) body. No game names or bodies
 * are copied into the fixture, and no bytecode is generated or patched. */
static int kc_empty_sink(int nargs)
{
	if (!ain || !ain->code || !ain->functions || ain->code_size < 2 || nargs < 0 || nargs > 3) {
		puts("KEY-CONSUMER invalid sink schema/code");
		exit(2);
	}
	for (int i = 0; i < ain->nr_functions; i++) {
		struct ain_function *f = &ain->functions[i];
		if (f->nr_args != nargs || f->nr_vars < nargs || (nargs && !f->vars)
				|| f->return_type.data != AIN_VOID || f->return_type.rank || f->return_type.array_type
				|| f->is_lambda || f->is_label) continue;
		/* ain_addr_t is unsigned: reject the -1 sentinel and check the
		 * remaining span before reading, without address + 2 overflow. */
		size_t address = (size_t)f->address;
		if (f->address == (ain_addr_t)-1 || address > ain->code_size
				|| ain->code_size - address < 2) continue;
		bool ok = true;
		for (int j = 0; j < nargs; j++)
			ok &= f->vars[j].type.data == AIN_INT && !f->vars[j].type.rank && !f->vars[j].type.array_type;
		if (ok && (get_opcode(address) & ~OPTYPE_MASK) == RETURN) return i;
	}
	printf("KEY-CONSUMER no empty %d-int sink\n", nargs);
	exit(2);
}

static void kc_event(int set, const char *member, int obj, int fn)
{
	struct page *p = heap[set].page;
	int m = br_member(p->index, member);
	assert(ain->structures[p->index].members[m].type.data == AIN_OPTION);
	if (p->values[m].i > 0) heap_unref(p->values[m].i);
	p->values[m].i = heap_alloc_page(delegate_new_from_method_env(obj, fn, 0));
	p->values[m + 1].i = 0;
}

static void kc_source(int parts, int unique, int n, int a, int b, int c)
{
	kc.parts = parts; kc.unique = unique; kc.nvars = n;
	kc.vars[0] = a; kc.vars[1] = b; kc.vars[2] = c;
	memset(kc.reads, 0, sizeof(kc.reads));
	memset(kc.args, 0, sizeof(kc.args));
	kc.pops = kc.seen = kc.nargs = 0; kc.receiver = -1;
}

static int kc_dispatch(int manager, int type)
{
	call_method_args(8724, manager, 1, &type);
	return stack_pop().i;
}

static void kc_wire_body(void)
{
	assert(ain->version == 14);
	assert(ain->structures[327].nr_members > 29 && ain->structures[328].nr_members == 5);
	kc.key_sink = kc_empty_sink(2); kc.click_sink = kc_empty_sink(3);
	/* init_probe only links legacy exports. PostLink prepares the v14-only
	 * functions' CIFs before the child replaces their implementation. */
	bu_postlink();
	kc_override("GetMessagePartsNumber", kc_parts);
	kc_override("GetMessageDelegateIndex", kc_index);
	kc_override("GetMessageUniqueID", kc_unique);
	kc_override("GetMessageVariableInt", kc_var);
	kc_override("PopMessage", kc_pop);
	int manager = alloc_struct(328), sets[2] = { alloc_struct(327), alloc_struct(327) };
	heap[manager].page->values[0].i = sets[0];
	observer_fixture_array_one(heap[manager].page->values[1].i, AIN_ARRAY, 327, sets[1]);
	static const char *events[] = { "KeyTriggerEvent", "KeyDownEvent", "KeyPressEvent", "KeyUpEvent",
		"MouseLClickEvent", "MouseRClickEvent", "MouseMClickEvent" };
	int receivers[2][7];
	for (int s = 0; s < 2; s++) {
		heap[sets[s]].page->values[1].i = s ? 731 : 0;
		heap[sets[s]].page->values[2].i = 981;
		for (int e = 0; e < 7; e++) {
			receivers[s][e] = alloc_struct(327);
			kc_event(sets[s], events[e], receivers[s][e], e < 4 ? kc.key_sink : kc.click_sink);
		}
	}
	kc.active = true;
	static const int keys[] = { 1, 2, 4, 65 };
	for (int s = 0; s < 2; s++) {
		int parts = s ? 731 : 0;
		for (int type = 15; type <= 18; type++) for (int k = 0; k < 4; k++) {
			kc_source(parts, 981, 1, keys[k], -919, 827);
			int ok = kc_dispatch(manager, type);
			KC_CHECK(ok && kc.seen == 1 && kc.receiver == receivers[s][type - 15]
				&& kc.nargs == 2 && kc.args[0] == parts && kc.args[1] == keys[k],
				"type=%d parts=%d key=%d callback count=%d args=(%d,%d) route=%d",
				type, parts, keys[k], kc.seen, kc.args[0], kc.args[1], kc.receiver);
			KC_CHECK(kc.pops == 1 && kc.reads[0] == 1 && !kc.reads[1] && !kc.reads[2],
				"type=%d must read exactly one int then Pop", type);
		}
		for (int k = 0; k < 4; k++) {
			kc_source(parts, 981, 3, -37, 509, keys[k]);
			int ok = kc_dispatch(manager, 4);
			KC_CHECK(ok && kc.pops == 1 && kc.reads[0] == 1 && kc.reads[1] == 1 && kc.reads[2] == 1,
				"Click key=%d three-int decode", keys[k]);
			if (k < 3) KC_CHECK(kc.seen == 1 && kc.receiver == receivers[s][4 + k]
				&& kc.nargs == 3 && kc.args[0] == parts && kc.args[1] == -37 && kc.args[2] == 509,
				"Click key=%d parts=%d L/R/M route or callback args", keys[k], parts);
			else KC_CHECK(!kc.seen, "nonmouse key incorrectly routed as Click");
		}
	}
	kc_source(731, 982, 1, 1, 0, 0);
	KC_CHECK(!kc_dispatch(manager, 16) && !kc.pops && !kc.seen && !kc.reads[0],
		"stale local unique ID must reject before decode/callback");
	kc.active = false;
	printf("KC1 checked: 32 key + 8 Click + stale identity; sinks FUNC %d/%d\n", kc.key_sink, kc.click_sink);
}

/* Direct original Provider handlers: the receiver is seeded, not Attach'ed.
 * The actual virtual registration through slots 270/274 remains a gap. */
static void kc_provider_body(void)
{
	da_lambda_setup();
	int rect, offset;
	da_create_rect(&rect, &offset);
	if (da_failures || !da_live_page(rect) || offset < 0) {
		puts("KEY-CONSUMER Provider rect receiver unavailable"); exit(2);
	}
	/* The rect implements IRectParts, not IParts. Reuse the original
	 * selector's Core getter with the factory's actual [rect, offset]. */
	int dgslot = heap_alloc_page(delegate_new_from_method(-1, da_lambda_fno));
	int slots[] = { rect, offset }, nret = 0;
	union vm_value core[2] = {{ .i = -1 }, { .i = -1 }};
	da_dgcall(da_selector_dg, dgslot, 2, slots, NULL, core, &nret);
	if (da_live_page(dgslot)) heap_unref(dgslot);
	if (da_failures || nret != 2 || !da_live_page(core[0].i) || core[1].i < 0) {
		puts("KEY-CONSUMER Provider Core getter did not return an IParts pair"); exit(2);
	}
	struct page *core_page = heap[core[0].i].page;
	int iparts = ain_get_struct(ain, "IParts");
	bool core_iface = false;
	if (iparts >= 0 && core_page->type == STRUCT_PAGE && core_page->index >= 0
			&& core_page->index < ain->nr_structures) {
		struct ain_struct *s = &ain->structures[core_page->index];
		for (int i = 0; s->interfaces && i < s->nr_interfaces; i++)
			core_iface |= s->interfaces[i].struct_type == iparts && s->interfaces[i].vtable_offset == core[1].i;
	}
	if (!core_iface) { puts("KEY-CONSUMER Provider Core receiver/interface mismatch"); exit(2); }
	int st = ain_get_struct(ain, "PartsStateEventProvider");
	int posst = ain_get_struct(ain, "CASPosF");
	if (st < 0 || posst < 0) { puts("KEY-CONSUMER Provider schema missing"); exit(2); }
	int provider = alloc_struct(st);
	struct page *p = heap[provider].page;
	int mparts = br_member(st, "m_parts"), down = br_member(st, "m_isKeyDown");
	int enter = br_member(st, "m_isEnter"), enable = br_member(st, "<Enable>"), state = br_member(st, "<State>");
	int startpos = br_member(st, "m_dragStartPos");
	if (mparts < 0 || mparts > p->nr_vars - 3 || mparts > ain->structures[st].nr_members - 3
			|| ain->structures[st].members[mparts].type.data != AIN_OPTION
			|| ain->structures[st].members[mparts + 1].type.data != AIN_VOID
			|| ain->structures[st].members[mparts + 2].type.data != AIN_VOID) {
		puts("KEY-CONSUMER Provider m_parts option schema mismatch"); exit(2);
	}
	/* Transfer the getter's owned IParts pair into the present option. */
	p->values[mparts].i = core[0].i;
	p->values[mparts + 1].i = core[1].i;
	p->values[mparts + 2].i = 0;
	p->values[startpos].i = alloc_struct(posst);
	p->values[enable].i = 1; p->values[state].i = 1;
	p->values[down].i = 0; p->values[enter].i = 0;
	int args[] = { 731, 2 };
	for (int k = 2; k <= 4; k += 2) {
		args[1] = k;
		call_method_args(26942, provider, 2, args);
		call_method_args(26943, provider, 2, args);
		KC_CHECK(p->values[state].i == 1 && p->values[down].i == 0, "Provider R/M changed idle state key=%d", k);
	}
	args[1] = 1;
	call_method_args(26942, provider, 2, args);
	KC_CHECK(p->values[state].i == 3 && p->values[down].i == 1, "Provider left Down did not enter state 3");
	call_method_args(26942, provider, 2, args);
	KC_CHECK(p->values[state].i == 3 && p->values[down].i == 1, "Provider held Down changed state");
	for (int k = 2; k <= 4; k += 2) {
		args[1] = k;
		call_method_args(26942, provider, 2, args);
		call_method_args(26943, provider, 2, args);
		KC_CHECK(p->values[state].i == 3 && p->values[down].i == 1, "Provider R/M disturbed held left key=%d", k);
	}
	args[1] = 1;
	call_method_args(26943, provider, 2, args);
	KC_CHECK(p->values[state].i == 1 && !p->values[down].i, "Provider left Up outside did not restore state 1");
	p->values[enter].i = 1;
	call_method_args(26942, provider, 2, args);
	call_method_args(26943, provider, 2, args);
	KC_CHECK(p->values[state].i == 2 && !p->values[down].i, "Provider left Up inside did not restore state 2");
	p->values[enable].i = 0;
	call_method_args(26942, provider, 2, args);
	KC_CHECK(p->values[state].i == 2 && !p->values[down].i, "disabled Provider reacted to left Down");
	kc_failures += da_failures;
	puts("KC2 checked: original FUNC 26942/26943 left-only state transitions; Attach slots 270/274 NOT exercised");
}

static int test_key_consumer(void)
{
	static const struct { const char *name; void (*body)(void); } cases[] = {
		{ "KC1", kc_wire_body }, { "KC2", kc_provider_body },
	};
	int failed = 0, env = 0;
	for (int i = 0; i < 2; i++) {
		fflush(NULL);
		pid_t pid = fork();
		if (pid < 0) { perror("KEY-CONSUMER fork"); env++; continue; }
		if (!pid) {
			kc_failures = da_failures = 0;
			cases[i].body();
			fflush(NULL);
			_exit(kc_failures ? 1 : 0);
		}
		int status = 0;
		pid_t got;
		do { got = waitpid(pid, &status, 0); } while (got < 0 && errno == EINTR);
		if (got != pid) { perror("KEY-CONSUMER waitpid"); env++; continue; }
		if (WIFEXITED(status) && WEXITSTATUS(status) == 2) env++;
		else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			printf("KEY-CONSUMER %s failed status=%d\n", cases[i].name, status); failed++;
		}
	}
	printf("KEY-CONSUMER %s: failed=%d environment=%d; slot-registration coverage remains open\n",
		env ? "ENVIRONMENT FAILURE" : failed ? "FAIL" : "PASS", failed, env);
	return env ? 2 : failed ? 1 : 0;
}

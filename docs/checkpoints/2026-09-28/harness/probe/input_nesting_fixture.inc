/* input-nesting: v14 BeginInput/EndInput are nested scopes, not a latch.
 * Native 0x58a720/0x58a750 maintain depth at +0x1fc and reset/restart the
 * current input session; restarting samples the current button state.
 *
 * IN1-5 use the real AIN HLL declarations with synthetic clickable boxes,
 * production PE_UpdateInputState, and the production v14 message queue.
 * IN6 executes the game's tiny parts::detail wrappers unchanged. IN7 keeps
 * the v13 latch contract. IN8 resolves the new manager-reset hook by dlsym
 * so before-check can still compile against the unfixed revision. No GL,
 * game assets or files are needed.
 */
#include <dlfcn.h>

static int in_failures;
#define IN_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); in_failures++; } } while (0)

enum { IN_BOX = 18501, IN_OTHER = 18502 };

static void in_hll_scope(const char *name)
{
	union vm_value result[1];
	int n = br_call("PartsEngine", name, 0, 0, 0, NULL, result, 1);
	IN_CHECK(n == 0, "%s returned %d stack slots, want void", name, n);
}

static int in_click_number(void)
{
	union vm_value result[1] = { BR_I(-1) };
	int n = br_call("PartsEngine", "GetClickNumber", 0, 0, 0, NULL, result, 1);
	IN_CHECK(n == 1, "GetClickNumber returned %d stack slots, want 1", n);
	return result[0].i;
}

static void in_setup(void)
{
	br_input_setup();
	br_box(IN_BOX, 100, 100, 200, 80, true);
	br_box(IN_OTHER, 500, 100, 200, 80, true);
	extern bool key_state[];
	key_state[VK_LBUTTON] = false;
	mouse_set_pos(150, 140);
	PE_UpdateComponent(0);
	PE_UpdateInputState(0);
	br_msg_release();
}

static void in_button(bool down)
{
	extern bool key_state[];
	key_state[VK_LBUTTON] = down;
	PE_UpdateInputState(0);
}

static void in_messages(const char *step, int expected_count, int expected_parts)
{
	int count = 0, first_type = -1, first_parts = -1;
	for (int t; (t = br_msg_type()) >= 0 && count < 64; br_msg_pop()) {
		if (!count) {
			first_type = t;
			first_parts = br_msg_parts();
		}
		count++;
	}
	bool ok = count == expected_count;
	if (expected_count)
		ok = ok && first_type == 4 && first_parts == expected_parts;
	IN_CHECK(ok, "%s: messages=%d first=(%d,%d), want count=%d%s parts=%d",
		step, count, first_type, first_parts, expected_count,
		expected_count ? " type=4" : "", expected_parts);
}

static void in_fresh_click(const char *step, bool active)
{
	in_button(false);
	in_messages("released before fresh click", 0, 0);
	in_button(true);
	in_messages(step, active ? 1 : 0, IN_BOX);
	in_button(false);
	in_messages("release must not duplicate DOWN click", 0, 0);
}

static void in_two_levels(void)
{
	in_setup();
	in_hll_scope("BeginInput");
	in_hll_scope("BeginInput");
	in_fresh_click("inner scope", true);
	in_hll_scope("EndInput");
	in_fresh_click("outer scope after inner EndInput", true);
	in_hll_scope("EndInput");
	in_fresh_click("after outermost EndInput", false);
	printf("  IN1 nested HLL scope returns input to its caller\n");
}

static void in_three_levels_and_underflow(void)
{
	in_setup();
	in_hll_scope("EndInput");
	in_hll_scope("EndInput");
	in_hll_scope("BeginInput");
	in_hll_scope("BeginInput");
	in_hll_scope("BeginInput");
	in_fresh_click("depth three", true);
	in_hll_scope("EndInput");
	in_fresh_click("depth two after return", true);
	in_hll_scope("EndInput");
	in_fresh_click("depth one after return", true);
	in_hll_scope("EndInput");
	in_fresh_click("depth zero", false);
	in_hll_scope("EndInput");
	in_hll_scope("BeginInput");
	in_fresh_click("fresh scope after unmatched EndInput", true);
	in_hll_scope("EndInput");
	in_fresh_click("fresh scope ended", false);
	printf("  IN2 three levels, depth zero and unmatched EndInput checked\n");
}

static void in_begin_held(void)
{
	in_setup();
	extern bool key_state[];
	/* No input update between the hardware press and BeginInput: the begin
	 * itself must sample the button, otherwise the first update invents DOWN. */
	key_state[VK_LBUTTON] = true;
	in_hll_scope("BeginInput");
	PE_UpdateInputState(0);
	in_messages("held before outer BeginInput is not a new DOWN", 0, 0);
	IN_CHECK(in_click_number() == 0, "outer begin retained a ghost click number");
	in_fresh_click("real click after outer begin", true);
	key_state[VK_LBUTTON] = true;
	in_hll_scope("BeginInput");
	PE_UpdateInputState(0);
	in_messages("held before nested BeginInput is not a new DOWN", 0, 0);
	IN_CHECK(in_click_number() == 0, "nested begin did not reset clicked number");
	in_fresh_click("real click after nested begin", true);
	in_hll_scope("EndInput");
	in_hll_scope("EndInput");
	printf("  IN3 outer/nested BeginInput snapshot held buttons without ghost DOWN\n");
}

static void in_end_held(void)
{
	in_setup();
	in_hll_scope("BeginInput");
	in_hll_scope("BeginInput");
	extern bool key_state[];
	key_state[VK_LBUTTON] = true;
	/* Resume the parent before it has seen this press. Simply retaining a
	 * boolean or decrementing depth without resampling is insufficient. */
	in_hll_scope("EndInput");
	PE_UpdateInputState(0);
	in_messages("held across inner EndInput is not a new parent DOWN", 0, 0);
	IN_CHECK(in_click_number() == 0, "inner EndInput left a clicked number");
	in_fresh_click("parent gets a subsequent fresh press", true);
	in_hll_scope("EndInput");
	in_fresh_click("parent ended", false);
	printf("  IN4 inner EndInput snapshots held buttons while restoring parent input\n");
}

static void in_clicked_state(void)
{
	in_setup();
	in_hll_scope("BeginInput");
	in_button(true);
	IN_CHECK(in_click_number() == IN_BOX, "outer click number not recorded");
	in_hll_scope("BeginInput");
	IN_CHECK(in_click_number() == 0, "nested begin kept the parent's old click");
	in_messages("queued parent message survives nested BeginInput", 1, IN_BOX);
	in_fresh_click("inner click", true);
	IN_CHECK(in_click_number() == IN_BOX, "inner click number not recorded");
	/* Leave with a press in progress, as the game's click callback does. */
	in_button(true);
	in_hll_scope("EndInput");
	IN_CHECK(in_click_number() == 0, "return kept the child's old click");
	in_messages("queued child message survives EndInput", 1, IN_BOX);
	mouse_set_pos(550, 140);
	PE_UpdateInputState(0);
	in_messages("moving held button after return cannot click another part", 0, 0);
	IN_CHECK(in_click_number() == 0, "held movement restored stale clicked number");
	mouse_set_pos(150, 140);
	in_fresh_click("parent new click after child state was cleared", true);
	in_hll_scope("EndInput");
	IN_CHECK(in_click_number() == 0, "outer EndInput retained clicked number");
	printf("  IN5 Begin/End clear stale click state, preserve queued messages and accept new clicks\n");
}

static void in_wrapper(int f)
{
	int sp = stack_ptr;
	call_method0(f, -1);
	IN_CHECK(stack_ptr == sp, "input wrapper changed stack height");
}

static void in_true_wrappers(void)
{
	in_setup();
	int begin = ain_get_function(ain, "parts::detail::BeginInput");
	int end = ain_get_function(ain, "parts::detail::EndInput");
	IN_CHECK(begin >= 0 && end >= 0, "real input wrappers missing");
	if (begin < 0 || end < 0)
		return;
	unsigned long begin_before = calls[begin], end_before = calls[end];
	in_wrapper(begin);
	for (int i = 0; i < 8; i++) {
		in_wrapper(begin);
		in_fresh_click("real wrapper child click", true);
		in_wrapper(end);
		in_fresh_click("real wrapper parent click after return", true);
	}
	in_wrapper(end);
	in_fresh_click("real wrapper outer scope ended", false);
	IN_CHECK(calls[begin] - begin_before == 9 && calls[end] - end_before == 9,
		"expected 9 real begin and 9 real end wrapper entries");
	printf("  IN6 unchanged AIN wrappers survive eight open/return cycles\n");
}

static void in_legacy(void)
{
	in_setup();
	int version = ain->version;
	ain->version = 13;
	in_hll_scope("BeginInput");
	IN_CHECK(parts_began_click, "v13 BeginInput did not activate the latch");
	in_hll_scope("BeginInput");
	in_hll_scope("EndInput");
	IN_CHECK(!parts_began_click, "v13 Begin, Begin, End must still disable input");
	in_hll_scope("EndInput");
	in_hll_scope("BeginInput");
	IN_CHECK(parts_began_click, "v13 new BeginInput after unmatched EndInput failed");
	in_hll_scope("EndInput");
	IN_CHECK(!parts_began_click, "v13 final EndInput did not clear latch");
	ain->version = version;
	printf("  IN7 v13 Begin/End retain their non-nested latch contract\n");
}

static void in_manager_reset(void)
{
	in_setup();
	void (*reset_input)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "parts_reset_input");
	IN_CHECK(reset_input != NULL, "parts_reset_input is unavailable");
	if (!reset_input)
		return;
	in_hll_scope("BeginInput");
	in_hll_scope("BeginInput");
	in_button(true);
	IN_CHECK(in_click_number() == IN_BOX, "click before manager reset not recorded");
	/* An unrelated part's script-selected state is not stale hover/press.
	 * Read immediately, before any frame can legitimately update its state. */
	BU_PE("Parts_SetInputState", BU_I(IN_OTHER), BU_I(3));
	IN_CHECK(!parts_get(IN_OTHER)->is_hovered
		&& BU_PE("Parts_GetInputState", BU_I(IN_OTHER)).i == 3,
		"manual non-hover state fixture was not established");
	reset_input();
	IN_CHECK(!parts_began_click && in_click_number() == 0,
		"manager reset retained an active session or clicked number");
	IN_CHECK(BU_PE("Parts_GetInputState", BU_I(IN_OTHER)).i == 3,
		"manager reset overwrote an unrelated script-selected state");
	in_messages("session reset preserves an already queued HLL message", 1, IN_BOX);
	in_fresh_click("no input after manager reset", false);
	in_hll_scope("BeginInput");
	IN_CHECK(BU_PE("Parts_GetInputState", BU_I(IN_OTHER)).i == 3,
		"BeginInput overwrote an unrelated script-selected state");
	in_hll_scope("EndInput");
	IN_CHECK(BU_PE("Parts_GetInputState", BU_I(IN_OTHER)).i == 3,
		"EndInput overwrote an unrelated script-selected state");
	/* If reset only cleared the bool while preserving depth two, ending a
	 * fresh scope here would revive that discarded parent and emit a click. */
	in_fresh_click("fresh Begin/End does not revive pre-reset scopes", false);
	in_hll_scope("BeginInput");
	in_fresh_click("another fresh scope still works after reset", true);
	in_hll_scope("EndInput");
	in_fresh_click("another fresh scope ends independently", false);
	printf("  IN8 manager reset discards nesting, preserves queue/manual state and starts afresh\n");
}

static int test_input_nesting(void)
{
	void (*cases[])(void) = { in_two_levels, in_three_levels_and_underflow,
		in_begin_held, in_end_held, in_clicked_state, in_true_wrappers, in_legacy,
		in_manager_reset };
	const char *names[] = { "IN1", "IN2", "IN3", "IN4", "IN5", "IN6", "IN7", "IN8" };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			in_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(in_failures ? 1 : 0);
		}
		int st = 0;
		bool ok = pid > 0 && waitpid(pid, &st, 0) == pid
			&& WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("INPUT-NESTING %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS", failed,
		sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}

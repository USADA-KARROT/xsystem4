/*
 * confirm-exit: the confirmation box, system.Exit and closing the window
 * (headless).
 *
 * The original (static disassembly of this game's executable only):
 * system.MsgBoxOkCancel (0x728810) is a MessageBoxA on the game window and
 * returns 1 for OK only; started with the option "C" it shows no box and
 * answers OK (0x4b54af). system.Exit (0x693e30) clears [vm+0x2a0], and the
 * loop 0x667e80 executes no instruction after that. The window procedure
 * asks before it closes (WM_CLOSE: 0x4be920) while window setting 4 is on,
 * with a box of its own, and leaves the game running on the answer no.
 *
 * The probe has no window, so no box is ever shown here. In CE1 to CE7 the
 * answers come from XSYS4_AUTO_MSGBOX, or are OK without it. From CE8 on the
 * engine's stand-in for showing the box (vm_ask_test in vm.c) is set where a
 * scenario asks for real: the probe's function gets what SDL would get and
 * gives the answer, or fails. What a real box looks like and which window it
 * is on is for the GUI runs.
 *
 * CE1 system.MsgBoxOkCancel: the answer follows XSYS4_AUTO_MSGBOX, OK
 *     without a window; the text is converted from the game's encoding
 * CE2 the game's "quit the game" function: Cancel returns to the caller
 *     through the wait for Return and Escape, OK ends the program with
 *     status 0 before the Peek loop after the call; with window setting 4
 *     off nobody is asked
 * CE3 the game's "back to the title" function: Cancel does not reach the
 *     function that resets, OK does; window setting 3 turns the question off
 * CE4 system.Exit: through the ffi alone it only raises the flag, the
 *     argument is not the exit status, an ordinary library call leaves the
 *     flag down
 * CE5 after a modal box nothing stays held and the events of that time are
 *     gone, other events stay; the game's wait for Return and Escape then
 *     returns at once
 * CE6 SDL_QUIT: asks only while the v14 window setting 4 is on (set by the
 *     script, read from the file at start-up), never before v14, never for a
 *     signal; the answer no leaves the program running
 * CE7 guard: the system call SYS_EXIT of the older VM is left as it was
 * CE8 a box that is shown: what it is given (caption, text, buttons in the
 *     user's language), OK and Cancel, and the input reset after it; a set
 *     XSYS4_AUTO_MSGBOX still answers first, an empty one is no setting
 * CE9 a box that cannot be shown: Cancel for the script's question, as in
 *     the original, and the game's functions stay; yes for the question of
 *     the closing window, which closes; nothing is reset
 * CE10 guard: the older VM's SYS_MSGBOX_OK_CANCEL gives the box the same as
 *     before and a failure is still 0
 * CE11 closing the window: one answer for all the SDL_QUITs queued, a signal
 *     while the box is open ends the program whatever the answer, SIGINT as
 *     SIGTERM, handlers that appear later are watched too, and the
 *     read-message table is on disk before the box opens
 * CE12 system.Exit's flag is down again when the program ends, and after a
 *     restart
 *
 * Every scenario runs in a child, whose output is captured and printed.
 * Symbols this change adds are looked up with dlsym, so the fixture builds
 * against an engine without them and the cases fail there. A scenario's
 * words are English whatever the machine's language is (vm_ask_test again),
 * so that no child uses the system's locale interface after the fork.
 */
#include <signal.h>
#include <sys/wait.h>

static int ce_failures;
#define CE_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ce_failures++; } } while (0)

enum { CE_QUIT = 20524, CE_TITLE = 20525, CE_TITLE_RESET = 20526, CE_WAIT_KEYS = 20600 };
/* How a child ended when the scenario returned instead of ending the program. */
enum { CE_ALIVE = 50, CE_PEEK = 51, CE_SETUP = 52 };

static char ce_out[32768];

/* Runs fn in a child with stdout and stderr captured in ce_out: its exit
 * status (CE_ALIVE when fn returned), or minus the signal that ended it. */
static int ce_child(void (*fn)(void))
{
	int fd[2];
	fflush(stdout);
	fflush(stderr);
	if (pipe(fd))
		return -1000;
	pid_t pid = fork();
	if (pid == 0) {
		close(fd[0]);
		dup2(fd[1], 1);
		dup2(fd[1], 2);
		close(fd[1]);
		alarm(60);
		fn();
		fflush(stdout);
		fflush(stderr);
		_exit(CE_ALIVE);
	}
	close(fd[1]);
	size_t n = 0;
	char drop[512];
	for (;;) {
		bool full = n >= sizeof(ce_out) - 1;
		ssize_t r = read(fd[0], full ? drop : ce_out + n, full ? sizeof(drop) : sizeof(ce_out) - 1 - n);
		if (r <= 0)
			break;
		if (!full)
			n += r;
	}
	ce_out[n] = '\0';
	close(fd[0]);
	int st = 0;
	if (pid < 0 || waitpid(pid, &st, 0) != pid)
		return -1000;
	fputs(ce_out, stdout);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -WTERMSIG(st);
}

static void ce_expect(const char *what, void (*fn)(void), int want)
{
	int got = ce_child(fn);
	CE_CHECK(got == want, "%s: the child ended with %d, want %d", what, got, want);
	CE_CHECK(!strstr(ce_out, "FAIL:"), "%s: a check failed in the child", what);
}

/* How often needle is in the last child's output. */
static int ce_count(const char *needle)
{
	int n = 0;
	for (const char *p = ce_out; (p = strstr(p, needle)); p += strlen(needle))
		n++;
	return n;
}

/* The engine's stand-ins for the probe (vm.c); NULL on an engine without. */
struct ce_ask_test {
	int (*show)(const SDL_MessageBoxData *box, int *button);
	const char *language, *country;
};
static struct ce_ask_test *ce_test(void)
{
	return (struct ce_ask_test *)dlsym(RTLD_DEFAULT, "vm_ask_test");
}

/* XSYS4_AUTO_MSGBOX of the next scenario; NULL leaves it unset. Its locale
 * as SDL would give it; English without one. */
static const char *ce_auto, *ce_language, *ce_country;
static void ce_apply_auto(void)
{
	if (ce_auto)
		setenv("XSYS4_AUTO_MSGBOX", ce_auto, 1);
	else
		unsetenv("XSYS4_AUTO_MSGBOX");
	struct ce_ask_test *test = ce_test();
	if (test) {
		test->language = ce_language ? ce_language : "en";
		test->country = ce_country;
	}
}

static int ce_key_calls, ce_key_release_at;
static bool ce_key_is_down(int key)
{
	extern bool key_state[];
	if (++ce_key_calls == ce_key_release_at)
		memset(key_state, 0, VK_NR_KEYCODES * sizeof(bool));
	return key >= 0 && key < VK_NR_KEYCODES && key_state[key];
}

/* The loop after the script's system.Exit is `system.Peek; jump back`. */
static void ce_peek(void)
{
	printf("PEEK reached\n");
	fflush(stdout);
	_exit(CE_PEEK);
}

static void ce_hooks(void)
{
	gi_override_hll("IbisInputEngine", "Key_IsDown", ce_key_is_down);
	gi_override_hll("system", "Peek", ce_peek);
}

static void ce_run(int fno)
{
	_function_call(fno, VM_RETURN);
	vm_execute();
}

/* Makes a script function return at once (its callers are what is run). */
static void ce_stub(int fno)
{
	size_t a = ain->functions[fno].address;
	ain->code[a] = RETURN & 0xff;
	ain->code[a + 1] = RETURN >> 8;
}

/* The address of fno's first CALLHLL lib.fn; 0 when it has none. */
static size_t ce_callhll(int fno, const char *libname, const char *fnname)
{
	int lib = sp_lib(libname), fn = sp_libfn(lib, fnname);
	size_t start = ain->functions[fno].address, end = ain->code_size;
	for (int i = 0; i < ain->nr_functions; i++) {
		size_t a = ain->functions[i].address;
		if (a > start && a < end)
			end = a;
	}
	for (size_t a = start; a + 2 <= end; ) {
		int op = get_opcode(a);
		if (op < 0 || op >= NR_OPCODES)
			break;
		if (op == CALLHLL && LittleEndian_getDW(ain->code, a + 2) == lib
				&& LittleEndian_getDW(ain->code, a + 6) == fn)
			return a;
		a += instruction_width(op);
	}
	return 0;
}

static bool *ce_exit_pending(void)
{
	return (bool *)dlsym(RTLD_DEFAULT, "vm_exit_pending");
}

/* --- CE1 --- */
static const char *ce1_want_line;
static int ce_msgbox(const char *text)
{
	int s = cf_str_slot(text);
	int r = bu_call("system", "MsgBoxOkCancel", 1, 0, 1, (union vm_value[]){ BU_I(s) }).i;
	heap_unref(s);
	return r;
}

/* three characters in the game's encoding (two of them have an ASCII second
 * byte in GBK), a line break, and a second line */
static const char *ce_text(void)
{
	return game_charset_is_gbk()
		? "\xb4\x5f\xd5\x4a\xbf\xf2\nline two"
		: "\x8a\x6d\x94\x46\x9e\x79\nline two";
}
/* the same in UTF-8 */
#define CE_TEXT_UTF8 "\xe7\xa2\xba\xe8\xaa\x8d\xe6\xa1\x86"

static void ce1_scenario(void)
{
	ce_apply_auto();
	printf("ANSWER=%d\n", ce_msgbox(ce_text()));
}

static void ce1_quit_question(void)
{
	ce_apply_auto();
	bool (*ask)(int, const char *, bool) = (bool (*)(int, const char *, bool))dlsym(RTLD_DEFAULT, "vm_ask");
	if (!ask) {
		printf("  FAIL: NO SYMBOL vm_ask\n");
		return;
	}
	printf("ANSWER=%d\n", ask(1, NULL, true));
}

static void ce_answers(void)
{
	static const struct { const char *env; int want; const char *how; } rows[] = {
		{ NULL, 1, "(ok)" }, { "ok", 1, "(ok)" }, { "cancel", 0, "(cancel)" }, { "yes", 1, "(ok)" },
		{ "", 1, "(ok)" },
	};
	for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		char what[64], want[16];
		snprintf(what, sizeof what, "MsgBoxOkCancel with %s",
			 !rows[i].env ? "no setting" : rows[i].env[0] ? rows[i].env : "an empty setting");
		snprintf(want, sizeof want, "ANSWER=%d", rows[i].want);
		ce_auto = rows[i].env;
		ce_expect(what, ce1_scenario, CE_ALIVE);
		CE_CHECK(strstr(ce_out, want), "%s: the answer is not %d", what, rows[i].want);
		/* the text in UTF-8, the line break as a blank */
		CE_CHECK(strstr(ce_out, CE_TEXT_UTF8 " line two"),
			 "%s: the box's text was not converted from the game's encoding", what);
		CE_CHECK(strstr(ce_out, "without asking") && strstr(ce_out, rows[i].how),
			 "%s: not logged as answered %s without asking", what, rows[i].how);
		/* only a value that is neither answer is complained about */
		bool odd = rows[i].env && !strcmp(rows[i].env, "yes");
		CE_CHECK(odd == (strstr(ce_out, "is neither") != NULL), "%s: %s", what,
			 odd ? "the unknown value was not reported" : "reported as an unknown value");
	}
	ce_auto = "cancel";
	ce_expect("the quit question, cancel", ce1_quit_question, CE_ALIVE);
	CE_CHECK(strstr(ce_out, "ANSWER=0"), "the quit question was not answered no");
	ce_auto = NULL;
	ce_expect("the quit question, no window", ce1_quit_question, CE_ALIVE);
	CE_CHECK(strstr(ce_out, "ANSWER=1"), "the quit question without a window was not answered yes");
	printf("  CE1 the answer follows XSYS4_AUTO_MSGBOX and is OK without a window\n");
}

/* --- CE2, CE3: the game's own functions --- */
static int ce_fno, ce_setting, ce_setting_values[3], ce_nr_setting_values, ce_exit_argument = -1;

static bool ce_box_setup(void);
/* Whether the next script scenario asks through the probe's box. */
static bool ce_script_box;

static void ce_script_scenario(void)
{
	ce_apply_auto();
	if (ce_script_box && !ce_box_setup())
		return;
	ce_hooks();
	ce_stub(CE_TITLE_RESET);
	for (int i = 0; i < ce_nr_setting_values; i++)
		CE_CHECK(cw_set_window(ce_setting, ce_setting_values[i]), "SetWindowSetting(%d, %d) was refused",
			 ce_setting, ce_setting_values[i]);
	if (ce_exit_argument >= 0) {
		/* the PUSH before the CALLHLL system.Exit */
		size_t at = ce_callhll(CE_QUIT, "system", "Exit");
		CE_CHECK(at && get_opcode(at - 6) == PUSH, "no PUSH before the game's system.Exit");
		if (at)
			LittleEndian_putDW(ain->code, at - 4, ce_exit_argument);
	}
	ce_run(ce_fno);
	bool *pending = ce_exit_pending();
	printf("RETURNED wait=%lu reset=%lu keys=%d pending=%d\n", calls[CE_WAIT_KEYS], calls[CE_TITLE_RESET],
	       ce_key_calls, pending ? *pending : -1);
}

static void ce_script(const char *what, int fno, const char *env, int setting, int nvalues, const int *values,
		int want, const char *want_line)
{
	ce_fno = fno;
	ce_auto = env;
	ce_setting = setting;
	ce_nr_setting_values = nvalues;
	for (int i = 0; i < nvalues; i++)
		ce_setting_values[i] = values[i];
	ce_expect(what, ce_script_scenario, want);
	if (want_line)
		CE_CHECK(strstr(ce_out, want_line), "%s: want \"%s\"", what, want_line);
	else
		CE_CHECK(!strstr(ce_out, "RETURNED"), "%s: the function returned", what);
	CE_CHECK(!strstr(ce_out, "PEEK"), "%s: the loop after system.Exit ran", what);
}

static void ce_quit_game(void)
{
	CE_CHECK(ain_get_function(ain, "_system::detail::WaitToKeyUpFromMessageBox") == CE_WAIT_KEYS,
		 "function %d is not the wait for the keys", CE_WAIT_KEYS);
	CE_CHECK(ce_callhll(CE_QUIT, "system", "MsgBoxOkCancel") && ce_callhll(CE_QUIT, "system", "Exit"),
		 "function %d does not ask and exit", CE_QUIT);
	cw_window_dir();
	/* Cancel: back to the caller through the wait for Return and Escape
	 * (two key checks, both up) */
	ce_script("quit, cancel", CE_QUIT, "cancel", 4, 0, NULL, CE_ALIVE, "RETURNED wait=1 reset=0 keys=2 pending=0");
	CE_CHECK(strstr(ce_out, "(cancel)"), "quit, cancel: nobody was asked");
	/* OK: the program ends with status 0 and the Peek loop does not run */
	ce_script("quit, ok", CE_QUIT, "ok", 4, 0, NULL, 0, NULL);
	ce_script("quit, no window", CE_QUIT, NULL, 4, 0, NULL, 0, NULL);
	/* the confirmation turned off: nobody is asked */
	ce_script("quit, setting 4 off", CE_QUIT, "cancel", 4, 1, (int[]){ 0 }, 0, NULL);
	CE_CHECK(!strstr(ce_out, "Message box"), "quit, setting 4 off: somebody was asked");
	/* and on again */
	ce_script("quit, setting 4 off and on", CE_QUIT, "cancel", 4, 2, (int[]){ 0, 1 }, CE_ALIVE,
		  "RETURNED wait=1 reset=0 keys=2 pending=0");
	/* the other setting is not this one's */
	ce_script("quit, setting 3 off", CE_QUIT, "cancel", 3, 1, (int[]){ 0 }, CE_ALIVE,
		  "RETURNED wait=1 reset=0 keys=2 pending=0");
	/* the script's argument is not the exit status */
	ce_exit_argument = 5;
	ce_script("quit, ok, Exit(5)", CE_QUIT, "ok", 4, 0, NULL, 0, NULL);
	ce_exit_argument = -1;
	cw_window_rmdir();
	printf("  CE2 the game's quit function: cancel returns, ok ends the program\n");
}

static void ce_back_to_title(void)
{
	CE_CHECK(ce_callhll(CE_TITLE, "system", "MsgBoxOkCancel") && ce_callhll(CE_TITLE_RESET, "system", "Reset"),
		 "functions %d and %d do not ask and reset", CE_TITLE, CE_TITLE_RESET);
	cw_window_dir();
	ce_script("title, cancel", CE_TITLE, "cancel", 3, 0, NULL, CE_ALIVE, "RETURNED wait=1 reset=0 keys=2 pending=0");
	ce_script("title, ok", CE_TITLE, "ok", 3, 0, NULL, CE_ALIVE, "RETURNED wait=0 reset=1 keys=0 pending=0");
	ce_script("title, no window", CE_TITLE, NULL, 3, 0, NULL, CE_ALIVE, "RETURNED wait=0 reset=1 keys=0 pending=0");
	ce_script("title, setting 3 off", CE_TITLE, "cancel", 3, 1, (int[]){ 0 }, CE_ALIVE,
		  "RETURNED wait=0 reset=1 keys=0 pending=0");
	CE_CHECK(!strstr(ce_out, "Message box"), "title, setting 3 off: somebody was asked");
	ce_script("title, setting 3 off and on", CE_TITLE, "cancel", 3, 2, (int[]){ 0, 1 }, CE_ALIVE,
		  "RETURNED wait=1 reset=0 keys=2 pending=0");
	ce_script("title, setting 4 off", CE_TITLE, "cancel", 4, 1, (int[]){ 0 }, CE_ALIVE,
		  "RETURNED wait=1 reset=0 keys=2 pending=0");
	cw_window_rmdir();
	printf("  CE3 the game's back-to-title function: cancel does not reset, ok does\n");
}

/* --- CE4 --- */
static void ce4_scenario(void)
{
	bool *pending = ce_exit_pending();
	if (!pending) {
		printf("  FAIL: NO SYMBOL vm_exit_pending\n");
		return;
	}
	CE_CHECK(!*pending, "the flag is up before any call");
	bu_call("system", "GetTime", 0, 0, 0, NULL);
	CE_CHECK(!*pending, "system.GetTime raised the flag");
	bu_call("system", "Exit", 1, 0, 1, (union vm_value[]){ BU_I(0) });
	CE_CHECK(*pending, "system.Exit(0) did not raise the flag");
	*pending = false;
	bu_call("system", "Exit", 1, 0, 1, (union vm_value[]){ BU_I(1) });
	CE_CHECK(*pending, "system.Exit(1) did not raise the flag");
	*pending = false;
	CE_CHECK(stack_ptr == 0, "the calls left %d values on the stack", stack_ptr);
	printf("STILL RUNNING\n");
}

static void ce_exit_flag(void)
{
	ce_expect("system.Exit through the ffi", ce4_scenario, CE_ALIVE);
	CE_CHECK(strstr(ce_out, "STILL RUNNING"), "system.Exit through the ffi ended the program");
	printf("  CE4 through the ffi alone system.Exit only raises the flag\n");
}

/* --- CE5 --- */
static int ce5_count(Uint32 type)
{
	SDL_Event e[8];
	SDL_PumpEvents();
	return SDL_PeepEvents(e, 8, SDL_PEEKEVENT, type, type);
}

static void ce5_scenario(void)
{
	extern bool key_state[];
	void (*closed)(void) = (void (*)(void))dlsym(RTLD_DEFAULT, "input_modal_closed");
	if (!closed) {
		printf("  FAIL: NO SYMBOL input_modal_closed\n");
		return;
	}
	if (SDL_Init(SDL_INIT_EVENTS)) {
		printf("  FAIL: SDL_Init: %s\n", SDL_GetError());
		return;
	}
	static const int held[] = { VK_RETURN, VK_ESCAPE, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_CONTROL, VK_SPACE };
	for (unsigned i = 0; i < sizeof(held) / sizeof(held[0]); i++)
		key_state[held[i]] = true;
	SDL_Event e = { .type = SDL_KEYDOWN };
	e.key.keysym.scancode = SDL_SCANCODE_RETURN;
	SDL_PushEvent(&e);
	e.type = SDL_KEYUP;
	SDL_PushEvent(&e);
	e = (SDL_Event){ .type = SDL_MOUSEBUTTONDOWN };
	e.button.button = SDL_BUTTON_LEFT;
	SDL_PushEvent(&e);
	e.type = SDL_MOUSEBUTTONUP;
	SDL_PushEvent(&e);
	e = (SDL_Event){ .type = SDL_USEREVENT };
	SDL_PushEvent(&e);
	closed();
	for (unsigned i = 0; i < sizeof(held) / sizeof(held[0]); i++)
		CE_CHECK(!key_state[held[i]], "key %d is still held", held[i]);
	CE_CHECK(ce5_count(SDL_KEYDOWN) == 0 && ce5_count(SDL_KEYUP) == 0, "keyboard events of the box are still queued");
	CE_CHECK(ce5_count(SDL_MOUSEBUTTONDOWN) == 0 && ce5_count(SDL_MOUSEBUTTONUP) == 0,
		 "mouse button events of the box are still queued");
	CE_CHECK(ce5_count(SDL_USEREVENT) == 1, "another event was dropped");
	/* the events were not let through to the engine's state either */
	SDL_Delay(2);
	handle_events();
	CE_CHECK(!key_state[VK_RETURN] && !key_state[VK_LBUTTON], "a dropped event reached the key state");

	/* The script's wait after a cancelled box: with Return still held it
	 * keeps updating the view (stubbed; the key comes up at the fifth
	 * check); after the modal reset it returns at once. */
	int view = ain_get_function(ain, "AFL_View_Update");
	CE_CHECK(view > 0, "AFL_View_Update not found");
	if (view <= 0)
		return;
	ce_hooks();
	ce_stub(view);
	key_state[VK_RETURN] = true;
	ce_key_calls = 0;
	ce_key_release_at = 5;
	ce_run(CE_WAIT_KEYS);
	CE_CHECK(calls[view] == 4, "with Return held the wait updated the view %lu times, want 4", calls[view]);
	key_state[VK_RETURN] = key_state[VK_ESCAPE] = true;
	closed();
	ce_key_calls = 0;
	ce_key_release_at = 0;
	ce_run(CE_WAIT_KEYS);
	CE_CHECK(calls[view] == 4 && ce_key_calls == 2, "after the modal reset the wait did not return at once "
		 "(%lu view updates, %d key checks)", calls[view], ce_key_calls);
}

static void ce_modal_input(void)
{
	ce_expect("input after a modal box", ce5_scenario, CE_ALIVE);
	printf("  CE5 nothing stays held after a modal box\n");
}

/* --- CE6 --- */
enum { CE6_PLAIN, CE6_ON, CE6_MODULE_INIT, CE6_SET_OFF, CE6_SET_OFF_ON, CE6_WRITE_OFF, CE6_V13, CE6_SIGNAL,
       CE6_SIGNAL_OFF, CE6_WINDOW_EVENTS };
static int ce6_how;

static void ce6_quit_event(void)
{
	SDL_Event e = { .type = SDL_QUIT };
	SDL_PushEvent(&e);
	SDL_Delay(2);
	if (ce6_how == CE6_WINDOW_EVENTS)
		handle_window_events();
	else
		handle_events();
}

static void ce6_module_init(void)
{
	extern struct static_library lib_SystemService;
	void (*init)(void) = (void (*)(void))sr_fn(&lib_SystemService, "_ModuleInit");
	if (init)
		init();
	else
		printf("  FAIL: SystemService has no _ModuleInit\n");
}

static void ce6_scenario(void)
{
	ce_apply_auto();
	void (*confirm)(bool) = (void (*)(bool))dlsym(RTLD_DEFAULT, "input_set_close_confirm");
	if (!confirm && ce6_how != CE6_PLAIN && ce6_how != CE6_V13 && ce6_how != CE6_SIGNAL_OFF) {
		printf("  FAIL: NO SYMBOL input_set_close_confirm\n");
		return;
	}
	if (SDL_Init(SDL_INIT_EVENTS)) {
		printf("  FAIL: SDL_Init: %s\n", SDL_GetError());
		return;
	}
	switch (ce6_how) {
	case CE6_PLAIN:
	case CE6_SIGNAL_OFF:
		break;
	case CE6_ON:
	case CE6_SIGNAL:
	case CE6_WINDOW_EVENTS:
		confirm(true);
		break;
	case CE6_MODULE_INIT:
		ce6_module_init();
		break;
	case CE6_SET_OFF:
		ce6_module_init();
		CE_CHECK(cw_set_window(4, 0), "SetWindowSetting(4, 0) was refused");
		break;
	case CE6_SET_OFF_ON:
		ce6_module_init();
		CE_CHECK(cw_set_window(4, 0) && cw_set_window(4, 1), "SetWindowSetting(4, ...) was refused");
		break;
	case CE6_WRITE_OFF:
		/* leaves the setting in the file for the next scenario */
		CE_CHECK(cw_set_window(4, 0), "SetWindowSetting(4, 0) was refused");
		printf("STILL RUNNING\n");
		return;
	case CE6_V13:
		/* the older table: 6 is its "confirm closing the game" */
		ain->version = 13;
		ce6_module_init();
		CE_CHECK(cw_set_window(6, 1), "the older SetWindowSetting(6, 1) was refused");
		break;
	}
	if (ce6_how == CE6_SIGNAL || ce6_how == CE6_SIGNAL_OFF) {
		/* one pump first: the signals are watched from then on (SDL's
		 * clock starts at its first reading, and handle_events pumps
		 * once per millisecond of it) */
		SDL_GetTicks();
		SDL_Delay(2);
		handle_events();
		raise(SIGTERM);
		SDL_Delay(2);
		handle_events();
	} else {
		ce6_quit_event();
	}
	printf("STILL RUNNING\n");
}

static void ce6(const char *what, int how, const char *env, int want)
{
	ce6_how = how;
	ce_auto = env;
	ce_expect(what, ce6_scenario, want);
	CE_CHECK((want == CE_ALIVE) == (strstr(ce_out, "STILL RUNNING") != NULL), "%s: wrong end", what);
}

static void ce_close_window(void)
{
	cw_window_dir();
	/* nothing turned the confirmation on (before v14, and before the
	 * libraries start): the program ends, whatever the answer would be */
	ce6("SDL_QUIT, confirmation never set", CE6_PLAIN, "cancel", 0);
	CE_CHECK(!strstr(ce_out, "Message box"), "SDL_QUIT asked although nothing turned the confirmation on");
	ce6("SDL_QUIT, confirmation on, no", CE6_ON, "cancel", CE_ALIVE);
	CE_CHECK(strstr(ce_out, "(cancel)"), "SDL_QUIT with the confirmation on asked nobody");
	ce6("SDL_QUIT, confirmation on, yes", CE6_ON, "ok", 0);
	ce6("SDL_QUIT, confirmation on, no window", CE6_ON, NULL, 0);
	/* the debugger's pump */
	ce6("handle_window_events, no", CE6_WINDOW_EVENTS, "cancel", CE_ALIVE);
	ce6("handle_window_events, yes", CE6_WINDOW_EVENTS, "ok", 0);
	/* SystemService: the setting starts on, the script turns it off and on */
	ce6("SDL_QUIT after SystemService started", CE6_MODULE_INIT, "cancel", CE_ALIVE);
	ce6("SDL_QUIT, setting 4 off", CE6_SET_OFF, "cancel", 0);
	CE_CHECK(!strstr(ce_out, "Message box"), "SDL_QUIT asked with setting 4 off");
	ce6("SDL_QUIT, setting 4 off and on", CE6_SET_OFF_ON, "cancel", CE_ALIVE);
	/* the setting comes back from the file at the next start */
	ce6("write setting 4 off", CE6_WRITE_OFF, "cancel", CE_ALIVE);
	ce6("SDL_QUIT, setting 4 off in the file", CE6_MODULE_INIT, "cancel", 0);
	remove(cw_window_path);
	/* before v14 nobody is asked, also with the older table's setting on */
	ce6("SDL_QUIT before v14", CE6_V13, "cancel", 0);
	CE_CHECK(!strstr(ce_out, "Message box"), "SDL_QUIT asked before v14");
	/* a signal ends the program at once */
	ce6("SIGTERM, confirmation on", CE6_SIGNAL, "cancel", 0);
	CE_CHECK(!strstr(ce_out, "Message box"), "SIGTERM asked");
	ce6("SIGTERM, confirmation never set", CE6_SIGNAL_OFF, "cancel", 0);
	cw_window_rmdir();
	printf("  CE6 SDL_QUIT asks only for the user, only with the v14 setting on\n");
}

/* --- CE7 --- */
static void ce7_scenario(void)
{
	stack_push(1);
	system_call(SYS_EXIT);
	CE_CHECK(stack_ptr == 0, "SYS_EXIT left %d values on the stack", stack_ptr);
	bool *pending = ce_exit_pending();
	CE_CHECK(!pending || !*pending, "SYS_EXIT raised system.Exit's flag");
	printf("STILL RUNNING\n");
}

static void ce_old_syscall(void)
{
	ce_expect("SYS_EXIT", ce7_scenario, CE_ALIVE);
	CE_CHECK(strstr(ce_out, "STILL RUNNING"), "SYS_EXIT ended the program");
	printf("  CE7 guard: SYS_EXIT is left as it was\n");
}


/* --- CE8 to CE12: the probe's box --- */
enum { CE_BOX_FAILS = -100 };
/* The answer of the next box: a button's ID, -1 for a box that was closed
 * with no button (SDL reports that as shown), or CE_BOX_FAILS. */
static int ce_box_answer;
/* A signal that arrives while the box is open; a file whose presence the
 * box reports. */
static int ce_box_signal;
static const char *ce_box_file;

static int ce_box(const SDL_MessageBoxData *box, int *button)
{
	extern bool key_state[];
	printf("BOX title=[%s] text=[%s] window=%d buttons=%d", box->title, box->message, box->window != NULL,
	       box->numbuttons);
	for (int i = 0; i < box->numbuttons; i++)
		printf(" [%s]=%d/%u", box->buttons[i].text, box->buttons[i].buttonid, (unsigned)box->buttons[i].flags);
	printf(" held=%d", key_state[VK_CONTROL]);
	if (ce_box_file)
		printf(" file=%d", access(ce_box_file, F_OK) == 0);
	printf("\n");
	fflush(stdout);
	if (ce_box_signal)
		raise(ce_box_signal);
	if (ce_box_answer == CE_BOX_FAILS) {
		SDL_SetError("the probe shows no box");
		return -1;
	}
	*button = ce_box_answer;
	return 0;
}

/* From here the child asks for real, through ce_box. */
static bool ce_box_setup(void)
{
	struct ce_ask_test *test = ce_test();
	if (!test) {
		printf("  FAIL: NO SYMBOL vm_ask_test\n");
		return false;
	}
	if (SDL_Init(SDL_INIT_EVENTS)) {
		printf("  FAIL: SDL_Init: %s\n", SDL_GetError());
		return false;
	}
	test->show = ce_box;
	config.game_name = strdup("probe game");
	return true;
}

/* --- CE8 --- */
enum { CE8_MSGBOX, CE8_QUIT_QUESTION, CE8_OLD_SYSCALL };
static int ce8_how;

static void ce8_scenario(void)
{
	extern bool key_state[];
	ce_apply_auto();
	if (!ce_box_setup())
		return;
	key_state[VK_CONTROL] = key_state[VK_RETURN] = key_state[VK_LBUTTON] = true;
	int answer;
	if (ce8_how == CE8_QUIT_QUESTION) {
		bool (*ask)(int, const char *, bool) = (bool (*)(int, const char *, bool))dlsym(RTLD_DEFAULT, "vm_ask");
		answer = ask(1, NULL, true);
	} else if (ce8_how == CE8_OLD_SYSCALL) {
		stack_push(cf_str_slot(ce_text()));
		system_call(SYS_MSGBOX_OK_CANCEL);
		answer = stack_pop().i;
	} else {
		answer = ce_msgbox(ce_text());
	}
	printf("ANSWER=%d held=%d%d%d stack=%d\n", answer, key_state[VK_CONTROL], key_state[VK_RETURN],
	       key_state[VK_LBUTTON], stack_ptr);
}

/* Runs one box and checks the answer line and how many boxes were shown. */
static void ce8(const char *what, int how, const char *env, int answer, const char *want_answer, int want_boxes)
{
	ce8_how = how;
	ce_auto = env;
	ce_box_answer = answer;
	ce_expect(what, ce8_scenario, CE_ALIVE);
	CE_CHECK(strstr(ce_out, want_answer), "%s: want \"%s\"", what, want_answer);
	CE_CHECK(ce_count("BOX title=") == want_boxes, "%s: %d boxes were shown, want %d", what, ce_count("BOX title="),
		 want_boxes);
}

#define CE_BOX_OK_CANCEL "BOX title=[probe game] text=[" CE_TEXT_UTF8 "\nline two] window=0 buttons=2 " \
	"[OK]=1/1 [Cancel]=0/2 held=1\n"

static void ce_shown_box(void)
{
	/* OK and Cancel: the box gets the game's name, the text in UTF-8 with
	 * its line break, the default button for Return and the other for
	 * Escape; what was held when it opened is up when it has closed */
	ce8("a box answered OK", CE8_MSGBOX, NULL, 1, "ANSWER=1 held=000 stack=0", 1);
	CE_CHECK(strstr(ce_out, CE_BOX_OK_CANCEL), "a box answered OK: the box was not given what it should show");
	CE_CHECK(strstr(ce_out, "Message box answered (ok): " CE_TEXT_UTF8 " line two") && !strstr(ce_out, "without asking"),
		 "a box answered OK: not logged as answered ok");
	ce8("a box answered Cancel", CE8_MSGBOX, NULL, 0, "ANSWER=0 held=000 stack=0", 1);
	CE_CHECK(strstr(ce_out, CE_BOX_OK_CANCEL), "a box answered Cancel: the box was not given what it should show");
	CE_CHECK(strstr(ce_out, "Message box answered (cancel)"), "a box answered Cancel: not logged as cancelled");
	/* closed with no button: shown, so Cancel and not a failure */
	ce8("a box closed with no button", CE8_MSGBOX, NULL, -1, "ANSWER=0 held=000 stack=0", 1);
	CE_CHECK(strstr(ce_out, "Message box answered (cancel)") && !strstr(ce_out, "could not be shown"),
		 "a box closed with no button: not logged as cancelled");
	/* the question before the window closes: yes and no */
	ce8("the quit question answered yes", CE8_QUIT_QUESTION, NULL, 1, "ANSWER=1 held=000 stack=0", 1);
	CE_CHECK(strstr(ce_out, "BOX title=[probe game] text=[The game is about to close.") && strstr(ce_out, " [Yes]=1/1 [No]=0/2 held=1\n"),
		 "the quit question: the box was not given what it should show");
	ce8("the quit question answered no", CE8_QUIT_QUESTION, NULL, 0, "ANSWER=0 held=000 stack=0", 1);
	/* a set answer comes first: nobody is asked and nothing is reset */
	ce8("a box with the answer set to cancel", CE8_MSGBOX, "cancel", 1, "ANSWER=0 held=111 stack=0", 0);
	ce8("a box with the answer set to ok", CE8_MSGBOX, "ok", 0, "ANSWER=1 held=111 stack=0", 0);
	/* an empty setting is none, and so is one that is neither answer */
	ce8("a box with an empty setting", CE8_MSGBOX, "", 0, "ANSWER=0 held=000 stack=0", 1);
	CE_CHECK(!strstr(ce_out, "is neither"), "an empty setting was reported as an unknown value");
	ce8("a box with an unknown setting", CE8_MSGBOX, "yes", 0, "ANSWER=0 held=000 stack=0", 1);
	CE_CHECK(strstr(ce_out, "is neither"), "the unknown setting was not reported");

	/* the buttons in the user's language; Chinese by the script where the
	 * locale names one (either way SDL cuts it), by the region otherwise */
	static const struct { const char *language, *country, *buttons; } rows[] = {
		{ "en", "US", " [OK]=1/1 [Cancel]=0/2 " },
		{ "fr", "FR", " [OK]=1/1 [Cancel]=0/2 " },
		{ "ja", "JP", " [OK]=1/1 [\xe3\x82\xad\xe3\x83\xa3\xe3\x83\xb3\xe3\x82\xbb\xe3\x83\xab]=0/2 " },
		{ "ja", NULL, " [OK]=1/1 [\xe3\x82\xad\xe3\x83\xa3\xe3\x83\xb3\xe3\x82\xbb\xe3\x83\xab]=0/2 " },
		{ "zh-Hant", "TW", " [\xe7\xa2\xba\xe5\xae\x9a]=1/1 [\xe5\x8f\x96\xe6\xb6\x88]=0/2 " },
		{ "zh", "Hant", " [\xe7\xa2\xba\xe5\xae\x9a]=1/1 [\xe5\x8f\x96\xe6\xb6\x88]=0/2 " },
		{ "zh", "TW", " [\xe7\xa2\xba\xe5\xae\x9a]=1/1 [\xe5\x8f\x96\xe6\xb6\x88]=0/2 " },
		{ "zh", "HK", " [\xe7\xa2\xba\xe5\xae\x9a]=1/1 [\xe5\x8f\x96\xe6\xb6\x88]=0/2 " },
		{ "zh-Hans", "CN", " [\xe7\xa1\xae\xe5\xae\x9a]=1/1 [\xe5\x8f\x96\xe6\xb6\x88]=0/2 " },
		{ "zh", "Hans", " [\xe7\xa1\xae\xe5\xae\x9a]=1/1 [\xe5\x8f\x96\xe6\xb6\x88]=0/2 " },
		{ "zh-Hans", "HK", " [\xe7\xa1\xae\xe5\xae\x9a]=1/1 [\xe5\x8f\x96\xe6\xb6\x88]=0/2 " },
		{ "zh", "CN", " [\xe7\xa1\xae\xe5\xae\x9a]=1/1 [\xe5\x8f\x96\xe6\xb6\x88]=0/2 " },
		{ "zh", NULL, " [\xe7\xa1\xae\xe5\xae\x9a]=1/1 [\xe5\x8f\x96\xe6\xb6\x88]=0/2 " },
	};
	for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		char what[64];
		snprintf(what, sizeof what, "the buttons for %s %s", rows[i].language, rows[i].country ? rows[i].country : "-");
		ce_language = rows[i].language;
		ce_country = rows[i].country;
		ce8(what, CE8_MSGBOX, NULL, 1, "ANSWER=1 held=000 stack=0", 1);
		CE_CHECK(strstr(ce_out, rows[i].buttons), "%s are not%s", what, rows[i].buttons);
	}
	/* the quit question follows the language too (not the English one) */
	ce_language = "zh";
	ce_country = "Hant";
	ce8("the quit question for zh Hant", CE8_QUIT_QUESTION, NULL, 0, "ANSWER=0 held=000 stack=0", 1);
	CE_CHECK(!strstr(ce_out, "The game is about to close.") && strstr(ce_out, " [\xe6\x98\xaf]=1/1 [\xe5\x90\xa6]=0/2 "),
		 "the quit question for zh Hant is not in Chinese");
	ce_language = ce_country = NULL;
	printf("  CE8 a box that is shown: its contents, the answers, the input reset\n");
}

/* --- CE9 --- */
static void ce9_close_scenario(void)
{
	ce_apply_auto();
	void (*confirm)(bool) = (void (*)(bool))dlsym(RTLD_DEFAULT, "input_set_close_confirm");
	if (!confirm) {
		printf("  FAIL: NO SYMBOL input_set_close_confirm\n");
		return;
	}
	if (!ce_box_setup())
		return;
	confirm(true);
	SDL_Event e = { .type = SDL_QUIT };
	SDL_PushEvent(&e);
	SDL_Delay(2);
	handle_events();
	printf("STILL RUNNING\n");
}

static void ce_failed_box(void)
{
	/* the script's question: Cancel, with nothing reset, and logged as what it is */
	ce8("a box that cannot be shown", CE8_MSGBOX, NULL, CE_BOX_FAILS, "ANSWER=0 held=111 stack=0", 1);
	CE_CHECK(strstr(ce_out, "could not be shown (the probe shows no box)"), "the failure was not logged with SDL's error");
	CE_CHECK(!strstr(ce_out, "Message box answered") && !strstr(ce_out, "(cancel)") && strstr(ce_out, "taken as cancel"),
		 "the failure was logged as an answer");
	/* the question of the closing window: yes */
	ce8("a quit question that cannot be shown", CE8_QUIT_QUESTION, NULL, CE_BOX_FAILS, "ANSWER=1 held=111 stack=0", 1);
	CE_CHECK(strstr(ce_out, "taken as ok"), "the quit question that cannot be shown was not logged as taken for yes");
	/* the game's functions stay where they are, as after Cancel */
	ce_script_box = true;
	ce_box_answer = CE_BOX_FAILS;
	cw_window_dir();
	ce_script("title, the box cannot be shown", CE_TITLE, NULL, 3, 0, NULL, CE_ALIVE,
		  "RETURNED wait=1 reset=0 keys=2 pending=0");
	CE_CHECK(ce_count("BOX title=") == 1, "title, the box cannot be shown: %d boxes", ce_count("BOX title="));
	ce_script("quit, the box cannot be shown", CE_QUIT, NULL, 4, 0, NULL, CE_ALIVE,
		  "RETURNED wait=1 reset=0 keys=2 pending=0");
	CE_CHECK(ce_count("BOX title=") == 1, "quit, the box cannot be shown: %d boxes", ce_count("BOX title="));
	/* with a box that can be shown the answer decides */
	ce_box_answer = 0;
	ce_script("title, the box answered Cancel", CE_TITLE, NULL, 3, 0, NULL, CE_ALIVE,
		  "RETURNED wait=1 reset=0 keys=2 pending=0");
	ce_script("quit, the box answered Cancel", CE_QUIT, NULL, 4, 0, NULL, CE_ALIVE,
		  "RETURNED wait=1 reset=0 keys=2 pending=0");
	ce_box_answer = 1;
	ce_script("quit, the box answered OK", CE_QUIT, NULL, 4, 0, NULL, 0, NULL);
	ce_script_box = false;
	cw_window_rmdir();
	/* and the window closes */
	ce_auto = NULL;
	ce_box_answer = CE_BOX_FAILS;
	ce_expect("SDL_QUIT, the box cannot be shown", ce9_close_scenario, 0);
	CE_CHECK(ce_count("BOX title=") == 1 && strstr(ce_out, "could not be shown") && !strstr(ce_out, "STILL RUNNING"),
		 "SDL_QUIT, the box cannot be shown: the program did not end on the failure");
	ce_box_answer = 0;
	ce_expect("SDL_QUIT, the box answered no", ce9_close_scenario, CE_ALIVE);
	CE_CHECK(strstr(ce_out, " [Yes]=1/1 [No]=0/2 ") && strstr(ce_out, "STILL RUNNING"),
		 "SDL_QUIT, the box answered no: the program did not go on");
	ce_box_answer = 1;
	ce_expect("SDL_QUIT, the box answered yes", ce9_close_scenario, 0);
	printf("  CE9 a box that cannot be shown: Cancel for the script, yes for the closing window, nothing reset\n");
}

/* --- CE10 --- */
static void ce_old_box(void)
{
	/* the older VM's box: no window, its own caption and English buttons
	 * whatever the language, never the automatic answer, no input reset */
	ce_language = "zh";
	ce_country = "Hant";
	ce8("SYS_MSGBOX_OK_CANCEL answered OK", CE8_OLD_SYSCALL, "cancel", 1, "ANSWER=1 held=111 stack=0", 1);
	CE_CHECK(strstr(ce_out, "BOX title=[xsystem4] text=[" CE_TEXT_UTF8 "\nline two] window=0 buttons=2 "
			"[OK]=1/1 [Cancel]=0/2 held=1\n"),
		 "SYS_MSGBOX_OK_CANCEL: the box was not given what it was before");
	CE_CHECK(!strstr(ce_out, "Message box"), "SYS_MSGBOX_OK_CANCEL went through the new question");
	ce8("SYS_MSGBOX_OK_CANCEL answered Cancel", CE8_OLD_SYSCALL, "ok", 0, "ANSWER=0 held=111 stack=0", 1);
	/* a failure is 0 with the warning it always had */
	ce8("SYS_MSGBOX_OK_CANCEL, the box cannot be shown", CE8_OLD_SYSCALL, NULL, CE_BOX_FAILS,
	    "ANSWER=0 held=111 stack=0", 1);
	CE_CHECK(strstr(ce_out, "Error displaying message box") && !strstr(ce_out, "could not be shown"),
		 "SYS_MSGBOX_OK_CANCEL: the failure is not reported as before");
	ce_language = ce_country = NULL;
	printf("  CE10 guard: SYS_MSGBOX_OK_CANCEL is left as it was\n");
}

/* --- CE11 --- */
enum { CE11_TWO_QUITS, CE11_QUIT_AGAIN, CE11_WINDOW_EVENTS, CE11_SIGNAL_IN_BOX, CE11_SIGNAL, CE11_LATE_HANDLERS,
       CE11_TABLE };
static int ce11_how, ce11_signal;
static bool ce11_box;
static char ce11_table_path[1100];
static volatile sig_atomic_t ce11_got_int, ce11_got_term;
static void ce11_on_int(int sig) { (void)sig; ce11_got_int = 1; }
static void ce11_on_term(int sig) { (void)sig; ce11_got_term = 1; }

static void ce11_handler(int sig, void (*fn)(int))
{
	struct sigaction action;
	memset(&action, 0, sizeof action);
	action.sa_handler = fn;
	sigemptyset(&action.sa_mask);
	sigaction(sig, &action, NULL);
}

static void ce11_quits(int n)
{
	SDL_Event e = { .type = SDL_QUIT };
	while (n-- > 0)
		SDL_PushEvent(&e);
}

/* handle_events pumps once per millisecond of SDL's clock */
static void ce11_pump(void)
{
	SDL_Delay(2);
	handle_events();
}

static void ce11_scenario(void)
{
	ce_apply_auto();
	void (*confirm)(bool) = (void (*)(bool))dlsym(RTLD_DEFAULT, "input_set_close_confirm");
	if (!confirm) {
		printf("  FAIL: NO SYMBOL input_set_close_confirm\n");
		return;
	}
	if (ce11_box) {
		if (!ce_box_setup())
			return;
	} else if (SDL_Init(SDL_INIT_EVENTS)) {
		printf("  FAIL: SDL_Init: %s\n", SDL_GetError());
		return;
	}
	SDL_GetTicks();
	switch (ce11_how) {
	case CE11_TWO_QUITS:
		confirm(true);
		ce11_quits(2);
		ce11_pump();
		break;
	case CE11_QUIT_AGAIN:
		/* the answer no is not for a request made after it */
		confirm(true);
		ce11_quits(2);
		ce11_pump();
		ce11_quits(1);
		ce11_pump();
		break;
	case CE11_WINDOW_EVENTS:
		confirm(true);
		ce11_quits(2);
		handle_window_events();
		handle_window_events();
		break;
	case CE11_SIGNAL_IN_BOX:
		confirm(true);
		ce11_pump();
		ce_box_signal = ce11_signal;
		ce11_quits(1);
		ce11_pump();
		break;
	case CE11_SIGNAL:
		confirm(true);
		ce11_pump();
		raise(ce11_signal);
		ce11_pump();
		break;
	case CE11_LATE_HANDLERS:
		/* no handler at the first pump; the probe's own from the
		 * second on, one for each signal */
		ce11_handler(SIGINT, SIG_DFL);
		ce11_handler(SIGTERM, SIG_DFL);
		confirm(true);
		ce11_pump();
		ce11_handler(SIGINT, ce11_on_int);
		ce11_handler(SIGTERM, ce11_on_term);
		ce11_pump();
		raise(ce11_signal);
		printf("HANDLERS int=%d term=%d\n", (int)ce11_got_int, (int)ce11_got_term);
		fflush(stdout);
		/* the SDL_QUIT that SDL's handler would have queued */
		ce11_quits(1);
		ce11_pump();
		break;
	case CE11_TABLE:
		mk_start();
		mk_set(5);
		ce_box_file = ce11_table_path;
		confirm(true);
		ce11_quits(1);
		ce11_pump();
		break;
	}
	printf("STILL RUNNING\n");
}

static void ce11(const char *what, int how, bool box, const char *env, int answer, int want)
{
	ce11_how = how;
	ce11_box = box;
	ce_auto = env;
	ce_box_answer = answer;
	ce_expect(what, ce11_scenario, want);
	CE_CHECK((want == CE_ALIVE) == (strstr(ce_out, "STILL RUNNING") != NULL), "%s: wrong end", what);
}

static void ce_close_window_more(void)
{
	cw_window_dir();
	/* one answer for every SDL_QUIT that is queued */
	ce11("two SDL_QUITs, the set answer no", CE11_TWO_QUITS, false, "cancel", 0, CE_ALIVE);
	CE_CHECK(ce_count("Message box answered") == 1, "two SDL_QUITs asked %d times", ce_count("Message box answered"));
	ce11("two SDL_QUITs, the box answered no", CE11_TWO_QUITS, true, NULL, 0, CE_ALIVE);
	CE_CHECK(ce_count("BOX title=") == 1, "two SDL_QUITs showed %d boxes", ce_count("BOX title="));
	ce11("two SDL_QUITs, then another", CE11_QUIT_AGAIN, true, NULL, 0, CE_ALIVE);
	CE_CHECK(ce_count("BOX title=") == 2, "two SDL_QUITs and a later one showed %d boxes", ce_count("BOX title="));
	ce11("two SDL_QUITs, handle_window_events", CE11_WINDOW_EVENTS, true, NULL, 0, CE_ALIVE);
	CE_CHECK(ce_count("BOX title=") == 1, "handle_window_events showed %d boxes", ce_count("BOX title="));
	ce11("two SDL_QUITs, the box answered yes", CE11_TWO_QUITS, true, NULL, 1, 0);
	CE_CHECK(ce_count("BOX title=") == 1, "two SDL_QUITs answered yes showed %d boxes", ce_count("BOX title="));

	/* a signal while the box is open: the answer no does not keep the
	 * program */
	static const struct { int sig; const char *name, *handlers; } sigs[] = {
		{ SIGINT, "SIGINT", "HANDLERS int=1 term=0" }, { SIGTERM, "SIGTERM", "HANDLERS int=0 term=1" },
	};
	for (int i = 0; i < 2; i++) {
		char what[64];
		ce11_signal = sigs[i].sig;
		snprintf(what, sizeof what, "%s while the box is open, no", sigs[i].name);
		ce11(what, CE11_SIGNAL_IN_BOX, true, NULL, 0, 0);
		CE_CHECK(ce_count("BOX title=") == 1 && strstr(ce_out, "Message box answered (cancel)"),
			 "%s: the box was not shown and answered no", what);
		/* either signal ends the program at once */
		snprintf(what, sizeof what, "%s, confirmation on", sigs[i].name);
		ce11(what, CE11_SIGNAL, false, "cancel", 0, 0);
		CE_CHECK(!strstr(ce_out, "Message box"), "%s asked", what);
		/* handlers that were not there at first are watched as well,
		 * and each signal's own handler still runs */
		snprintf(what, sizeof what, "%s, a handler set later", sigs[i].name);
		ce11(what, CE11_LATE_HANDLERS, false, "cancel", 0, 0);
		CE_CHECK(strstr(ce_out, sigs[i].handlers), "%s: want %s", what, sigs[i].handlers);
		CE_CHECK(!strstr(ce_out, "Message box"), "%s asked", what);
	}

	/* the read-message table is written before the box opens, and the
	 * program goes on after the answer no */
	snprintf(ce11_table_path, sizeof ce11_table_path, "%s/MsgSkip.msk", cw_dir);
	ce11("the table before the box", CE11_TABLE, true, NULL, 0, CE_ALIVE);
	CE_CHECK(strstr(ce_out, " held=0 file=1\n"), "the table was not on disk while the box was open");
	remove(ce11_table_path);
	ce11("the table before the set answer", CE11_TABLE, false, "cancel", 0, CE_ALIVE);
	const char *saved = strstr(ce_out, "MsgSkip: saved 1 of "), *asked = strstr(ce_out, "Message box answered");
	CE_CHECK(saved && asked && saved < asked, "the table was not written before the question");
	remove(ce11_table_path);
	ce_box_file = NULL;
	cw_window_rmdir();
	printf("  CE11 closing the window: one answer, signals, the table before the box\n");
}

/* --- CE12 --- */
static void ce12_fini(void)
{
	bool *pending = ce_exit_pending();
	printf("FINI pending=%d\n", pending ? *pending : -1);
}

static void ce12_exit_scenario(void)
{
	extern bool libraries_initialized;
	ce_apply_auto();
	ce_hooks();
	/* every library's _ModuleFini reports the flag instead of stopping
	 * a module the probe never started */
	for (int j = 0; static_libraries[j]; j++) {
		for (int i = 0; static_libraries[j]->functions[i].name; i++) {
			if (!strcmp(static_libraries[j]->functions[i].name, "_ModuleFini"))
				static_libraries[j]->functions[i].fun = ce12_fini;
		}
	}
	libraries_initialized = true;
	ce_run(CE_QUIT);
	printf("RETURNED\n");
}

static void ce12_reset_scenario(void)
{
	bool *pending = ce_exit_pending();
	if (!pending) {
		printf("  FAIL: NO SYMBOL vm_exit_pending\n");
		return;
	}
	*pending = true;
	vm_reset_runtime_state();
	printf("AFTER RESET pending=%d\n", *pending);
}

static void ce_exit_flag_down(void)
{
	cw_window_dir();
	ce_auto = "ok";
	ce_expect("the flag while the program ends", ce12_exit_scenario, 0);
	CE_CHECK(ce_count("FINI pending=0") > 0, "no library was stopped on the way out");
	CE_CHECK(!strstr(ce_out, "FINI pending=1") && !strstr(ce_out, "FINI pending=-1"),
		 "system.Exit's flag was still up while the program ended");
	CE_CHECK(!strstr(ce_out, "RETURNED") && !strstr(ce_out, "PEEK"), "the program did not end in the call");
	cw_window_rmdir();
	ce_expect("the flag after a restart", ce12_reset_scenario, CE_ALIVE);
	CE_CHECK(strstr(ce_out, "AFTER RESET pending=0"), "a restart left system.Exit's flag up");
	printf("  CE12 system.Exit's flag is down when the program ends and after a restart\n");
}

static int test_confirm_exit(void)
{
	void (*cases[])(void) = { ce_answers, ce_quit_game, ce_back_to_title, ce_exit_flag, ce_modal_input,
				  ce_close_window, ce_old_syscall, ce_shown_box, ce_failed_box, ce_old_box,
				  ce_close_window_more, ce_exit_flag_down };
	const char *names[] = { "CE1", "CE2", "CE3", "CE4", "CE5", "CE6", "CE7", "CE8", "CE9", "CE10", "CE11", "CE12" };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		ce_failures = 0;
		cases[i]();
		printf("%s %s\n", names[i], ce_failures ? "FAIL" : "ok");
		failed += ce_failures != 0;
	}
	printf("CONFIRM-EXIT %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}

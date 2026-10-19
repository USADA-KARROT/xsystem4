/* auto-input: the scheduled input (XSYS4_AUTO_CLICK_SEQ, the test facility
 * of src/input.c) and the pointer position the program reads.
 *
 * The original moves the system cursor for Mouse_MovePosImmediately and
 * reads the system cursor back (AIN: the only caller of the move in the
 * game is input::detail::MoveMousePos, from AFL_Mouse_MovePos, which only
 * the uncalled Ａ＿指針移動 and the debug menu's CTestMenuItem@SetCursor
 * call); the real mouse goes on moving the cursor afterwards. Before this
 * fix a position set once (by the schedule or by such a move) was read for
 * ever, and every scheduled click warped the desktop cursor.
 *
 *   AU1 the first form "T,X,Y" reads and runs as before: the same tokens
 *       are taken (extra numbers ignored, the malformed skipped), the click
 *       is pressed at T and released 100 ms later, the next step does not
 *       start in the update that released the button and starts late when
 *       its time came while the button was down; ';', line breaks and
 *       comments (from '#' to the end of the line, ';' included); more
 *       than 256 steps and 4096 characters.
 *   AU2 the new steps, update by update at chosen times: move (also on a
 *       straight line in MS ms), down and up of each button, click with a
 *       button and a hold, rclick, mclick, wheel (one SDL wheel event per
 *       notch, marked as the schedule's, one notch per input update) and
 *       drag (down, a pause, a straight line, a pause, up); steps not
 *       understood are refused.
 *   AU3 without a schedule, mouse_set_pos warps the desktop cursor and the
 *       position is read until a real event of the mouse that SDL
 *       timestamped after it: a motion, a button or the wheel (the
 *       schedule's wheel events do not count, and events queued before the
 *       move do not either); then the real pointer is read.
 *   AU4 with a schedule, nothing warps the desktop cursor: neither a step
 *       nor a move of the HLLs (IbisInputEngine.Mouse_MovePosImmediately
 *       through its AIN declaration); a real event ends the schedule's
 *       pointer and the next step sets it again; while the schedule holds a
 *       button (a click, a down before its up) a real motion does not, a
 *       real button does; after a release has been read, a motion does.
 *   AU5 the production path: XSYS4_AUTO_CLICK_SEQ read by handle_events
 *       with SDL's clock, as the engine runs it (with an input update every
 *       16 ms): the state the program sees, sampled after every
 *       handle_events for 1.2 s, goes through the steps in order, and no
 *       step warps the desktop cursor.
 *   AU6 what the v14 parts input makes of it (PE_UpdateInputState): a move
 *       alone hovers a button and sends nothing, a straight move passes
 *       over nothing to the other button, a first-form click sends the
 *       whole-screen click and the button's, a drag off the button sends
 *       the whole-screen click alone, a right click sends nothing (the
 *       port's state, input-model IM4), a wheel step sends nothing (the
 *       whole-screen wheel, type 3, is not done) and is read by
 *       MouseWheel_GetCount's engine function.
 *   AU7 XSYS4_AUTO_CLICK=<ms> (the periodic clicks of gui-run.sh): at
 *       XSYS4_AUTO_CLICK_X/Y, at most XSYS4_AUTO_CLICK_COUNT, and the last
 *       one is released (before, the count stopped the release as well).
 *   AU8 the schedule's pointer is in the window for the HLLs that say
 *       whether the cursor is (SACT2 and IbisInputEngine Mouse_GetPos,
 *       SACT2.Mouse_SetPos, InputDevice Get/SetCsrPos, vmCursor.GetPos:
 *       mouse_focus && keyboard_focus upstream) also after the real cursor
 *       entered and left the window; the real pointer outside it is not;
 *       the keyboard focus still counts. Every scheduled click used to warp
 *       the cursor, and SDL then gave the window the mouse focus again.
 *   AU9 XSYS4_NO_DESKTOP_WARP=1 (run-gui-bounded.py sets it): without a
 *       schedule, mouse_set_pos and Mouse_MovePosImmediately do not warp,
 *       the position is read until a real motion; "0" and "" are off (they
 *       warp, as upstream); an XSYS4_AUTO_CLICK_SEQ with no step understood
 *       does not warp either; SDL's motion for a warp that comes a few ms
 *       into the call does not end the position the warp set.
 *   AU10 a scheduled click is read where it was made: on SDL's clock with
 *       an input update after every handle_events, a real motion while the
 *       click is held (R0) or in the handle_events that releases it (R1)
 *       leaves the schedule's pointer, and the button gets its click; after
 *       the input update the real mouse is back; the next step (R2) or
 *       500 ms without an input update (R3) end the wait as well.
 *   AU11 a wheel step queues one notch per input update and is over when
 *       an input update has read its last one; without input updates, one
 *       every 500 ms; a notch sets the schedule's pointer again; on SDL's
 *       clock with a frame every 16 ms that reads and clears the wheel as a
 *       script does, 3 notches forward and 2 back are read in 5 frames.
 *   AU12 limits: coordinates of at most 1000000 from 0 and at most 1000
 *       notches out of the first form (refused beyond); a straight move
 *       from the first form's INT_MIN and the wheel's window coordinates on
 *       a viewport twice the game's size compute without overflow.
 *   AU13 XSYS4_STAGE2_TRACE turns the S2 wheel and S2 hover lines on for
 *       anything but "" and "0", as S2 mouse already was.
 *
 * The desktop cursor's warps are counted by a definition of
 * SDL_WarpMouseInWindow in this binary, which passes every call on to SDL.
 * The functions this fix added are taken with dlsym; without them a case
 * prints NO SYMBOL and fails, so before-check.sh builds the old revision
 * (AU3, AU5 and AU7 then fail on the old behaviour itself). An input
 * update is a run of PE_UpdateInputState. No GL, no game assets; each case
 * runs in its own fork, AU9, AU10 and AU13 each part in a fork of its own.
 */
#include <dlfcn.h>
#include <stdarg.h>

extern bool key_state[];
static int au_failures;
#define AU_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); au_failures++; } } while (0)

/* The sources of the position (enum mouse_pos_source; not named here, so
 * that the old revision's headers build). */
enum { AU_SDL = 0, AU_WARP = 1, AU_AUTO = 2 };
#define AU_AUTO_MOUSEID ((Uint32)-2)

static int au_warps, au_warp_x, au_warp_y;
/* > 0: a warp takes that many ms and ends with a motion, as SDL's motion for
 * a warp that crosses a millisecond (AU9). */
static int au_warp_motion_ms;
static void au_push_motion(int x, int y);

void SDL_WarpMouseInWindow(SDL_Window *window, int x, int y)
{
	static void (*real)(SDL_Window *, int, int);
	au_warps++;
	au_warp_x = x;
	au_warp_y = y;
	if (!real)
		real = (void (*)(SDL_Window *, int, int))dlsym(RTLD_NEXT, "SDL_WarpMouseInWindow");
	if (real)
		real(window, x, y);
	if (au_warp_motion_ms > 0) {
		SDL_Delay(au_warp_motion_ms);
		au_push_motion(x, y);
	}
}

static int (*au_load)(const char *);
static void (*au_update)(uint32_t);
static int (*au_peek)(int *, int *);
static const char *(*au_source_name)(int);

static bool au_symbols(void)
{
	au_load = (int (*)(const char *))dlsym(RTLD_DEFAULT, "input_auto_load");
	au_update = (void (*)(uint32_t))dlsym(RTLD_DEFAULT, "input_auto_update");
	au_peek = (int (*)(int *, int *))dlsym(RTLD_DEFAULT, "mouse_peek_pos");
	au_source_name = (const char *(*)(int))dlsym(RTLD_DEFAULT, "mouse_pos_source_name");
	if (!au_load || !au_update || !au_peek || !au_source_name) {
		printf("  NO SYMBOL input_auto_load/input_auto_update/mouse_peek_pos/mouse_pos_source_name\n");
		au_failures++;
		return false;
	}
	return true;
}

static bool au_sdl(void)
{
	if (SDL_Init(SDL_INIT_EVENTS)) {
		printf("  FAIL: SDL_Init: %s\n", SDL_GetError());
		au_failures++;
		return false;
	}
	sdl.w = 1280;
	sdl.h = 720;
	sdl.viewport = (SDL_Rect){ 0, 0, 1280, 720 };
	extern bool key_state[];
	key_state[VK_LBUTTON] = key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = false;
	au_warps = 0;
	return true;
}

/* The real pointer, as mouse_get_pos reads it without a position set. */
static void au_sdl_pos(int *x, int *y)
{
	int wx, wy;
	SDL_GetMouseState(&wx, &wy);
	*x = (wx - sdl.viewport.x) * sdl.w / sdl.viewport.w;
	*y = (wy - sdl.viewport.y) * sdl.h / sdl.viewport.h;
}

/* handle_events pumps once per millisecond of SDL's clock. */
static void au_pump(void)
{
	SDL_Delay(2);
	handle_events();
}

/* handle_events, the engine's way, until SDL's clock reaches t. */
static void au_until(uint32_t t)
{
	while (SDL_GetTicks() < t) {
		SDL_Delay(1);
		handle_events();
	}
}

static void au_push_motion(int x, int y)
{
	SDL_Event e = { .type = SDL_MOUSEMOTION };
	e.motion.which = 0;
	e.motion.x = x;
	e.motion.y = y;
	SDL_PushEvent(&e);
}

static void au_push_button(bool down)
{
	SDL_Event e = { .type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP };
	e.button.button = SDL_BUTTON_LEFT;
	e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	SDL_PushEvent(&e);
}

static void au_push_wheel(Uint32 which, int y)
{
	SDL_Event e = { .type = SDL_MOUSEWHEEL };
	e.wheel.which = which;
	e.wheel.y = y;
	SDL_PushEvent(&e);
}

struct au_state { int src, x, y; bool l, r, m; };

static struct au_state au_state(void)
{
	extern bool key_state[];
	struct au_state s;
	s.src = au_peek(&s.x, &s.y);
	s.l = key_state[VK_LBUTTON];
	s.r = key_state[VK_RBUTTON];
	s.m = key_state[VK_MBUTTON];
	return s;
}

static uint32_t au_base;

/* One update of the schedule at au_base + t and the state it leaves. */
#define AU_AT(t, src_, x_, y_, l_, r_, m_, what) do { \
	au_update(au_base + (t)); \
	struct au_state s_ = au_state(); \
	AU_CHECK(s_.src == (src_) && s_.x == (x_) && s_.y == (y_) && s_.l == (l_) && s_.r == (r_) && s_.m == (m_), \
		 "%s (+%d ms): pointer %s (%d,%d), buttons L%d R%d M%d; want %s (%d,%d), L%d R%d M%d", what, (int)(t), \
		 au_source_name(s_.src), s_.x, s_.y, s_.l, s_.r, s_.m, au_source_name(src_), (x_), (y_), (l_), (r_), (m_)); \
} while (0)

/* The wheel events the last update queued: their count and that each is a
 * notch of `dir` marked as the schedule's. */
static int au_queued_wheel(int dir)
{
	SDL_Event e[16];
	SDL_PumpEvents();
	int n = SDL_PeepEvents(e, 16, SDL_PEEKEVENT, SDL_MOUSEWHEEL, SDL_MOUSEWHEEL);
	for (int i = 0; i < n; i++) {
		if (e[i].wheel.which != AU_AUTO_MOUSEID || e[i].wheel.y != dir || e[i].wheel.x != 0)
			return -100 - i;
	}
	return n;
}

/* The same, and they are taken out of the queue. */
static int au_take_wheel(int dir)
{
	int n = au_queued_wheel(dir);
	SDL_FlushEvent(SDL_MOUSEWHEEL);
	return n;
}

/* One input update of the parts input, as a frame of the game runs it. */
static void au_input_update(void)
{
	PE_UpdateInputState(16);
}

static bool au_setup(void)
{
	if (!au_symbols() || !au_sdl())
		return false;
	/* Far in the future: handle_events, which runs the schedule at SDL's
	 * clock, does not reach these steps. */
	au_base = SDL_GetTicks() + 1000000;
	return true;
}

static char *au_spec(const char *fmt, ...)
{
	static char buf[8192];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}

/* AU1 */
static void au_first_form(void)
{
	if (!au_setup())
		return;
	uint32_t B = au_base;
	AU_CHECK(au_load("100,1,2,3;abc;5,6;;200,7,8") == 2, "\"100,1,2,3;abc;5,6;;200,7,8\": %d steps, want 2 (the extra number ignored, the malformed skipped)",
		 au_load("100,1,2,3;abc;5,6;;200,7,8"));
	static const char *const lines = " 12000,230,675 ;\n16000,1180,678 # to the end of the line; 99,1,1\r\n# a comment; 98,2,2\nmove,1,2,3";
	AU_CHECK(au_load(lines) == 3, "spaces, line breaks, CRLF and comments to the end of their line: %d steps, want 3", au_load(lines));
	static char many[16384];
	int len = 0;
	for (int i = 0; i < 400; i++)
		len += snprintf(many + len, sizeof(many) - len, "%d,%d,%d;", 1000000 + i * 1000, 600 + i % 100, 300);
	AU_CHECK(len > 4096 && au_load(many) == 400, "400 steps in %d characters: %d steps, want 400 (the old limits were 256 and 4096)", len, au_load(many));
	AU_CHECK(au_load(NULL) == 0 && au_load("") == 0, "no schedule is not 0 steps");

	AU_CHECK(au_load(au_spec("%u,10,20;%u,30,40;%u,50,60", B, B + 50, B + 1000)) == 3, "the timeline's schedule is not 3 steps");
	AU_AT(-1, AU_SDL, 0, 0, false, false, false, "before the first step");
	AU_AT(0, AU_AUTO, 10, 20, true, false, false, "the first click pressed");
	AU_AT(50, AU_AUTO, 10, 20, true, false, false, "the second step's time, the button still down");
	AU_AT(99, AU_AUTO, 10, 20, true, false, false, "99 ms");
	AU_AT(100, AU_AUTO, 10, 20, false, false, false, "100 ms: released, the second not started in the same update");
	AU_AT(101, AU_AUTO, 30, 40, true, false, false, "the second click, late");
	AU_AT(200, AU_AUTO, 30, 40, true, false, false, "99 ms after the second press");
	AU_AT(201, AU_AUTO, 30, 40, false, false, false, "the second released");
	AU_AT(999, AU_AUTO, 30, 40, false, false, false, "the pointer stays where it clicked");
	AU_AT(1000, AU_AUTO, 50, 60, true, false, false, "the third click on time");
	AU_AT(1100, AU_AUTO, 50, 60, false, false, false, "the third released");
	AU_AT(5000, AU_AUTO, 50, 60, false, false, false, "after the last step");
	AU_CHECK(au_warps == 0, "the schedule warped the desktop cursor %d time(s)", au_warps);
	printf("  AU1 the first form\n");
}

/* AU2 */
static void au_new_steps(void)
{
	if (!au_setup())
		return;
	uint32_t B = au_base;
	static const char *const refused[] = {
		"click,1,2,3,X", "click,1,2,3,150", "click,1,2,3,R,-5", "rclick,1,2,3,R", "move,-1,2,3", "move,1,2",
		"move,1,2,3,-1", "down,1,2,3,R,5", "wheel,1,2,3,0", "wheel,1,2,3", "drag,10,2,3,9,5,6", "drag,1,2,3,4,5",
		"hover,1,2,3", "click,1,2,3,L,100,7",
	};
	for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); i++)
		AU_CHECK(au_load(refused[i]) == 0, "\"%s\" was taken as %d step(s)", refused[i], au_load(refused[i]));
	AU_CHECK(au_load("click,1,2,3,r,50;down,1,2,3,M;up,4,5,6,m;mclick,7,8,9,20;MOVE,1,2,3") == 5, "verbs and buttons in either case are not 5 steps");
	AU_CHECK(au_load("drag,1,2,3,4,5,6") == 3, "a drag is not a down, a move and an up");

	int n = au_load(au_spec("move,%u,100,100;move,%u,300,200,100;down,%u,300,200,R;up,%u,320,210,R;"
				"click,%u,50,60,M,150;rclick,%u,70,80;mclick,%u,90,95;wheel,%u,500,400,3;wheel,%u,510,410,-2;"
				"drag,%u,100,100,%u,300,100",
				B + 1000, B + 1100, B + 1300, B + 1400, B + 1500, B + 1700, B + 1900, B + 2100, B + 2200,
				B + 3000, B + 3500));
	AU_CHECK(n == 12, "the schedule is %d steps, want 12", n);
	AU_AT(999, AU_SDL, 0, 0, false, false, false, "before the first step");
	AU_AT(1000, AU_AUTO, 100, 100, false, false, false, "move");
	AU_AT(1100, AU_AUTO, 100, 100, false, false, false, "a move in 100 ms sets out");
	AU_AT(1150, AU_AUTO, 200, 150, false, false, false, "halfway");
	AU_AT(1199, AU_AUTO, 298, 199, false, false, false, "1 ms before it arrives");
	AU_AT(1200, AU_AUTO, 300, 200, false, false, false, "arrived");
	AU_AT(1300, AU_AUTO, 300, 200, false, true, false, "down R");
	AU_AT(1350, AU_AUTO, 300, 200, false, true, false, "R stays down");
	AU_AT(1400, AU_AUTO, 320, 210, false, false, false, "up R where it was told");
	AU_AT(1500, AU_AUTO, 50, 60, false, false, true, "click M held 150 ms");
	AU_AT(1649, AU_AUTO, 50, 60, false, false, true, "M at 149 ms");
	AU_AT(1650, AU_AUTO, 50, 60, false, false, false, "M released at 150 ms");
	AU_AT(1700, AU_AUTO, 70, 80, false, true, false, "rclick");
	AU_AT(1800, AU_AUTO, 70, 80, false, false, false, "rclick released at 100 ms");
	AU_AT(1900, AU_AUTO, 90, 95, false, false, true, "mclick");
	AU_AT(2000, AU_AUTO, 90, 95, false, false, false, "mclick released at 100 ms");
	/* A wheel: one notch per input update (AU11 has more). */
	AU_AT(2100, AU_AUTO, 500, 400, false, false, false, "wheel forward");
	int fwd = -1, back = -1;
	struct au_state s;
	for (int k = 1; k <= 3; k++) {
		if (k > 1) {
			au_update(au_base + 2100 + 2 * k - 2);
			int early = au_queued_wheel(1);
			AU_CHECK(early == 0, "notch %d of 3 was queued before an input update read notch %d (%d event(s))", k, k - 1, early);
			au_input_update();
			au_update(au_base + 2100 + 2 * k - 1);
		}
		int q = au_queued_wheel(1);
		AU_CHECK(q == 1, "notch %d of 3: %d wheel event(s) of the schedule's queued (want 1 of y = 1)", k, q);
		au_pump();
		mouse_get_wheel(&fwd, &back);
		s = au_state();
		AU_CHECK(fwd == 1 && back == 0, "after notch %d the wheel reads forward %d back %d", k, fwd, back);
		AU_CHECK(s.src == AU_AUTO && s.x == 500 && s.y == 400, "the schedule's own wheel event ended its pointer (%s (%d,%d))", au_source_name(s.src), s.x, s.y);
		mouse_clear_wheel();
	}
	au_input_update();
	au_update(au_base + 2110);
	AU_CHECK(au_queued_wheel(1) == 0, "a fourth notch of a wheel of 3");
	AU_AT(2200, AU_AUTO, 510, 410, false, false, false, "wheel back");
	for (int k = 1; k <= 2; k++) {
		if (k > 1) {
			au_input_update();
			au_update(au_base + 2201);
		}
		int q = au_queued_wheel(-1);
		AU_CHECK(q == 1, "back notch %d of 2: %d wheel event(s) of the schedule's queued (want 1 of y = -1)", k, q);
		SDL_Delay(2);
		handle_events();
		mouse_get_wheel(&fwd, &back);
		AU_CHECK(fwd == 0 && back == 1, "after back notch %d the wheel reads forward %d back %d", k, fwd, back);
		mouse_clear_wheel();
	}
	au_input_update();
	au_update(au_base + 2202);
	AU_AT(3000, AU_AUTO, 100, 100, true, false, false, "drag: down");
	AU_AT(3099, AU_AUTO, 100, 100, true, false, false, "drag: the pause after the press");
	AU_AT(3100, AU_AUTO, 100, 100, true, false, false, "drag: sets out");
	AU_AT(3300, AU_AUTO, 200, 100, true, false, false, "drag: halfway, held");
	AU_AT(3500, AU_AUTO, 300, 100, true, false, false, "drag: arrived, held");
	AU_AT(3599, AU_AUTO, 300, 100, true, false, false, "drag: the pause before the release");
	AU_AT(3600, AU_AUTO, 300, 100, false, false, false, "drag: up");
	AU_CHECK(au_warps == 0, "the schedule warped the desktop cursor %d time(s)", au_warps);
	printf("  AU2 the new steps\n");
}

/* AU3 */
static void au_real_events(void)
{
	if (!au_sdl())
		return;
	int x, y, rx, ry;
	mouse_set_pos(640, 600);
	mouse_get_pos(&x, &y);
	AU_CHECK(x == 640 && y == 600, "right after the move the program reads (%d,%d), want (640,600)", x, y);
	AU_CHECK(au_warps == 1 && au_warp_x == 640 && au_warp_y == 600, "the move warped the desktop cursor %d time(s), last to (%d,%d); want once to (640,600)",
		 au_warps, au_warp_x, au_warp_y);
	/* A motion queued before the move does not end it. */
	au_push_motion(5, 5);
	SDL_Delay(3);
	mouse_set_pos(200, 300);
	au_pump();
	mouse_get_pos(&x, &y);
	AU_CHECK(x == 200 && y == 300, "a motion queued before the move ended it: the program reads (%d,%d), want (200,300)", x, y);
	/* A later one does. */
	au_push_motion(7, 9);
	au_pump();
	mouse_get_pos(&x, &y);
	au_sdl_pos(&rx, &ry);
	AU_CHECK(x == rx && y == ry && !(x == 200 && y == 300), "after a real motion the program reads (%d,%d), want the real pointer (%d,%d)", x, y, rx, ry);
	/* A real button and a real wheel event as well. */
	mouse_set_pos(400, 300);
	SDL_Delay(2);
	au_push_button(true);
	au_pump();
	mouse_get_pos(&x, &y);
	AU_CHECK(x == rx && y == ry, "after a real button event the program reads (%d,%d), want the real pointer (%d,%d)", x, y, rx, ry);
	au_push_button(false);
	au_pump();
	mouse_set_pos(500, 300);
	SDL_Delay(2);
	au_push_wheel(0, 1);
	au_pump();
	mouse_get_pos(&x, &y);
	AU_CHECK(x == rx && y == ry, "after a real wheel event the program reads (%d,%d), want the real pointer (%d,%d)", x, y, rx, ry);
	mouse_clear_wheel();
	/* A wheel event of the schedule's does not. */
	mouse_set_pos(600, 300);
	SDL_Delay(2);
	au_push_wheel(AU_AUTO_MOUSEID, 1);
	au_pump();
	mouse_get_pos(&x, &y);
	AU_CHECK(x == 600 && y == 300, "a wheel event of the schedule's ended the move: the program reads (%d,%d), want (600,300)", x, y);
	AU_CHECK(au_warps == 5, "%d warps for five moves", au_warps);
	if (au_symbols()) {
		int s = au_peek(&x, &y);
		AU_CHECK(s == AU_WARP && x == 600 && y == 300 && !strcmp(au_source_name(s), "warp"),
			 "mouse_peek_pos after a move: %s (%d,%d)", au_source_name(s), x, y);
		au_push_motion(1, 1);
		au_pump();
		s = au_peek(&x, &y);
		AU_CHECK(s == AU_SDL && x == rx && y == ry && !strcmp(au_source_name(s), "sdl"),
			 "mouse_peek_pos after a real motion: %s (%d,%d)", au_source_name(s), x, y);
	}
	printf("  AU3 a move without a schedule\n");
}

/* AU4 */
static void au_schedule_pointer(void)
{
	if (!au_setup())
		return;
	uint32_t B = au_base;
	AU_CHECK(au_load(au_spec("move,%u,100,100;move,%u,150,150;move,%u,160,160;move,%u,170,170", B, B + 100, B + 200, B + 300)) == 4,
		 "the schedule is not 4 steps");
	AU_AT(0, AU_AUTO, 100, 100, false, false, false, "the first step");
	mouse_set_pos(700, 500);
	struct au_state s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 700 && s.y == 500, "a move during the schedule: %s (%d,%d), want the schedule's pointer at (700,500)",
		 au_source_name(s.src), s.x, s.y);
	/* The script's own HLL. */
	int lib = ain_get_library(ain, "IbisInputEngine");
	int f = lib >= 0 ? sf_find(lib, "Mouse_MovePosImmediately", 2) : -1;
	AU_CHECK(f >= 0, "IbisInputEngine.Mouse_MovePosImmediately is not declared");
	if (f >= 0) {
		union vm_value r[1] = { BR_I(-1) };
		br_call("IbisInputEngine", "Mouse_MovePosImmediately", 2, 0, 2, (union vm_value[]){ BR_I(321), BR_I(234) }, r, 1);
		int x = -1, y = -1;
		mouse_get_pos(&x, &y);
		AU_CHECK(x == 321 && y == 234 && r[0].i == 1, "Mouse_MovePosImmediately(321, 234) during the schedule: returned %d, the program reads (%d,%d)", r[0].i, x, y);
	}
	AU_CHECK(au_warps == 0, "with a schedule the desktop cursor was warped %d time(s)", au_warps);
	int rx, ry;
	au_sdl_pos(&rx, &ry);
	SDL_Delay(2);
	au_push_motion(9, 9);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_SDL && s.x == rx && s.y == ry, "a real motion did not end the schedule's pointer: %s (%d,%d)", au_source_name(s.src), s.x, s.y);
	AU_AT(100, AU_AUTO, 150, 150, false, false, false, "the next step after the real motion");
	SDL_Delay(2);
	au_push_button(true);
	au_pump();
	au_push_button(false);
	au_pump();
	/* the button is kept down 50 ms for the polling scripts (mouse_event) */
	SDL_Delay(60);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_SDL, "a real button event did not end the schedule's pointer: %s (%d,%d)", au_source_name(s.src), s.x, s.y);
	AU_AT(200, AU_AUTO, 160, 160, false, false, false, "the next step after the real button");
	SDL_Delay(2);
	au_push_wheel(0, -1);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_SDL, "a real wheel event did not end the schedule's pointer: %s (%d,%d)", au_source_name(s.src), s.x, s.y);
	mouse_clear_wheel();
	AU_AT(300, AU_AUTO, 170, 170, false, false, false, "the next step after the real wheel");
	/* While the schedule holds a button, a real motion leaves its pointer.
	 * On SDL's clock: handle_events runs the schedule. */
	uint32_t T = SDL_GetTicks() + 20;
	AU_CHECK(au_load(au_spec("click,%u,180,180,L,300;down,%u,190,190,M;up,%u,195,195,M", T, T + 500, T + 1000)) == 3,
		 "the second schedule is not 3 steps");
	au_until(T + 100);
	s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 180 && s.y == 180 && s.l, "a scheduled click held: %s (%d,%d) L%d", au_source_name(s.src), s.x, s.y, s.l);
	au_push_motion(11, 11);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 180 && s.y == 180 && s.l, "a real motion took the pointer from a held scheduled click: %s (%d,%d) L%d",
		 au_source_name(s.src), s.x, s.y, s.l);
	au_until(T + 400);
	s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 180 && s.y == 180 && !s.l, "the scheduled click released: %s (%d,%d) L%d", au_source_name(s.src), s.x, s.y, s.l);
	/* the release is read by an input update (AU10), then the real mouse is back */
	au_input_update();
	au_push_motion(12, 12);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_SDL, "after the release was read a real motion did not end the schedule's pointer: %s (%d,%d)", au_source_name(s.src), s.x, s.y);
	au_until(T + 600);
	s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 190 && s.y == 190 && s.m, "a scheduled down of M: %s (%d,%d) M%d", au_source_name(s.src), s.x, s.y, s.m);
	au_push_motion(13, 13);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 190 && s.m, "a real motion took the pointer from a scheduled down: %s (%d,%d) M%d", au_source_name(s.src), s.x, s.y, s.m);
	au_push_button(true);
	au_pump();
	au_push_button(false);
	au_pump();
	SDL_Delay(60);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_SDL && s.m, "a real button during a scheduled down: %s (%d,%d) M%d; want the real pointer, M still down", au_source_name(s.src), s.x, s.y, s.m);
	au_until(T + 1100);
	s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 195 && s.y == 195 && !s.m && !s.l, "the scheduled up: %s (%d,%d) L%d M%d", au_source_name(s.src), s.x, s.y, s.l, s.m);
	AU_CHECK(au_warps == 0, "with a schedule the desktop cursor was warped %d time(s)", au_warps);
	printf("  AU4 the schedule's pointer\n");
}

/* AU5: the state after every handle_events, kept where it changed. A
 * sample's time is read after its handle_events, so a 100 ms hold may be
 * sampled as 99 ms. */
struct au_sample { uint32_t t; int x, y; bool l, r, m, fwd, back; };
static struct au_sample au_samples[4096];
static int au_nr_samples;

static void au_sample(uint32_t t)
{
	extern bool key_state[];
	struct au_sample s = { .t = t, .l = key_state[VK_LBUTTON], .r = key_state[VK_RBUTTON], .m = key_state[VK_MBUTTON] };
	mouse_get_pos(&s.x, &s.y);
	int f, b;
	mouse_get_wheel(&f, &b);
	s.fwd = f;
	s.back = b;
	mouse_clear_wheel();
	struct au_sample *p = au_nr_samples ? &au_samples[au_nr_samples - 1] : NULL;
	if (p && p->x == s.x && p->y == s.y && p->l == s.l && p->r == s.r && p->m == s.m && !s.fwd && !s.back)
		return;
	if (au_nr_samples < 4096)
		au_samples[au_nr_samples++] = s;
}

/* The first sample from index `from` on that matches; -1 if none. */
static int au_find(int from, bool (*pred)(const struct au_sample *, const void *), const void *arg)
{
	for (int i = from; i < au_nr_samples; i++)
		if (pred(&au_samples[i], arg))
			return i;
	return -1;
}
struct au_want { int x, y; int l, r, m, fwd; };
static bool au_match(const struct au_sample *s, const void *arg)
{
	const struct au_want *w = arg;
	return (w->x < 0 || s->x == w->x) && (w->y < 0 || s->y == w->y) && (w->l < 0 || s->l == w->l)
		&& (w->r < 0 || s->r == w->r) && (w->m < 0 || s->m == w->m) && (w->fwd < 0 || s->fwd == w->fwd);
}

static void au_production(void)
{
	if (!au_sdl())
		return;
	uint32_t T = SDL_GetTicks() + 40;
	char spec[512];
	snprintf(spec, sizeof(spec), "%u,10,20;move,%u,300,300;rclick,%u,320,320;wheel,%u,330,330,2;drag,%u,100,100,%u,400,100",
		 T, T + 200, T + 300, T + 450, T + 550, T + 850);
	setenv("XSYS4_AUTO_CLICK_SEQ", spec, 1);
	unsetenv("XSYS4_AUTO_CLICK");
	au_nr_samples = 0;
	uint32_t frame = SDL_GetTicks();
	while (SDL_GetTicks() < T + 1150) {
		SDL_Delay(1);
		handle_events();
		au_sample(SDL_GetTicks());
		/* an input update a frame, as the game's */
		if (SDL_GetTicks() - frame >= 16) {
			frame = SDL_GetTicks();
			au_input_update();
		}
	}
	unsetenv("XSYS4_AUTO_CLICK_SEQ");
	int i;
	/* the first-form click */
	i = au_find(0, au_match, &(struct au_want){ 10, 20, 1, 0, 0, -1 });
	AU_CHECK(i >= 0, "no left press at (10,20)");
	int j = i >= 0 ? au_find(i, au_match, &(struct au_want){ 10, 20, 0, 0, 0, -1 }) : -1;
	AU_CHECK(j > i && au_samples[j].t - au_samples[i].t >= 90 && au_samples[j].t - au_samples[i].t < 300,
		 "the first click was released %d ms after its press (want about 100, at most 300)", j > i ? (int)(au_samples[j].t - au_samples[i].t) : -1);
	/* the move: no button */
	int k = j > 0 ? au_find(j, au_match, &(struct au_want){ 300, 300, 0, 0, 0, -1 }) : -1;
	AU_CHECK(k > 0 && au_samples[k].t >= T + 200, "no move to (300,300) without a button after the click");
	/* the right click */
	int r1 = k > 0 ? au_find(k, au_match, &(struct au_want){ 320, 320, 0, 1, 0, -1 }) : -1;
	int r2 = r1 > 0 ? au_find(r1, au_match, &(struct au_want){ 320, 320, 0, 0, 0, -1 }) : -1;
	AU_CHECK(r1 > 0 && r2 > r1 && au_samples[r2].t - au_samples[r1].t >= 90, "the right click at (320,320): pressed %d, released %d", r1, r2);
	/* the wheel */
	int w = r2 > 0 ? au_find(r2, au_match, &(struct au_want){ 330, 330, 0, 0, 0, 1 }) : -1;
	AU_CHECK(w > 0 && au_samples[w].t >= T + 450, "no forward wheel at (330,330)");
	/* the drag: pressed at the start, positions in between, released at the end */
	int d1 = w > 0 ? au_find(w, au_match, &(struct au_want){ 100, 100, 1, 0, 0, -1 }) : -1;
	int between = 0, d2 = -1;
	for (int n = d1 > 0 ? d1 : au_nr_samples; n < au_nr_samples; n++) {
		if (au_samples[n].l && au_samples[n].y == 100 && au_samples[n].x > 100 && au_samples[n].x < 400)
			between++;
		if (!au_samples[n].l) {
			d2 = n;
			break;
		}
	}
	AU_CHECK(d1 > 0 && au_samples[d1].t >= T + 550, "no left press at (100,100) for the drag");
	AU_CHECK(between >= 3, "%d position(s) held between (100,100) and (400,100), want at least 3", between);
	AU_CHECK(d2 > 0 && au_samples[d2].x == 400 && au_samples[d2].y == 100 && au_samples[d2].t >= T + 950,
		 "the drag was not released at (400,100) after T+950 (sample %d)", d2);
	if (d2 > 0 && d2 > d1 + 1)
		AU_CHECK(au_samples[d2 - 1].x == 400 && au_samples[d2 - 1].l, "the drag did not reach (400,100) held before the release");
	AU_CHECK(au_warps == 0, "the schedule warped the desktop cursor %d time(s) (to (%d,%d) last)", au_warps, au_warp_x, au_warp_y);
	for (int n = 0; n < au_nr_samples && n < 40; n++)
		printf("    %+5d ms (%d,%d) L%d R%d M%d%s%s\n", (int)(au_samples[n].t - T), au_samples[n].x, au_samples[n].y,
		       au_samples[n].l, au_samples[n].r, au_samples[n].m, au_samples[n].fwd ? " wheel forward" : "", au_samples[n].back ? " wheel back" : "");
	printf("  AU5 handle_events runs XSYS4_AUTO_CLICK_SEQ (%d states)\n", au_nr_samples);
}

/* AU6 */
enum { AU_A = 18701, AU_B = 18702 };
struct au_frame { int n; struct { int type, no, x, y, key; } m[3]; };
static struct au_frame au_parts(uint32_t t)
{
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	au_update(au_base + t);
	PE_UpdateInputState(16);
	struct au_frame f = { 0 };
	for (int ty; (ty = br_msg_type()) >= 0; br_msg_pop()) {
		if (f.n < 3) {
			f.m[f.n].type = ty;
			f.m[f.n].no = br_msg_parts();
			f.m[f.n].x = var(0);
			f.m[f.n].y = var(1);
			f.m[f.n].key = var(2);
		}
		f.n++;
	}
	return f;
}

static void au_engine(void)
{
	if (!au_setup())
		return;
	br_input_setup();
	struct parts *a = br_box(AU_A, 100, 100, 200, 80, true);
	struct parts *b = br_box(AU_B, 500, 100, 200, 80, true);
	cp_register_event(AU_A, 3, 31);
	cp_register_event(AU_B, 4, 32);
	uint32_t B = au_base;
	int n = au_load(au_spec("move,%u,150,140;move,%u,550,140,100;%u,150,140;drag,%u,150,140,%u,560,150;rclick,%u,150,140;wheel,%u,150,140,1",
				B, B + 100, B + 300, B + 500, B + 800, B + 1100, B + 1300));
	AU_CHECK(n == 8, "the schedule is %d steps, want 8", n);
	mouse_set_pos(1000, 600);	/* beside both, as the schedule's pointer */
	PE_UpdateComponent(0);
	PE_BeginInput();
	au_parts(-1);
	struct au_frame f = au_parts(0);
	AU_CHECK(f.n == 0 && a->state == PARTS_STATE_HOVERED && b->state == PARTS_STATE_DEFAULT,
		 "a move onto A: %d message(s), A in state %d, B in %d; want none, A on-cursor", f.n, a->state, b->state);
	f = au_parts(100);
	f = au_parts(150);
	AU_CHECK(f.n == 0 && a->state == PARTS_STATE_DEFAULT && b->state == PARTS_STATE_DEFAULT,
		 "halfway to B (between the two): %d message(s), A in state %d, B in %d", f.n, a->state, b->state);
	f = au_parts(200);
	AU_CHECK(f.n == 0 && b->state == PARTS_STATE_HOVERED && a->state == PARTS_STATE_DEFAULT,
		 "the move arrived on B: %d message(s), A in state %d, B in %d", f.n, a->state, b->state);
	f = au_parts(300);
	AU_CHECK(f.n == 0 && a->state == PARTS_STATE_CLICKED, "the click's press on A: %d message(s), A in state %d", f.n, a->state);
	f = au_parts(400);
	AU_CHECK(f.n == 2 && f.m[0].type == 4 && f.m[0].no == 0 && f.m[0].x == 150 && f.m[0].y == 140 && f.m[0].key == 1
		 && f.m[1].type == 4 && f.m[1].no == AU_A, "the click's release: %d message(s), (%d,%d,(%d,%d,%d)) (%d,%d); want the whole-screen click and A's",
		 f.n, f.m[0].type, f.m[0].no, f.m[0].x, f.m[0].y, f.m[0].key, f.m[1].type, f.m[1].no);
	f = au_parts(500);
	AU_CHECK(f.n == 0 && a->state == PARTS_STATE_CLICKED, "the drag's press on A: %d message(s), A in state %d", f.n, a->state);
	f = au_parts(600);
	f = au_parts(700);
	AU_CHECK(f.n == 0 && a->state == PARTS_STATE_DEFAULT, "the drag off A, held: %d message(s), A in state %d", f.n, a->state);
	f = au_parts(800);
	f = au_parts(900);
	AU_CHECK(f.n == 1 && f.m[0].type == 4 && f.m[0].no == 0 && f.m[0].x == 560 && f.m[0].y == 150,
		 "the drag's release on B: %d message(s), first (%d,%d,(%d,%d)); want the whole-screen click alone at (560,150)",
		 f.n, f.m[0].type, f.m[0].no, f.m[0].x, f.m[0].y);
	f = au_parts(1100);
	extern bool key_state[];
	AU_CHECK(f.n == 0 && key_state[VK_RBUTTON] && a->state == PARTS_STATE_HOVERED,
		 "the right click's press on A: %d message(s), right button %d, A in state %d", f.n, key_state[VK_RBUTTON], a->state);
	f = au_parts(1200);
	AU_CHECK(f.n == 0 && !key_state[VK_RBUTTON], "the right click's release: %d message(s) (the port sends none yet, input-model IM4)", f.n);
	f = au_parts(1300);
	AU_CHECK(f.n == 0, "the wheel: %d message(s) (the whole-screen wheel is not done yet)", f.n);
	SDL_Delay(2);
	handle_events();
	int fwd = -1, back = -1;
	mouse_get_wheel(&fwd, &back);
	AU_CHECK(fwd == 1 && back == 0, "MouseWheel_GetCount's function after the wheel step: forward %d back %d", fwd, back);
	PE_EndInput();
	AU_CHECK(au_warps == 0, "the schedule warped the desktop cursor %d time(s)", au_warps);
	printf("  AU6 the v14 parts input\n");
}

/* AU7 */
static void au_periodic(void)
{
	if (!au_sdl())
		return;
	unsetenv("XSYS4_AUTO_CLICK_SEQ");
	setenv("XSYS4_AUTO_CLICK", "150", 1);
	setenv("XSYS4_AUTO_CLICK_X", "640", 1);
	setenv("XSYS4_AUTO_CLICK_Y", "400", 1);
	setenv("XSYS4_AUTO_CLICK_COUNT", "2", 1);
	extern bool key_state[];
	int presses = 0;
	bool was = false, bad_pos = false;
	uint32_t end = SDL_GetTicks() + 900;
	while (SDL_GetTicks() < end) {
		SDL_Delay(1);
		handle_events();
		bool l = key_state[VK_LBUTTON];
		if (l && !was) {
			presses++;
			int x, y;
			mouse_get_pos(&x, &y);
			bad_pos |= x != 640 || y != 400;
		}
		was = l;
	}
	AU_CHECK(presses == 2 && !bad_pos, "%d press(es) of the periodic clicks (want 2), %s at (640,400)", presses, bad_pos ? "not all" : "all");
	AU_CHECK(!key_state[VK_LBUTTON], "the last periodic click is still down");
	AU_CHECK(au_warps == 0, "the periodic clicks warped the desktop cursor %d time(s)", au_warps);
	printf("  AU7 the periodic clicks\n");
}

/* Runs fn in a fork of its own (it sets the environment or a state that is
 * read once); true if it passed. */
static bool au_sub(const char *what, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		au_failures = 0;
		fn();
		fflush(stdout);
		_exit(au_failures ? 1 : 0);
	}
	int st = 0;
	bool ok = pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
	if (!ok) {
		printf("  FAIL: %s\n", what);
		au_failures++;
	}
	return ok;
}

/* AU8 */
extern struct static_library lib_SACT2, lib_InputDevice, lib_vmCursor, lib_IbisInputEngine;
static void *au_hll(struct static_library *lib, const char *name)
{
	for (int i = 0; lib->functions[i].name; i++)
		if (!strcmp(lib->functions[i].name, name))
			return lib->functions[i].fun;
	return NULL;
}

static void au_window_event(Uint8 event)
{
	SDL_Event e = { .type = SDL_WINDOWEVENT };
	e.window.event = event;
	SDL_PushEvent(&e);
}

static void au_focus(void)
{
	if (!au_setup())
		return;
	struct { const char *name; int (*fn)(int *, int *); } get[] = {
		{ "SACT2.Mouse_GetPos", (int (*)(int *, int *))au_hll(&lib_SACT2, "Mouse_GetPos") },
		{ "IbisInputEngine.Mouse_GetPos", (int (*)(int *, int *))au_hll(&lib_IbisInputEngine, "Mouse_GetPos") },
		{ "InputDevice.GetCsrPos", (int (*)(int *, int *))au_hll(&lib_InputDevice, "GetCsrPos") },
		{ "vmCursor.GetPos", (int (*)(int *, int *))au_hll(&lib_vmCursor, "GetPos") },
	};
	struct { const char *name; int (*fn)(int, int); } set[] = {
		{ "SACT2.Mouse_SetPos", (int (*)(int, int))au_hll(&lib_SACT2, "Mouse_SetPos") },
		{ "InputDevice.SetCsrPos", (int (*)(int, int))au_hll(&lib_InputDevice, "SetCsrPos") },
	};
	for (unsigned i = 0; i < 4; i++)
		AU_CHECK(get[i].fn, "%s not found", get[i].name);
	for (unsigned i = 0; i < 2; i++)
		AU_CHECK(set[i].fn, "%s not found", set[i].name);
	if (au_failures)
		return;
	uint32_t B = au_base;
	AU_CHECK(au_load(au_spec("move,%u,300,200;move,%u,310,210", B, B + 100)) == 2, "the schedule is not 2 steps");
	AU_AT(0, AU_AUTO, 300, 200, false, false, false, "the first step");
	/* the real cursor passes over the window and leaves it */
	au_window_event(SDL_WINDOWEVENT_ENTER);
	au_window_event(SDL_WINDOWEVENT_LEAVE);
	au_pump();
	AU_CHECK(!mouse_focus && keyboard_focus, "the window events: mouse focus %d, keyboard focus %d; want 0 and 1", mouse_focus, keyboard_focus);
	for (unsigned i = 0; i < 4; i++) {
		int x = -1, y = -1, r = get[i].fn(&x, &y);
		AU_CHECK(r == 1 && x == 300 && y == 200, "%s at the schedule's pointer after the cursor left the window: returned %d at (%d,%d); want 1 at (300,200)",
			 get[i].name, r, x, y);
	}
	for (unsigned i = 0; i < 2; i++) {
		int r = set[i].fn(400 + (int)i, 250), x = -1, y = -1;
		mouse_get_pos(&x, &y);
		AU_CHECK(r == 1 && x == 400 + (int)i && y == 250, "%s(%d, 250) during the schedule after the cursor left the window: returned %d, the program reads (%d,%d)",
			 set[i].name, 400 + (int)i, r, x, y);
	}
	/* the real pointer again: as upstream, the mouse focus */
	SDL_Delay(2);
	au_push_motion(5, 5);
	au_pump();
	struct au_state s = au_state();
	AU_CHECK(s.src == AU_SDL, "a real motion did not end the schedule's pointer: %s", au_source_name(s.src));
	for (unsigned i = 0; i < 4; i++) {
		int x, y, r = get[i].fn(&x, &y);
		AU_CHECK(r == 0, "%s at the real pointer outside the window returned %d, want 0", get[i].name, r);
	}
	AU_AT(100, AU_AUTO, 310, 210, false, false, false, "the next step");
	for (unsigned i = 0; i < 4; i++) {
		int x, y, r = get[i].fn(&x, &y);
		AU_CHECK(r == 1 && x == 310 && y == 210, "%s at the next step: returned %d at (%d,%d)", get[i].name, r, x, y);
	}
	keyboard_focus = false;
	int x, y;
	AU_CHECK(get[0].fn(&x, &y) == 0, "without the keyboard focus %s still returns 1", get[0].name);
	keyboard_focus = true;
	AU_CHECK(au_warps == 0, "the schedule warped the desktop cursor %d time(s)", au_warps);
	printf("  AU8 the schedule's pointer is in the window\n");
}

/* AU9 */
static void au_hll_move(int x, int y)
{
	union vm_value r[1] = { BR_I(-1) };
	br_call("IbisInputEngine", "Mouse_MovePosImmediately", 2, 0, 2, (union vm_value[]){ BR_I(x), BR_I(y) }, r, 1);
}

static void au_moves(int want_warps, int want_src, const char *what)
{
	int x = -1, y = -1;
	mouse_set_pos(300, 200);
	int src = au_peek(&x, &y);
	AU_CHECK(src == want_src && x == 300 && y == 200, "%s: after mouse_set_pos(300, 200) %s (%d,%d); want %s (300,200)", what,
		 au_source_name(src), x, y, au_source_name(want_src));
	au_hll_move(321, 234);
	mouse_get_pos(&x, &y);
	AU_CHECK(x == 321 && y == 234, "%s: after Mouse_MovePosImmediately(321, 234) the program reads (%d,%d)", what, x, y);
	AU_CHECK(au_warps == want_warps, "%s: the desktop cursor was warped %d time(s), want %d", what, au_warps, want_warps);
	int rx, ry;
	au_sdl_pos(&rx, &ry);
	SDL_Delay(2);
	au_push_motion(3, 4);
	au_pump();
	src = au_peek(&x, &y);
	AU_CHECK(src == AU_SDL && x == rx && y == ry, "%s: a real motion did not end the position: %s (%d,%d)", what, au_source_name(src), x, y);
}

static void au_env_clear(void)
{
	unsetenv("XSYS4_AUTO_CLICK_SEQ");
	unsetenv("XSYS4_AUTO_CLICK");
	unsetenv("XSYS4_NO_DESKTOP_WARP");
}
static void au_nowarp_on(void)
{
	au_env_clear();
	setenv("XSYS4_NO_DESKTOP_WARP", "1", 1);
	if (au_symbols() && au_sdl())
		au_moves(0, AU_AUTO, "XSYS4_NO_DESKTOP_WARP=1 without a schedule");
}
static void au_nowarp_zero(void)
{
	au_env_clear();
	setenv("XSYS4_NO_DESKTOP_WARP", "0", 1);
	if (au_symbols() && au_sdl())
		au_moves(2, AU_WARP, "XSYS4_NO_DESKTOP_WARP=0");
}
static void au_nowarp_empty(void)
{
	au_env_clear();
	setenv("XSYS4_NO_DESKTOP_WARP", "", 1);
	if (au_symbols() && au_sdl())
		au_moves(2, AU_WARP, "XSYS4_NO_DESKTOP_WARP empty");
}
static void au_nowarp_bad_seq(void)
{
	au_env_clear();
	setenv("XSYS4_AUTO_CLICK_SEQ", "nonsense;move,1,2;wheel,1,2,3,0;# a comment", 1);
	if (au_symbols() && au_sdl())
		au_moves(0, AU_AUTO, "XSYS4_AUTO_CLICK_SEQ with no step understood");
}
static void au_warp_order(void)
{
	au_env_clear();
	if (!au_symbols() || !au_sdl())
		return;
	/* SDL's motion for the warp comes a millisecond or two after the call
	 * began: it does not end the position the warp set. */
	au_warp_motion_ms = 3;
	mouse_set_pos(640, 600);
	au_warp_motion_ms = 0;
	au_pump();
	int x = -1, y = -1, src = au_peek(&x, &y);
	AU_CHECK(src == AU_WARP && x == 640 && y == 600, "the warp's own motion, 3 ms into the call, ended the position: %s (%d,%d); want warp (640,600)",
		 au_source_name(src), x, y);
	AU_CHECK(au_warps == 1, "%d warps for one move", au_warps);
}

static void au_no_desktop_warp(void)
{
	au_sub("XSYS4_NO_DESKTOP_WARP=1", au_nowarp_on);
	au_sub("XSYS4_NO_DESKTOP_WARP=0", au_nowarp_zero);
	au_sub("XSYS4_NO_DESKTOP_WARP empty", au_nowarp_empty);
	au_sub("XSYS4_AUTO_CLICK_SEQ set, nothing understood", au_nowarp_bad_seq);
	au_sub("the warp's own motion", au_warp_order);
	printf("  AU9 the switch that keeps the desktop cursor still\n");
}

/* AU10: on SDL's clock, as the engine runs it: a first-form click on A, an
 * input update (PE_UpdateInputState) after every handle_events, and a real
 * motion that comes while the button is held (R0) or in the very
 * handle_events that releases it (R1). */
static int au_drain(int *a_clicks)
{
	int n = 0;
	for (int ty; (ty = br_msg_type()) >= 0; br_msg_pop()) {
		if (ty == 4 && br_msg_parts() == AU_A)
			(*a_clicks)++;
		n++;
	}
	return n;
}

static void au_frame(int *a_clicks)
{
	au_input_update();
	au_drain(a_clicks);
}

/* Up to the press of a first-form click on A at (150,140); its time. */
static uint32_t au_release_setup(const char *more, int *a_clicks)
{
	br_input_setup();
	br_box(AU_A, 100, 100, 200, 80, true);
	cp_register_event(AU_A, 3, 31);
	uint32_t T = SDL_GetTicks() + 30;
	char spec[256];
	snprintf(spec, sizeof(spec), "%u,150,140%s", T, more ? more : "");
	if (more) {
		char *p = strstr(spec, "@");
		if (p) {
			char tail[200];
			snprintf(tail, sizeof(tail), "%u%s", T + 150, p + 1);
			*p = '\0';
			strncat(spec, tail, sizeof(spec) - strlen(spec) - 1);
		}
	}
	au_load(spec);
	PE_UpdateComponent(0);
	PE_BeginInput();
	while (!key_state[VK_LBUTTON] && SDL_GetTicks() < T + 500) {
		SDL_Delay(1);
		handle_events();
		au_frame(a_clicks);
	}
	AU_CHECK(key_state[VK_LBUTTON], "the click was not pressed");
	return SDL_GetTicks();
}

static void au_r0(void)
{
	if (!au_symbols() || !au_sdl())
		return;
	int clicks = 0;
	uint32_t pressed = au_release_setup(NULL, &clicks);
	SDL_Delay(20);
	au_push_motion(7, 9);
	SDL_Delay(1);
	handle_events();
	au_frame(&clicks);
	struct au_state s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 150 && s.y == 140 && s.l, "a real motion while the click is held: %s (%d,%d) L%d", au_source_name(s.src), s.x, s.y, s.l);
	while (key_state[VK_LBUTTON] && SDL_GetTicks() < pressed + 500) {
		SDL_Delay(1);
		handle_events();
		au_frame(&clicks);
	}
	AU_CHECK(clicks == 1, "the scheduled click on A was delivered %d time(s), want 1", clicks);
	AU_CHECK(au_warps == 0, "the schedule warped the desktop cursor %d time(s)", au_warps);
	PE_EndInput();
}

static void au_r1(void)
{
	if (!au_symbols() || !au_sdl())
		return;
	int clicks = 0;
	uint32_t pressed = au_release_setup(NULL, &clicks);
	/* updates until 90 ms into the hold, then none until 101 ms: the next
	 * handle_events is the one that releases (the press may have come a
	 * few ms before `pressed`, so not later than 90) */
	while (SDL_GetTicks() < pressed + 90) {
		SDL_Delay(1);
		handle_events();
		au_frame(&clicks);
	}
	while (SDL_GetTicks() < pressed + 101)
		SDL_Delay(1);
	bool held = key_state[VK_LBUTTON];
	au_push_motion(7, 9);
	SDL_Delay(1);
	handle_events();		/* releases, then takes the motion */
	struct au_state s = au_state();
	AU_CHECK(held && !s.l && s.src == AU_AUTO && s.x == 150 && s.y == 140,
		 "the handle_events that released the click (held before: %d) and took a real motion: %s (%d,%d) L%d; want the schedule's (150,140)",
		 held, au_source_name(s.src), s.x, s.y, s.l);
	au_frame(&clicks);
	AU_CHECK(clicks == 1, "the scheduled click on A was delivered %d time(s), want 1", clicks);
	/* read: the real mouse is back */
	SDL_Delay(2);
	au_push_motion(8, 9);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_SDL, "after the input update read the release a real motion did not end the schedule's pointer: %s (%d,%d)",
		 au_source_name(s.src), s.x, s.y);
	PE_EndInput();
}

/* The next step ends the wait for the read. */
static void au_r2(void)
{
	if (!au_symbols() || !au_sdl())
		return;
	int clicks = 0;
	uint32_t pressed = au_release_setup(";move,@,600,600", &clicks);
	/* no input update from here on */
	while (key_state[VK_LBUTTON] && SDL_GetTicks() < pressed + 400) {
		SDL_Delay(1);
		handle_events();
	}
	au_until(SDL_GetTicks() + 120);		/* the move, 150 ms after the click */
	struct au_state s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 600 && s.y == 600, "the move after the click: %s (%d,%d)", au_source_name(s.src), s.x, s.y);
	SDL_Delay(2);
	au_push_motion(8, 9);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_SDL, "after the next step a real motion did not end the schedule's pointer: %s (%d,%d)", au_source_name(s.src), s.x, s.y);
	PE_EndInput();
}

/* Without an input update the wait ends AUTO_INPUT_READ_WAIT (500) ms after
 * the release. */
static void au_r3(void)
{
	if (!au_symbols() || !au_sdl())
		return;
	int clicks = 0;
	uint32_t pressed = au_release_setup(NULL, &clicks);
	while (key_state[VK_LBUTTON] && SDL_GetTicks() < pressed + 400) {
		SDL_Delay(1);
		handle_events();
	}
	uint32_t released = SDL_GetTicks();
	au_until(released + 100);
	au_push_motion(8, 9);
	au_pump();
	struct au_state s = au_state();
	AU_CHECK(s.src == AU_AUTO && s.x == 150, "100 ms after the release, no input update: a real motion took the schedule's pointer (%s (%d,%d))",
		 au_source_name(s.src), s.x, s.y);
	au_until(released + 560);
	au_push_motion(9, 9);
	au_pump();
	s = au_state();
	AU_CHECK(s.src == AU_SDL, "560 ms after the release, no input update: a real motion did not end the schedule's pointer (%s (%d,%d))",
		 au_source_name(s.src), s.x, s.y);
	PE_EndInput();
}

static void au_release_read(void)
{
	au_sub("R0 a real motion while the click is held", au_r0);
	au_sub("R1 a real motion in the handle_events that releases the click", au_r1);
	au_sub("R2 the next step ends the wait", au_r2);
	au_sub("R3 without an input update the wait ends after 500 ms", au_r3);
	printf("  AU10 a release is read where the schedule made it\n");
}

/* AU11 */
static int au_watch_n, au_watch_frame[64], au_watch_y[64], au_frame_no;
static int au_watch(void *data, SDL_Event *e)
{
	(void)data;
	if (e->type == SDL_MOUSEWHEEL && e->wheel.which == AU_AUTO_MOUSEID && au_watch_n < 64) {
		au_watch_frame[au_watch_n] = au_frame_no;
		au_watch_y[au_watch_n++] = e->wheel.y;
	}
	return 1;
}

static void au_wheel_notches(void)
{
	if (!au_setup())
		return;
	/* update by update */
	uint32_t B = au_base;
	AU_CHECK(au_load(au_spec("wheel,%u,640,360,3;move,%u,10,10;wheel,%u,20,20,-2;move,%u,30,30", B, B + 1, B + 5000, B + 5001)) == 4,
		 "the schedule is not 4 steps");
	au_update(B);
	AU_CHECK(au_take_wheel(1) == 1, "the first notch of 3 is not one wheel event of the schedule's alone");
	au_update(B + 1);
	AU_CHECK(au_take_wheel(1) == 0, "the second notch came before an input update");
	/* a real motion between two notches: the next notch is at (X,Y) again */
	SDL_Delay(2);
	au_push_motion(4, 4);
	au_pump();
	AU_CHECK(au_state().src == AU_SDL, "a real motion between two notches did not end the schedule's pointer");
	for (int k = 2; k <= 3; k++) {
		au_input_update();
		au_update(B + k);
		AU_CHECK(au_take_wheel(1) == 1, "notch %d of 3 after an input update", k);
		struct au_state n = au_state();
		AU_CHECK(n.src == AU_AUTO && n.x == 640 && n.y == 360, "notch %d of 3 at %s (%d,%d), want the schedule's (640,360)", k, au_source_name(n.src), n.x, n.y);
	}
	au_update(B + 4);
	struct au_state s = au_state();
	AU_CHECK(au_take_wheel(1) == 0 && s.x == 640 && s.y == 360, "before an input update read the last notch: the next step started (%d,%d) or a notch more", s.x, s.y);
	au_input_update();
	au_update(B + 5);
	s = au_state();
	AU_CHECK(s.x == 640 && s.y == 360, "the next step started in the update that ended the wheel (%d,%d)", s.x, s.y);
	AU_AT(6, AU_AUTO, 10, 10, false, false, false, "the step after the wheel");
	/* no input update at all: a notch every 500 ms */
	au_update(B + 5000);
	AU_CHECK(au_take_wheel(-1) == 1, "the first notch of -2");
	au_update(B + 5499);
	AU_CHECK(au_take_wheel(-1) == 0, "the second notch of -2 came 499 ms later without an input update");
	au_update(B + 5500);
	AU_CHECK(au_take_wheel(-1) == 1, "the second notch of -2 did not come 500 ms later without an input update");
	AU_AT(5999, AU_AUTO, 20, 20, false, false, false, "the wheel, 499 ms after its last notch");
	AU_AT(6000, AU_AUTO, 20, 20, false, false, false, "the wheel over");
	AU_AT(6001, AU_AUTO, 30, 30, false, false, false, "the step after it");

	/* On SDL's clock with frames: an input update every 16 ms, and the
	 * wheel read and cleared once a frame before it, as a script reads it. */
	SDL_AddEventWatch(au_watch, NULL);
	uint32_t T = SDL_GetTicks() + 30;
	AU_CHECK(au_load(au_spec("wheel,%u,640,360,3;wheel,%u,640,360,-2;move,%u,5,5", T, T + 1, T + 2)) == 3, "the frame schedule is not 3 steps");
	int reads[64], read_frame_moved = -1;
	int nr_reads = 0;
	au_frame_no = 0;
	uint32_t last = SDL_GetTicks();
	while (SDL_GetTicks() < T + 600) {
		SDL_Delay(1);
		handle_events();
		if (SDL_GetTicks() - last < 16)
			continue;
		last = SDL_GetTicks();
		int f, b, x, y;
		mouse_get_wheel(&f, &b);
		mouse_clear_wheel();
		mouse_get_pos(&x, &y);
		if ((f || b) && nr_reads < 64)
			reads[nr_reads++] = f ? 1 : -1;
		if (x == 5 && y == 5 && read_frame_moved < 0)
			read_frame_moved = au_frame_no;
		au_input_update();
		au_frame_no++;
	}
	SDL_DelEventWatch(au_watch, NULL);
	printf("    %d notch(es) queued, in frames", au_watch_n);
	for (int i = 0; i < au_watch_n; i++)
		printf(" %d(%+d)", au_watch_frame[i], au_watch_y[i]);
	printf("; read in %d frame(s):", nr_reads);
	for (int i = 0; i < nr_reads; i++)
		printf(" %+d", reads[i]);
	printf("; the move read first in frame %d\n", read_frame_moved);
	static const int want[] = { 1, 1, 1, -1, -1 };
	bool order = au_watch_n == 5 && nr_reads == 5;
	for (int i = 0; order && i < 5; i++)
		order = au_watch_y[i] == want[i] && reads[i] == want[i] && (i == 0 || au_watch_frame[i] > au_watch_frame[i - 1]);
	AU_CHECK(order, "3 notches forward and 2 back are not queued one per input update and read one a frame");
	AU_CHECK(au_watch_n == 5 && read_frame_moved > au_watch_frame[4], "the move after the wheels was read in frame %d, the last notch was queued after frame %d",
		 read_frame_moved, au_watch_n ? au_watch_frame[au_watch_n - 1] : -1);
	AU_CHECK(au_warps == 0, "the schedule warped the desktop cursor %d time(s)", au_warps);
	printf("  AU11 a notch per input update\n");
}

/* AU12 */
static void au_limits(void)
{
	if (!au_setup())
		return;
	static const char *const refused[] = {
		"wheel,1,2,3,1001", "wheel,1,2,3,-1001", "wheel,1,2,3,2147483647", "wheel,1,2,3,-2147483648",
		"wheel,1,2000000,0,1", "move,1,1000001,0", "move,1,0,-1000001,10", "click,1,-1000001,0",
		"down,1,0,2147483647", "drag,1,0,0,2,1000001,0", "drag,1,-2147483648,0,2,0,0",
	};
	for (unsigned i = 0; i < sizeof(refused) / sizeof(refused[0]); i++)
		AU_CHECK(au_load(refused[i]) == 0, "\"%s\" was taken as %d step(s)", refused[i], au_load(refused[i]));
	static const char *const taken[] = {
		"wheel,1,2,3,1000", "wheel,1,2,3,-1000", "move,1,1000000,-1000000", "1,2147483647,-2147483648",
	};
	for (unsigned i = 0; i < sizeof(taken) / sizeof(taken[0]); i++)
		AU_CHECK(au_load(taken[i]) == 1, "\"%s\" was not taken (%d steps)", taken[i], au_load(taken[i]));
	/* A straight move from the first form's most negative x to the largest
	 * coordinate of the others: 64-bit arithmetic, no overflow. */
	uint32_t B = au_base;
	AU_CHECK(au_load(au_spec("%u,-2147483648,0;move,%u,1000000,0,100", B, B + 200)) == 2, "the extreme schedule is not 2 steps");
	AU_AT(0, AU_AUTO, INT_MIN, 0, true, false, false, "the click at INT_MIN");
	AU_AT(100, AU_AUTO, INT_MIN, 0, false, false, false, "its release");
	AU_AT(200, AU_AUTO, INT_MIN, 0, false, false, false, "the move sets out");
	AU_AT(250, AU_AUTO, -1073241824, 0, false, false, false, "halfway from INT_MIN to 1000000");
	AU_AT(300, AU_AUTO, 1000000, 0, false, false, false, "arrived");
#if SDL_VERSION_ATLEAST(2, 26, 0)
	/* The wheel's window coordinates on a viewport twice the game's size. */
	sdl.viewport = (SDL_Rect){ 0, 0, 2560, 1440 };
	AU_CHECK(au_load(au_spec("wheel,%u,1000000,-1000000,1", B + 1000)) == 1, "the wheel at the limits");
	au_update(B + 1000);
	SDL_Event e;
	SDL_PumpEvents();
	int n = SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_MOUSEWHEEL, SDL_MOUSEWHEEL);
	AU_CHECK(n == 1 && e.wheel.mouseX == 2000000 && e.wheel.mouseY == -2000000, "the wheel event at (1000000,-1000000) on a 2x viewport: %d event(s), at (%d,%d)",
		 n, n == 1 ? e.wheel.mouseX : 0, n == 1 ? e.wheel.mouseY : 0);
#endif
	printf("  AU12 limits\n");
}

/* AU13: XSYS4_STAGE2_TRACE for the lines of the input (S2 wheel, S2 hover;
 * S2 mouse already was so): on for anything but "" and "0". */
static int au_trace_lines(const char *value, int *wheel, int *hover)
{
	if (value)
		setenv("XSYS4_STAGE2_TRACE", value, 1);
	else
		unsetenv("XSYS4_STAGE2_TRACE");
	if (!au_sdl())
		return -1;
	FILE *f = tmpfile();
	if (!f)
		return -1;
	fflush(stdout);
	fflush(stderr);
	int o1 = dup(1), o2 = dup(2);
	dup2(fileno(f), 1);
	dup2(fileno(f), 2);
	au_push_wheel(0, 1);
	au_pump();
	au_input_update();
	fflush(stdout);
	fflush(stderr);
	dup2(o1, 1);
	dup2(o2, 2);
	close(o1);
	close(o2);
	rewind(f);
	char line[512];
	*wheel = *hover = 0;
	while (fgets(line, sizeof(line), f)) {
		*wheel += strstr(line, "S2 wheel") != NULL;
		*hover += strstr(line, "S2 hover") != NULL;
	}
	fclose(f);
	return 0;
}
static void au_trace_check(const char *value, bool on)
{
	int wheel = -1, hover = -1;
	AU_CHECK(au_trace_lines(value, &wheel, &hover) == 0, "the trace could not be read");
	AU_CHECK(wheel == (on ? 1 : 0) && hover == (on ? 1 : 0), "XSYS4_STAGE2_TRACE %s%s%s: %d S2 wheel and %d S2 hover line(s), want %d of each",
		 value ? "\"" : "unset", value ? value : "", value ? "\"" : "", wheel, hover, on ? 1 : 0);
}
static void au_trace_1(void) { au_trace_check("1", true); }
static void au_trace_0(void) { au_trace_check("0", false); }
static void au_trace_empty(void) { au_trace_check("", false); }
static void au_trace_unset(void) { au_trace_check(NULL, false); }
static void au_trace(void)
{
	au_sub("XSYS4_STAGE2_TRACE=1", au_trace_1);
	au_sub("XSYS4_STAGE2_TRACE=0", au_trace_0);
	au_sub("XSYS4_STAGE2_TRACE empty", au_trace_empty);
	au_sub("XSYS4_STAGE2_TRACE unset", au_trace_unset);
	printf("  AU13 the trace switch\n");
}

static int test_auto_input(void)
{
	void (*cases[])(void) = { au_first_form, au_new_steps, au_real_events, au_schedule_pointer, au_production, au_engine, au_periodic,
				  au_focus, au_no_desktop_warp, au_release_read, au_wheel_notches, au_limits, au_trace };
	const char *names[] = { "AU1", "AU2", "AU3", "AU4", "AU5", "AU6", "AU7", "AU8", "AU9", "AU10", "AU11", "AU12", "AU13" };
	int failed = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			au_failures = 0;
			cases[i]();
			fflush(stdout);
			_exit(au_failures ? 1 : 0);
		}
		int st = 0;
		bool ok = pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
		printf("%s %s\n", names[i], ok ? "ok" : "FAIL");
		failed += !ok;
	}
	printf("AUTO-INPUT %s: %d of %zu cases failed\n", failed ? "FAIL" : "PASS", failed, sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}

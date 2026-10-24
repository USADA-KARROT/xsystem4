/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <SDL.h>
#include "system4.h"
#include "system4/ain.h"
#include "queue.h"
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "input.h"
#include "mixer.h"
#include "msgskip.h"
#include "scene.h"
#include "vm.h"
#include "xsystem4.h"
#include "debugger.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

bool key_state[VK_NR_KEYCODES];
static enum sact_keycode sdl_keytable[] = {
	[SDL_SCANCODE_0] = VK_0,
	[SDL_SCANCODE_1] = VK_1,
	[SDL_SCANCODE_2] = VK_2,
	[SDL_SCANCODE_3] = VK_3,
	[SDL_SCANCODE_4] = VK_4,
	[SDL_SCANCODE_5] = VK_5,
	[SDL_SCANCODE_6] = VK_6,
	[SDL_SCANCODE_7] = VK_7,
	[SDL_SCANCODE_8] = VK_8,
	[SDL_SCANCODE_9] = VK_9,
	[SDL_SCANCODE_A] = VK_A,
	[SDL_SCANCODE_B] = VK_B,
	[SDL_SCANCODE_C] = VK_C,
	[SDL_SCANCODE_D] = VK_D,
	[SDL_SCANCODE_E] = VK_E,
	[SDL_SCANCODE_F] = VK_F,
	[SDL_SCANCODE_G] = VK_G,
	[SDL_SCANCODE_H] = VK_H,
	[SDL_SCANCODE_I] = VK_I,
	[SDL_SCANCODE_J] = VK_J,
	[SDL_SCANCODE_K] = VK_K,
	[SDL_SCANCODE_L] = VK_L,
	[SDL_SCANCODE_M] = VK_M,
	[SDL_SCANCODE_N] = VK_N,
	[SDL_SCANCODE_O] = VK_O,
	[SDL_SCANCODE_P] = VK_P,
	[SDL_SCANCODE_Q] = VK_Q,
	[SDL_SCANCODE_R] = VK_R,
	[SDL_SCANCODE_S] = VK_S,
	[SDL_SCANCODE_T] = VK_T,
	[SDL_SCANCODE_U] = VK_U,
	[SDL_SCANCODE_V] = VK_V,
	[SDL_SCANCODE_W] = VK_W,
	[SDL_SCANCODE_X] = VK_X,
	[SDL_SCANCODE_Y] = VK_Y,
	[SDL_SCANCODE_Z] = VK_Z,
	[SDL_SCANCODE_BACKSPACE] = VK_BACK,
	[SDL_SCANCODE_TAB] = VK_TAB,
	[SDL_SCANCODE_CLEAR] = VK_CLEAR,
	[SDL_SCANCODE_RETURN] = VK_RETURN,
	[SDL_SCANCODE_LSHIFT] = VK_SHIFT,
	[SDL_SCANCODE_RSHIFT] = VK_SHIFT,
	[SDL_SCANCODE_LCTRL] = VK_CONTROL,
	[SDL_SCANCODE_RCTRL] = VK_CONTROL,
	[SDL_SCANCODE_LALT] = VK_MENU,
	[SDL_SCANCODE_RALT] = VK_MENU,
	[SDL_SCANCODE_PAUSE] = VK_PAUSE,
	[SDL_SCANCODE_CAPSLOCK] = VK_CAPITAL,
	[SDL_SCANCODE_ESCAPE] = VK_ESCAPE,
	[SDL_SCANCODE_SPACE] = VK_SPACE,
	[SDL_SCANCODE_PAGEUP] = VK_PRIOR,
	[SDL_SCANCODE_PAGEDOWN] = VK_NEXT,
	[SDL_SCANCODE_END] = VK_END,
	[SDL_SCANCODE_HOME] = VK_HOME,
	[SDL_SCANCODE_LEFT] = VK_LEFT,
	[SDL_SCANCODE_UP] = VK_UP,
	[SDL_SCANCODE_RIGHT] = VK_RIGHT,
	[SDL_SCANCODE_DOWN] = VK_DOWN,
	[SDL_SCANCODE_SELECT] = VK_SELECT,
	[SDL_SCANCODE_EXECUTE] = VK_EXECUTE,
	[SDL_SCANCODE_PRINTSCREEN] = VK_SNAPSHOT,
	[SDL_SCANCODE_INSERT] = VK_INSERT,
	[SDL_SCANCODE_DELETE] = VK_DELETE,
	[SDL_SCANCODE_HELP] = VK_HELP,
	[SDL_SCANCODE_KP_0] = VK_NUMPAD0,
	[SDL_SCANCODE_KP_1] = VK_NUMPAD1,
	[SDL_SCANCODE_KP_2] = VK_NUMPAD2,
	[SDL_SCANCODE_KP_3] = VK_NUMPAD3,
	[SDL_SCANCODE_KP_4] = VK_NUMPAD4,
	[SDL_SCANCODE_KP_5] = VK_NUMPAD5,
	[SDL_SCANCODE_KP_6] = VK_NUMPAD6,
	[SDL_SCANCODE_KP_7] = VK_NUMPAD7,
	[SDL_SCANCODE_KP_8] = VK_NUMPAD8,
	[SDL_SCANCODE_KP_9] = VK_NUMPAD9,
	[SDL_SCANCODE_KP_MULTIPLY] = VK_MULTIPLY,
	[SDL_SCANCODE_KP_PLUS] = VK_ADD,
	[SDL_SCANCODE_KP_VERTICALBAR] = VK_SEPARATOR, // ???
	[SDL_SCANCODE_KP_MINUS] = VK_SUBTRACT,
	[SDL_SCANCODE_KP_PERIOD] = VK_DECIMAL,
	[SDL_SCANCODE_KP_DIVIDE] = VK_DIVIDE,
	[SDL_SCANCODE_F1] = VK_F1,
	[SDL_SCANCODE_F2] = VK_F2,
	[SDL_SCANCODE_F3] = VK_F3,
	[SDL_SCANCODE_F4] = VK_F4,
	[SDL_SCANCODE_F5] = VK_F5,
	[SDL_SCANCODE_F6] = VK_F6,
	[SDL_SCANCODE_F7] = VK_F7,
	[SDL_SCANCODE_F8] = VK_F8,
	[SDL_SCANCODE_F9] = VK_F9,
	[SDL_SCANCODE_F10] = VK_F10,
	[SDL_SCANCODE_F11] = VK_F11,
	[SDL_SCANCODE_F12] = VK_F12,
	[SDL_SCANCODE_F13] = VK_F13,
	[SDL_SCANCODE_F14] = VK_F14,
	[SDL_SCANCODE_F15] = VK_F15,
	[SDL_SCANCODE_F16] = VK_F16,
	[SDL_SCANCODE_F17] = VK_F17,
	[SDL_SCANCODE_F18] = VK_F18,
	[SDL_SCANCODE_F19] = VK_F19,
	[SDL_SCANCODE_F20] = VK_F20,
	[SDL_SCANCODE_F21] = VK_F21,
	[SDL_SCANCODE_F22] = VK_F22,
	[SDL_SCANCODE_F23] = VK_F23,
	[SDL_SCANCODE_F24] = VK_F24,
	[SDL_SCANCODE_NUMLOCKCLEAR] = VK_NUMLOCK,
	[SDL_SCANCODE_SCROLLLOCK] = VK_SCROLL,
};

bool mouse_focus = true;
bool keyboard_focus = true;

#define MAX_CONTROLLERS 4
static SDL_GameController *controllers[MAX_CONTROLLERS];

#ifdef __ANDROID__
struct deferred_keyevent {
	STAILQ_ENTRY(deferred_keyevent) entry;
	SDL_Event e;
};
static STAILQ_HEAD(deferred_keyevent_queue, deferred_keyevent) deferred_keyevent_queue =
	STAILQ_HEAD_INITIALIZER(deferred_keyevent_queue);
static uint32_t last_keyevent_timestamp;
#define DEFERRED_KEY_DELAY 10
#endif

// Stores a mouse button event synthesized from a touch event (valid if
// .timestamp != 0). We defer such events to prevent games from handling
// button down events before reading the pointer position.
static SDL_MouseButtonEvent deferred_synthetic_mouse_event;
#define SYNTHETIC_MOUSE_EVENT_DELAY 50

static uint32_t long_touch_start_timestamp;
static SDL_FRect long_touch_finger_rect;
#define LONG_TOUCH_DURATION 1000

static float scroll_gesture_y;
#define SCROLL_GESTURE_SENSITIVITY 0.02f  // 2% of screen height

static enum sact_keycode sdl_to_sact_button(int button)
{
	switch (button) {
	case SDL_BUTTON_LEFT:   return VK_LBUTTON;
	case SDL_BUTTON_RIGHT:  return VK_RBUTTON;
	case SDL_BUTTON_MIDDLE: return VK_MBUTTON;
	default:                return 0;
	}
}

bool key_is_down(enum sact_keycode code)
{
	if (code < 0 || code >= VK_NR_KEYCODES) {
		return false;
	}
	return key_state[code];
}

void key_clear_flag(bool no_ctrl)
{
	for (int i = 0; i < VK_NR_KEYCODES; i++) {
		if (no_ctrl && i == VK_CONTROL)
			continue;
		key_state[i] = false;
	}
}

/*
 * The pointer position the program reads (mouse_get_pos).
 *
 * It is the pointer as SDL reports it, as upstream, except while a position
 * the program set stands in front of it:
 *  - MOUSE_POS_WARP, from mouse_set_pos: the HLLs that move the cursor
 *    (IbisInputEngine.Mouse_MovePosImmediately, SystemService, SACT2,
 *    InputDevice, vmCursor). The original moves the system cursor and reads
 *    the system cursor back: Mouse_MovePosImmediately calls its mouse
 *    object (0x498856), and the window's move, presumably what that
 *    reaches, is SetCursorPos (0x4c0e00 -> 0x4c1200, only while the window
 *    is not minimized and for a point in its client area), the read
 *    GetCursorPos (0x4c0ce0); the real mouse goes on moving the cursor
 *    afterwards. So a read right after the move gets the new position.
 *    Here SDL warps the desktop cursor, as upstream does, but may report
 *    the new position only later, or not at all while the window is not
 *    focused (the reason a3eabb4 gave for the override), so the position
 *    asked for is read until the mouse reports again.
 *  - MOUSE_POS_AUTO, from the scheduled input (XSYS4_AUTO_CLICK_SEQ and
 *    XSYS4_AUTO_CLICK, a test facility, see auto_input below): a pointer of
 *    its own, which never moves the desktop cursor, since somebody may be
 *    working at the same desktop while a run goes on. A move the HLLs ask
 *    for during such a run moves this pointer and not the cursor; so does
 *    one while XSYS4_NO_DESKTOP_WARP is set (to anything but "" and "0";
 *    the bounded GUI runs of the harness always set it), or while
 *    XSYS4_AUTO_CLICK_SEQ is set, even if none of its steps is understood.
 *    This pointer counts as in the window (mouse_in_window): the HLLs that
 *    report whether the cursor is in the window say it is, as they did when
 *    every scheduled click warped the cursor and SDL gave the window the
 *    mouse focus with it.
 * Either ends at the next real event of the mouse, a motion, a button or
 * the wheel, that SDL timestamped after the position was set (an event SDL
 * had taken in before does not count; on macOS SDL timestamps a motion when
 * it pumps it, so one made just before may still count): from then on the
 * real pointer is read again, until a move or a scheduled step sets a
 * position again. A motion does not end it while the schedule holds a
 * button down, nor after the schedule released one until the release has
 * been read (auto_input_guards), so that a scheduled click or drag is seen
 * where it was told whatever passes over the window meanwhile; a real
 * button or the wheel does.
 *
 * Before, a position once set was read for ever (a3eabb4): the real mouse
 * did nothing to the program after a scheduled click or a move (no hover,
 * no drag in a window a schedule had clicked), and the hover a run's
 * screenshots showed was where its last click had been.
 */
static struct {
	enum mouse_pos_source source;
	int x, y;		// game coordinates, while source is not MOUSE_POS_SDL
	uint32_t set_at;	// SDL_GetTicks() when they were set
} pointer;

// The scheduled input's SDL events (the wheel) carry this as their mouse.
#define AUTO_INPUT_MOUSEID ((Uint32)-2)

static bool auto_input_guards(uint32_t timestamp);

static void pointer_set(enum mouse_pos_source source, int x, int y)
{
	pointer.source = source;
	pointer.x = x;
	pointer.y = y;
	pointer.set_at = SDL_GetTicks();
}

// A real event of the mouse: the real pointer is read again.
static void pointer_real_event(uint32_t timestamp, bool motion)
{
	if (motion && auto_input_guards(timestamp))
		return;
	if (pointer.source != MOUSE_POS_SDL && SDL_TICKS_PASSED(timestamp, pointer.set_at + 1))
		pointer.source = MOUSE_POS_SDL;
}

bool mouse_in_window(void)
{
	return pointer.source == MOUSE_POS_AUTO || mouse_focus;
}

bool input_stage2_trace(void)
{
	static int on = -1;
	if (on < 0) {
		const char *v = getenv("XSYS4_STAGE2_TRACE");
		on = v && *v && strcmp(v, "0");
	}
	return on;
}

static void sdl_mouse_pos(int *x, int *y)
{
	int wx, wy;
	SDL_GetMouseState(&wx, &wy);
	*x = (wx - sdl.viewport.x) * sdl.w / sdl.viewport.w;
	*y = (wy - sdl.viewport.y) * sdl.h / sdl.viewport.h;
}

const char *mouse_pos_source_name(enum mouse_pos_source source)
{
	switch (source) {
	case MOUSE_POS_SDL:  return "sdl";
	case MOUSE_POS_WARP: return "warp";
	case MOUSE_POS_AUTO: return "auto";
	}
	return "?";
}

enum mouse_pos_source mouse_peek_pos(int *x, int *y)
{
	if (pointer.source != MOUSE_POS_SDL) {
		*x = pointer.x;
		*y = pointer.y;
	} else {
		sdl_mouse_pos(x, y);
	}
	return pointer.source;
}

/*
 * Scheduled input, a test facility: XSYS4_AUTO_CLICK_SEQ is a list of steps
 * separated by ';' or line breaks; a '#' starts a comment that runs to the end
 * of its line.
 * T is a time in ms of SDL_GetTicks(), X and Y are game coordinates, B is a
 * button, L (the default), R or M, MS a time in ms.
 *   T,X,Y                    a left click at (X,Y): pressed at T, released
 *                            100 ms later (the only form before the others)
 *   click,T,X,Y[,B[,MS]]     a click of B held MS ms (default 100)
 *   rclick,T,X,Y[,MS]        a click of the right button
 *   mclick,T,X,Y[,MS]        a click of the middle button
 *   move,T,X,Y[,MS]          moves the pointer and presses nothing (a hover);
 *                            with MS it goes in a straight line from where
 *                            the program reads it to (X,Y), there MS ms later
 *   down,T,X,Y[,B]           moves to (X,Y) and presses B, which stays down
 *   up,T,X,Y[,B]             moves to (X,Y) and releases B
 *   drag,T,X,Y,T2,X2,Y2[,B]  down at (X,Y) at T; from T+100 a straight line
 *                            to (X2,Y2), there at T2; up there at T2+100
 *                            (made into a down, a move and an up)
 *   wheel,T,X,Y,N            moves to (X,Y) and turns the wheel N notches,
 *                            forward (up) for N > 0, back (down) for N < 0,
 *                            one notch per input update (below)
 *   key,T,VK[,MS]            presses the key with the engine's code VK (as
 *                            in include/input.h: 27 Esc, 32 space, 13
 *                            Return) for MS ms (default 100); the pointer
 *                            does not move; not the mouse buttons (1, 2, 4),
 *                            which have the steps above
 * The steps run in their order: a step starts at its T, or when the one
 * before it is over if that is later (a click when its button is up again,
 * a move with MS when it has arrived, a wheel when its last notch has been
 * read, a key when it is up again), and never in the handle_events that
 * ended the one before. The
 * pointer is the schedule's own (MOUSE_POS_AUTO, see the pointer above):
 * the desktop cursor does not move. A button is pressed and released in
 * the key state directly, as the first form always was; a notch goes
 * through SDL's event queue, as a real one does, marked as the schedule's
 * so that it does not end the schedule's pointer.
 *
 * An input update is a run of PE_UpdateInputState (input_auto_read), where
 * the parts input reads the buttons and the pointer once a frame (v14 from
 * PartsEngine.Update, older programs also from system.Peek). A notch is
 * queued, and handled, in one handle_events; the next one waits for an
 * input update after it, so that each input update sees one notch, as a
 * wheel turned notch by notch is seen, whether the engine keeps the
 * direction of the last notch (wheel_dir, now) or adds them up. After the
 * schedule released a button, a real motion leaves the schedule's pointer
 * until an input update has read the release (or the next step starts), so
 * that the release is read where the step was. A program without input
 * updates gets each of these AUTO_INPUT_READ_WAIT ms later instead.
 *
 * Out of the first form, which is read as it always was, a coordinate is
 * at most AUTO_INPUT_COORD_MAX away from 0 and a wheel turns at most
 * AUTO_INPUT_NOTCHES_MAX notches; beyond that the step is not understood.
 *
 * XSYS4_AUTO_CLICK=<ms>, when there is no XSYS4_AUTO_CLICK_SEQ, clicks every
 * <ms> at XSYS4_AUTO_CLICK_X/Y (default 350,180), at most
 * XSYS4_AUTO_CLICK_COUNT times if that is given.
 */
enum auto_step_kind { AUTO_CLICK, AUTO_MOVE, AUTO_DOWN, AUTO_UP, AUTO_WHEEL, AUTO_KEY };

#define AUTO_INPUT_READ_WAIT 500
#define AUTO_INPUT_COORD_MAX 1000000
#define AUTO_INPUT_NOTCHES_MAX 1000

struct auto_step {
	enum auto_step_kind kind;
	bool first_form;		// "T,X,Y": logged as it always was
	uint32_t t;
	int x, y;
	enum sact_keycode button;
	uint32_t ms;			// a click, a key: held; a move: on its way
	int notches;
	int key;			// a key: its code
};

static struct {
	bool loaded;
	bool given;			// a schedule was given (understood or not)
	struct auto_step *steps;
	int nr_steps;
	int next;			// the next step to start
	bool busy;			// steps[next - 1] is going on
	uint32_t started;
	int from_x, from_y;		// where a move with MS set out from
	int notches_left;		// a wheel: the notches still to queue
	int notches_done;
	uint32_t notch_at;		// a wheel: its last notch, then
	unsigned notch_reads;		// and input_reads then
	// XSYS4_AUTO_CLICK
	int interval, max, count, x, y;
	bool down;
	uint32_t last;
	unsigned held;			// the buttons it holds down now (auto_button_bit)
	bool guard;			// a release not read yet: released_at, released_reads
	uint32_t released_at;
	unsigned released_reads;
} auto_input;

// The input updates so far (input_auto_read).
static unsigned input_reads;

// XSYS4_NO_DESKTOP_WARP
static bool no_desktop_warp;

// Whether a move of the HLLs moves the program's own pointer and not the
// desktop cursor: during a schedule, or when it was asked for.
static bool desktop_warp_off(void)
{
	return auto_input.given || auto_input.interval > 0 || no_desktop_warp;
}

static unsigned auto_button_bit(enum sact_keycode b)
{
	return b == VK_RBUTTON ? 2 : b == VK_MBUTTON ? 4 : 1;
}

// Whether a real motion at `timestamp` leaves the schedule's pointer: while
// the schedule holds a button, or released one that no input update has read
// yet (at most AUTO_INPUT_READ_WAIT ms).
static bool auto_input_guards(uint32_t timestamp)
{
	if (auto_input.held != 0)
		return true;
	return auto_input.guard && input_reads == auto_input.released_reads
		&& !SDL_TICKS_PASSED(timestamp, auto_input.released_at + AUTO_INPUT_READ_WAIT);
}

static void auto_input_button(enum sact_keycode b, bool down)
{
	key_state[b] = down;
	if (down) {
		auto_input.held |= auto_button_bit(b);
	} else {
		auto_input.held &= ~auto_button_bit(b);
		auto_input.guard = true;
		auto_input.released_at = SDL_GetTicks();
		auto_input.released_reads = input_reads;
	}
}

void input_auto_read(void)
{
	input_reads++;
}

static char auto_button_name(enum sact_keycode b)
{
	return b == VK_RBUTTON ? 'R' : b == VK_MBUTTON ? 'M' : 'L';
}

static const char *auto_step_describe(const struct auto_step *s, char *buf, size_t size)
{
	switch (s->kind) {
	case AUTO_CLICK: snprintf(buf, size, "click %c %ums", auto_button_name(s->button), s->ms); break;
	case AUTO_MOVE:  snprintf(buf, size, "move %ums", s->ms); break;
	case AUTO_DOWN:  snprintf(buf, size, "down %c", auto_button_name(s->button)); break;
	case AUTO_UP:    snprintf(buf, size, "up %c", auto_button_name(s->button)); break;
	case AUTO_WHEEL: snprintf(buf, size, "wheel %d", s->notches); break;
	case AUTO_KEY:   snprintf(buf, size, "key %d %ums", s->key, s->ms); break;
	}
	return buf;
}

static void auto_input_add(struct auto_step s)
{
	int i = auto_input.nr_steps++;
	auto_input.steps = xrealloc(auto_input.steps, auto_input.nr_steps * sizeof(struct auto_step));
	auto_input.steps[i] = s;
	if (s.first_form) {
		WARNING("Auto-click seq[%d]: t=%dms pos=(%d,%d)", i, (int)s.t, s.x, s.y);
	} else if (s.kind == AUTO_KEY) {
		char d[32];
		WARNING("Auto-input seq[%d]: %s t=%ums", i, auto_step_describe(&s, d, sizeof(d)), s.t);
	} else {
		char d[32];
		WARNING("Auto-input seq[%d]: %s t=%ums pos=(%d,%d)", i,
				auto_step_describe(&s, d, sizeof(d)), s.t, s.x, s.y);
	}
}

static bool auto_parse_int(const char *s, int *out)
{
	char *end;
	errno = 0;
	long v = strtol(s, &end, 10);
	while (*end == ' ' || *end == '\t')
		end++;
	if (end == s || *end || errno || v < INT_MIN || v > INT_MAX)
		return false;
	*out = v;
	return true;
}

static bool auto_coord_ok(int v)
{
	return v >= -AUTO_INPUT_COORD_MAX && v <= AUTO_INPUT_COORD_MAX;
}

static bool auto_parse_button(const char *s, enum sact_keycode *out)
{
	if (!strcasecmp(s, "L")) *out = VK_LBUTTON;
	else if (!strcasecmp(s, "R")) *out = VK_RBUTTON;
	else if (!strcasecmp(s, "M")) *out = VK_MBUTTON;
	else return false;
	return true;
}

// One step of the schedule; false if it is not understood.
static bool auto_input_parse_step(char *tok)
{
	if (isdigit((unsigned char)*tok) || *tok == '-' || *tok == '+') {
		// The first form, read as it always was.
		int t, x, y;
		if (sscanf(tok, "%d,%d,%d", &t, &x, &y) != 3)
			return false;
		auto_input_add((struct auto_step) { .kind = AUTO_CLICK, .first_form = true,
				.t = t, .x = x, .y = y, .button = VK_LBUTTON, .ms = 100 });
		return true;
	}
	char *f[9];
	int n = 0;
	for (char *p = tok; n < 9; n++) {
		f[n] = p;
		p = strchr(p, ',');
		if (!p) {
			n++;
			break;
		}
		*p++ = '\0';
	}
	for (int i = 0; i < n; i++) {
		while (*f[i] == ' ' || *f[i] == '\t')
			f[i]++;
		char *e = f[i] + strlen(f[i]);
		while (e > f[i] && (e[-1] == ' ' || e[-1] == '\t'))
			*--e = '\0';
	}
	int v[8];
	// The numbers after the verb, up to the first field that is not one.
	int nr_ints = 0;
	while (1 + nr_ints < n && nr_ints < 8 && auto_parse_int(f[1 + nr_ints], &v[nr_ints]))
		nr_ints++;
	int rest = n - 1 - nr_ints;		// fields after the numbers
	if (!strcasecmp(f[0], "key")) {
		// key,T,VK[,MS]
		if (rest || nr_ints < 2 || nr_ints > 3 || v[0] < 0 || v[1] <= 0 || v[1] >= VK_NR_KEYCODES
				|| v[1] == VK_LBUTTON || v[1] == VK_RBUTTON || v[1] == VK_MBUTTON
				|| (nr_ints == 3 && v[2] < 0))
			return false;
		auto_input_add((struct auto_step) { .kind = AUTO_KEY, .t = v[0], .key = v[1],
				.ms = nr_ints == 3 ? v[2] : 100 });
		return true;
	}
	if (nr_ints < 3 || v[0] < 0 || !auto_coord_ok(v[1]) || !auto_coord_ok(v[2]))
		return false;
	struct auto_step s = { .t = v[0], .x = v[1], .y = v[2], .button = VK_LBUTTON };
	const char *verb = f[0];
	if (!strcasecmp(verb, "click")) {
		// click,T,X,Y[,B[,MS]]
		if (nr_ints != 3 || rest > 2)
			return false;
		if (rest >= 1 && !auto_parse_button(f[4], &s.button))
			return false;
		int ms = 100;
		if (rest == 2 && (!auto_parse_int(f[5], &ms) || ms < 0))
			return false;
		s.kind = AUTO_CLICK;
		s.ms = ms;
	} else if (!strcasecmp(verb, "rclick") || !strcasecmp(verb, "mclick")) {
		// rclick,T,X,Y[,MS]
		if (rest || nr_ints > 4 || (nr_ints == 4 && v[3] < 0))
			return false;
		s.kind = AUTO_CLICK;
		s.button = tolower((unsigned char)verb[0]) == 'r' ? VK_RBUTTON : VK_MBUTTON;
		s.ms = nr_ints == 4 ? v[3] : 100;
	} else if (!strcasecmp(verb, "move")) {
		// move,T,X,Y[,MS]
		if (rest || nr_ints > 4 || (nr_ints == 4 && v[3] < 0))
			return false;
		s.kind = AUTO_MOVE;
		s.ms = nr_ints == 4 ? v[3] : 0;
	} else if (!strcasecmp(verb, "down") || !strcasecmp(verb, "up")) {
		// down,T,X,Y[,B]
		if (nr_ints != 3 || rest > 1 || (rest && !auto_parse_button(f[4], &s.button)))
			return false;
		s.kind = tolower((unsigned char)verb[0]) == 'd' ? AUTO_DOWN : AUTO_UP;
	} else if (!strcasecmp(verb, "drag")) {
		// drag,T,X,Y,T2,X2,Y2[,B]
		if (nr_ints != 6 || rest > 1 || (rest && !auto_parse_button(f[7], &s.button)))
			return false;
		if (v[3] < v[0] || !auto_coord_ok(v[4]) || !auto_coord_ok(v[5]))
			return false;
		uint32_t t2 = v[3], move_at = s.t + 100;
		s.kind = AUTO_DOWN;
		auto_input_add(s);
		auto_input_add((struct auto_step) { .kind = AUTO_MOVE, .t = move_at, .x = v[4], .y = v[5],
				.button = s.button, .ms = t2 > move_at ? t2 - move_at : 0 });
		auto_input_add((struct auto_step) { .kind = AUTO_UP, .t = t2 + 100, .x = v[4], .y = v[5],
				.button = s.button });
		return true;
	} else if (!strcasecmp(verb, "wheel")) {
		// wheel,T,X,Y,N
		if (nr_ints != 4 || rest || v[3] == 0 || v[3] < -AUTO_INPUT_NOTCHES_MAX || v[3] > AUTO_INPUT_NOTCHES_MAX)
			return false;
		s.kind = AUTO_WHEEL;
		s.notches = v[3];
	} else {
		return false;
	}
	auto_input_add(s);
	return true;
}

int input_auto_load(const char *spec)
{
	free(auto_input.steps);
	memset(&auto_input, 0, sizeof(auto_input));
	auto_input.loaded = true;
	auto_input.x = 350;
	auto_input.y = 180;
	if (!spec)
		return 0;
	auto_input.given = *spec != '\0';
	char *buf = xstrdup(spec);
	for (char *c = buf; (c = strchr(c, '#')); ) {
		while (*c && *c != '\n')
			*c++ = ' ';
	}
	char *saveptr;
	for (char *tok = strtok_r(buf, ";\n", &saveptr); tok; tok = strtok_r(NULL, ";\n", &saveptr)) {
		while (*tok == ' ' || *tok == '\t' || *tok == '\r')
			tok++;
		char *e = tok + strlen(tok);
		while (e > tok && e[-1] == '\r')
			*--e = '\0';
		if (!*tok)
			continue;
		char copy[128];
		snprintf(copy, sizeof(copy), "%s", tok);
		if (!auto_input_parse_step(tok))
			WARNING("Auto-input: step not understood, skipped: %s", copy);
	}
	free(buf);
	return auto_input.nr_steps;
}

static void auto_input_load_env(void)
{
	if (auto_input.loaded)
		return;
	const char *nw = getenv("XSYS4_NO_DESKTOP_WARP");
	no_desktop_warp = nw && *nw && strcmp(nw, "0");
	if (no_desktop_warp)
		WARNING("Auto-input: XSYS4_NO_DESKTOP_WARP: a move of the HLLs moves the program's pointer, not the desktop cursor");
	const char *senv = getenv("XSYS4_AUTO_CLICK_SEQ");
	if (senv && *senv) {
		if (!input_auto_load(senv))
			WARNING("Auto-input: no step understood; the desktop cursor is not moved all the same");
		return;
	}
	input_auto_load(NULL);
	const char *env = getenv("XSYS4_AUTO_CLICK");
	auto_input.interval = env ? atoi(env) : 0;
	const char *cx = getenv("XSYS4_AUTO_CLICK_X");
	const char *cy = getenv("XSYS4_AUTO_CLICK_Y");
	const char *cn = getenv("XSYS4_AUTO_CLICK_COUNT");
	if (cx) auto_input.x = atoi(cx);
	if (cy) auto_input.y = atoi(cy);
	if (cn) auto_input.max = atoi(cn);
	if (auto_input.interval > 0)
		WARNING("Auto-click enabled: interval=%dms pos=(%d,%d) max=%d", auto_input.interval,
				auto_input.x, auto_input.y, auto_input.max);
}

// One notch of a wheel step, steps[i]: through SDL's queue, so that the
// handle_events that queued it handles it.
static void auto_input_notch(const struct auto_step *s, int i, uint32_t now)
{
	SDL_Event e = { .type = SDL_MOUSEWHEEL };
	e.wheel.windowID = sdl.window ? SDL_GetWindowID(sdl.window) : 0;
	e.wheel.which = AUTO_INPUT_MOUSEID;
	e.wheel.y = s->notches > 0 ? 1 : -1;
	e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	e.wheel.preciseY = e.wheel.y;
#endif
#if SDL_VERSION_ATLEAST(2, 26, 0)
	if (sdl.w > 0 && sdl.h > 0) {
		e.wheel.mouseX = (int)((int64_t)s->x * sdl.viewport.w / sdl.w + sdl.viewport.x);
		e.wheel.mouseY = (int)((int64_t)s->y * sdl.viewport.h / sdl.h + sdl.viewport.y);
	}
#endif
	pointer_set(MOUSE_POS_AUTO, s->x, s->y);
	if (SDL_PushEvent(&e) < 0)
		WARNING("Auto-input: wheel event not queued: %s", SDL_GetError());
	auto_input.notches_left--;
	auto_input.notches_done++;
	auto_input.notch_at = now;
	auto_input.notch_reads = input_reads;
	WARNING("Auto-input #%d notch %d of %d at %ums (input update %u)", i, auto_input.notches_done,
			abs(s->notches), now, input_reads);
}

static void auto_input_sequence(uint32_t now)
{
	char d[32];
	if (auto_input.busy) {
		struct auto_step *s = &auto_input.steps[auto_input.next - 1];
		uint32_t elapsed = now - auto_input.started;
		if (s->kind == AUTO_KEY) {
			if (elapsed >= s->ms) {
				key_state[s->key] = false;
				auto_input.busy = false;
				WARNING("Auto-input #%d released key %d at %ums", auto_input.next - 1, s->key, now);
			}
			return;
		}
		if (s->kind == AUTO_CLICK) {
			if (elapsed >= s->ms) {
				auto_input_button(s->button, false);
				auto_input.busy = false;
				if (!s->first_form)
					WARNING("Auto-input #%d released %c at %ums", auto_input.next - 1,
							auto_button_name(s->button), now);
			}
			return;
		}
		if (s->kind == AUTO_WHEEL) {
			// The last notch is read by an input update, or it is late.
			if (input_reads == auto_input.notch_reads
					&& !SDL_TICKS_PASSED(now, auto_input.notch_at + AUTO_INPUT_READ_WAIT))
				return;
			if (auto_input.notches_left > 0) {
				auto_input_notch(s, auto_input.next - 1, now);
			} else {
				auto_input.busy = false;
				WARNING("Auto-input #%d wheel read at %ums (input update %u)", auto_input.next - 1,
						now, input_reads);
			}
			return;
		}
		// a move on its way
		if (elapsed >= s->ms) {
			pointer_set(MOUSE_POS_AUTO, s->x, s->y);
			auto_input.busy = false;
			WARNING("Auto-input #%d arrived at %ums game=(%d,%d)", auto_input.next - 1, now, s->x, s->y);
		} else {
			// In 64 bits: the first form takes any int as a coordinate.
			int64_t dx = (int64_t)s->x - auto_input.from_x, dy = (int64_t)s->y - auto_input.from_y;
			pointer_set(MOUSE_POS_AUTO,
				(int)(auto_input.from_x + dx * elapsed / s->ms),
				(int)(auto_input.from_y + dy * elapsed / s->ms));
		}
		return;
	}
	if (auto_input.next >= auto_input.nr_steps || now < auto_input.steps[auto_input.next].t)
		return;
	int i = auto_input.next++;
	struct auto_step *s = &auto_input.steps[i];
	// A new step: the pointer is where it says from now on.
	auto_input.guard = false;
	switch (s->kind) {
	case AUTO_CLICK:
		pointer_set(MOUSE_POS_AUTO, s->x, s->y);
		auto_input_button(s->button, true);
		auto_input.busy = true;
		auto_input.started = now;
		break;
	case AUTO_MOVE:
		if (s->ms) {
			mouse_peek_pos(&auto_input.from_x, &auto_input.from_y);
			auto_input.busy = true;
			auto_input.started = now;
		} else {
			pointer_set(MOUSE_POS_AUTO, s->x, s->y);
		}
		break;
	case AUTO_DOWN:
		pointer_set(MOUSE_POS_AUTO, s->x, s->y);
		auto_input_button(s->button, true);
		break;
	case AUTO_UP:
		pointer_set(MOUSE_POS_AUTO, s->x, s->y);
		auto_input_button(s->button, false);
		break;
	case AUTO_WHEEL:
		pointer_set(MOUSE_POS_AUTO, s->x, s->y);
		auto_input.notches_left = abs(s->notches);
		auto_input.notches_done = 0;
		auto_input.busy = true;
		break;
	case AUTO_KEY:
		// The key alone: the pointer stays where it is.
		key_state[s->key] = true;
		auto_input.busy = true;
		auto_input.started = now;
		break;
	}
	if (s->first_form) {
		WARNING("Auto-click seq #%d at %ums game=(%d,%d)", i, now, s->x, s->y);
	} else if (s->kind == AUTO_KEY) {
		WARNING("Auto-input #%d %s at %ums", i, auto_step_describe(s, d, sizeof(d)), now);
	} else if (s->kind == AUTO_MOVE && s->ms) {
		WARNING("Auto-input #%d %s at %ums from=(%d,%d) to=(%d,%d)", i, auto_step_describe(s, d, sizeof(d)),
				now, auto_input.from_x, auto_input.from_y, s->x, s->y);
	} else {
		WARNING("Auto-input #%d %s at %ums game=(%d,%d)", i, auto_step_describe(s, d, sizeof(d)),
				now, s->x, s->y);
	}
	if (s->kind == AUTO_WHEEL)
		auto_input_notch(s, i, now);
}

void input_auto_update(uint32_t now)
{
	if (auto_input.nr_steps > 0) {
		auto_input_sequence(now);
		return;
	}
	if (auto_input.interval <= 0)
		return;
	if (auto_input.down) {
		if (now - auto_input.last >= 100) {
			auto_input_button(VK_LBUTTON, false);
			auto_input.down = false;
		}
	} else if ((auto_input.max <= 0 || auto_input.count < auto_input.max)
			&& now - auto_input.last >= (uint32_t)auto_input.interval) {
		auto_input.count++;
		pointer_set(MOUSE_POS_AUTO, auto_input.x, auto_input.y);
		auto_input_button(VK_LBUTTON, true);
		auto_input.down = true;
		if (auto_input.count <= 10 || auto_input.count % 100 == 0)
			WARNING("Auto-click #%d DOWN at %ums game=(%d,%d)", auto_input.count, now,
					auto_input.x, auto_input.y);
		auto_input.last = now;
	}
}

void mouse_get_pos(int *x, int *y)
{
	if (pointer.source != MOUSE_POS_SDL) {
		*x = pointer.x;
		*y = pointer.y;
		return;
	}
	SDL_PumpEvents();
	sdl_mouse_pos(x, y);
}

void mouse_set_pos(int x, int y)
{
	auto_input_load_env();
	if (desktop_warp_off()) {
		pointer_set(MOUSE_POS_AUTO, x, y);
		return;
	}
	int wx = x * sdl.viewport.w / sdl.w + sdl.viewport.x;
	int wy = y * sdl.viewport.h / sdl.h + sdl.viewport.y;
	SDL_WarpMouseInWindow(sdl.window, wx, wy);
	// After the warp, so that the motion SDL sends for it is not later.
	pointer_set(MOUSE_POS_WARP, x, y);
}

static int wheel_dir = 0;

/*
 * v14's native wheel handler (0x4be830) accumulates signed delta in units
 * of 120, keeping its signed remainder and two independent notch counts.
 * The port can pump SDL midway through a script's input cycle, so its first
 * GetCount takes a snapshot until ClearCount. Events received after that
 * read stay pending for the next cycle. This snapshot is a port adaptation:
 * the native getters themselves simply read the current counts.
 */
static struct {
	double remainder;
	int pending_forward, pending_back;
	int forward, back;
	bool snapshot;
} v14_wheel;

static bool wheel_is_v14(void)
{
	return ain && ain->version >= 14;
}

static void wheel_add_notches(double notches)
{
	if (!isfinite(notches) || notches == 0)
		return;
	double delta = v14_wheel.remainder + notches * 120.0;
	double whole = trunc(delta / 120.0);
	v14_wheel.remainder = fmod(delta, 120.0);
	int *count = whole >= 0 ? &v14_wheel.pending_forward : &v14_wheel.pending_back;
	double added = fabs(whole);
	// An oversized SDL event must not overflow an integer or a float-to-int
	// conversion. Saturation is the port's boundary for such input.
	if (added >= INT_MAX - *count)
		*count = INT_MAX;
	else
		*count += (int)added;
}

static void wheel_event(const SDL_MouseWheelEvent *event)
{
	if (!wheel_is_v14()) {
		wheel_dir = event->y;
		return;
	}
	double notches = event->y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	// Older synthetic producers fill only y. A genuine horizontal-only
	// event has both y values zero and leaves all wheel state untouched.
	if (event->preciseY != 0 || event->y == 0)
		notches = event->preciseY;
#endif
	if (event->direction == SDL_MOUSEWHEEL_FLIPPED)
		notches = -notches;
	wheel_add_notches(notches);
}

void mouse_get_wheel(int *forward, int *back)
{
	if (wheel_is_v14()) {
		if (!v14_wheel.snapshot) {
			v14_wheel.forward = v14_wheel.pending_forward;
			v14_wheel.back = v14_wheel.pending_back;
			v14_wheel.pending_forward = v14_wheel.pending_back = 0;
			v14_wheel.snapshot = true;
		}
		*forward = v14_wheel.forward;
		*back = v14_wheel.back;
		return;
	}
	*forward = wheel_dir > 0;
	*back = wheel_dir < 0;
}

void mouse_clear_wheel(void)
{
	if (wheel_is_v14()) {
		v14_wheel.forward = v14_wheel.back = 0;
		v14_wheel.snapshot = false;
		return;
	}
	wheel_dir = 0;
}

bool mouse_show_cursor(bool show)
{
	return SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE) >= 0;
}

static void(*input_handler)(const char*);
static void(*editing_handler)(const char*, int, int);
static void(*key_handler)(int);

static void key_event(SDL_KeyboardEvent *e, bool pressed)
{
	if (e->keysym.scancode >= (sizeof(sdl_keytable)/sizeof(*sdl_keytable)))
		return;
	enum sact_keycode code = sdl_keytable[e->keysym.scancode];
	if (code) {
		key_state[code] = pressed;
		if (pressed && key_handler)
			key_handler(code);
	}
}

// Track mouse button hold: when DOWN arrives, hold for at least one
// handle_events cycle so AFL_IsKeyDown can see it.  Real SDL events
// often deliver DOWN+UP in the same event pump, making the click
// invisible to bytecode that checks key_state between pumps.
static uint32_t mouse_hold_until[8]; // per-button, SDL_GetTicks deadline
static bool mouse_release_pending[8]; // an UP received before the deadline

static void release_pending_mouse_buttons(uint32_t now)
{
	for (int btn = 1; btn <= 3; btn++) {
		if (mouse_release_pending[btn] && SDL_TICKS_PASSED(now, mouse_hold_until[btn])) {
			enum sact_keycode code = sdl_to_sact_button(btn);
			if (code) key_state[code] = false;
			mouse_release_pending[btn] = false;
			mouse_hold_until[btn] = 0;
		}
	}
}

/*
 * A modal box (vm_ask) had the keyboard and the mouse while it was open:
 * what was held when it opened may have come up inside it, and what was
 * pressed for it was not for the game. Nothing stays held and the events of
 * that time are dropped.
 *
 * This differs from the original, whose key query reads the system's key
 * state at the time of each call (IbisInputEngine.Key_IsDown, 0x498bf0, ends
 * in GetKeyState): there a key that is still down when the box closes is
 * still down for the script, and the wait the script runs after a box it
 * cancelled lasts until Return and Escape have really come up. Here both
 * read as up, so that wait passes at once, and a key that is still held
 * physically comes back as a new press with its next repeat event (a
 * modifier, which does not repeat, with its next press).
 */
void input_modal_closed(void)
{
	SDL_PumpEvents();
	SDL_FlushEvents(SDL_KEYDOWN, SDL_TEXTINPUT);
	SDL_FlushEvents(SDL_MOUSEBUTTONDOWN, SDL_MOUSEWHEEL);
	key_clear_flag(false);
	memset(mouse_hold_until, 0, sizeof(mouse_hold_until));
	memset(mouse_release_pending, 0, sizeof(mouse_release_pending));
	deferred_synthetic_mouse_event.timestamp = 0;
	long_touch_start_timestamp = 0;
	wheel_dir = 0;
	memset(&v14_wheel, 0, sizeof(v14_wheel));
}

static void mouse_event(SDL_MouseButtonEvent *e)
{
	static unsigned trace_count;
	if (input_stage2_trace() && trace_count++ < 40) {
		int ww, wh, dw, dh, mx, my;
		SDL_GetWindowSize(sdl.window, &ww, &wh);
		SDL_GL_GetDrawableSize(sdl.window, &dw, &dh);
		SDL_GetMouseState(&mx, &my);
		bool set = pointer.source != MOUSE_POS_SDL;
		WARNING("S2 mouse t=%u state=%d event=(%d,%d) polled=(%d,%d) override=(%d,%d) window=%dx%d drawable=%dx%d logical=%dx%d viewport=%d,%d,%d,%d pointer=%s",
			SDL_GetTicks(), e->state, e->x, e->y, mx, my,
			set ? pointer.x : -1, set ? pointer.y : -1, ww, wh, dw, dh, sdl.w, sdl.h,
			sdl.viewport.x, sdl.viewport.y, sdl.viewport.w, sdl.viewport.h,
			mouse_pos_source_name(pointer.source));
	}
	enum sact_keycode code = sdl_to_sact_button(e->button);
	if (code) {
		int button = e->button & 7;
		if (e->state == SDL_PRESSED) {
			key_state[code] = true;
			mouse_hold_until[button] = SDL_GetTicks() + 50;
			mouse_release_pending[button] = false;
		} else {
			if (!key_state[code] || SDL_TICKS_PASSED(SDL_GetTicks(), mouse_hold_until[button])) {
				key_state[code] = false;
				mouse_hold_until[button] = 0;
				mouse_release_pending[button] = false;
			} else {
				mouse_release_pending[button] = true;
			}
		}
	}
#ifdef DEBUGGER_ENABLED
	if (e->button == SDL_BUTTON_MIDDLE && e->state == SDL_PRESSED
			&& (dbg_start_in_debugger || dbg_dap))
		dbg_repl(DBG_STOP_PAUSE, NULL);
#endif
}

static void synthetic_mouse_event(SDL_MouseButtonEvent *e)
{
	if (e->state == SDL_PRESSED) {
		// Touch outside the viewport is treated as right-click.
		SDL_Point p = { .x = e->x, .y = e->y };
		if (SDL_PointInRect(&p, &sdl.viewport))
			key_state[VK_LBUTTON] = true;
		else
			key_state[VK_RBUTTON] = true;
	} else {
		key_state[VK_LBUTTON] = false;
		key_state[VK_RBUTTON] = false;
	}
}

#define JOYAXIS_DEADZONE 13500

enum joyaxis_axis {
	JOYAXIS_X,
	JOYAXIS_Y
};

enum joyaxis_direction {
	JOYAXIS_DIR_LEFT_UP    = -1,
	JOYAXIS_DIR_NONE       =  0,
	JOYAXIS_DIR_RIGHT_DOWN =  1
};

enum joyaxis_state {
	// A: Initial state
	// - when joy_clear_flag is called, goes on long cooldown
	//   and then enters JOYAXIS_STATE_B
	JOYAXIS_STATE_A,
	// B: Held state
	// - when joy_clear_flag is called, goes on short cooldown
	//   state remains JOYAXIS_STATE_B until direction changes
	JOYAXIS_STATE_B,
};

struct joyaxis {
	enum joyaxis_state state;
	unsigned long cooldown_expire_time;
	enum joyaxis_direction real_direction;
	enum joyaxis_direction logical_direction;
};

struct joyaxis joyaxis_state[2] = { 0 };

bool joybutton_state[SDL_CONTROLLER_BUTTON_MAX];

static unsigned long joy_time(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((unsigned long)ts.tv_sec * 1000) + ((unsigned long)ts.tv_nsec / 1000000);
}

static bool joy_check_axis(struct joyaxis *axis, enum joyaxis_direction direction)
{
	if (axis->logical_direction == direction)
		return true;
	if (!axis->cooldown_expire_time)
		return false;
	// on cooldown: check if cooldown expired
	if (joy_time() >= axis->cooldown_expire_time) {
		axis->state = JOYAXIS_STATE_B;
		axis->cooldown_expire_time = 0;
		axis->logical_direction = axis->real_direction;
	}
	return axis->logical_direction == direction;
}

bool joy_key_is_down(uint8_t code)
{
	// Default SACT key mapping in comments below
	switch (code) {
	case 1: // Left Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_X], JOYAXIS_DIR_LEFT_UP);
	case 2: // Right Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_X], JOYAXIS_DIR_RIGHT_DOWN);
	case 3: // Up Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_Y], JOYAXIS_DIR_LEFT_UP);
	case 4: // Down Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_Y], JOYAXIS_DIR_RIGHT_DOWN);
	case 5: // Esc
		return joybutton_state[SDL_CONTROLLER_BUTTON_A];
	case 6: // Enter
		return joybutton_state[SDL_CONTROLLER_BUTTON_B];
	case 7: // Space
		return joybutton_state[SDL_CONTROLLER_BUTTON_X];
	case 8: // Ctrl
		return joybutton_state[SDL_CONTROLLER_BUTTON_Y];
	case 9: // a
		return joybutton_state[SDL_CONTROLLER_BUTTON_LEFTSHOULDER];
	case 10: // z
		return joybutton_state[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER];
	case 11: // Page Up
		return joybutton_state[SDL_CONTROLLER_BUTTON_BACK];
	case 12: // Page Down
		return joybutton_state[SDL_CONTROLLER_BUTTON_START];
	case 13:
		return joybutton_state[SDL_CONTROLLER_BUTTON_GUIDE];
	case 14:
		return joybutton_state[SDL_CONTROLLER_BUTTON_LEFTSTICK];
	case 15:
		return joybutton_state[SDL_CONTROLLER_BUTTON_RIGHTSTICK];
	case 16:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_LEFT];
	case 17:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_RIGHT];
	case 18:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_UP];
	case 19:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_DOWN];
	default:
		return false;
	}
}

static float joy_calc_deg(int x, int y)
{
	// calculate angle (degrees)
	float a = atan2f(y, x) * (180.0f/M_PI);
	if (a < 0)
		a += 360.0f;
	// rotate so that up is 0/360
	a += 90.0f;
	if (a > 360.0f)
		a -= 360.0f;
	return a;
}

static float joy_calc_pow(int x, int y)
{
	// XXX: in theory this should be scaled according to the angle so that
	//      (32767,32767) returns exactly 1, but alicesoft themselves seem
	//      to simply clamp pythagoras as below
	return min(1.0f, sqrtf(x*x + y*y) / 32767.0f);
}

static float joy_calc_trigger(int left, int right)
{
	// for whatever reason alicesoft treats triggers as an analog stick
	// (at least on an xbox controller)
	float v = 315.0f;
	v += ((float)left/32767.0f) * 90.0f;
	v -= ((float)right/32767.0f) * 90.0f;
	if (v < 0)
		v += 360.0f;
	if (v > 360.0f)
		v -= 360.0f;
	return v;
}

bool joy_get_stick_status(int controller, int type, float *deg, float *pow)
{
	SDL_GameController *ctrl = controllers[controller];
	if (!ctrl)
		goto invalid;
	if (type == 0) {
		int x = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX);
		int y = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY);
		if (abs(x) < JOYAXIS_DEADZONE && abs(y) < JOYAXIS_DEADZONE) {
			*pow = 0.0f;
			*deg = 0.0f;
		} else {
			*pow = joy_calc_pow(x, y);
			*deg = joy_calc_deg(x, y);
		}
		return true;
	}
	if (type == 1) {
		int left = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
		int right = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
		*pow = 1.0f;
		*deg = joy_calc_trigger(left, right);
		return true;
	}
invalid:
	*deg = 0.0f;
	*pow = 0.0f;
	return false;
}

static void joyaxis_clear_flag(struct joyaxis *joy)
{
	// put axis on cooldown
	if (joy->logical_direction) {
		joy->logical_direction = 0;
		if (joy->state == JOYAXIS_STATE_A) {
			joy->cooldown_expire_time = joy_time() + 300;
		} else {
			joy->cooldown_expire_time = joy_time() + 50;
		}
	}
}

void joy_clear_flag(void)
{
	for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
		joybutton_state[i] = false;
	}
	joyaxis_clear_flag(&joyaxis_state[JOYAXIS_X]);
	joyaxis_clear_flag(&joyaxis_state[JOYAXIS_Y]);
}

static void joyaxis_update(enum joyaxis_axis axis, int value)
{
	// determine direction
	int direction = 0;
	if (value < -JOYAXIS_DEADZONE)
		direction = -1;
	else if (value > JOYAXIS_DEADZONE)
		direction = 1;

	// reset axis state if direction changed
	if (direction != joyaxis_state[axis].real_direction) {
		joyaxis_state[axis].state = JOYAXIS_STATE_A;
		joyaxis_state[axis].cooldown_expire_time = 0;
		joyaxis_state[axis].real_direction = direction;
		joyaxis_state[axis].logical_direction = direction;
	}
}

static void controller_axis_event(SDL_ControllerAxisEvent *e)
{
	switch (e->axis) {
	case SDL_CONTROLLER_AXIS_LEFTX:
		joyaxis_update(JOYAXIS_X, e->value);
		break;
	case SDL_CONTROLLER_AXIS_LEFTY:
		joyaxis_update(JOYAXIS_Y, e->value);
		break;
	default:
		break;
	}
}

static void controller_button_event(SDL_ControllerButtonEvent *e)
{
	joybutton_state[e->button] = e->state == SDL_PRESSED;
}

void register_input_handler(void(*handler)(const char*))
{
	input_handler = handler;
}

void clear_input_handler(void)
{
	input_handler = NULL;
}

void register_editing_handler(void(*handler)(const char*, int, int))
{
	editing_handler = handler;
}

void clear_editing_handler(void)
{
	editing_handler = NULL;
}

void register_key_handler(void(*handler)(int))
{
	key_handler = handler;
}

void clear_key_handler(void)
{
	key_handler = NULL;
}

static void fire_deferred_events(void)
{
	uint32_t now = SDL_GetTicks();

	// Flush the deferred mouse button event if it's older than 50ms.
	if (deferred_synthetic_mouse_event.timestamp &&
		deferred_synthetic_mouse_event.timestamp + SYNTHETIC_MOUSE_EVENT_DELAY < now) {
		synthetic_mouse_event(&deferred_synthetic_mouse_event);
		deferred_synthetic_mouse_event.timestamp = 0;
	}
	// Long touch emulates pressing the Ctrl key.
	if (long_touch_start_timestamp && long_touch_start_timestamp + LONG_TOUCH_DURATION < now) {
		key_state[VK_LBUTTON] = false;
		key_state[VK_CONTROL] = true;
	}

#ifdef __ANDROID__
	// Fire the deferred keyboard and text input events.
	while (!STAILQ_EMPTY(&deferred_keyevent_queue) && now >= last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
		struct deferred_keyevent *ev = STAILQ_FIRST(&deferred_keyevent_queue);
		switch (ev->e.type) {
		case SDL_TEXTINPUT:
			if (input_handler)
				input_handler(ev->e.text.text);
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			last_keyevent_timestamp = now;
			key_event(&ev->e.key, ev->e.type == SDL_KEYDOWN);
			break;
		}
		STAILQ_REMOVE_HEAD(&deferred_keyevent_queue, entry);
		free(ev);
	}
#endif
}

static void handle_window_event(SDL_Event *e)
{
	extern bool scene_is_dirty;
	switch (e->window.event) {
	case SDL_WINDOWEVENT_EXPOSED:
		// Don't call gfx_swap() directly — it blocks on macOS
		// Cocoa_GL_SwapWindow (pthread_cond_wait for vsync) if called
		// during global struct init (before the main event loop starts).
		// Mark dirty so the next regular render cycle handles it.
		scene_is_dirty = true;
		break;
	case SDL_WINDOWEVENT_ENTER:
		mouse_focus = true;
		break;
	case SDL_WINDOWEVENT_LEAVE:
		mouse_focus = false;
		break;
	case SDL_WINDOWEVENT_FOCUS_GAINED:
		keyboard_focus = true;
		// the v14 sound groups mute an inactive window (SetGlobalFocus)
		mixer_set_window_focus(true);
		break;
	case SDL_WINDOWEVENT_FOCUS_LOST:
		keyboard_focus = false;
		mixer_set_window_focus(false);
		break;
	case SDL_WINDOWEVENT_SIZE_CHANGED:
		gfx_update_screen_scale();
		scene_is_dirty = true;
		break;
	}
}

// Whether the user is asked before the game is closed from outside the
// script. Only the v14 window settings turn this on (SystemService).
static bool close_confirm;

void input_set_close_confirm(bool on)
{
#ifdef __ANDROID__
	// Never on Android, where SDL_QUIT ends the program as before: SDL
	// sends it when the activity is destroyed and then waits on the UI
	// thread for this thread to end, while a message box is shown by the
	// UI thread and waited for here. (SDLActivity.java of SDL 2.32,
	// onDestroy and messageboxShowMessageBox; not tried.)
	(void)on;
#else
	close_confirm = on;
#endif
}

/*
 * SDL turns SIGINT and SIGTERM into the SDL_QUIT that the window's close
 * button gives. They are noted here, on top of SDL's own handlers, so that
 * only the user's requests are confirmed.
 */
static volatile sig_atomic_t quit_signal;

#ifndef _WIN32
static void (*sdl_signal_handler[2])(int);

static void quit_signal_handler(int sig)
{
	quit_signal = 1;
	void (*next)(int) = sdl_signal_handler[sig == SIGTERM];
	if (next)
		next(sig);
}

// True once a handler is watched; until then there was none to put this on.
static bool watch_quit_signals(void)
{
	static const int signals[2] = { SIGINT, SIGTERM };
	bool watched = false;
	for (int i = 0; i < 2; i++) {
		struct sigaction action;
		if (sigaction(signals[i], NULL, &action) < 0 || (action.sa_flags & SA_SIGINFO))
			continue;
		if (action.sa_handler == SIG_DFL || action.sa_handler == SIG_IGN)
			continue;
		if (action.sa_handler != quit_signal_handler) {
			sdl_signal_handler[i] = action.sa_handler;
			action.sa_handler = quit_signal_handler;
			if (sigaction(signals[i], &action, NULL) < 0)
				continue;
		}
		watched = true;
	}
	return watched;
}
#else
// Signals are not told apart on Windows: an SDL_QUIT that SDL makes of a
// Ctrl+C on the console there would be confirmed like the close button
// (not built, not tried).
static bool watch_quit_signals(void) { return true; }
#endif

/*
 * SDL_QUIT: the window's close button, the application's Quit, or a signal.
 *
 * The original's window procedure asks before it closes while the window
 * setting "confirm closing the game" is on: 0x4be920 (WM_CLOSE) reads
 * [wnd+0x86], which SystemService.SetWindowSetting(4) writes (0x4c10c0),
 * and shows a yes/no box; the answer no leaves the game running. The script
 * has no part in it unless it hooked the closing with
 * SystemService.SetHookCloseApp. The script turns the hook on where it
 * registers a closing handler and keeps it on only while one is registered,
 * and a single function of it registers one (not implemented: the hook is
 * never on here).
 *
 * A signal is not the user closing the window: the program ends at once,
 * also when it arrives while the box is open and the answer is no.
 *
 * The read-message table is written before the box opens. Otherwise it is
 * written when the program ends (MsgSkip's _ModuleFini), which a program
 * that is killed while it waits for the answer does not reach.
 */
static void close_requested(void)
{
	if (close_confirm && !quit_signal) {
		msgskip_flush();
		// A box that cannot be shown must not keep the window from
		// closing (the original would not close then).
		if (!vm_ask(VM_ASK_YES_NO, NULL, true) && !quit_signal) {
			// One answer is for every request made so far: the
			// SDL_QUITs still queued do not ask again.
			SDL_FlushEvent(SDL_QUIT);
			return;
		}
	}
	vm_exit(0);
}

void handle_window_events(void)
{
	// NOTE: This function only processes QUIT and WINDOWEVENT.
	// Using SDL_PeepEvents to avoid consuming other event types
	// (mouse, keyboard, etc.) which need to be processed by handle_events().
	SDL_PumpEvents();
	SDL_Event events[32];
	int n;
	// Process QUIT events
	n = SDL_PeepEvents(events, 32, SDL_GETEVENT, SDL_QUIT, SDL_QUIT);
	if (n > 0)
		close_requested();
	// Process WINDOWEVENT
	n = SDL_PeepEvents(events, 32, SDL_GETEVENT, SDL_WINDOWEVENT, SDL_WINDOWEVENT);
	for (int i = 0; i < n; i++) {
		handle_window_event(&events[i]);
	}
}

void handle_events(void)
{
	// Throttle: SDL_PollEvent involves system calls; avoid pumping more
	// than once per millisecond when many HLL functions call handle_events.
	static uint32_t last_pump_ms = 0;
	uint32_t now_ms = SDL_GetTicks();
	if (now_ms == last_pump_ms)
		return;
	last_pump_ms = now_ms;

	fire_deferred_events();

	/* XSYS4_HOLD_KEYS: comma-separated list of key codes to hold down always.
	 * E.g. XSYS4_HOLD_KEYS=17 holds Ctrl, triggering full-text skip in ADV scenes. */
	{
		static int hold_keys[32];
		static int hold_keys_len = -1;
		if (hold_keys_len < 0) {
			hold_keys_len = 0;
			const char *env = getenv("XSYS4_HOLD_KEYS");
			if (env && *env) {
				char buf[256];
				strncpy(buf, env, sizeof(buf)-1);
				buf[sizeof(buf)-1] = 0;
				char *saveptr, *tok = strtok_r(buf, ",", &saveptr);
				while (tok && hold_keys_len < 32) {
					hold_keys[hold_keys_len++] = atoi(tok);
					tok = strtok_r(NULL, ",", &saveptr);
				}
			}
		}
		for (int i = 0; i < hold_keys_len; i++)
			key_state[hold_keys[i]] = true;
	}

	// The scheduled input (see auto_input above).
	auto_input_load_env();
	input_auto_update(SDL_GetTicks());

	// Delay an early UP long enough for polling clients to observe the press.
	// A button still physically held must remain down until its actual UP.
	release_pending_mouse_buttons(SDL_GetTicks());

	// Tried again until SDL's handlers are there to be watched.
	static bool quit_signals_watched;
	if (close_confirm && !quit_signals_watched)
		quit_signals_watched = watch_quit_signals();

	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			close_requested();
			break;
		case SDL_WINDOWEVENT:
			handle_window_event(&e);
			break;
		case SDL_APP_DIDENTERFOREGROUND:
			gfx_swap();
			break;
		case SDL_KEYDOWN:
			if (e.key.keysym.scancode == SDL_SCANCODE_F9) {
				vm_stack_trace();
			} else if (e.key.keysym.scancode == SDL_SCANCODE_F11) {
				gfx_toggle_fullscreen();
			} else if (e.key.keysym.scancode == SDL_SCANCODE_S) {
				if (e.key.keysym.mod & (KMOD_LALT | KMOD_RALT)) {
					screenshot_save();
				}
			}
			// fallthrough
		case SDL_KEYUP:
#ifdef __ANDROID__
			if (input_handler && e.key.timestamp < last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
				// Input from virtual keyboard is sent as consecutive
				// SDL_KEYDOWN and SDL_KEYUP events. To give the game a chance
				// to see the previous key event, delay the event.
				struct deferred_keyevent *ev = xmalloc(sizeof(struct deferred_keyevent));
				ev->e = e;
				STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
			} else {
				last_keyevent_timestamp = e.key.timestamp;
				key_event(&e.key, e.type == SDL_KEYDOWN);
			}
#else
			key_event(&e.key, e.type == SDL_KEYDOWN);
#endif
			break;
		case SDL_MOUSEMOTION:
			pointer_real_event(e.motion.timestamp, true);
			break;
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEBUTTONDOWN:
			pointer_real_event(e.button.timestamp, false);
			if (e.button.which == SDL_TOUCH_MOUSEID) {
				if (deferred_synthetic_mouse_event.timestamp)
					synthetic_mouse_event(&deferred_synthetic_mouse_event);
				deferred_synthetic_mouse_event = e.button;
			} else {
				mouse_event(&e.button);
			}
			break;
		case SDL_MOUSEWHEEL:
			if (e.wheel.which != AUTO_INPUT_MOUSEID)
				pointer_real_event(e.wheel.timestamp, false);
			wheel_event(&e.wheel);
			// Trace (XSYS4_STAGE2_TRACE): every wheel event the engine
			// takes, the schedule's (auto) or the mouse's (real).
			{
				static unsigned traced;
				if (input_stage2_trace() && traced++ < 2000)
					NOTICE("S2 wheel ms=%u y=%d (%s)", SDL_GetTicks(), e.wheel.y,
							e.wheel.which == AUTO_INPUT_MOUSEID ? "auto" : "real");
			}
			break;
		case SDL_FINGERDOWN:
			if (SDL_GetNumTouchFingers(e.tfinger.touchId) >= 2) {
				// The user is about to start a multi-touch gesture.
				key_state[VK_LBUTTON] = false;
				key_state[VK_RBUTTON] = false;
				deferred_synthetic_mouse_event.timestamp = 0;
				long_touch_start_timestamp = 0;
				scroll_gesture_y = -1.0f;
				break;
			}
			long_touch_start_timestamp = e.tfinger.timestamp;
			// Movement within this rect (1% of the screen size from the touch
			// start position) will be ignored.
			long_touch_finger_rect = (SDL_FRect) {
				.x = e.tfinger.x - 0.01,
				.y = e.tfinger.y - 0.01,
				.w = 0.02,
				.h = 0.02
			};
			break;
		case SDL_FINGERMOTION:
			if (SDL_PointInFRect(&(SDL_FPoint){ e.tfinger.x, e.tfinger.y }, &long_touch_finger_rect))
				break;
			// Cancel the timer only if Ctrl emulation has not already started.
			if (!key_state[VK_CONTROL])
				long_touch_start_timestamp = 0;
			break;
		case SDL_FINGERUP:
			long_touch_start_timestamp = 0;
			key_state[VK_CONTROL] = false;
			break;
		case SDL_MULTIGESTURE:
			if (e.mgesture.numFingers == 2) {
				if (scroll_gesture_y < 0.0f)
					scroll_gesture_y = e.mgesture.y;
				float dy = scroll_gesture_y - e.mgesture.y;
				if (dy * dy > SCROLL_GESTURE_SENSITIVITY * SCROLL_GESTURE_SENSITIVITY) {
					// Swipe up to scroll down, retaining the old
					// gesture threshold and one notch per crossing.
					if (wheel_is_v14())
						wheel_add_notches(dy < 0 ? 1 : -1);
					else
						wheel_dir = dy < 0 ? 1 : -1;
					scroll_gesture_y = e.mgesture.y;
				}
			}
		case SDL_CONTROLLERDEVICEADDED:
			if (e.cdevice.which < MAX_CONTROLLERS)
				controllers[e.cdevice.which] = SDL_GameControllerOpen(e.cdevice.which);
			break;
		case SDL_CONTROLLERAXISMOTION:
			controller_axis_event(&e.caxis);
			break;
		case SDL_CONTROLLERBUTTONUP:
		case SDL_CONTROLLERBUTTONDOWN:
			controller_button_event(&e.cbutton);
			break;
		case SDL_TEXTINPUT:
			if (!input_handler) {
				break;
#ifdef __ANDROID__
			} else if (!STAILQ_EMPTY(&deferred_keyevent_queue)) {
				// The order of key events and text events must be preserved.
				struct deferred_keyevent *ev = xmalloc(sizeof(struct deferred_keyevent));
				ev->e = e;
				STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
#endif
			} else {
				input_handler(e.text.text);
			}
			break;
		case SDL_TEXTEDITING:
			if (editing_handler)
				editing_handler(e.edit.text, e.edit.start, e.edit.length);
			break;
		default:
			break;
		}
	}
	if (dbg_dap)
		dbg_dap_handle_messages();
}

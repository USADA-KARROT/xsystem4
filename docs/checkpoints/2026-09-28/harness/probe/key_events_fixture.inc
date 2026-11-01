/* Synthetic input contracts from native 0x547c90, 0x574d50,
 * 0x578580 and 0x5788d0. No assets or script text.
 * This mode reads the unfiltered queue. Other event families are covered
 * by hover-events, wheel-events and layer-input.
 * Not covered (no synthetic case here; the expectations would rest on the
 * disassembly alone, or need parts or counters this mode does not build):
 *  - a cancelled source's own pressed mark (+4) after EndInput; the port
 *    keeps one pressed parts, cleared at every scope end. Inferred from
 *    the disassembly (0x577b90, 0x578650, 0x5462b0, 0x5461a0), with no
 *    native screen: +4 is set only by a left Trigger (0x578650..0x578675),
 *    neither the left cancel (0x5462b0) nor End (0x5461a0, slots only)
 *    clears it, and every walk over the entered element shows the pressed
 *    look from +4 (0x577c28..0x577c38). So after End and Begin such an
 *    element natively looks pressed as long as it stays entered, until it
 *    is the focus again and its left Up is handled (0x5788d0), while the
 *    port shows it hovered right after Begin. No case guards that look,
 *    nor its state when another button is pressed on it with the left one
 *    held over End and Begin (natively pressed there too); only a new
 *    left press on it is guarded, where both show it pressed;
 *  - the old focus' full release at a focus change (0x578810 -> 0x5788d0)
 *    while that element is still entered with a mouse button in its
 *    table, which needs a button held since Begin and a passing clickable
 *    parts in front of the old focus;
 *  - the left source's leave without a position when the focus is a
 *    scroll bar's track (0x548311..0x548339 -> 0x5476b0 -> 0x577cf0,
 *    which clears the inner element's +0x1ac when its +0x1e3 is 0): a
 *    later left release while another button's cancellation suppresses
 *    the walk must then not click the track; it needs a passing knob, a
 *    no-position update and that suppressed release;
 *  - the port's disabled-button policy for the right and middle local
 *    Click (the native input update has no counterpart; the left one is
 *    an input-model case);
 *  - a release reads the scroll bar inner element's cached entered flag
 *    (0x5788d0 reads +0x1ac, no new hit test); the port has no branch that
 *    could recompute it, and key-scrollbar covers "must be entered";
 *  - the old focus' scroll bar role, kept per parts, when that focus is
 *    flushed with a mouse button still held (a button held since Begin
 *    and another press changing the focus);
 *  - right and middle releases play no click sound (0x5789e8..0x578a34
 *    sends only the Click; the left one plays it at 0x5789a0..0x5789bc);
 *    this mode has no sound counter;
 *  - that the SDL activation clear (port policy for 0x4bdaf0) happens
 *    only when the window is active after the event: a clear on other
 *    window events while inactive would differ only for a script reading
 *    the keyboard state itself while inactive (the key tables are not
 *    sampled then and are cleared again on activation);
 *  - the key table of the manager's root element when no element of the
 *    update has the focus: 0x545c70 returns the root when it finds no
 *    focused element in the list, and 0x54836a runs 0x578580 on it, so
 *    natively the root presumably gets its local key events (and, if its
 *    +0x1ac allows, a MouseClick), where the port runs no local key table
 *    then (only the whole-screen one). Inferred from the disassembly, with
 *    no native screen; whether the script reads the root's messages was
 *    not followed;
 *  - the part of the original's manager per controller the port does not
 *    follow (KE81-KE123 follow its slots, its left press mark and its
 *    focus): a button held over a change of the active layer
 *    (SetActiveLayer, or the removal of the designated layer) is no new
 *    press for the manager of the layer it lands on (0x57ba47, 0x53d500,
 *    0x547f2d): no whole-screen Trigger, no slot and no new choice of the
 *    focus; only a focus that layer kept, whose own table had the button
 *    up, sees it go down (KE115-KE119: the native press's messages on the
 *    same element when the cursor is on that focus; with no element of
 *    that layer under the cursor, as in KE116 and KE119, the native press
 *    presumably finds nothing, sends the focus LostFocus and leaves
 *    GetActiveParts 0, where the port keeps the focus, with its Trigger,
 *    Down and a KeyUp without Click; with another element of that layer
 *    under the cursor that one presumably takes the focus and the click,
 *    where the port sends it its MouseEnter alone); at a switch back
 *    only the cancellation of a kept slot uses what the old manager
 *    sampled (a button that manager sampled down, released and pressed
 *    again at the very update of the switch back, is a press here, KE112,
 *    natively presumably none); the old manager, acting
 *    again after a release elsewhere, sends no second whole-screen KeyUp
 *    and Click; a BeginInput or an EndInput releases the one whole-screen
 *    key table, not one per manager (0x5461a0 -> 0x547ad0); the
 *    cancellation suppression (+0x1fc) is not kept per controller; and a
 *    focus moved under another controller's parts (SetParentPartsNumber)
 *    before a switch gets its LostFocus at the next press in its old
 *    controller, natively presumably at the next BeginInput (0x545c70
 *    finds it in no list); pressed in its new controller it gets Focus
 *    again and the next BeginInput sends it LostFocus once for each
 *    manager that has it, natively presumably no Focus and one LostFocus
 *    (+0x1b4 is the element's). Inferred from the disassembly, with no
 *    native screen; where the cases meet these frames their expectations
 *    are the port's. Only KE122 moves a focus to another controller, with
 *    no press after the move: it reads GetActiveParts in the old
 *    controller and the next BeginInput's one LostFocus;
 *  - a button held over the switch back with another element of the
 *    returning layer under the cursor, or with the cursor leaving the
 *    kept focus at the update of the switch back or after it (KE116 and
 *    KE119 leave it two updates before, while the other layer acts);
 *  - a table entry of a removed layer, empty but for its sampled buttons,
 *    taken up by a later layer under the same controller number (harmless
 *    with its slots empty, as read in the code, but for a moved focus). */
#include <errno.h>

static int ke_failures;
#define KE_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); puts(""); ke_failures++; } } while (0)
enum { KE_A = 20301, KE_B = 20302, KE_CAP = 512, KE_VARS = 5 };
struct ke_msg { int type, no, delegate, unique, n, v[KE_VARS]; };
struct ke_frame { int n; struct ke_msg m[KE_CAP]; };

static struct ke_frame ke_read(void)
{
	int (*delegate)(void) = (int (*)(void))br_pe_fn("GetMessageDelegateIndex");
	int (*count)(void) = (int (*)(void))br_pe_fn("GetMessageVariableCount");
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	struct ke_frame f = { 0 };
	for (int t; (t = br_msg_type_raw()) >= 0; br_msg_pop()) {
		int nr_vars = count();
		KE_CHECK(nr_vars >= 0 && nr_vars <= KE_VARS, "type %d payload size %d exceeds collector", t, nr_vars);
		if (f.n < KE_CAP) {
			struct ke_msg *m = &f.m[f.n];
			*m = (struct ke_msg){ t, br_msg_parts(), delegate(), he_unique(), nr_vars, { 0 } };
			for (int j = 0; j < nr_vars && j < KE_VARS; j++)
				m->v[j] = var(j);
		}
		f.n++;
	}
	KE_CHECK(f.n <= KE_CAP, "queue overflow: %d", f.n);
	return f;
}
static struct ke_frame ke_frame(int x, int y, int ms)
{
	mouse_set_pos(x, y);
	PE_UpdateComponent(0);
	PE_UpdateInputState(ms);
	return ke_read();
}
static void ke_key(int k, bool down) { extern bool key_state[]; key_state[k] = down; }
static int ke_count(const struct ke_frame *f, int type, int no, int key)
{
	int n = 0;
	for (int i = 0; i < f->n && i < KE_CAP; i++) {
		const struct ke_msg *m = &f->m[i];
		if (m->type != type || m->no != no)
			continue;
		int pos = type == 4 ? 2 : 0;
		if (key >= 0 && (m->n <= pos || m->v[pos] != key))
			continue;
		n++;
		KE_CHECK(m->n == (type == 4 ? 3 : 1), "type %d variable count %d", type, m->n);
		KE_CHECK(m->delegate == he_delegate_of(no) && m->unique == he_unique_of(no), "type %d wrong receiver metadata for %d", type, no);
	}
	return n;
}
enum {
	KE_CLICK = 1 << 4, KE_TRIGGER = 1 << 15, KE_DOWN = 1 << 16,
	KE_PRESS = 1 << 17, KE_UP = 1 << 18,
	KE_START = KE_TRIGGER | KE_DOWN | KE_PRESS,
};
struct ke_expected { int key, whole, local; };
/* Compare the entire key/click multiset. Hover and other event families
 * remain in the raw frame for their own oracles. */
static void ke_events_are(const struct ke_frame *f, const struct ke_expected *want, int n, int line)
{
	int expected = 0, actual = 0;
	for (int i = 0; i < n; i++) {
		for (int local = 0; local <= 1; local++) {
			int mask = local ? want[i].local : want[i].whole;
			int target = local ? KE_A : 0;
			for (int type = 4; type <= 18; type++) {
				if (!(mask & (1 << type))) continue;
				expected++;
				KE_CHECK(ke_count(f, type, target, want[i].key) == 1,
					"line %d: expected one type %d target %d key %d", line, type, target, want[i].key);
			}
		}
	}
	for (int i = 0; i < f->n && i < KE_CAP; i++)
		actual += f->m[i].type == 4 || (f->m[i].type >= 15 && f->m[i].type <= 18);
	KE_CHECK(actual == expected, "line %d: key/click event count %d, want %d", line, actual, expected);
}
#define KE_EXPECT(f, ...) do { \
	const struct ke_expected want[] = { __VA_ARGS__ }; \
	ke_events_are((f), want, sizeof(want) / sizeof(*want), __LINE__); \
} while (0)
static void ke_setup(void)
{
	he_env();
	keyboard_focus = true;
	key_clear_flag(false);
	br_input_setup();
	br_box(KE_A, 100, 100, 200, 80, true);
	br_box(KE_B, 400, 100, 200, 80, true);
	cp_register_event(KE_A, 3, 30);
	cp_register_event(KE_B, 7, 70);
	PE_BeginInput();
	ke_frame(640, 600, 0);
	ke_frame(640, 600, 0);
}
static void ke_focus_a(void)
{
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, KE_START});
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
}
static void ke_mouse(int key)
{
	ke_setup();
	ke_key(key, true);
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "button %d did not focus target", key);
	for (int no = 0; no <= 1; no++) {
		int target = no ? KE_A : 0;
		KE_CHECK(ke_count(&f, 15, target, key) == 1, "button %d trigger target %d", key, target);
		KE_CHECK(ke_count(&f, 16, target, key) == 1, "button %d down target %d", key, target);
		KE_CHECK(ke_count(&f, 17, target, key) == 1, "button %d first press target %d", key, target);
		KE_CHECK(ke_count(&f, 4, target, key) == 0, "button %d clicked on press", key);
	}
	KE_EXPECT(&f, {key, KE_START, KE_START});
	f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 15, KE_A, key) == 0 && ke_count(&f, 16, KE_A, key) == 1, "held button %d trigger/down", key);
	KE_EXPECT(&f, {key, KE_DOWN, KE_DOWN});
	ke_key(key, false);
	f = ke_frame(150, 140, 16);
	for (int no = 0; no <= 1; no++) {
		int target = no ? KE_A : 0;
		KE_CHECK(ke_count(&f, 18, target, key) == 1, "button %d release target %d", key, target);
		KE_CHECK(ke_count(&f, 4, target, key) == 1, "button %d click target %d", key, target);
	}
	KE_EXPECT(&f, {key, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
	KE_CHECK(he_hll_int("GetClickNumber") == (key == VK_LBUTTON ? KE_A : 0), "button %d changed click-number incorrectly", key);
	f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 4, KE_A, key) && !ke_count(&f, 18, KE_A, key), "duplicate release for %d", key);
	KE_EXPECT(&f, {key, 0, 0});
}
static void ke_left(void) { ke_mouse(VK_LBUTTON); }
static void ke_right(void) { ke_mouse(VK_RBUTTON); }
static void ke_middle(void) { ke_mouse(VK_MBUTTON); }
static void ke_keyboard(void)
{
	ke_setup();
	ke_focus_a();
	ke_key('A', true);
	struct ke_frame f = ke_frame(150, 140, 16);
	for (int no = 0; no <= 1; no++) {
		int target = no ? KE_A : 0;
		KE_CHECK(ke_count(&f, 15, target, 'A') == 1 && ke_count(&f, 16, target, 'A') == 1, "keyboard trigger/down target %d", target);
	}
	KE_EXPECT(&f, {'A', KE_START, KE_START});
	ke_key('A', false); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 18, 0, 'A') == 1 && ke_count(&f, 18, KE_A, 'A') == 1, "keyboard release");
	KE_CHECK(!ke_count(&f, 4, 0, -1) && !ke_count(&f, 4, KE_A, -1) && he_hll_int("GetClickNumber") == 0, "keyboard became mouse click");
	KE_EXPECT(&f, {'A', KE_UP, KE_UP});
}
static void ke_blocker(void)
{
	ke_setup(); parts_get(KE_A)->clickable = false;
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "blocker not focused");
	ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 4, 0, VK_RBUTTON) == 1 && !ke_count(&f, 4, KE_A, VK_RBUTTON), "blocker click permission");
	KE_CHECK(ke_count(&f, 18, KE_A, VK_RBUTTON) == 1, "blocker lost key release");
	KE_EXPECT(&f, {VK_RBUTTON, KE_UP | KE_CLICK, KE_UP});
}
static void ke_blank(void)
{
	ke_setup(); ke_key(VK_MBUTTON, true); struct ke_frame f = ke_frame(640, 600, 16);
	KE_EXPECT(&f, {VK_MBUTTON, KE_START, 0});
	ke_key(VK_MBUTTON, false); f = ke_frame(640, 600, 16);
	KE_CHECK(ke_count(&f, 4, 0, VK_MBUTTON) == 1 && !ke_count(&f, 4, KE_A, -1) && !ke_count(&f, 4, KE_B, -1), "blank release recipients");
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "blank has focus");
	KE_EXPECT(&f, {VK_MBUTTON, KE_UP | KE_CLICK, 0});
}
static void ke_outside(void)
{
	ke_setup(); ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	mouse_focus = false; ke_key(VK_RBUTTON, false);
	f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 4, 0, -1) && !ke_count(&f, 4, KE_A, -1), "outside release became click");
	KE_CHECK(ke_count(&f, 18, 0, VK_RBUTTON) == 1 && ke_count(&f, 18, KE_A, VK_RBUTTON) == 1, "outside lost key release");
	KE_EXPECT(&f, {VK_RBUTTON, KE_UP, KE_UP});
}
static void ke_legacy(void)
{
	ke_setup(); ain->version = 13;
	/* ke_frame reads only the v14 queue; legacy messages stay separate. */
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(f.n == 0, "v13 press gained v14 messages: %d", f.n);
	f = ke_frame(150, 140, 16);
	KE_CHECK(f.n == 0, "v13 hold gained v14 messages: %d", f.n);
	ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(f.n == 0, "v13 release gained v14 messages: %d", f.n);
}
/* The literals below come from 0x575060, not the production key table. */
static const int ke_keys[] = {
  0x01,0x02,0x04,0x08,0x09,0x0d,0x10,0x11,0x1b,0x20,
  0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x2d,0x2e,
  0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,
  0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,
  0x4b,0x4c,0x4d,0x4e,0x4f,0x50,0x51,0x52,0x53,0x54,
  0x55,0x56,0x57,0x58,0x59,0x5a,0x60,0x61,0x62,0x63,
  0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6d,0x6e,
  0x6f,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78
};
static void ke_repeat(void)
{
	ke_setup(); ke_focus_a(); ke_key('A', true);
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_START, KE_START});
	f = ke_frame(150, 140, 500);
	KE_EXPECT(&f, {'A', KE_DOWN, KE_DOWN});
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_DOWN | KE_PRESS, KE_DOWN | KE_PRESS});
	f = ke_frame(150, 140, 80);
	KE_EXPECT(&f, {'A', KE_DOWN, KE_DOWN});
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_DOWN | KE_PRESS, KE_DOWN | KE_PRESS});
	f = ke_frame(150, 140, 1000);
	KE_EXPECT(&f, {'A', KE_DOWN | KE_PRESS, KE_DOWN | KE_PRESS});
	/* No carry of the 920 ms remainder. */
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_DOWN, KE_DOWN});
}
static void ke_shared_repeat(void)
{
	ke_setup(); ke_focus_a(); ke_key('A', true);
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_START, KE_START});
	f = ke_frame(150, 140, 400);
	KE_EXPECT(&f, {'A', KE_DOWN, KE_DOWN});
	ke_key('B', true); f = ke_frame(150, 140, 100);
	KE_EXPECT(&f, {'A', KE_DOWN, KE_DOWN}, {'B', KE_START, KE_START});
	f = ke_frame(150, 140, 400);
	KE_EXPECT(&f, {'A', KE_DOWN, KE_DOWN}, {'B', KE_DOWN, KE_DOWN});
	f = ke_frame(150, 140, 116);
	KE_EXPECT(&f, {'A', KE_DOWN | KE_PRESS, KE_DOWN | KE_PRESS}, {'B', KE_DOWN | KE_PRESS, KE_DOWN | KE_PRESS});
	ke_key('B', false); f = ke_frame(150, 140, 80);
	KE_EXPECT(&f, {'A', KE_DOWN, KE_DOWN}, {'B', KE_UP, KE_UP});
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_DOWN | KE_PRESS, KE_DOWN | KE_PRESS});
	/* A new trigger at an overdue instant preserves that instant's repeat,
	 * then restarts the shared initial delay. */
	ke_key('B', true); f = ke_frame(150, 140, 96);
	KE_EXPECT(&f, {'A', KE_DOWN | KE_PRESS, KE_DOWN | KE_PRESS}, {'B', KE_START, KE_START});
	f = ke_frame(150, 140, 96);
	KE_EXPECT(&f, {'A', KE_DOWN, KE_DOWN}, {'B', KE_DOWN, KE_DOWN});
}
static void ke_order(void)
{
	ke_setup(); ke_focus_a();
	ke_key('B', true); ke_key('A', true); ke_key(VK_CONTROL, true);
	struct ke_frame f = ke_frame(150, 140, 16);
	int keys[] = {VK_CONTROL, 'A', 'B'}, index = 0;
	for (int i = 0; i < f.n && i < KE_CAP; i++) {
		struct ke_msg *m = &f.m[i];
		if (m->type < 15 || m->type > 18) continue;
		KE_CHECK(index < 18, "extra ordered event %d", index);
		if (index < 18) {
			int within = index % 9, no = index < 9 ? 0 : KE_A;
			KE_CHECK(m->type == 15 + within / 3 && m->no == no && m->n == 1 && m->v[0] == keys[within % 3], "batch order mismatch at %d", index);
		}
		index++;
	}
	KE_CHECK(index == 18, "ordered event count %d", index);
}
/* 0x574d50 builds four vectors before 0x578580 dispatches them.
 * Mix an overdue held key, a release and a new key in one sample. */
static void ke_mixed_order(void)
{
	ke_setup(); ke_focus_a(); ke_key('B', true); ke_key('A', true);
	ke_frame(150, 140, 16); ke_frame(150, 140, 400);
	ke_key('B', false); ke_key('C', true);
	struct ke_frame f = ke_frame(150, 140, 116);
	KE_EXPECT(&f, {'A', KE_DOWN | KE_PRESS, KE_DOWN | KE_PRESS},
		{'B', KE_UP, KE_UP}, {'C', KE_START, KE_START});
	const int types[] = {15, 16, 16, 17, 17, 18};
	const int keys[] = {'C', 'A', 'C', 'A', 'C', 'B'};
	int index = 0;
	for (int i = 0; i < f.n && i < KE_CAP; i++) {
		const struct ke_msg *m = &f.m[i];
		if (m->type < 15 || m->type > 18) continue;
		KE_CHECK(index < 12, "extra mixed-batch event");
		if (index < 12) {
			int at = index % 6, no = index < 6 ? 0 : KE_A;
			KE_CHECK(m->type == types[at] && m->no == no
				&& m->n == 1 && m->v[0] == keys[at],
				"mixed batch order mismatch at %d", index);
		}
		index++;
	}
	KE_CHECK(index == 12, "mixed-batch count %d", index);
}
static void ke_table(void)
{
	ke_setup();
	for (int i = 79; i >= 0; i--) ke_key(ke_keys[i], true);
	int excluded[] = {0x03,0x12,0x6c,0x79,0x87};
	for (int i = 0; i < 5; i++) ke_key(excluded[i], true);
	struct ke_frame f = ke_frame(150, 140, 16);
	int index = 0;
	for (int i = 0; i < f.n && i < KE_CAP; i++) {
		struct ke_msg *m = &f.m[i];
		if (m->type < 15 || m->type > 18) continue;
		if (index < 480) {
			int n = index % 240, no = index < 240 ? 0 : KE_A;
			KE_CHECK(m->type == 15 + n / 80 && m->no == no && m->n == 1 && m->v[0] == ke_keys[n % 80], "80-key order mismatch at %d", index);
		}
		index++;
	}
	KE_CHECK(index == 480, "80-key event count %d", index);
}
static void ke_focus_switch(void)
{
	ke_setup(); ke_focus_a(); ke_key('A', true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(450, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B, "new focus not selected");
	KE_CHECK(ke_count(&f, 15, KE_B, 'A') == 1 && ke_count(&f, 18, KE_A, 'A') == 1, "new sample / old flush");
	int last_new = -1, first_old = KE_CAP;
	for (int i = 0; i < f.n && i < KE_CAP; i++) {
		if (f.m[i].no == KE_B && f.m[i].type >= 15 && f.m[i].type <= 18) last_new = i;
		if (f.m[i].no == KE_A && f.m[i].type == 18 && first_old == KE_CAP) first_old = i;
	}
	KE_CHECK(last_new >= 0 && last_new < first_old && first_old < KE_CAP, "new-before-old ordering");
	f = ke_frame(450, 140, 16); KE_CHECK(!ke_count(&f, 18, KE_A, 'A'), "old table flushed twice");
	ke_key(VK_RBUTTON, false); ke_frame(450, 140, 16);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 15, KE_A, 'A') == 1, "returning focus kept stale held bit");
}
static void ke_release_order(void)
{
	ke_setup(); ke_key(VK_RBUTTON, true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, false); struct ke_frame f = ke_frame(150, 140, 16);
	int stage = 0;
	for (int i = 0; i < f.n && i < KE_CAP; i++) {
		struct ke_msg *m = &f.m[i];
		if (m->type != 18 && m->type != 4) continue;
		int want_type[] = {18,4,4,18}, want_no[] = {0,0,KE_A,KE_A};
		KE_CHECK(stage < 4, "extra release event");
		if (stage < 4) KE_CHECK(m->type == want_type[stage] && m->no == want_no[stage], "release order at %d", stage);
		stage++;
	}
	KE_CHECK(stage == 4, "release order count %d", stage);
}
#include "key_cancel_fixture.inc"
#include "key_lifecycle_fixture.inc"
#include "key_platform_fixture.inc"

/* Compare the source events supported here without projecting them out
 * of the raw queue. Drop-target selection/completion remains separate. */
struct ke_drag_expected { int type, n, v[KE_VARS]; };
static void ke_drag_events_are(const struct ke_frame *f,
		const struct ke_drag_expected *want, int n, int line)
{
	int at = 0;
	for (int i = 0; i < f->n && i < KE_CAP; i++) {
		const struct ke_msg *m = &f->m[i];
		if (m->type != 7 && m->type != 8 && m->type != 10) continue;
		KE_CHECK(at < n, "line %d: extra source event %d", line, m->type);
		if (at < n) {
			KE_CHECK(m->type == want[at].type && m->n == want[at].n,
				"line %d: source event %d type/length", line, at);
			KE_CHECK(m->no == KE_A && m->delegate == 3 && m->unique == 30,
				"line %d: source event %d receiver", line, at);
			for (int j = 0; j < want[at].n; j++)
				KE_CHECK(m->v[j] == want[at].v[j],
					"line %d: source event %d payload %d", line, at, j);
		}
		at++;
	}
	KE_CHECK(at == n, "line %d: source event count %d, want %d", line, at, n);
}
#define KE_DRAG(f, ...) do { \
	const struct ke_drag_expected want[] = { __VA_ARGS__ }; \
	ke_drag_events_are((f), want, sizeof(want) / sizeof(*want), __LINE__); \
} while (0)
#define KE_NO_DRAG(f) ke_drag_events_are((f), NULL, 0, __LINE__)

/* 0x546c02..0x546d90: left Manhattan threshold/latch, but no such
 * threshold or parts movement for right/middle. 0x547460/0x547530/
 * 0x5475a0 and 0x577660 define normal End and clear the begun state. */
static void ke_normal_drag(int key)
{
	ke_setup(); struct parts *a = parts_get(KE_A); a->draggable = true;
	ke_frame(150, 140, 16);
	ke_key(key, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {key, KE_START, KE_START}); KE_NO_DRAG(&f);
	f = ke_frame(150, 140, 16); KE_NO_DRAG(&f);
	int x, y;
	if (key == VK_LBUTTON) {
		/* |6| + |3| is below the default 10; equality begins once. */
		f = ke_frame(156, 143, 16); KE_NO_DRAG(&f);
		KE_CHECK(a->local.pos.x == 100 && a->local.pos.y == 100, "left moved below threshold");
		f = ke_frame(156, 144, 16);
		KE_DRAG(&f, {7, 1, {key}}, {8, 5, {150, 140, 156, 144, key}});
		KE_CHECK(a->local.pos.x == 106 && a->local.pos.y == 104, "left equality did not move parts");
		f = ke_frame(157, 144, 16);
		KE_DRAG(&f, {8, 5, {150, 140, 157, 144, key}});
		x = 151; y = 141;
		f = ke_frame(x, y, 16);
		KE_DRAG(&f, {8, 5, {150, 140, x, y, key}});
		KE_CHECK(a->local.pos.x == 101 && a->local.pos.y == 101, "left threshold did not remain latched");
	} else {
		f = ke_frame(151, 141, 16);
		KE_DRAG(&f, {7, 1, {key}}, {8, 5, {150, 140, 151, 141, key}});
		x = 152; y = 141;
		f = ke_frame(x, y, 16);
		KE_DRAG(&f, {8, 5, {150, 140, x, y, key}});
		KE_CHECK(a->local.pos.x == 100 && a->local.pos.y == 100, "non-left drag moved parts");
	}
	f = ke_frame(x, y, 16); KE_NO_DRAG(&f);
	ke_key(key, false); f = ke_frame(x, y, 16);
	KE_DRAG(&f, {10, 2, {key, key == VK_LBUTTON ? 1 : 0}});
	KE_EXPECT(&f, {key, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
	/* Motion after release must not reuse the old source or emit End twice. */
	x++; y++; f = ke_frame(x, y, 16); KE_NO_DRAG(&f);
	KE_EXPECT(&f, {key, 0, 0});
	ke_key(key, true); f = ke_frame(x, y, 16);
	KE_EXPECT(&f, {key, KE_START, KE_START}); KE_NO_DRAG(&f);
	f = ke_frame(x, y, 16); KE_NO_DRAG(&f);
	int next_x = x + (key == VK_LBUTTON ? 6 : 1);
	int next_y = y + (key == VK_LBUTTON ? 4 : 0);
	f = ke_frame(next_x, next_y, 16);
	KE_DRAG(&f, {7, 1, {key}}, {8, 5, {x, y, next_x, next_y, key}});
	ke_key(key, false); f = ke_frame(next_x, next_y, 16);
	KE_DRAG(&f, {10, 2, {key, key == VK_LBUTTON ? 1 : 0}});
	f = ke_frame(next_x + 1, next_y, 16); KE_NO_DRAG(&f);
}
static void ke_left_threshold(void) { ke_normal_drag(VK_LBUTTON); }
static void ke_right_small_motion(void) { ke_normal_drag(VK_RBUTTON); }
static void ke_middle_small_motion(void) { ke_normal_drag(VK_MBUTTON); }

/* The clock fields are read only where the tree has the per-element key
 * table (PARTS_V14_NR_KEYS). A tree without the v14 key events (a parent
 * commit's before check) leaves these structural checks out; such a case
 * then fails on its queue checks, not at compile time.
 * This depends on parts_internal.h being included before this file
 * (runtime_probe.c includes it through activity_text_fixture.inc; the
 * struct parts fields used below would not compile otherwise) and on the
 * macro's name: if PARTS_V14_NR_KEYS is renamed, the clock checks of the
 * flush, End and empty-repeat cases disappear silently on the current tree
 * too. A mutant that resets the clock in the old-focus flush must fail the
 * flush case (its "flush changed old-focus clock" check) to show that the
 * checks are compiled in. */
#ifdef PARTS_V14_NR_KEYS
#define KE_CLOCK_IS(no, e, t, ...) KE_CHECK(parts_get(no)->v14_keys.elapsed == (e) \
	&& parts_get(no)->v14_keys.threshold == (t), __VA_ARGS__)
#else
#define KE_CLOCK_IS(no, e, t, ...) ((void)0)
#endif

/* 0x574fd0 clears every held bit in table order without touching the
 * clock. The two field assertions are structural checks of that native
 * rule; a subsequent new trigger would hide the clock difference. */
static void ke_flush_multiple(void)
{
	ke_setup(); ke_focus_a();
	const int keys[] = {VK_CONTROL, 'A', 'B'};
	for (int i = 0; i < 3; i++) ke_key(keys[i], true);
	ke_frame(150, 140, 16); ke_frame(150, 140, 516); ke_frame(150, 140, 31);
	KE_CLOCK_IS(KE_A, 31, 80, "old-focus repeat precondition");
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(450, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B, "multi-held focus switch");
	int old_up = 0, last_new = -1, first_old = KE_CAP, total = 0;
	for (int i = 0; i < f.n && i < KE_CAP; i++) {
		const struct ke_msg *m = &f.m[i];
		total += m->type == 4 || (m->type >= 15 && m->type <= 18);
		if (m->no == KE_B && m->type >= 15 && m->type <= 18) last_new = i;
		if (m->no != KE_A || m->type != 18) continue;
		if (!old_up) first_old = i;
		KE_CHECK(old_up < 3 && m->n == 1 && m->v[0] == keys[old_up < 3 ? old_up : 0], "old multi-held Up order");
		old_up++;
	}
	KE_CHECK(total == 21 && old_up == 3 && last_new >= 0 && last_new < first_old,
		"multi-held event count/new-before-old order");
	for (int i = 0; i < 3; i++) {
		KE_CHECK(ke_count(&f, 16, 0, keys[i]) == 1
			&& ke_count(&f, 18, KE_A, keys[i]) == 1, "multi-held Whole/old-focus key %d", keys[i]);
		for (int type = 15; type <= 17; type++)
			KE_CHECK(ke_count(&f, type, KE_B, keys[i]) == 1, "new focus key %d type %d", keys[i], type);
	}
	for (int type = 15; type <= 17; type++)
		KE_CHECK(ke_count(&f, type, 0, VK_RBUTTON) == 1
			&& ke_count(&f, type, KE_B, VK_RBUTTON) == 1, "new right key type %d", type);
	KE_CLOCK_IS(KE_A, 47, 80, "flush changed old-focus clock");
	f = ke_frame(450, 140, 0);
	for (int i = 0; i < 3; i++)
		KE_CHECK(!ke_count(&f, 18, KE_A, keys[i]), "multi-held flush repeated key %d", keys[i]);
	ke_key(VK_RBUTTON, false); ke_frame(450, 140, 0);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 0);
	for (int i = 0; i < 3; i++)
		KE_CHECK(ke_count(&f, 15, KE_A, keys[i]) == 1, "flush retained old held key %d", keys[i]);
}

/* End's Whole flush and its non-slot focused table are independent:
 * 0x5461a0/0x547ad0 and 0x577af0 do not clear all local tables. */
static void ke_end_multiple(void)
{
	ke_setup(); ke_focus_a();
	ke_key(VK_CONTROL, true); ke_key('A', true); ke_key(VK_F9, true);
	ke_frame(150, 140, 16); ke_frame(150, 140, 516); ke_frame(150, 140, 31);
	PE_EndInput(); struct ke_frame f = ke_read();
	KE_EXPECT(&f, {VK_CONTROL, KE_UP, 0}, {'A', KE_UP, 0}, {VK_F9, KE_UP, 0});
	KE_CLOCK_IS(KE_A, 31, 80, "End changed non-slot local clock");
	f = ke_frame(150, 140, 16); KE_CHECK(f.n == 0, "End still sampled multiple keys");
	PE_BeginInput(); ke_read(); ke_key(VK_LBUTTON, true);
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_LBUTTON, KE_START, KE_START},
		{VK_CONTROL, KE_START, KE_DOWN}, {'A', KE_START, KE_DOWN}, {VK_F9, KE_START, KE_DOWN});
}

/* 0x574f96..0x574fc1: an empty held batch restores 500 and zero,
 * even without a new trigger. Inspect only those two native fields. */
static void ke_empty_repeat_reset(void)
{
	ke_setup(); ke_focus_a(); ke_key('A', true);
	ke_frame(150, 140, 16); ke_frame(150, 140, 516); ke_frame(150, 140, 31);
	KE_CLOCK_IS(KE_A, 31, 80, "empty-repeat precondition");
	ke_key('A', false); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_UP, KE_UP});
	KE_CLOCK_IS(KE_A, 0, 500, "empty held batch did not reset repeat");
	f = ke_frame(150, 140, 516); KE_EXPECT(&f, {'A', 0, 0});
	KE_CLOCK_IS(KE_A, 0, 500, "empty overdue batch did not reset repeat");
	ke_key('A', true); f = ke_frame(150, 140, 0);
	KE_EXPECT(&f, {'A', KE_START, KE_START});
	f = ke_frame(150, 140, 500); KE_EXPECT(&f, {'A', KE_DOWN, KE_DOWN});
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_DOWN | KE_PRESS, KE_DOWN | KE_PRESS});
}

/* Native 0x574ded samples each generic VK independently. SDL alias
 * aggregation is the port policy: another VK must not keep this one
 * held. Exercise each source while both unrelated groups remain down. */
static void ke_alias_cross_keys(void)
{
	unsetenv("XSYS4_HOLD_KEYS");
	ke_setup(); ke_focus_a(); KE_CHECK(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL events init");
	const SDL_Scancode scans[][2] = {
		{SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT},
		{SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL},
		{SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER}
	};
	const int keys[] = {VK_SHIFT, VK_CONTROL, VK_RETURN};
	for (int group = 0; group < 3; group++) {
		for (int first = 0; first < 2; first++) {
			for (int k = 0; k < 3; k++)
				for (int j = 0; j < 2; j++) kl_sdl_key(scans[k][j], true);
			struct ke_frame f = ke_frame(150, 140, 16);
			KE_EXPECT(&f, {VK_SHIFT, KE_START, KE_START},
				{VK_CONTROL, KE_START, KE_START}, {VK_RETURN, KE_START, KE_START});
			kl_sdl_key(scans[group][first], false); f = ke_frame(150, 140, 16);
			KE_EXPECT(&f, {VK_SHIFT, KE_DOWN, KE_DOWN},
				{VK_CONTROL, KE_DOWN, KE_DOWN}, {VK_RETURN, KE_DOWN, KE_DOWN});
			kl_sdl_key(scans[group][1-first], false);
			f = ke_frame(150, 140, 16);
			struct ke_expected want[3];
			for (int k = 0; k < 3; k++) {
				KE_CHECK(key_is_down(keys[k]) == (k != group), "alias group %d contaminated key %d", group, keys[k]);
				int mask = k == group ? KE_UP : KE_DOWN;
				want[k] = (struct ke_expected){keys[k], mask, mask};
			}
			ke_events_are(&f, want, 3, __LINE__);
			kl_sdl_key(scans[group][first], true); f = ke_frame(150, 140, 16);
			for (int k = 0; k < 3; k++) {
				int mask = k == group ? KE_START : KE_DOWN;
				want[k] = (struct ke_expected){keys[k], mask, mask};
			}
			ke_events_are(&f, want, 3, __LINE__);
			for (int k = 0; k < 3; k++)
				for (int j = 0; j < 2; j++) kl_sdl_key(scans[k][j], false);
			f = ke_frame(150, 140, 16);
			KE_EXPECT(&f, {VK_SHIFT, KE_UP, KE_UP},
				{VK_CONTROL, KE_UP, KE_UP}, {VK_RETURN, KE_UP, KE_UP});
		}
	}
}

/* 0x5788d0 key 1 (0x578944..0x5789e6): the focused element's own left
 * release, entered and with a position, sets the on-cursor state (2);
 * that also holds while another button's cancellation keeps the walk
 * suppressed. 0x578650..0x578675: a later left Trigger on the focused,
 * entered, clickable element sets the down state again meanwhile. */
static void ke_cancelled_left_visual(void)
{
	ke_setup(); struct parts *a = parts_get(KE_A); ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	KE_CHECK(a->state == PARTS_STATE_CLICKED, "left press state %d", a->state);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, VK_LBUTTON, 2, false);
	f = ke_frame(150, 140, 16); kc_no_drag_events(&f);
	KE_CHECK(a->state == PARTS_STATE_CLICKED && a->is_hovered,
		"cancellation changed the pressed visual: state %d", a->state);
	ke_key(VK_LBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(a->is_hovered, "suppressed walk left the focus");
	KE_CHECK(a->state == PARTS_STATE_HOVERED,
		"entered left release with a position kept state %d", a->state);
	KE_CHECK(ke_count(&f, 18, 0, VK_LBUTTON) == 1 && ke_count(&f, 18, KE_A, VK_LBUTTON) == 1,
		"left release during suppression lost key-up");
	f = ke_frame(150, 140, 16);
	KE_CHECK(a->state == PARTS_STATE_HOVERED, "suppressed frame changed state %d", a->state);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16); kc_no_drag_events(&f);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "suppressed re-press changed focus");
	KE_CHECK(ke_count(&f, 15, KE_A, VK_LBUTTON) == 1, "suppressed re-press lost local Trigger");
	KE_CHECK(a->state == PARTS_STATE_CLICKED, "left re-press during suppression: state %d", a->state);
	ke_key(VK_LBUTTON, false); ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(a->state == PARTS_STATE_HOVERED, "release of every button: state %d", a->state);
}

/* A slot holds the native element (0x5462bc..0x5462cc reads +0x1ec's
 * element). A parts created again under the same number is a new element
 * that no press made a source, so it gets no drag event. */
static void ke_recreated_source(void)
{
	ke_setup(); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START});
	f = ke_frame(152, 140, 16);
	KE_DRAG(&f, {7, 1, {VK_RBUTTON}}, {8, 5, {150, 140, 152, 140, VK_RBUTTON}});
	PE_ReleaseParts(KE_A);
	br_box(KE_A, 100, 100, 200, 80, true);
	f = ke_frame(152, 140, 16); KE_NO_DRAG(&f);
	f = ke_frame(160, 145, 16); KE_NO_DRAG(&f);
	f = ke_frame(170, 150, 16); KE_NO_DRAG(&f);
	ke_key(VK_RBUTTON, false); f = ke_frame(170, 150, 16); KE_NO_DRAG(&f);
}

/* 0x53e690 runs only the active controller's manager (0x547c90 from
 * 0x53c64d). The new layer's manager never had the press, so the old
 * layer's source gets no drag event while the new layer is active. */
static void kl_layer_held_source(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_A, 100, 100, 200, 80, 3); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "right press did not focus");
	f = ke_frame(155, 141, 16);
	KE_CHECK(kc_count(&f, 7, KE_A) == 1 && kc_count(&f, 8, KE_A) == 1,
		"right source did not begin before the layer");
	int layer = ci_add(-1);
	li_button(KE_B, 400, 100, 200, 80, 7);
	KE_CHECK(ci_active() == layer, "new layer not active");
	f = ke_frame(155, 141, 16); KE_NO_DRAG(&f);
	f = ke_frame(165, 145, 16); KE_NO_DRAG(&f);
	f = ke_frame(175, 150, 16); KE_NO_DRAG(&f);
	ke_key(VK_RBUTTON, false); f = ke_frame(175, 150, 16); KE_NO_DRAG(&f);
	KE_CHECK(ke_count(&f, 4, 0, VK_RBUTTON) == 1 && !ke_count(&f, 4, KE_A, VK_RBUTTON),
		"new layer release receiver");
}

/* 0x5461a0 resets the five slot elements with 0x577af0, which clears
 * the focus flag (+0x1b4) and the entered flag silently; the next Begin
 * (0x538060) then has no focused element to send LostFocus to. */
static void kl_right_slot_reset(void)
{
	ke_setup(); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "right press did not focus");
	PE_EndInput(); struct ke_frame f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 0, "End sent LostFocus");
	KE_EXPECT(&f, {VK_RBUTTON, KE_UP, 0});
	KE_CHECK(!parts_get(KE_A)->is_hovered, "right slot remained entered");
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 0, "Begin sent LostFocus to a reset slot element");
	ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 4, 0, VK_RBUTTON) == 1 && !ke_count(&f, 4, KE_A, VK_RBUTTON),
		"reset right slot retained its local click");
}

/* 0x577af0 clears all 80 one-byte key flags of a slot element
 * (+0xc..+0x5b) without sending Up; when that element is focused again,
 * its held keys start again (Trigger), including the table's last key. */
static void kl_slot_keys_reset(void)
{
	ke_setup(); ke_frame(150, 140, 16);
	ke_key(VK_F9, true); ke_key(VK_RBUTTON, true);
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START}, {VK_F9, KE_START, KE_START});
	ke_frame(150, 140, 16);
	PE_EndInput(); f = ke_read();
	KE_EXPECT(&f, {VK_RBUTTON, KE_UP, 0}, {VK_F9, KE_UP, 0});
	PE_BeginInput(); ke_read();
	ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "right re-press did not focus");
	KE_EXPECT(&f, {VK_RBUTTON, KE_START, KE_START}, {VK_F9, KE_DOWN, KE_START});
}

/* 0x578650..0x578675: a left Trigger sets the down state only on a focused
 * element that is entered (+0x1ac). A right source dragged off its element
 * leaves it; a left press then cancels that source, selection stays
 * suppressed, and the focus is still the element the cursor left. */
static void ke_unentered_focus_press(void)
{
	ke_setup(); struct parts *a = parts_get(KE_A); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "right press did not focus");
	ke_frame(640, 600, 16);
	KE_CHECK(!a->is_hovered && a->state == PARTS_STATE_DEFAULT,
		"right drag off the focus did not leave it");
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(640, 600, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "cancelling press changed focus");
	KE_CHECK(ke_count(&f, 15, KE_A, VK_LBUTTON) == 1, "focus did not sample the left Trigger");
	KE_CHECK(a->state == PARTS_STATE_DEFAULT, "unentered focus took state %d", a->state);
	ke_frame(640, 600, 16);
	KE_CHECK(a->state == PARTS_STATE_DEFAULT, "held frame: state %d", a->state);
	ke_key(VK_LBUTTON, false); ke_key(VK_RBUTTON, false); ke_frame(640, 600, 16);
	KE_CHECK(a->state == PARTS_STATE_DEFAULT, "release of every button: state %d", a->state);
}

/* 0x5789e8..0x578a34: a right or middle release clicks the focused element
 * only while it is entered (+0x1ac) and the position is known. */
static void ke_release_off_focus(int key)
{
	ke_setup(); ke_frame(150, 140, 16);
	ke_key(key, true); ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "button %d did not focus", key);
	ke_frame(640, 600, 16);
	ke_frame(640, 600, 16);
	KE_CHECK(!parts_get(KE_A)->is_hovered, "moving off did not leave the focus");
	ke_key(key, false); struct ke_frame f = ke_frame(640, 600, 16);
	KE_CHECK(ke_count(&f, 4, 0, key) == 1 && !ke_count(&f, 4, KE_A, key),
		"button %d release off the focus clicked it", key);
	KE_CHECK(ke_count(&f, 18, 0, key) == 1 && ke_count(&f, 18, KE_A, key) == 1,
		"button %d release off the focus lost key-up", key);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "release changed focus");
}
static void ke_right_release_off(void) { ke_release_off_focus(VK_RBUTTON); }
static void ke_middle_release_off(void) { ke_release_off_focus(VK_MBUTTON); }

/* 0x53e690 runs only the active controller's manager on that controller's
 * elements. An element of a now inactive layer that is still the port's
 * focus keeps its table untouched: no local key events while the other
 * layer is active, and no flush when that layer's press takes the focus. */
static void kl_layer_focus_keys(void)
{
	li_setup(); key_clear_flag(false); keyboard_focus = true;
	li_button(KE_A, 100, 100, 200, 80, 3); PE_BeginInput();
	ke_frame(150, 140, 0); ke_frame(150, 140, 0); ke_focus_a();
	ke_key('A', true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {'A', KE_START, KE_START});
	int layer = ci_add(-1);
	li_button(KE_B, 400, 100, 200, 80, 7);
	KE_CHECK(ci_active() == layer, "new layer not active");
	for (int n = 0; n < 2; n++) {
		f = ke_frame(150, 140, 16);
		KE_CHECK(ke_count(&f, 16, 0, 'A') == 1, "Whole lost the held key under the new layer");
		for (int type = 15; type <= 18; type++)
			KE_CHECK(!ke_count(&f, type, KE_A, -1),
				"inactive layer's focus got key type %d (frame %d)", type, n);
		KE_CHECK(!ke_count(&f, 4, KE_A, -1), "inactive layer's focus clicked (frame %d)", n);
	}
	ke_key(VK_RBUTTON, true); f = ke_frame(450, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B, "new layer press did not focus");
	KE_CHECK(ke_count(&f, 15, KE_B, 'A') == 1, "new focus did not start the held key");
	KE_CHECK(!ke_count(&f, 18, KE_A, 'A') && !ke_count(&f, 4, KE_A, -1),
		"inactive layer's old focus was flushed");
}

/* 0x546100..0x54618c: Begin samples each button through the activation
 * gate ([W+0x3c] before GetKeyState). While inactive the sample is up, so
 * the first update while still inactive has no release edge. */
static void kl_begin_inactive_held(void)
{
	ke_setup(); keyboard_focus = false; ke_key(VK_LBUTTON, true); ke_key(VK_RBUTTON, true);
	PE_BeginInput(); ke_read();
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 4, 0, -1) && !ke_count(&f, 4, KE_A, -1),
		"inactive Begin sampled a held button: %d messages", f.n);
	f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 4, 0, -1), "inactive held button clicked");
	keyboard_focus = true; f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 15, 0, VK_LBUTTON) == 1 && ke_count(&f, 15, 0, VK_RBUTTON) == 1,
		"activation did not start the held buttons");
}

/* 0x4bdaf0..0x4bdb28 clears the keyboard (SetKeyboardState) on every
 * active WM_ACTIVATE, and WM_SETFOCUS (0x4bcb8c..0x4bcbaf) clears it too.
 * The SDL mapping (port policy) clears on a focus gain while already
 * active. */
static void kp_repeated_focus_gain(void)
{
	unsetenv("XSYS4_HOLD_KEYS");
	ke_setup(); KE_CHECK(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL events init");
	kl_sdl_key(SDL_SCANCODE_A, true);
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 15, 0, 'A') == 1, "initial key missing");
	/* The Trigger above shows the gate open; keyboard_focus is the SDL
	 * focus both trees have (input_is_active is the v14 key events'). */
	KE_CHECK(keyboard_focus, "window not active before the repeated gain");
	kp_window(SDL_WINDOWEVENT_FOCUS_GAINED);
	KE_CHECK(!key_is_down('A'), "repeated focus gain kept a held key");
	f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 18, 0, 'A') == 1, "cleared key did not release");
}

/* 0x4bdaf0..0x4bdb28 zeroes the whole keyboard state on activation. With
 * the SDL source aggregation (port policy) every source, including the
 * table's last one (keypad Enter), must be forgotten: a later press and
 * release of the other Return source leaves Return up. */
static void kp_clear_last_source(void)
{
	unsetenv("XSYS4_HOLD_KEYS");
	ke_setup(); KE_CHECK(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL events init");
	kl_sdl_key(SDL_SCANCODE_KP_ENTER, true);
	KE_CHECK(key_is_down(VK_RETURN), "keypad Enter not Return");
	key_clear_flag(false);
	KE_CHECK(!key_is_down(VK_RETURN), "clear kept Return");
	kl_sdl_key(SDL_SCANCODE_RETURN, true);
	KE_CHECK(key_is_down(VK_RETURN), "Return press after clear");
	kl_sdl_key(SDL_SCANCODE_RETURN, false);
	KE_CHECK(!key_is_down(VK_RETURN), "clear kept the keypad Enter source");
}


/* 0x547460/0x547530/0x5475a0: each button's ordinary release ends its own
 * source. A left button held since BeginInput has no source (0x546030
 * samples it), so it neither cancels a right press nor stops the right
 * source's own release. */
static void kl_right_source_under_held_left(void)
{
	ke_setup(); ke_key(VK_LBUTTON, true); PE_BeginInput(); ke_read();
	/* Begin forgets the stored position: two frames make the press frame
	 * a stationary one (0x5460c9 clears the "had a position" byte). */
	ke_frame(150, 140, 16); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "right press under a held left did not focus");
	KE_NO_DRAG(&f);
	f = ke_frame(152, 140, 16);
	KE_DRAG(&f, {7, 1, {VK_RBUTTON}}, {8, 5, {150, 140, 152, 140, VK_RBUTTON}});
	ke_key(VK_RBUTTON, false); f = ke_frame(152, 140, 16);
	KE_DRAG(&f, {10, 2, {VK_RBUTTON, 0}});
	f = ke_frame(155, 141, 16); KE_NO_DRAG(&f);
	ke_key(VK_LBUTTON, false); f = ke_frame(155, 141, 16); KE_NO_DRAG(&f);
}

/* 0x547e88..0x547f2d: the update samples each button (GetKeyState 1, 2
 * and 4) only through the activation gate (0x496670 and the gate object
 * at [0x87f8a0]+0x1c); otherwise the button reads as up. A button held on
 * the focus when the window becomes inactive is therefore released at the
 * next update: the whole-screen Click (0x547f81..0x548133, the position
 * being valid) and the focus' own release (0x578580 -> 0x5788d0: Click,
 * then KeyUp). The two inactive updates with the button still held make
 * no press. Active again with the button held, that update is a press:
 * it takes the focus (0x546a4e..0x546aa2) and starts the keys (Trigger).
 * keyboard_focus stands for the native gate (port policy, key_platform). */
static void kl_inactive_held_button(int key)
{
	ke_setup(); ke_frame(150, 140, 16);
	ke_key(key, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "button %d did not focus", key);
	KE_EXPECT(&f, {key, KE_START, KE_START});
	f = ke_frame(150, 140, 16);
	KE_EXPECT(&f, {key, KE_DOWN, KE_DOWN});
	keyboard_focus = false;
	f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 4, 0, key) == 1, "inactive: no whole-screen release of %d", key);
	KE_CHECK(ke_count(&f, 4, KE_A, key) == 1, "inactive: the focus lost its click of %d", key);
	KE_EXPECT(&f, {key, KE_UP | KE_CLICK, KE_UP | KE_CLICK});
	for (int n = 0; n < 2; n++) {
		f = ke_frame(450, 140, 16);
		KE_EXPECT(&f, {key, 0, 0});
		KE_CHECK(!kc_count(&f, 19, KE_B) && !kc_count(&f, 20, KE_A)
			&& he_hll_int("GetActiveParts") == KE_A,
			"inactive update %d with %d held changed the focus", n, key);
	}
	keyboard_focus = true;
	f = ke_frame(450, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_B, "active again: held %d made no press", key);
	KE_CHECK(kc_count(&f, 20, KE_A) == 1 && kc_count(&f, 19, KE_B) == 1,
		"active again: focus messages for %d", key);
	KE_CHECK(ke_count(&f, 15, 0, key) == 1 && ke_count(&f, 15, KE_B, key) == 1
		&& !ke_count(&f, 15, KE_A, key), "active again: Trigger of %d", key);
	ke_key(key, false); f = ke_frame(450, 140, 16);
	KE_CHECK(ke_count(&f, 4, 0, key) == 1 && ke_count(&f, 4, KE_B, key) == 1,
		"release of %d after activation", key);
}
static void kl_inactive_held_left(void) { kl_inactive_held_button(VK_LBUTTON); }
static void kl_inactive_held_middle(void) { kl_inactive_held_button(VK_MBUTTON); }
static void kl_inactive_held_right(void) { kl_inactive_held_button(VK_RBUTTON); }

/* 0x5462b0 empties the left slot with 0x548530 (0x54636e..0x546378),
 * which drops the reference and leaves the element; 0x546470 and
 * 0x5463c0 do the same for the right and middle slots. The end of the
 * scope (0x5461a0) resets only the elements still in the five slots
 * (0x577af0), so a source cancelled by another button keeps its focus
 * (+0x1b4) and its entered flag (+0x1ac) over an EndInput with both
 * buttons held. The next Begin takes the focus from it with LostFocus
 * (0x538060), and the next walk neither enters it again (no MouseEnter,
 * 0x577b90 at +0x1ac set) nor misses its MouseLeave (0x577cf0). With a
 * right source the left Trigger of the cancelling press lands on that
 * focus too (0x578650..0x578675), the case of the port's pressed parts. */
static void kc_end_after_cancel(int source, int other, bool leave)
{
	ke_setup(); struct parts *a = parts_get(KE_A); ke_frame(150, 140, 16);
	ke_key(source, true); ke_frame(150, 140, 16);
	ke_key(other, true); struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, source, 2, false);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "cancellation changed the focus");
	ke_frame(150, 140, 16);
	PE_EndInput(); f = ke_read();
	KE_CHECK(!kc_count(&f, 20, KE_A), "End sent LostFocus");
	KE_EXPECT(&f, {source, KE_UP, 0}, {other, KE_UP, 0});
	KE_CHECK(a->is_hovered, "End reset the cancelled source");
	PE_BeginInput(); f = ke_read();
	KE_CHECK(kc_count(&f, 20, KE_A) == 1, "Begin did not take the focus from the cancelled source");
	KE_CHECK(he_hll_int("GetActiveParts") == 0, "Begin kept the focus");
	f = ke_frame(leave ? 640 : 150, 140, 16);
	if (leave)
		KE_CHECK(kc_count(&f, 2, KE_A) == 1, "the cancelled source was not left");
	else
		KE_CHECK(!kc_count(&f, 0, KE_A) && a->is_hovered, "the cancelled source was entered again");
	ke_key(source, false); ke_key(other, false); ke_frame(leave ? 640 : 150, 140, 16);
}
static void kc_end_after_left_cancel(void) { kc_end_after_cancel(VK_LBUTTON, VK_RBUTTON, false); }
static void kc_end_after_left_cancel_leave(void) { kc_end_after_cancel(VK_LBUTTON, VK_RBUTTON, true); }
static void kc_end_after_right_cancel(void) { kc_end_after_cancel(VK_RBUTTON, VK_LBUTTON, false); }

/* 0x5481bb..0x548201: without a position each button's release cancels
 * only its own slot (mode 3). A right button held since BeginInput has no
 * source (0x546030 samples it), so its release outside the window leaves
 * the left source alone, and the left drag begins later as usual. */
static void kc_outside_other_release(void)
{
	ke_setup(); ke_key(VK_RBUTTON, true); PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16); ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "left press under a held right did not focus");
	KE_NO_DRAG(&f);
	mouse_focus = false; ke_key(VK_RBUTTON, false); f = ke_frame(150, 140, 16);
	KE_NO_DRAG(&f);
	mouse_focus = true; f = ke_frame(170, 145, 16);
	KE_DRAG(&f, {7, 1, {VK_LBUTTON}}, {8, 5, {150, 140, 170, 145, VK_LBUTTON}});
	ke_key(VK_LBUTTON, false); f = ke_frame(170, 145, 16);
	KE_DRAG(&f, {10, 2, {VK_LBUTTON, 1}});
}

/* 0x548206..0x548218 clears the cancellation flag (+0x1fc) when all three
 * buttons are up, before 0x548278 tests it for the press and the cursor
 * walk. The update that releases the last button is walked: the cursor
 * moved off the cancelled source leaves it and enters what is under it,
 * and the source's own left release then is no Click (0x5788d0: not
 * entered), only its KeyUp. */
static void kc_release_all_walk(void)
{
	ke_setup(); struct parts *a = parts_get(KE_A); ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, VK_LBUTTON, 2, false);
	f = ke_frame(150, 140, 16); kc_no_drag_events(&f);
	ke_key(VK_LBUTTON, false); ke_key(VK_RBUTTON, false); f = ke_frame(450, 140, 16);
	KE_CHECK(kc_count(&f, 2, KE_A) == 1 && kc_count(&f, 0, KE_B) == 1 && !a->is_hovered,
		"the update releasing every button was not walked");
	KE_CHECK(ke_count(&f, 4, 0, VK_LBUTTON) == 1 && !ke_count(&f, 4, KE_A, VK_LBUTTON),
		"left release off the cancelled source clicked it");
	KE_CHECK(ke_count(&f, 18, KE_A, VK_LBUTTON) == 1, "the focus lost its left key-up");
}

/* 0x548311..0x548339 -> 0x5476b0: an update without a position leaves only
 * the left slot's element, and only when it is the focus (0x5476dc..
 * 0x5476ec calls 0x577cf0). A right or middle source that is the focus
 * stays entered while the pointer is outside the window, button held. */
static void kl_outside_held_other(int key)
{
	ke_setup(); struct parts *a = parts_get(KE_A); ke_frame(150, 140, 16);
	ke_key(key, true); ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A && a->is_hovered, "button %d did not focus", key);
	mouse_focus = false;
	for (int n = 0; n < 2; n++) {
		struct ke_frame f = ke_frame(150, 140, 16);
		KE_CHECK(!kc_count(&f, 2, KE_A) && a->is_hovered,
			"no position: the source of %d was left (update %d)", key, n);
	}
	mouse_focus = true; ke_key(key, false); ke_frame(150, 140, 16);
}
static void kl_outside_held_right(void) { kl_outside_held_other(VK_RBUTTON); }
static void kl_outside_held_middle(void) { kl_outside_held_other(VK_MBUTTON); }

/* Port policy (as kp_activation): SDL MAXIMIZED also ends the minimized
 * gate, standing for the native active && !minimized cache (0x4bdaf0..
 * 0x4bdb28); the SDL event order on that path is not checked here. */
static void kp_maximize_unminimizes(void)
{
	unsetenv("XSYS4_HOLD_KEYS");
	ke_setup(); KE_CHECK(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL events init");
	kp_window(SDL_WINDOWEVENT_MINIMIZED);
	kl_sdl_key(SDL_SCANCODE_A, true);
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 15, 0, 'A'), "minimized window sampled a key");
	kp_window(SDL_WINDOWEVENT_MAXIMIZED);
	kl_sdl_key(SDL_SCANCODE_A, true);
	f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 15, 0, 'A') == 1, "maximize kept the minimized gate");
}

/* 0x5461a0 resets only the elements in the five slots, with 0x577af0
 * (clears, among others, +4, the 80 one-byte key flags at +0xc..+0x5b,
 * +0x1ac, the time since entered at +0x1b0, +0x1b4 and the last position
 * at +0x1b8/+0x1bc), and
 * the Begin's 0x564cc0 (false) only sends LostFocus and clears +0x1b4. A
 * source cancelled by another button is no longer in its slot, so over End
 * and Begin it keeps its key table, its last position and its time: the
 * next update at the same place sends it no MouseMove and an OnCursor time
 * that kept counting. When a left press focuses it again, the kept left
 * bit makes that press a Down without Trigger and the kept right bit is
 * released (KeyUp). */
static void kc_end_cancel_residue(void)
{
	ke_setup(); ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	PE_EndInput(); ke_read();
	PE_BeginInput(); ke_read();
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(!kc_count(&f, 1, KE_A), "Begin forgot the last position (local MouseMove %d)", kc_count(&f, 1, KE_A));
	int t = -1;
	for (int i = 0; i < f.n && i < KE_CAP; i++)
		if (f.m[i].type == 6 && f.m[i].no == KE_A && f.m[i].n == 3) t = f.m[i].v[2];
	KE_CHECK(t > 16, "time since entered restarted (%d)", t);
	ke_key(VK_LBUTTON, false); ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); f = ke_frame(150, 140, 16);
	KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "press did not focus the cancelled source");
	KE_CHECK(!ke_count(&f, 15, KE_A, VK_LBUTTON) && ke_count(&f, 16, KE_A, VK_LBUTTON) == 1,
		"the kept left bit (Trigger %d)", ke_count(&f, 15, KE_A, VK_LBUTTON));
	KE_CHECK(ke_count(&f, 18, KE_A, VK_RBUTTON) == 1, "the kept right bit was not released (%d)", ke_count(&f, 18, KE_A, VK_RBUTTON));
}

/* 0x546100..0x54618c: Begin samples the middle button (GetKeyState 4,
 * 0x546175..0x54618c) through the same activation gate (0x496670 and
 * [0x87f8a0]+0x1c) as the left and right ones. */
static void kl_begin_inactive_held_middle(void)
{
	ke_setup(); keyboard_focus = false; ke_key(VK_MBUTTON, true);
	PE_BeginInput(); ke_read();
	struct ke_frame f = ke_frame(150, 140, 16);
	KE_CHECK(!ke_count(&f, 4, 0, -1) && !ke_count(&f, 4, KE_A, -1),
		"inactive Begin sampled the held middle button: %d messages", f.n);
	keyboard_focus = true; f = ke_frame(150, 140, 16);
	KE_CHECK(ke_count(&f, 15, 0, VK_MBUTTON) == 1, "activation did not start the held middle button");
}

/* 0x547460, 0x547530 and 0x5475a0: an ordinary release sends DragEnd
 * only when that button's drag began (the flags +0x1e0..+0x1e2, tested at
 * 0x5474c3, 0x54753e and 0x5475ae). A press and a release in place is a
 * click with no source event at all. */
static void ke_click_no_drag_end(void)
{
	static const int keys[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
	ke_setup(); ke_frame(150, 140, 16);
	for (int k = 0; k < 3; k++) {
		ke_key(keys[k], true); struct ke_frame f = ke_frame(150, 140, 16);
		KE_CHECK(he_hll_int("GetActiveParts") == KE_A, "button %d did not focus", keys[k]);
		KE_NO_DRAG(&f);
		f = ke_frame(150, 140, 16); KE_NO_DRAG(&f);
		ke_key(keys[k], false); f = ke_frame(150, 140, 16);
		KE_NO_DRAG(&f);
		KE_EXPECT(&f, {keys[k], KE_UP | KE_CLICK, KE_UP | KE_CLICK});
		f = ke_frame(150, 140, 16); KE_NO_DRAG(&f);
	}
}

/* 0x5462cc..0x5462ec: only the left cancel restores the source's
 * position, and only for a draggable source (+0x1a6, tested at 0x5462cf;
 * the left drag moves only such a source, 0x546c6b). The other cancels
 * (0x546470, 0x5463c0) write no position. So a position the script set
 * while a right source, or a left source that is not draggable, is held
 * stays after the cancellation. */
static void kc_cancel_keeps_set_pos(int source, int other, bool draggable)
{
	ke_setup(); struct parts *a = parts_get(KE_A); a->draggable = draggable;
	ke_frame(150, 140, 16);
	ke_key(source, true); ke_frame(150, 140, 16);
	parts_set_pos(a, (Point){130, 120});
	ke_key(other, true); struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, source, 2, false);
	KE_CHECK(a->local.pos.x == 130 && a->local.pos.y == 120,
		"cancel of %d restored a position the input did not move (%d, %d)",
		source, a->local.pos.x, a->local.pos.y);
	ke_key(other, false); ke_key(source, false); ke_frame(150, 140, 16);
}
static void kc_right_cancel_keeps_pos(void) { kc_cancel_keeps_set_pos(VK_RBUTTON, VK_LBUTTON, true); }
static void kc_left_cancel_keeps_pos(void) { kc_cancel_keeps_set_pos(VK_LBUTTON, VK_RBUTTON, false); }

/* 0x547e5f..0x547e84 records whether the cursor moved and passes it to
 * 0x546890 (its last argument, pushed at 0x5482c6), whose left drag writes
 * the source's position only then (0x546c41..0x546c56, then +0x1a6 at
 * 0x546c6b). An update with the cursor at rest leaves a position the
 * script set during the drag; the next motion moves the source again. */
static void ke_drag_rest_keeps_set_pos(void)
{
	ke_setup(); struct parts *a = parts_get(KE_A); a->draggable = true;
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	struct ke_frame f = ke_frame(170, 145, 16);
	KE_DRAG(&f, {7, 1, {VK_LBUTTON}}, {8, 5, {150, 140, 170, 145, VK_LBUTTON}});
	KE_CHECK(a->local.pos.x == 120 && a->local.pos.y == 105, "drag did not move the source");
	parts_set_pos(a, (Point){110, 120});
	f = ke_frame(170, 145, 16); KE_NO_DRAG(&f);
	KE_CHECK(a->local.pos.x == 110 && a->local.pos.y == 120,
		"an update at rest moved the source back (%d, %d)", a->local.pos.x, a->local.pos.y);
	ke_frame(172, 145, 16);
	KE_CHECK(a->local.pos.x == 122 && a->local.pos.y == 105, "the next motion did not move the source");
	ke_key(VK_LBUTTON, false); ke_frame(172, 145, 16);
}

/* 0x548206..0x548218 clears the cancellation flag (+0x1fc) only when all
 * three buttons are up. When the cancelling right button is released
 * first and the cancelled left one is still held, the walk stays
 * suppressed: the cursor moved off the source neither leaves it nor enters
 * what is under it until the left button is released too. */
static void kc_left_held_after_cancel(void)
{
	ke_setup(); struct parts *a = parts_get(KE_A); ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); struct ke_frame f = ke_frame(150, 140, 16);
	kc_cancel_events(&f, VK_LBUTTON, 2, false);
	ke_key(VK_RBUTTON, false); f = ke_frame(450, 140, 16);
	KE_CHECK(!kc_count(&f, 2, KE_A) && !kc_count(&f, 0, KE_B) && a->is_hovered,
		"releasing the cancelling button with the left one held lifted the suppression");
	f = ke_frame(450, 140, 16);
	KE_CHECK(!kc_count(&f, 0, KE_B) && a->is_hovered, "the walk resumed with the left button held");
	ke_key(VK_LBUTTON, false); f = ke_frame(450, 140, 16);
	KE_CHECK(kc_count(&f, 2, KE_A) == 1 && kc_count(&f, 0, KE_B) == 1,
		"the update releasing the last button was not walked");
}

/* The cancelled source of kc_end_cancel_residue, here clickable and
 * passing the cursor, is pressed again with the left button after End and
 * Begin. Its kept left flag gives no Trigger, so 0x578650..0x578675 (a
 * left Trigger on an entered clickable element sets +4 and the pressed
 * look) does not run again; but its +4 from the first press was never
 * cleared, and every walk over the entered element shows the pressed look
 * from +4 (0x577b90, 0x577c28..0x577c38), so natively it is pressed while
 * the button is held. The port gets the same held state from the press,
 * which takes the click for a clickable target even when it passes the
 * cursor. Only the held state is checked: before the new press the two
 * differ (first item of Not covered above). */
static void kc_end_cancel_residue_passing(void)
{
	ke_setup(); struct parts *a = parts_get(KE_A); a->pass_cursor = true;
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	ke_key(VK_RBUTTON, true); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	PE_EndInput(); ke_read();
	PE_BeginInput(); ke_read();
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, false); ke_key(VK_RBUTTON, false); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	ke_key(VK_LBUTTON, true); ke_frame(150, 140, 16);
	ke_frame(150, 140, 16);
	KE_CHECK(a->state == PARTS_STATE_CLICKED, "new left press on the cancelled source: held state %d", a->state);
	ke_key(VK_LBUTTON, false); ke_frame(150, 140, 16);
}

static int test_key_events(void)
{
	void (*cases[])(void) = {ke_left, ke_right, ke_middle, ke_keyboard, ke_blocker, ke_blank, ke_outside, ke_legacy, ke_repeat, ke_shared_repeat, ke_order, ke_table, ke_focus_switch, ke_release_order,
		kc_left_right, kc_left_middle, kc_suppression, kc_priority_left, kc_priority_middle, kc_right_cancel, kc_outside,
		kl_begin_held, kl_end_keys, kl_slot_reset, kl_keyboard_outside, kl_mouse_press_gate, kl_layer_first_press, kl_keypad,
		ke_mixed_order, kc_left_right_moved, kc_left_unmoved_parts,
		kc_middle_left, kc_middle_right, kc_right_middle,
		kc_right_outside, kc_middle_outside, kp_modifiers, kp_activation, kl_cancelled_layer_press,
		ke_left_threshold, ke_right_small_motion, ke_middle_small_motion,
		ke_flush_multiple, ke_end_multiple, ke_empty_repeat_reset, ke_alias_cross_keys,
		ke_cancelled_left_visual, ke_recreated_source, kl_layer_held_source, kl_right_slot_reset,
		kl_slot_keys_reset, ke_unentered_focus_press, ke_right_release_off, ke_middle_release_off,
		kl_layer_focus_keys, kl_begin_inactive_held, kp_repeated_focus_gain, kp_clear_last_source,
		kl_right_source_under_held_left,
		kl_inactive_held_left, kl_inactive_held_middle, kl_inactive_held_right,
		kc_end_after_left_cancel, kc_end_after_left_cancel_leave, kc_end_after_right_cancel,
		kc_outside_other_release, kc_release_all_walk, kl_outside_held_right, kl_outside_held_middle,
		kp_maximize_unminimizes, kc_end_cancel_residue, kl_begin_inactive_held_middle,
		ke_click_no_drag_end, kc_right_cancel_keeps_pos, kc_left_cancel_keeps_pos,
		ke_drag_rest_keeps_set_pos, kc_left_held_after_cancel,
		kc_end_cancel_residue_passing, kl_layer_return_late_up, kl_layer_return_held,
		kl_kept_moved_left, kl_kept_moved_right, kl_kept_begin_reset, kl_kept_end_reset,
		kl_kept_outer_end, kl_kept_focus_outside_slots, kl_kept_two_focus_begin,
		kl_kept_return_held_look, kl_kept_no_hold, kl_kept_released, kl_kept_v15,
		kl_kept_overlay_order, kl_kept_right_slot_reset, kl_kept_drag_end,
		kl_kept_return_cancel_left, kl_kept_return_cancel_right, kl_kept_begin_after_release,
		kl_kept_return_held_begin_left, kl_kept_return_held_begin_right,
		kl_kept_return_held_begin_middle, kl_kept_late_click_begin, kl_kept_other_held_right,
		kl_kept_other_held_middle, kl_kept_moved_keys, kl_kept_begin_twice, kl_kept_v13,
		kl_kept_right_held_since_begin, kl_kept_cancelled_residue, kl_kept_cancelled_moved,
		kl_kept_middle_held_since_begin, kl_kept_left_held_since_begin, kl_kept_return_repress,
		kl_kept_slot_and_focus_begin, kl_kept_return_held_no_pos, kl_kept_focus_held_back_left,
		kl_kept_focus_held_back_away, kl_kept_focus_held_back_right, kl_kept_erased_layer_held_on,
		kl_kept_erased_layer_held_away, kl_kept_outside_switch, kl_kept_begin_after_designation,
		kl_kept_moved_focus, kl_kept_return_cancel_middle};
	int failed = 0, n = sizeof(cases) / sizeof(*cases);
	for (int i = 0; i < n; i++) {
		fflush(stdout);
		pid_t pid = fork();
		if (pid < 0) {
			printf("KE%d FAIL (fork errno %d)\n", i + 1, errno);
			failed++;
			continue;
		}
		if (!pid) { ke_failures = 0; cases[i](); fflush(stdout); _exit(ke_failures ? 1 : 0); }
		int status = 0;
		pid_t waited;
		do {
			waited = waitpid(pid, &status, 0);
		} while (waited < 0 && errno == EINTR);
		if (waited != pid) {
			printf("KE%d FAIL (waitpid returned %ld, expected %ld, errno %d)\n",
				i + 1, (long)waited, (long)pid, waited < 0 ? errno : 0);
			failed++;
			continue;
		}
		bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (WIFSIGNALED(status))
			printf("  FAIL: child died with signal %d\n", WTERMSIG(status));
		printf("KE%d %s (status %d)\n", i + 1, ok ? "PASS" : "FAIL", status);
		failed += !ok;
	}
	printf("KEY-EVENTS %s: %d of %d cases failed\n", failed ? "FAIL" : "PASS", failed, n);
	return failed ? 1 : 0;
}

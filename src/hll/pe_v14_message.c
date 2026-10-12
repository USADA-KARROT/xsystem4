/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

/* v14 PartsEngine message queue (Dohna Dohna) — ported from the
 * v14-dohnadohna fork's verified semantics.
 *
 * Contract with the game bytecode (CPartsMessageManager, traced from the
 * CN .ain dump):
 *  - GetMessageType returns -1 when the queue is empty; the game polls
 *    until -1.
 *  - GetMessageType/GetMessagePartsNumber/GetMessageDelegateIndex/
 *    GetMessageUniqueID and GetMessageVariable* PEEK the same queue head.
 *    CallEventN copies variables before PopMessage, then invokes the delegate.
 *  - SeekMessage leaves the matching message at the head for normal dispatch.
 *  - PopMessage removes one message; ReleaseMessage clears the pending queue.
 *  - CallDelegate compares the message's uniqueID against the parts'
 *    registered event ID (SetEventID) and silently drops mismatches.
 *  - Message types (bytecode SWITCH): 4=mouse click, 5=double click.
 *    MouseClick's keyCode distinguishes left (1), right (2), middle (4).
 */

#include "system4/ain.h"
#include "system4/string.h"

#include "parts.h"
#include "../parts/parts_internal.h"
#include "hll.h"
#include "xsystem4.h"

extern struct static_library lib_PartsEngine;

#define V14_MSG_QUEUE_SIZE 64
struct v14_parts_message {
	int type;
	int parts_no;
	int delegate_index;
	int unique_id;
	int vars[4];
	int nr_vars;
};
static struct v14_parts_message msg_queue[V14_MSG_QUEUE_SIZE];
static int msg_head = 0, msg_tail = 0;

// A restart (system.Reset): the messages nobody took name parts that are gone.
void pe_v14_message_reset(void)
{
	msg_head = msg_tail = 0;
}

void parts_enqueue_message_vars(int type, int parts_no, int delegate_index, int unique_id,
                                int nr_vars, const int *vars)
{
	int next = (msg_tail + 1) % V14_MSG_QUEUE_SIZE;
	if (next == msg_head) {
		WARNING("parts message queue full, dropping message type=%d parts=%d", type, parts_no);
		return;
	}
	msg_queue[msg_tail].type = type;
	msg_queue[msg_tail].parts_no = parts_no;
	msg_queue[msg_tail].delegate_index = delegate_index;
	msg_queue[msg_tail].unique_id = unique_id;
	int nv = (nr_vars > 4) ? 4 : nr_vars;
	msg_queue[msg_tail].nr_vars = nv;
	for (int i = 0; i < nv; i++)
		msg_queue[msg_tail].vars[i] = vars[i];
	msg_tail = next;
}

void parts_enqueue_message(int type, int parts_no, int delegate_index, int unique_id)
{
	parts_enqueue_message_vars(type, parts_no, delegate_index, unique_id, 0, NULL);
}

static void PE_v14_PopMessage(void)
{
	if (msg_head != msg_tail)
		msg_head = (msg_head + 1) % V14_MSG_QUEUE_SIZE;
}

static void PE_v14_ReleaseMessage(void)
{
	msg_head = msg_tail;
}

// A popped message is no longer visible, including during nested dispatch.
static int PE_v14_GetMessageType(void)
{
	if (msg_head != msg_tail) {
		return msg_queue[msg_head].type;
	}
	return -1;
}

static int PE_v14_GetMessagePartsNumber(void)
{
	if (msg_head != msg_tail) {
		return msg_queue[msg_head].parts_no;
	}
	return 0;
}

static int PE_v14_GetMessageDelegateIndex(void)
{
	if (msg_head != msg_tail) {
		return msg_queue[msg_head].delegate_index;
	}
	return 0;
}

static int PE_v14_GetMessageUniqueID(void)
{
	if (msg_head != msg_tail) {
		return msg_queue[msg_head].unique_id;
	}
	return 0;
}

static void PE_v14_SeekMessage(int target_parts_no)
{
	// Advance through the queue until a message for the target parts is found.
	while (msg_head != msg_tail) {
		if (msg_queue[msg_head].parts_no == target_parts_no)
			return;
		msg_head = (msg_head + 1) % V14_MSG_QUEUE_SIZE;
	}
}

// CallEventN reads these values before it pops the message.
static int PE_v14_GetMessageVariableInt(int idx)
{
	if (msg_head != msg_tail) {
		if (idx < 0 || idx >= msg_queue[msg_head].nr_vars) return 0;
		return msg_queue[msg_head].vars[idx];
	}
	return 0;
}

static float PE_v14_GetMessageVariableFloat(int idx)
{
	union { int i; float f; } u = { .i = PE_v14_GetMessageVariableInt(idx) };
	return u.f;
}

static bool PE_v14_GetMessageVariableBool(int idx)
{
	return PE_v14_GetMessageVariableInt(idx) != 0;
}

static void PE_v14_GetMessageVariableString(possibly_unused int idx, struct string **out)
{
	// String variables are not carried in v14 messages.
	if (*out)
		free_string(*out);
	*out = string_ref(&EMPTY_STRING);
}

static struct string *PE_v14_GetMessageVariableString_ret(possibly_unused int idx)
{
	// The v14 declaration returns the string instead of taking an out argument.
	return string_ref(&EMPTY_STRING);
}

static void pe_v14_link_message_string_getter(struct static_library *lib)
{
	int libno = ain_get_library(ain, "PartsEngine");
	if (libno < 0)
		return;
	int fno = ain_get_library_function(ain, libno, "GetMessageVariableString");
	if (fno < 0)
		return;
	struct ain_hll_function *f = &ain->libraries[libno].functions[fno];
	if (!f->arguments || f->nr_arguments < 1 || f->arguments[0].type.data != AIN_INT)
		return;
	if (f->return_type.data == AIN_STRING && f->nr_arguments == 1) {
		static_library_replace(lib, "GetMessageVariableString", PE_v14_GetMessageVariableString_ret);
	} else if (f->return_type.data == AIN_VOID && f->nr_arguments == 2
			&& f->arguments[1].type.data == AIN_REF_STRING) {
		static_library_replace(lib, "GetMessageVariableString", PE_v14_GetMessageVariableString);
	}
}

static int PE_v14_GetMessageVariableCount(void)
{
	if (msg_head != msg_tail)
		return msg_queue[msg_head].nr_vars;
	return 0;
}

/* --- event registration --- */

static void PE_v14_SetEventID(int number, int delegate_index, int unique_id)
{
	PE_SetDelegateIndex(number, delegate_index);
	struct parts *p = parts_try_get(number);
	if (p)
		p->unique_id = unique_id;
}

static int PE_v14_GetUniqueID(int number)
{
	struct parts *p = parts_try_get(number);
	return p ? p->unique_id : 0;
}

/* --- controller accessors over the upstream ctrl_stack --- */

static int PE_v14_GetActiveController(void)
{
	return PE_get_active_controller();
}

static void PE_v14_SetActiveController(int controller)
{
	PE_set_active_controller(controller);
}

static int PE_v14_GetControllerLength(void)
{
	return ctrl_stack.nr_controllers;
}

// The ID of the controller at a position of the stack (0x53d800): a negative
// index means the active controller's position, one past the end the top
// controller. -1 only while there is no controller at all.
static int PE_v14_GetControllerID(int index)
{
	if (!ctrl_stack.nr_controllers)
		return -1;
	if (index < 0) {
		index = parts_controller_index(ctrl_stack.active);
		if (index < 0)
			index = ctrl_stack.nr_controllers - 1;
	}
	return ctrl_stack.ids[min(index, ctrl_stack.nr_controllers - 1)];
}

// The position of a controller in the stack, -1 when there is no such
// controller (0x53fe30).
static int PE_v14_GetControllerIndex(int controller)
{
	return parts_controller_index(controller);
}

static void PE_v14_MoveController(int controller, int index)
{
	parts_controller_move(controller, index);
}

static int PE_v14_GetSystemOverlayController(void)
{
	return PARTS_CONTROLLER_SYSTEM_OVERLAY;
}

/* --- Message window (v14 ADV dialogue) --- thin adapters over the
 * upstream parts text/CG API, from the fork's verified behaviour. */

static void pe_v14_trace_window(const char *operation, const char *phase,
		int parts_no, int text_size, int active)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p) {
		WARNING("S2 message %s %s parts=%d bytes=%d active=%d missing", operation,
			phase, parts_no, text_size, active);
		return;
	}
	WARNING("S2 message %s %s parts=%d bytes=%d active=%d state=%d type=%d default_type=%d show=%d alpha=%d pos=(%d,%d)",
		operation, phase, parts_no, text_size, active, p->state,
		p->states[p->state].type, p->states[PARTS_STATE_DEFAULT].type,
		p->global.show, p->global.alpha, p->global.pos.x, p->global.pos.y);
}

// The first 12 calls of each operation are traced; a call on a parts that
// does not exist is traced past that, with its own bound, since the script
// writing to a window whose parts went (its layer was erased) is what the
// trace is for.
static bool pe_v14_trace_window_enabled(unsigned *count, int parts_no)
{
	static unsigned missing_count;
	if (!getenv("XSYS4_STAGE2_TRACE"))
		return false;
	if ((*count)++ < 12)
		return true;
	return !parts_try_get(parts_no) && missing_count++ < 20;
}

static void PE_v14_SetMessageWindowActive(int parts_no, bool active)
{
	static unsigned trace_count;
	bool trace = pe_v14_trace_window_enabled(&trace_count, parts_no);
	if (trace) pe_v14_trace_window("Active", "before", parts_no, -1, active);
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return;
	parts->message_window = true;
	PE_SetShow(parts_no, active);
	if (trace) pe_v14_trace_window("Active", "after", parts_no, -1, active);
}

static void PE_v14_SetMessageWindowText(int parts_no, struct string *text,
		int msg_num, struct string *func_name, int ver, int step)
{
	static unsigned trace_count;
	bool trace = pe_v14_trace_window_enabled(&trace_count, parts_no);
	int text_size = text ? text->size : -1;
	if (trace) pe_v14_trace_window("Text", "before", parts_no, text_size, -1);
	PE_SetMessageWindowText(parts_no, text, msg_num, func_name, ver, step);
	if (trace) pe_v14_trace_window("Text", "after", parts_no, text_size, -1);
}

static void PE_v14_FixMessageWindowText(possibly_unused int parts_no)
{
	// The text is laid out when it is next drawn (message_window.c).
}

static bool PE_v14_IsFixedMessageWindowText(possibly_unused int parts_no)
{
	return true;
}

static void PE_v14_SetMessageWindowCGName(int parts_no, struct string *name)
{
	static unsigned trace_count;
	bool trace = pe_v14_trace_window_enabled(&trace_count, parts_no);
	int name_size = name ? name->size : -1;
	if (trace) pe_v14_trace_window("CGName", "before", parts_no, name_size, -1);
	PE_SetMessageWindowCGName(parts_no, name);
	if (trace) pe_v14_trace_window("CGName", "after", parts_no, name_size, -1);
}

static void PE_v14_SetMessageWindowTextArea(int parts_no, int x, int y, int w, int h)
{
	PE_SetMessageWindowTextArea(parts_no, x, y, w, h);
}

static void PE_v14_SetMessageWindowTextFont(int parts_no, int type, int size,
		int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight)
{
	PE_SetMessageWindowTextFont(parts_no, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight);
}

static void PE_v14_SetMessageWindowTextSpace(int parts_no, int letter_space, int line_space)
{
	PE_SetMessageWindowTextSpace(parts_no, letter_space, line_space);
}

static void PE_v14_SetKeyWaitShow(int parts_no, bool show)
{
	PE_SetKeyWaitShow(parts_no, show);
}

/* void GetKeyWaitCGName(int Number, wrap<string> Name, wrap<int> StartNo,
 * wrap<int> NumofCG, wrap<int> TimePerCG): native case 514 -> 0x595410.
 * Nothing is written without a window. */
static void PE_v14_GetKeyWaitCGName(int parts_no, int name_slot, int *start_no,
		int *nr_cg, int *time_per_cg)
{
	struct string *name = NULL;
	if (PE_GetKeyWaitCGName(parts_no, &name, start_no, nr_cg, time_per_cg))
		wrap_slot_set_string(name_slot, name);
}

/* --- click/input state queries (v14 WaitForClick contract) ---
 * GetActiveParts (0x57bc90 -> 0x53c6c0) is the parts of the input scope
 * whose widget reports itself active (+0x280, found anew by every parts
 * update, 0x53c240 -> 0x538180): a widget is active while one of its
 * elements is the focus of the input, the target of the last press (the
 * element's +0x1b4; SpriteParts 0x564cb0, a button or a check box through
 * its inner parts 0x4f2ff0). The game's IsClickableParts(GetActiveParts())
 * keeps a release on a clickable parts from stepping the message, and
 * IsInputPartsActive asks for a text box (types 4, 7, 11). This port's value
 * follows the left button alone and is reset at other moments than the
 * original's; see PE_GetActivePartsNumber.
 *
 * GetClickNumber is the parts that got the left button's MouseClick in the
 * last input update, 0 otherwise (the element's +5, written at 0x5789cb in
 * the key 1 branch of 0x5788d0 alone; see v14_update_buttons). A right or
 * middle click of a parts does not set it in the original, and this port
 * sends no such click at all. */

static int PE_v14_GetActiveParts(void)
{
	return PE_GetActivePartsNumber();
}

static int PE_v14_GetClickNumber(void)
{
	return PE_GetClickPartsNumber();
}

static void PE_v14_SetClickMissSoundName(possibly_unused struct string *name) {}
static struct string *PE_v14_GetClickMissSoundName(void) { return string_ref(&EMPTY_STRING); }
static void PE_v14_StopSoundWithoutSystemSound(void) {}
static void PE_v14_SetEnableInput(possibly_unused bool enable) {}
static bool PE_v14_IsEnableInput(void) { return true; }
static void PE_v14_SetWantSaveBackScene(possibly_unused int n, possibly_unused bool enable) {}
static bool PE_v14_IsWantSaveBackScene(possibly_unused int n) { return false; }
static void PE_v14_SaveThumbnail(possibly_unused struct string *name, possibly_unused int width) {}

/* PartsEngine.SaveThumbnail(string SaveFileName, int ThumbnailWidth) -> bool
 * (original 0x56d140) under the declaration the original has; any other
 * shape keeps the empty function. */
static void *save_thumbnail_binding(void)
{
	int lib = ain_get_library(ain, "PartsEngine");
	int fno = lib >= 0 ? ain_get_library_function(ain, lib, "SaveThumbnail") : -1;
	if (fno < 0 || ain->version < 14)
		return PE_v14_SaveThumbnail;
	const struct ain_hll_function *f = &ain->libraries[lib].functions[fno];
	if (f->return_type.data != AIN_BOOL || f->nr_arguments != 2 || !f->arguments
	    || f->arguments[0].type.data != AIN_STRING || f->arguments[1].type.data != AIN_INT)
		return PE_v14_SaveThumbnail;
	return PE_SaveThumbnail;
}

/* Replace existing bindings with v14 semantics. Must run at _PreLink:
 * static_library_replace edits the static export table, which
 * link_libraries() copies into the runtime table — replacing after the
 * link has no effect. */
void pe_v14_message_replace(void)
{
	struct static_library *lib = &lib_PartsEngine;
	WARNING("V14: replacing PartsEngine message bindings (PreLink)");
	static_library_replace(lib, "PopMessage", PE_v14_PopMessage);
	static_library_replace(lib, "ReleaseMessage", PE_v14_ReleaseMessage);
	static_library_replace(lib, "GetMessageType", PE_v14_GetMessageType);
	static_library_replace(lib, "GetMessagePartsNumber", PE_v14_GetMessagePartsNumber);
	static_library_replace(lib, "GetMessageDelegateIndex", PE_v14_GetMessageDelegateIndex);
	static_library_replace(lib, "GetMessageVariableCount", PE_v14_GetMessageVariableCount);
	static_library_replace(lib, "GetMessageVariableInt", PE_v14_GetMessageVariableInt);
	static_library_replace(lib, "GetMessageVariableFloat", PE_v14_GetMessageVariableFloat);
	static_library_replace(lib, "GetMessageVariableBool", PE_v14_GetMessageVariableBool);
	pe_v14_link_message_string_getter(lib);
}

/* The message text fade and SetEnableSpeedUp take the place of the empty
 * functions only under the declaration the original has: the library of
 * another game may declare one of these names otherwise, and libffi builds
 * the call from the declaration. An unknown shape, a missing declaration and
 * a program before v14 keep the binding they had. */
static void *message_fade_select_function(const struct ain_hll_function *f, void *fallback)
{
	static const struct {
		const char *name;
		enum ain_data_type ret;
		int nr_args;
		enum ain_data_type args[2];
		void *fun;
	} shapes[] = {
		{ "SetMessageWindowTextSpeed", AIN_VOID, 2, { AIN_INT, AIN_INT }, PE_SetMessageWindowTextSpeed },
		{ "GetMessageWindowTextSpeed", AIN_INT, 1, { AIN_INT }, PE_GetMessageWindowTextSpeed },
		{ "FixMessageWindowText", AIN_VOID, 1, { AIN_INT }, PE_FixMessageWindowText },
		{ "IsFixedMessageWindowText", AIN_BOOL, 1, { AIN_INT }, PE_IsFixedMessageWindowText },
		{ "SetEnableSpeedUp", AIN_VOID, 2, { AIN_INT, AIN_BOOL }, PE_SetEnableSpeedUp },
		{ "IsEnableSpeedUp", AIN_BOOL, 1, { AIN_INT }, PE_IsEnableSpeedUp },
	};
	if (ain->version < 14 || !f->name || (f->nr_arguments && !f->arguments))
		return fallback;
	for (unsigned i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
		if (strcmp(f->name, shapes[i].name))
			continue;
		if (f->nr_arguments != shapes[i].nr_args || f->return_type.data != shapes[i].ret)
			return fallback;
		for (int j = 0; j < f->nr_arguments; j++) {
			if (f->arguments[j].type.data != shapes[i].args[j])
				return fallback;
		}
		return shapes[i].fun;
	}
	return fallback;
}

/* static_library_register links the function it is given without going
 * through the overload selection of the export table, so the choice by the
 * game's declaration is made here. */
static void *message_fade_binding(const char *name, void *fallback)
{
	int lib = ain_get_library(ain, "PartsEngine");
	int fno = lib >= 0 ? ain_get_library_function(ain, lib, name) : -1;
	if (fno < 0)
		return fallback;
	return message_fade_select_function(&ain->libraries[lib].functions[fno], fallback);
}

/* Register brand-new v14 functions (not in the upstream export table).
 * Must run at _PostLink: static_library_register writes into the runtime
 * library table, which only exists after link_libraries(). */
void pe_v14_message_register(void)
{
	struct static_library *lib = &lib_PartsEngine;
	WARNING("V14: registering PartsEngine v14 message functions (PostLink)");
	static_library_register(lib, "GetMessageUniqueID", PE_v14_GetMessageUniqueID);
	static_library_register(lib, "SeekMessage", PE_v14_SeekMessage);
	static_library_register(lib, "SetEventID", PE_v14_SetEventID);
	static_library_register(lib, "GetUniqueID", PE_v14_GetUniqueID);
	static_library_register(lib, "GetActiveController", PE_v14_GetActiveController);
	static_library_register(lib, "SetActiveController", PE_v14_SetActiveController);
	static_library_register(lib, "GetControllerLength", PE_v14_GetControllerLength);
	static_library_register(lib, "GetControllerID", PE_v14_GetControllerID);
	static_library_register(lib, "GetControllerIndex", PE_v14_GetControllerIndex);
	static_library_register(lib, "MoveController", PE_v14_MoveController);
	static_library_register(lib, "GetSystemOverlayController", PE_v14_GetSystemOverlayController);
	static_library_register(lib, "SetMessageWindowActive", PE_v14_SetMessageWindowActive);
	static_library_register(lib, "SetMessageWindowText", PE_v14_SetMessageWindowText);
	static_library_register(lib, "FixMessageWindowText",
		message_fade_binding("FixMessageWindowText", PE_v14_FixMessageWindowText));
	static_library_register(lib, "IsFixedMessageWindowText",
		message_fade_binding("IsFixedMessageWindowText", PE_v14_IsFixedMessageWindowText));
	// Ahead of the empty ones of pe_v14_prelink.h, which stay for a
	// declaration of another shape: the first registration is the one kept.
	static const char *const fade_names[] = {
		"SetMessageWindowTextSpeed", "GetMessageWindowTextSpeed",
		"SetEnableSpeedUp", "IsEnableSpeedUp",
	};
	for (unsigned i = 0; i < sizeof(fade_names) / sizeof(fade_names[0]); i++) {
		void *fun = message_fade_binding(fade_names[i], NULL);
		if (fun)
			static_library_register(lib, fade_names[i], fun);
	}
	static_library_register(lib, "SetMessageWindowCGName", PE_v14_SetMessageWindowCGName);
	static_library_register(lib, "SetMessageWindowTextArea", PE_v14_SetMessageWindowTextArea);
	static_library_register(lib, "GetMessageWindowTextArea", PE_GetMessageWindowTextArea);
	static_library_register(lib, "SetMessageWindowTextFont", PE_v14_SetMessageWindowTextFont);
	static_library_register(lib, "SetMessageWindowTextSpace", PE_v14_SetMessageWindowTextSpace);
	static_library_register(lib, "SetKeyWaitShow", PE_v14_SetKeyWaitShow);
	static_library_register(lib, "IsKeyWaitShow", PE_IsKeyWaitShow);
	// The rest of the key wait mark (native cases 513 to 520), ahead of the
	// empty ones of pe_v14_prelink.h: the first registration is the one kept.
	static_library_register(lib, "SetKeyWaitCGName", PE_SetKeyWaitCGName);
	static_library_register(lib, "GetKeyWaitCGName", PE_v14_GetKeyWaitCGName);
	static_library_register(lib, "SetKeyWaitFlatName", PE_SetKeyWaitFlatName);
	static_library_register(lib, "GetKeyWaitFlatName", PE_GetKeyWaitFlatName);
	static_library_register(lib, "SetKeyWaitPos", PE_SetKeyWaitPos);
	static_library_register(lib, "GetKeyWaitPosX", PE_GetKeyWaitPosX);
	static_library_register(lib, "GetKeyWaitPosY", PE_GetKeyWaitPosY);
	static_library_register(lib, "GetKeyWaitPosZ", PE_GetKeyWaitPosZ);
	static_library_register(lib, "GetActiveParts", PE_v14_GetActiveParts);
	static_library_register(lib, "GetClickNumber", PE_v14_GetClickNumber);
	static_library_register(lib, "SetClickMissSoundName", PE_v14_SetClickMissSoundName);
	static_library_register(lib, "GetClickMissSoundName", PE_v14_GetClickMissSoundName);
	static_library_register(lib, "StopSoundWithoutSystemSound", PE_v14_StopSoundWithoutSystemSound);
	static_library_register(lib, "SetEnableInput", PE_v14_SetEnableInput);
	static_library_register(lib, "IsEnableInput", PE_v14_IsEnableInput);
	static_library_register(lib, "SetWantSaveBackScene", PE_v14_SetWantSaveBackScene);
	static_library_register(lib, "IsWantSaveBackScene", PE_v14_IsWantSaveBackScene);
	static_library_register(lib, "SaveThumbnail", save_thumbnail_binding());
}

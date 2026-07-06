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
 *    GetMessageUniqueID always PEEK the queue head. PopMessage advances
 *    the head and stores the popped message in msg_current, which
 *    GetMessageVariable* then read (CallEventN reads variables after
 *    popping).
 *  - ReleaseMessage discards a single message, not the whole queue.
 *  - CallDelegate compares the message's uniqueID against the parts'
 *    registered event ID (SetEventID) and silently drops mismatches.
 *  - Message types (bytecode SWITCH): 1=button click (per part),
 *    5=mouse left click (also whole-screen when parts_no=0, which fires
 *    WholeMouseLClickEvent for scene navigation).
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
static struct v14_parts_message msg_current = { .type = -1 };

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
	if (msg_head != msg_tail) {
		msg_current = msg_queue[msg_head];
		msg_head = (msg_head + 1) % V14_MSG_QUEUE_SIZE;
	} else {
		msg_current.type = -1;
		msg_current.parts_no = 0;
	}
}

static void PE_v14_ReleaseMessage(void)
{
	if (msg_head != msg_tail) {
		msg_head = (msg_head + 1) % V14_MSG_QUEUE_SIZE;
	}
	msg_current.type = -1;
	msg_current.parts_no = 0;
	msg_current.nr_vars = 0;
}

// Always peek the queue head (not msg_current): msg_current holds the
// already-processed message after PopMessage; returning its type would
// re-fire the same handler in nested UpdateMessage calls.
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

static bool PE_v14_SeekMessage(int target_parts_no)
{
	// Advance through the queue until a message for the target parts is found.
	while (msg_head != msg_tail) {
		if (msg_queue[msg_head].parts_no == target_parts_no) {
			msg_current = msg_queue[msg_head];
			msg_head = (msg_head + 1) % V14_MSG_QUEUE_SIZE;
			return true;
		}
		msg_head = (msg_head + 1) % V14_MSG_QUEUE_SIZE;
	}
	msg_current.type = -1;
	return false;
}

// After PopMessage, msg_current holds the popped message; before, peek head.
static int PE_v14_GetMessageVariableInt(int idx)
{
	if (msg_current.type >= 0) {
		if (idx < 0 || idx >= msg_current.nr_vars) return 0;
		return msg_current.vars[idx];
	}
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

static int PE_v14_GetMessageVariableCount(void)
{
	if (msg_current.type >= 0)
		return msg_current.nr_vars;
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
	return ctrl_stack.active;
}

static void PE_v14_SetActiveController(int controller)
{
	PE_set_active_controller(controller);
}

static int PE_v14_GetControllerLength(void)
{
	return ctrl_stack.nr_controllers;
}

static int PE_v14_GetSystemOverlayController(void)
{
	return PARTS_CONTROLLER_SYSTEM_OVERLAY;
}

/* --- Message window (v14 ADV dialogue) --- thin adapters over the
 * upstream parts text/CG API, from the fork's verified behaviour. */

static void PE_v14_SetMessageWindowActive(int parts_no, bool active)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return;
	parts->message_window = true;
	PE_SetShow(parts_no, active);
}

static void PE_v14_SetMessageWindowText(int parts_no, struct string *text,
		int msg_num, struct string *func_name, int ver, int step)
{
	(void)msg_num; (void)func_name; (void)ver; (void)step;
	PE_SetText(parts_no, text, 1); // DEFAULT state, 1-based
}

static void PE_v14_FixMessageWindowText(possibly_unused int parts_no)
{
	// Text renders synchronously in SetMessageWindowText.
}

static bool PE_v14_IsFixedMessageWindowText(possibly_unused int parts_no)
{
	return true;
}

static void PE_v14_SetMessageWindowCGName(int parts_no, struct string *name)
{
	PE_SetPartsCG(parts_no, name, 0, 1);
}

static void PE_v14_SetMessageWindowTextArea(int parts_no, int x, int y, int w, int h)
{
	PE_SetPartsTextSurfaceArea(parts_no, x, y, w, h, 1);
}

static void PE_v14_SetMessageWindowTextFont(int parts_no, int type, int size,
		int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight)
{
	PE_SetFont(parts_no, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, 1);
}

static void PE_v14_SetMessageWindowTextSpace(int parts_no, int letter_space, int line_space)
{
	PE_SetTextCharSpace(parts_no, letter_space, 1);
	PE_SetTextLineSpace(parts_no, line_space, 1);
}

static void PE_v14_SetKeyWaitShow(int parts_no, bool show)
{
	PE_SetShow(parts_no, show);
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
	static_library_replace(lib, "GetMessageVariableString", PE_v14_GetMessageVariableString);
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
	static_library_register(lib, "GetSystemOverlayController", PE_v14_GetSystemOverlayController);
	static_library_register(lib, "SetMessageWindowActive", PE_v14_SetMessageWindowActive);
	static_library_register(lib, "SetMessageWindowText", PE_v14_SetMessageWindowText);
	static_library_register(lib, "FixMessageWindowText", PE_v14_FixMessageWindowText);
	static_library_register(lib, "IsFixedMessageWindowText", PE_v14_IsFixedMessageWindowText);
	static_library_register(lib, "SetMessageWindowCGName", PE_v14_SetMessageWindowCGName);
	static_library_register(lib, "SetMessageWindowTextArea", PE_v14_SetMessageWindowTextArea);
	static_library_register(lib, "SetMessageWindowTextFont", PE_v14_SetMessageWindowTextFont);
	static_library_register(lib, "SetMessageWindowTextSpace", PE_v14_SetMessageWindowTextSpace);
	static_library_register(lib, "SetKeyWaitShow", PE_v14_SetKeyWaitShow);
}

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

#ifndef SYSTEM4_VM_H
#define SYSTEM4_VM_H

#include <stdbool.h>
#include <stdint.h>
#include "system4.h"

struct ain;
enum ain_data_type;
struct page;
struct string;

// xsystem4-specific system calls (used for hacks)
enum vm_extra_syscall {
	VM_XSYS_KEY_IS_DOWN = 0x80000000,
};

// Non-heap values. Stored in pages and on the stack.
union vm_value {
	int32_t i;
	float f;
	void *ref; // for casting HLL return value
};

struct static_hll_function {
	char *name;
	void *fun;
};

struct static_library {
	char *name;
	struct static_hll_function functions[];
};

extern struct ain *ain;

extern union vm_value *stack;
extern int32_t stack_ptr;

static inline union vm_value _vm_id(union vm_value v)
{
	return v;
}

static inline union vm_value vm_int(int32_t v)
{
	return (union vm_value) { .i = v };
}

static inline union vm_value vm_bool(bool b)
{
	return (union vm_value) { .i = !!b };
}

static inline union vm_value vm_float(float v)
{
	return (union vm_value) { .f = v };
}

#define vm_value_cast(v) _Generic((v),				\
				  union vm_value: _vm_id,	\
				  int32_t: vm_int,		\
				  bool: vm_bool,		\
				  float: vm_float)(v)

static inline void stack_set_value(int n, union vm_value v)
{
	stack[stack_ptr - (1 + n)] = v;
}

static inline void stack_push_value(union vm_value v)
{
	stack[stack_ptr++] = v;
}

// Set the Nth value from the top of the stack to V.
#define stack_set(n, v) (stack_set_value((n), vm_value_cast(v)))
#define stack_push(v) (stack_push_value(vm_value_cast(v)))
union vm_value stack_pop(void);
union vm_value stack_peek(int n);
union vm_value *stack_peek_var(void);

union vm_value local_get(int varno);
union vm_value global_get(int varno);
union vm_value member_get(int varno);
void global_set(int varno, union vm_value val, bool call_dtors);
struct page *local_page(void);
struct page *get_local_page(int frame_no);
struct page *get_struct_page(int frame_no);

int vm_string_ref(struct string *s);
int vm_copy_page(struct page *page);
union vm_value vm_copy(union vm_value v, enum ain_data_type type);

int vm_execute_ain(struct ain *program);
void vm_init_globals(void);
void vm_call(int fno, int struct_page);
void vm_call_nopop(int fno, int nargs);
int vm_construct_struct(int struct_type);
int vm_time(void);
void vm_sleep(int ms);

void hll_call(int libno, int fno, int hll_arg3);
// v14 generic (hll_param) element of a CALLHLL type operand, see ffi.c
bool hll_generic_is_option(int hll_arg3);
int hll_generic_slots(int hll_arg3);
bool library_exists(int libno);
bool library_function_exists(int libno, int fno);
void init_libraries(void);
void exit_libraries(void);

// NOTE: This can probably be merged with ain_data_type, since the values are
// disjoint.
enum string_format_type {
	STRFMT_INT = 2,
	STRFMT_FLOAT = 3,
	STRFMT_STRING = 4,
	STRFMT_BOOL = 48,
	STRFMT_LONG_INT = 56,
};
struct string *string_format(struct string *fmt, union vm_value arg, enum string_format_type type);

void vm_stack_trace(void);
_Noreturn void _vm_error(const char *fmt, ...);
_Noreturn void vm_exit(int code);
_Noreturn void vm_reset(void);
void vm_reset_runtime_state(void);

extern bool vm_reset_once;
// system.Reset asks for a restart with this; the VM takes it after the call.
extern bool vm_reset_pending;
// system.Exit asks for the end of the program with this, in the same way.
extern bool vm_exit_pending;

enum vm_ask_type {
	VM_ASK_OK_CANCEL,
	VM_ASK_YES_NO,
};
// A modal question with two buttons; true for OK or Yes. A NULL text asks
// whether to quit the game. unshown is the answer when the box cannot be
// shown. See vm.c for when nobody is asked.
bool vm_ask(enum vm_ask_type type, const char *text, bool unshown);
// An engine image that cannot be read back: tells the player which file
// (path in the game's encoding) and ends the program. See vm.c.
_Noreturn void vm_image_unreadable(const char *path);

#if defined(__ANDROID__) || defined(RELEASE)
// Report the error with a message box and exit.
#define VM_ERROR ERROR
#else
#define VM_ERROR(fmt, ...) \
	_vm_error("*ERROR*(%s:%s:%d): " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)
#endif

#ifdef VM_PRIVATE
#include "system4/little_endian.h"
#include "system4/ain.h"

struct function_call {
	int32_t fno;
	uint32_t call_address;
	uint32_t return_address;
	int32_t page_slot;
	int32_t struct_page;
	int32_t base_sp;        // stack pointer after args popped (diagnostic)
	int32_t env_page;       // v14: closure environment (enclosing function's local page)
	int32_t delegate_obj_ref; // v14 callback owns these pins until frame removal
	int32_t delegate_env_ref;
	bool is_method;         // v14: struct_page left on stack below base_sp
	bool is_delegate_call;  // called via delegate: skip return-value stack adjustment
	int32_t dg_return_slots; // delegate-call: expected return slots (from delegate type)
	// Read back from an engine image (vm_resume_frames_loaded): base_sp is
	// not known, so function_return takes what the function returns from
	// the top of the stack and leaves the values below it.
	bool resumed;
	// A resumed delegate call whose delegate's cursor and limit were not in
	// the image: they are set from the stack when the frame returns.
	bool dg_resume_pending;
	// A resumed delegate call: the delegate it goes through, if the image
	// named it (0 otherwise), so that a later image can name it again.
	int32_t resume_dg_page;
};

extern struct function_call call_stack[4096];
extern int32_t call_stack_ptr;

/*
 * What an engine image keeps of a call frame besides its function, return
 * address, local page and struct page (resume.c writes and reads it; see
 * vm_resume_frames_loaded). `present` is false when the image has none for
 * the frame (an image of an earlier build).
 */
/* A non-delegate C method uses the otherwise unused delegate-page field. */
#define RESUME_C_METHOD_MARKER INT32_MIN
struct resume_frame_extra {
	bool present;
	int32_t fno;			// checked against the frame read back
	int32_t env_page;
	int32_t delegate_obj_ref, delegate_env_ref;
	int32_t dg_page;		// a delegate call: its delegate (0: not known)
	int32_t dg_cursor, dg_limit;	// and that delegate's cursor and limit
	bool c_method_present, c_method;	// explicit VM_RETURN method flag, if saved
};

// The extra fields of frame i of the call stack, for an image being written.
void vm_resume_frame_extra(int i, struct resume_frame_extra *out);
// Once an image is back (heap, call stack and stack): makes the port's own
// frame fields again. extra is NULL or has one entry per frame.
void vm_resume_frames_loaded(const struct resume_frame_extra *extra);

// the guard page of heap slot 0: an array page, element type void, 64 slots
struct page;
struct page *vm_alloc_guard_page(void);

// Diagnostics (XSYS4_RESUME_TRACE=1): the call frames of an image being
// written ("save") or just read back ("load"); see vm.c.
void vm_resume_trace(const char *tag, const char *path);

extern size_t instr_ptr;

// Read argument N for the current instruction.
static inline int32_t get_argument(int n)
{
	return LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
}

// XXX: not strictly portable
static inline float get_argument_float(int n)
{
	union vm_value v;
	v.i = LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
	return v.f;
}

uint32_t get_switch_address(int no, int val);
uint32_t get_strswitch_address(int no, struct string *str);

int vm_save_image(const char *key, const char *path);
// True once the image is back; false (the VM as it was) for an image of a
// rsm version libsys4 does not read. Any other failure does not return (see
// resume.c).
bool vm_load_image(const char *key, const char *path);
struct page *vm_load_image_comments(const char *key, const char *path, int *success);
int vm_write_image_comments(const char *key, const char *path, struct page *comments);

#endif /* VM_PRIVATE */
#endif /* SYSTEM4_VM_H */

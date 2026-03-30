/* Copyright (C) 2024 <nunuhara@haniwa.technology>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/string.h"

#include "hll.h"
#include "xsystem4.h"

/*
 * ADVEngine — bytecode-driven ADV scene step parser for AIN v14.
 *
 * Scene functions contain a linear sequence of:
 *   S_PUSH str   — push string argument
 *   PUSH int     — push int argument
 *   F_PUSH float — push float argument
 *   MSG idx      — message reference (CodeType=2)
 *   CALLFUNC no  — function call (CodeType=1)
 *   RETURN       — end
 *
 * ADVEngine.Load(FunctionName) parses a scene function's bytecode into steps.
 * Each step is either a message (MSG) or a function call (CALLFUNC) with
 * the preceding push instructions as arguments.
 */

/* Step code types (matching game's switch in func#3257) */
enum adv_code_type {
	ADV_CODE_FUNCTION = 1,  /* CALLFUNC step */
	ADV_CODE_MESSAGE  = 2,  /* MSG step */
	ADV_CODE_IF       = 3,  /* conditional (txtex only) */
	ADV_CODE_BLOCK_BEGIN = 4,
	ADV_CODE_BLOCK_END   = 5,
};

/* Argument types */
enum adv_arg_type {
	ADV_ARG_INT,
	ADV_ARG_FLOAT,
	ADV_ARG_STRING,
	ADV_ARG_BOOL,
};

struct adv_arg {
	enum adv_arg_type type;
	union {
		int i;
		float f;
		struct string *s;
	};
};

struct adv_step {
	enum adv_code_type code_type;
	int msg_idx;           /* for MESSAGE steps: index into ain->messages */
	int func_no;           /* for FUNCTION steps: function number */
	struct string *text;   /* step text representation */
	int nr_args;
	struct adv_arg *args;
};

static struct {
	struct adv_step *steps;
	int nr_steps;
	int capacity;
} adv_state = { NULL, 0, 0 };

static void adv_free_step(struct adv_step *step)
{
	if (step->text)
		free_string(step->text);
	for (int i = 0; i < step->nr_args; i++) {
		if (step->args[i].type == ADV_ARG_STRING && step->args[i].s)
			free_string(step->args[i].s);
	}
	free(step->args);
}

static void adv_clear(void)
{
	for (int i = 0; i < adv_state.nr_steps; i++)
		adv_free_step(&adv_state.steps[i]);
	free(adv_state.steps);
	adv_state.steps = NULL;
	adv_state.nr_steps = 0;
	adv_state.capacity = 0;
}

static struct adv_step *adv_add_step(void)
{
	if (adv_state.nr_steps >= adv_state.capacity) {
		adv_state.capacity = adv_state.capacity ? adv_state.capacity * 2 : 256;
		adv_state.steps = xrealloc(adv_state.steps,
			adv_state.capacity * sizeof(struct adv_step));
	}
	struct adv_step *step = &adv_state.steps[adv_state.nr_steps++];
	memset(step, 0, sizeof(*step));
	step->msg_idx = -1;
	step->func_no = -1;
	return step;
}

static struct adv_step *adv_get_step(int step_no)
{
	if (step_no < 0 || step_no >= adv_state.nr_steps)
		return NULL;
	return &adv_state.steps[step_no];
}

/* Pending arguments accumulator */
struct arg_accum {
	struct adv_arg *args;
	int nr_args;
	int capacity;
};

static void accum_init(struct arg_accum *acc)
{
	acc->args = NULL;
	acc->nr_args = 0;
	acc->capacity = 0;
}

static void accum_push(struct arg_accum *acc, struct adv_arg *arg)
{
	if (acc->nr_args >= acc->capacity) {
		acc->capacity = acc->capacity ? acc->capacity * 2 : 8;
		acc->args = xrealloc(acc->args, acc->capacity * sizeof(struct adv_arg));
	}
	acc->args[acc->nr_args++] = *arg;
}

static void accum_flush_to_step(struct arg_accum *acc, struct adv_step *step)
{
	step->nr_args = acc->nr_args;
	step->args = acc->args;
	acc->args = NULL;
	acc->nr_args = 0;
	acc->capacity = 0;
}

static void accum_clear(struct arg_accum *acc)
{
	for (int i = 0; i < acc->nr_args; i++) {
		if (acc->args[i].type == ADV_ARG_STRING && acc->args[i].s)
			free_string(acc->args[i].s);
	}
	free(acc->args);
	accum_init(acc);
}

/* Build step text from function name and arguments */
static struct string *build_step_text(const char *func_name, struct adv_step *step)
{
	/* Format: "funcname(arg1, arg2, ...)" */
	struct string *s = cstr_to_string(func_name);
	if (step->nr_args > 0) {
		string_append_cstr(&s, "(", 1);
		for (int i = 0; i < step->nr_args; i++) {
			if (i > 0)
				string_append_cstr(&s, ", ", 2);
			switch (step->args[i].type) {
			case ADV_ARG_STRING:
				string_append_cstr(&s, "\"", 1);
				string_append(&s, step->args[i].s);
				string_append_cstr(&s, "\"", 1);
				break;
			case ADV_ARG_INT: {
				char buf[32];
				snprintf(buf, sizeof(buf), "%d", step->args[i].i);
				string_append_cstr(&s, buf, strlen(buf));
				break;
			}
			case ADV_ARG_FLOAT: {
				char buf[32];
				snprintf(buf, sizeof(buf), "%g", step->args[i].f);
				string_append_cstr(&s, buf, strlen(buf));
				break;
			}
			case ADV_ARG_BOOL:
				string_append_cstr(&s, step->args[i].i ? "true" : "false",
					step->args[i].i ? 4 : 5);
				break;
			}
		}
		string_append_cstr(&s, ")", 1);
	}
	return s;
}

/* Read opcode at given bytecode address */
static inline int adv_get_opcode(uint32_t addr)
{
	return LittleEndian_getW(ain->code, addr);
}

/* Read the nth argument of instruction at given bytecode address */
static inline int32_t adv_get_arg(uint32_t addr, int n)
{
	return LittleEndian_getDW(ain->code, addr + 2 + n * 4);
}

/*
 * Parse a scene function's bytecode into ADV steps.
 * Returns true on success.
 */
static bool adv_parse_function(int func_no)
{
	if (func_no < 0 || func_no >= ain->nr_functions)
		return false;

	struct ain_function *func = &ain->functions[func_no];
	uint32_t ip = func->address;

	/* Skip FUNC opcode (2 bytes opcode + 4 bytes arg) */
	int opcode = adv_get_opcode(ip);
	if ((opcode & ~0xC000) == FUNC)
		ip += instruction_width(FUNC);
	else {
		WARNING("ADVEngine: function %d does not start with FUNC opcode", func_no);
		return false;
	}

	struct arg_accum acc;
	accum_init(&acc);

	/* Scan bytecode for MSG and CALLFUNC instructions */
	while (ip < ain->code_size) {
		opcode = adv_get_opcode(ip);
		int masked = opcode & ~0xC000;

		switch (masked) {
		case S_PUSH: {
			int str_idx = adv_get_arg(ip, 0);
			if (str_idx >= 0 && str_idx < ain->nr_strings) {
				struct adv_arg arg = {
					.type = ADV_ARG_STRING,
					.s = string_ref(ain->strings[str_idx]),
				};
				accum_push(&acc, &arg);
			}
			ip += instruction_width(S_PUSH);
			break;
		}
		case PUSH: {
			int val = adv_get_arg(ip, 0);
			struct adv_arg arg = {
				.type = ADV_ARG_INT,
				.i = val,
			};
			accum_push(&acc, &arg);
			ip += instruction_width(PUSH);
			break;
		}
		case F_PUSH: {
			union { int32_t i; float f; } u;
			u.i = adv_get_arg(ip, 0);
			struct adv_arg arg = {
				.type = ADV_ARG_FLOAT,
				.f = u.f,
			};
			accum_push(&acc, &arg);
			ip += instruction_width(F_PUSH);
			break;
		}
		case _MSG: {
			int msg_idx = adv_get_arg(ip, 0);
			struct adv_step *step = adv_add_step();
			step->code_type = ADV_CODE_MESSAGE;
			step->msg_idx = msg_idx;

			/* Build text from message content */
			if (msg_idx >= 0 && msg_idx < ain->nr_messages)
				step->text = string_ref(ain->messages[msg_idx]);
			else
				step->text = string_ref(&EMPTY_STRING);

			/* Any pending args belong to the previous step, clear them */
			accum_clear(&acc);

			ip += instruction_width(_MSG);
			break;
		}
		case CALLFUNC: {
			int target_func = adv_get_arg(ip, 0);
			struct adv_step *step = adv_add_step();
			step->code_type = ADV_CODE_FUNCTION;
			step->func_no = target_func;

			/* Transfer pending arguments to this step */
			accum_flush_to_step(&acc, step);

			/* Build step text */
			const char *fname = "";
			if (target_func >= 0 && target_func < ain->nr_functions)
				fname = ain->functions[target_func].name;
			step->text = build_step_text(fname, step);

			ip += instruction_width(CALLFUNC);
			break;
		}
		case RETURN:
		case ENDFUNC:
			goto done;
		default:
			/* Skip unknown opcodes — should not happen in scene functions
			 * but handle gracefully */
			if (masked >= 0 && masked < NR_OPCODES)
				ip += instruction_width(masked);
			else
				ip += 2; /* skip unknown 2-byte opcode */
			break;
		}
	}

done:
	accum_clear(&acc);
	return true;
}

/* Find a function by name in ain->functions */
static int find_function_by_name(const char *name)
{
	for (int i = 0; i < ain->nr_functions; i++) {
		if (ain->functions[i].name && strcmp(ain->functions[i].name, name) == 0)
			return i;
	}
	return -1;
}

/* ---- HLL exports ---- */

static void ADVEngine_GetFunctionList(struct string *filepath, struct page **array)
{
	/* Not needed for basic ADV playback */
}

static int ADVEngine_Load(struct string *function_name)
{
	adv_clear();

	int func_no = find_function_by_name(function_name->text);
	if (func_no < 0) {
		WARNING("ADVEngine.Load: function '%s' not found", function_name->text);
		return 0;
	}

	if (!adv_parse_function(func_no)) {
		WARNING("ADVEngine.Load: failed to parse function '%s'", function_name->text);
		adv_clear();
		return 0;
	}

	NOTICE("ADVEngine.Load('%s'): %d steps parsed from func#%d",
		function_name->text, adv_state.nr_steps, func_no);
	return 1;
}

static int ADVEngine_LoadByFile(struct string *filepath, struct string *function_name)
{
	/* For bytecode-based scenes, just delegate to Load */
	return ADVEngine_Load(function_name);
}

static int ADVEngine_LoadByText(struct string *text)
{
	/* Text-based scripts not supported (txtex format) */
	/* Returning 0 (false) causes the game to fall through to Load() */
	return 0;
}

static void ADVEngine_Release(void)
{
	adv_clear();
}

static int ADVEngine_IsExistLowerIf(void)
{
	return 0;
}

static int ADVEngine_NumofStep(void)
{
	return adv_state.nr_steps;
}

static struct string *ADVEngine_GetStepText(int step)
{
	struct adv_step *s = adv_get_step(step);
	if (!s || !s->text)
		return string_ref(&EMPTY_STRING);
	return string_ref(s->text);
}

static void ADVEngine_OpenFunctionFile(int step)
{
	/* Debug feature: open source file at step location. No-op. */
}

static int ADVEngine_GetCodeType(int step)
{
	struct adv_step *s = adv_get_step(step);
	if (!s)
		return 0;
	return s->code_type;
}

static struct string *ADVEngine_GetMessage(int step)
{
	struct adv_step *s = adv_get_step(step);
	if (!s || s->code_type != ADV_CODE_MESSAGE)
		return string_ref(&EMPTY_STRING);
	if (s->msg_idx >= 0 && s->msg_idx < ain->nr_messages)
		return string_ref(ain->messages[s->msg_idx]);
	return string_ref(&EMPTY_STRING);
}

static int ADVEngine_BindFunction(int step)
{
	struct adv_step *s = adv_get_step(step);
	if (!s || s->code_type != ADV_CODE_FUNCTION)
		return 0;

	/* Check that the function exists and has void return or acceptable return type */
	if (s->func_no < 0 || s->func_no >= ain->nr_functions)
		return 0;

	return 1;
}

static struct string *ADVEngine_GetFuncName(int step)
{
	struct adv_step *s = adv_get_step(step);
	if (!s || s->code_type != ADV_CODE_FUNCTION)
		return string_ref(&EMPTY_STRING);
	if (s->func_no >= 0 && s->func_no < ain->nr_functions)
		return cstr_to_string(ain->functions[s->func_no].name);
	return string_ref(&EMPTY_STRING);
}

static int ADVEngine_CreateDelegateObjectID(int step)
{
	struct adv_step *s = adv_get_step(step);
	if (!s || s->code_type != ADV_CODE_FUNCTION)
		return -1;
	/* Return the function number as the delegate ID.
	 * The game code uses this with global[25] delegate array. */
	return s->func_no;
}

static int ADVEngine_GetArgumentValue_int(struct page **value_ref, int step, int index)
{
	struct adv_step *s = adv_get_step(step);
	if (!s || index < 0 || index >= s->nr_args)
		return 0;
	if (s->args[index].type != ADV_ARG_INT)
		return 0;
	/* Write value through ref */
	/* The ref points to a variable on the heap; write the int value there */
	if (value_ref && *value_ref && (*value_ref)->nr_vars > 0) {
		(*value_ref)->values[0].i = s->args[index].i;
	}
	return 1;
}

static int ADVEngine_GetArgumentValue_float(struct page **value_ref, int step, int index)
{
	struct adv_step *s = adv_get_step(step);
	if (!s || index < 0 || index >= s->nr_args)
		return 0;
	if (s->args[index].type == ADV_ARG_FLOAT) {
		if (value_ref && *value_ref && (*value_ref)->nr_vars > 0)
			(*value_ref)->values[0].f = s->args[index].f;
		return 1;
	}
	if (s->args[index].type == ADV_ARG_INT) {
		if (value_ref && *value_ref && (*value_ref)->nr_vars > 0)
			(*value_ref)->values[0].f = (float)s->args[index].i;
		return 1;
	}
	return 0;
}

static int ADVEngine_GetArgumentValue_bool(struct page **value_ref, int step, int index)
{
	struct adv_step *s = adv_get_step(step);
	if (!s || index < 0 || index >= s->nr_args)
		return 0;
	if (s->args[index].type == ADV_ARG_INT || s->args[index].type == ADV_ARG_BOOL) {
		if (value_ref && *value_ref && (*value_ref)->nr_vars > 0)
			(*value_ref)->values[0].i = s->args[index].i ? 1 : 0;
		return 1;
	}
	return 0;
}

static int ADVEngine_GetArgumentValue_string(struct page **value_ref, int step, int index)
{
	struct adv_step *s = adv_get_step(step);
	if (!s || index < 0 || index >= s->nr_args)
		return 0;
	if (s->args[index].type == ADV_ARG_STRING) {
		if (value_ref && *value_ref && (*value_ref)->nr_vars > 0) {
			/* Free old string, assign new one */
			if ((*value_ref)->values[0].i > 0)
				heap_unref((*value_ref)->values[0].i);
			(*value_ref)->values[0].i = heap_alloc_string(string_ref(s->args[index].s));
		}
		return 1;
	}
	/* Convert int to string */
	if (s->args[index].type == ADV_ARG_INT) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%d", s->args[index].i);
		struct string *str = cstr_to_string(buf);
		if (value_ref && *value_ref && (*value_ref)->nr_vars > 0) {
			if ((*value_ref)->values[0].i > 0)
				heap_unref((*value_ref)->values[0].i);
			(*value_ref)->values[0].i = heap_alloc_string(str);
		} else {
			free_string(str);
		}
		return 1;
	}
	return 0;
}

static struct string *ADVEngine_GetIfFuncName(int step)
{
	return string_ref(&EMPTY_STRING);
}

static struct string *ADVEngine_GetIfFuncArg(int step)
{
	return string_ref(&EMPTY_STRING);
}

static int ADVEngine_GetIfValue(int step)
{
	return 0;
}

static int ADVEngine_IsLowerIf(int step)
{
	return 0;
}

static struct string *ADVEngine_GetAttribute(struct string *key)
{
	return string_ref(&EMPTY_STRING);
}

HLL_LIBRARY(ADVEngine,
	    HLL_EXPORT(GetFunctionList, ADVEngine_GetFunctionList),
	    HLL_EXPORT(Load, ADVEngine_Load),
	    HLL_EXPORT(LoadByFile, ADVEngine_LoadByFile),
	    HLL_EXPORT(LoadByText, ADVEngine_LoadByText),
	    HLL_EXPORT(Release, ADVEngine_Release),
	    HLL_EXPORT(IsExistLowerIf, ADVEngine_IsExistLowerIf),
	    HLL_EXPORT(NumofStep, ADVEngine_NumofStep),
	    HLL_EXPORT(GetStepText, ADVEngine_GetStepText),
	    HLL_EXPORT(OpenFunctionFile, ADVEngine_OpenFunctionFile),
	    HLL_EXPORT(GetCodeType, ADVEngine_GetCodeType),
	    HLL_EXPORT(GetMessage, ADVEngine_GetMessage),
	    HLL_EXPORT(BindFunction, ADVEngine_BindFunction),
	    HLL_EXPORT(GetFuncName, ADVEngine_GetFuncName),
	    HLL_EXPORT(CreateDelegateObjectID, ADVEngine_CreateDelegateObjectID),
	    HLL_EXPORT(GetArgumentValue_int, ADVEngine_GetArgumentValue_int),
	    HLL_EXPORT(GetArgumentValue_float, ADVEngine_GetArgumentValue_float),
	    HLL_EXPORT(GetArgumentValue_bool, ADVEngine_GetArgumentValue_bool),
	    HLL_EXPORT(GetArgumentValue_string, ADVEngine_GetArgumentValue_string),
	    HLL_EXPORT(GetIfFuncName, ADVEngine_GetIfFuncName),
	    HLL_EXPORT(GetIfFuncArg, ADVEngine_GetIfFuncArg),
	    HLL_EXPORT(GetIfValue, ADVEngine_GetIfValue),
	    HLL_EXPORT(IsLowerIf, ADVEngine_IsLowerIf),
	    HLL_EXPORT(GetAttribute, ADVEngine_GetAttribute));

/* v14 "system" HLL library — save/load, error reporting, system utilities */

#define VM_PRIVATE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <zlib.h>
#include <SDL.h>

#include "system4/ain.h"
#include "system4/file.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "cJSON.h"
#include "hll.h"
#include "input.h"
#include "parts.h"
#include "savedata.h"
#include "serialize_struct.h"
#include "gfx/gfx.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

// Pending ResumeLoad state — deferred to CALLHLL handler in vm.c
// because vm_load_image replaces the entire VM state (heap, stack, call stack, IP).
char *resume_load_key_pending = NULL;
char *resume_load_path_pending = NULL;

// [0] ResumeSave(keyName, fileName, wrap<int> result) -> bool
// v14: arg[2] is AIN_WRAP — wrap slot index
// Saves the entire VM state (heap, stack, call stack, IP) to a file.
// When resumed via ResumeLoad, execution continues after this CALLHLL
// with return value false (indicating a load, not a save).
static bool system_ResumeSave(struct string *keyName, struct string *fileName, int *result_out)
{
	// NOTE: the fork carried a 2-second throttle here that reported
	// success without writing. That fake success broke the memory-mode
	// (回想) sequence: MemoryModeEvent_Begin believes the -5 slot was
	// saved, the closing ResumeLoad finds no file, the rollback never
	// happens, and the opening dialogue fast-forwards with the
	// wait-key suppressed. Save for real; revisit performance with
	// measurements if init cost shows up (plan: deb1101 root fix).

	// save_stack_to_rsave assumes 2 extra values on the stack (the SYS_RESUME_SAVE
	// arguments in the old CALLSYS path). In the HLL path, arguments have already
	// been popped by hll_call. Push 2 dummy values to match the assumption.
	stack_push(0);
	stack_push(0);

	int ok = vm_save_image(keyName->text, fileName->text);

	stack_pop();
	stack_pop();

	// Match SYS_RESUME_SAVE convention: result = vm_save_image() return (1=OK, 0=fail)
	if (result_out)
		*result_out = ok;
	return ok ? true : false;
}

// [1] ResumeLoad(keyName, fileName) -> void
// Defers the actual load to the CALLHLL handler because vm_load_image
// replaces the entire VM state. Cannot be called from within hll_call
// since the write-back phase would operate on stale heap references.
static void system_ResumeLoad(struct string *keyName, struct string *fileName)
{
	// Check if save file exists before scheduling load
	char *full_path = savedir_path(fileName->text);
	bool exists = access(full_path, F_OK) == 0;
	free(full_path);

	if (!exists)
		return;

	free(resume_load_key_pending);
	free(resume_load_path_pending);
	resume_load_key_pending = strdup(keyName->text);
	resume_load_path_pending = strdup(fileName->text);
}

// [2] Peek() -> void — process one pending event
static void system_Peek(void)
{
	handle_events();
	PE_UpdateInputState(0);
}

// [3] PeekAll() -> void — process all pending events
static void system_PeekAll(void)
{
	handle_events();
	PE_UpdateInputState(0);
}

// [4] Exit(result) -> void
static void system_Exit(int result)
{
	static int exit_calls = 0;
	exit_calls++;
	if (exit_calls <= 5)
		WARNING("system.Exit(%d) — suppressing (call #%d)", result, exit_calls);
	// Don't actually exit — game assertions call Exit(1) for non-fatal errors.
	// Let the game continue and recover if possible.
}

// [5] Reset() -> void
static void system_Reset(void)
{
	WARNING("system.Reset() stub");
}

// [6] IsDebugMode() -> bool
// Game development mode is separate from the VM debugger or sanitizers.
// In Dohna Dohna it enables periodic full-VM debug dumps (5s / 60 frames).
// Normal component updates are registered outside this game's debug gate.
static bool system_IsDebugMode(void)
{
	const char *value = getenv("XSYS4_GAME_DEBUG");
	return value && !strcmp(value, "1");
}

// [7] ResumeWriteComment(keyName, fileName, wrap<string> commentList) -> bool
// v14: arg[2] is AIN_WRAP — wrap slot index
static bool system_ResumeWriteComment(struct string *keyName, struct string *fileName, int *commentList)
{
	(void)commentList;
	WARNING("system.ResumeWriteComment stub");
	return true;
}

// [8] ResumeReadComment(keyName, fileName, wrap<string> commentList) -> bool
static bool system_ResumeReadComment(struct string *keyName, struct string *fileName, int *commentList)
{
	(void)commentList;
	WARNING("system.ResumeReadComment stub");
	return true;
}

// [9] GroupSave(keyName, fileName, group, wrap<int> numofSave) -> bool
static bool system_GroupSave(struct string *keyName, struct string *fileName, struct string *group, int *numofSave)
{
	int n = 0;
	int ok = save_globals(keyName->text, fileName->text, group->text, &n);
	if (numofSave)
		*numofSave = n;
	return ok ? true : false;
}

// [10] GroupLoad(keyName, fileName, group, wrap<int> numofLoad) -> bool
static bool system_GroupLoad(struct string *keyName, struct string *fileName, struct string *group, int *numofLoad)
{
	int n = 0;
	int ok = load_globals(keyName->text, fileName->text, group->text, &n);
	if (numofLoad)
		*numofLoad = n;
	return ok ? true : false;
}

// [11] WriteGroupSaveComment
static bool system_WriteGroupSaveComment(struct string *keyName, struct string *fileName, struct string *log)
{
	WARNING("system.WriteGroupSaveComment stub");
	return true;
}

// [12] ReadGroupSaveComment(keyName, fileName, wrap<string> log) -> bool
static bool system_ReadGroupSaveComment(struct string *keyName, struct string *fileName, int *log_out)
{
	(void)log_out;
	WARNING("system.ReadGroupSaveComment stub");
	return true;
}

// [13] SerializeStruct(fileName, structPageList, saveFolder) -> bool
// structPageList is an array page whose first element is the struct to serialize.
// Writes a gsave-format .asd file compatible with the original System4 engine.
static bool system_SerializeStruct(struct string *fileName, struct page *structPageList, bool saveFolder)
{
	extern int hll_self_slot;
	if (!structPageList || structPageList->nr_vars == 0) {
		// Empty array — nothing to serialize, but return true to avoid game errors
		return true;
	}

	// structPageList is an ARRAY_PAGE; its first element is a heap slot for the struct
	int struct_slot = structPageList->values[0].i;
	struct page *struct_page = heap_get_page(struct_slot);
	if (!struct_page || struct_page->type != STRUCT_PAGE) {
		WARNING("system.SerializeStruct('%s'): first element is not a struct",
			display_game0(fileName->text));
		return true;
	}

	// Create gsave v7 with key "serialize_struct"
	struct gsave *save = gsave_create(7, "serialize_struct", 1, "serialize_struct");
	gsave_add_globals_record(save, 1);

	struct gsave_global *g = &save->globals[0];
	g->name = strdup("");
	g->type = AIN_STRUCT;
	g->value = add_value_to_gsave(AIN_STRUCT,
		(union vm_value){.i = struct_slot}, save);

	char *path = savedir_path(fileName->text);
	FILE *fp = file_open_utf8(path, "wb");
	if (!fp) {
		WARNING("system.SerializeStruct('%s'): cannot open file", display_game0(fileName->text));
		free(path);
		gsave_free(save);
		return false;
	}
	free(path);

	enum savefile_error error = gsave_write(save, fp, false, Z_BEST_SPEED);
	fclose(fp);
	gsave_free(save);

	if (error != SAVEFILE_SUCCESS) {
		WARNING("system.SerializeStruct('%s'): write failed: %s",
			display_game0(fileName->text), savefile_strerror(error));
		return false;
	}
	return true;
}

// [14] DeserializeStruct(fileName, structPageList, saveFolder) -> bool
// Reads a gsave-format .asd file and stores the struct in structPageList[0].
static bool system_DeserializeStruct(struct string *fileName, struct page *structPageList, bool saveFolder)
{
	extern int hll_self_slot;

	char *path = savedir_path(fileName->text);
	if (!file_exists(path)) {
		free(path);
		return false;
	}

	enum savefile_error error;
	struct gsave *save = gsave_read(path, &error);
	free(path);

	if (!save) {
		WARNING("system.DeserializeStruct('%s'): read failed: %s",
			display_game0(fileName->text), savefile_strerror(error));
		return false;
	}

	// Find the root struct: last non-GLOBALS record
	int root_record = -1;
	for (int i = save->nr_records - 1; i >= 0; i--) {
		if (save->records[i].type == GSAVE_RECORD_STRUCT) {
			root_record = i;
			break;
		}
	}

	if (root_record < 0) {
		WARNING("system.DeserializeStruct('%s'): no struct record", display_game0(fileName->text));
		gsave_free(save);
		return false;
	}

	// Resolve struct type from struct_defs or struct_name
	struct gsave_record *rec = &save->records[root_record];
	int struct_type = -1;
	const char *struct_name = NULL;

	if (save->version >= 7 && rec->struct_index >= 0 &&
	    rec->struct_index < save->nr_struct_defs) {
		struct_name = save->struct_defs[rec->struct_index].name;
	} else if (rec->struct_name) {
		struct_name = rec->struct_name;
	}

	if (struct_name) {
		for (int i = 0; i < ain->nr_structures; i++) {
			if (!strcmp(ain->structures[i].name, struct_name)) {
				struct_type = i;
				break;
			}
		}
	}

	if (struct_type < 0) {
		WARNING("system.DeserializeStruct('%s'): unknown struct '%s'",
			display_game0(fileName->text), struct_name ? display_game1(struct_name) : "(null)");
		gsave_free(save);
		return false;
	}

	// Reconstruct the struct
	union vm_value val = gsave_to_vm_value(save, AIN_STRUCT, struct_type,
		0, root_record);

	// Store result in the array: structPageList[0] = struct heap slot
	if (structPageList && structPageList->nr_vars > 0) {
		structPageList->values[0].i = val.i;
	} else if (hll_self_slot >= 0) {
		// Array was empty — need to resize it to hold 1 element
		// Create a new array page with 1 element and set it
		struct page *new_page = alloc_page(ARRAY_PAGE, structPageList ? structPageList->a_type : AIN_ARRAY_INT, 1);
		new_page->array.rank = 1;
		new_page->array.struct_type = struct_type;
		new_page->values[0].i = val.i;
		heap_set_page(hll_self_slot, new_page);
	}

	gsave_free(save);
	return true;
}

// [15] WriteSerializeStructComment
static bool system_WriteSerializeStructComment(struct string *fileName, struct string *comment, bool saveFolder)
{
	return true;
}

// [16] ReadSerializeStructComment(fileName, wrap<string> comment, saveFolder) -> bool
// v14: arg[1] is AIN_WRAP — wrap slot index
static bool system_ReadSerializeStructComment(struct string *fileName, int *comment_out, bool saveFolder)
{
	(void)comment_out;
	return false;
}

/*
 * v14 SerializeStruct family (native: system case 13..16 -> 0x65f160,
 * 0x65c910, 0x65f3b0, 0x65c250). Selected by declaration shape in
 * system_select_function; other shapes keep the functions above.
 */

static bool system_shape_is(const struct ain_type *t, enum ain_data_type d)
{
	return t->data == d;
}

static bool system_shape_inner(const struct ain_type *t, enum ain_data_type d)
{
	return t->array_type && t->array_type->data == d;
}

/* (string, array<int>, bool) -> bool */
static bool system_struct_list_shape(const struct ain_hll_function *f)
{
	return f && f->return_type.data == AIN_BOOL && f->nr_arguments == 3 && f->arguments
		&& system_shape_is(&f->arguments[0].type, AIN_STRING)
		&& system_shape_is(&f->arguments[1].type, AIN_ARRAY)
		&& system_shape_inner(&f->arguments[1].type, AIN_INT)
		&& system_shape_is(&f->arguments[2].type, AIN_BOOL);
}

/* Array.SYSTEMONLY_GetStructPageList only feeds these two functions; its v14
 * implementation is selected only when both take the list in the v14 shape,
 * so the older implementations never receive a real struct list. */
bool system_struct_list_consumers_supported(void)
{
	int lib = ain ? ain_get_library(ain, "system") : -1;
	if (lib < 0)
		return false;
	bool ser = false, des = false;
	for (int i = 0; i < ain->libraries[lib].nr_functions; i++) {
		const struct ain_hll_function *f = &ain->libraries[lib].functions[i];
		if (!f->name)
			continue;
		if (!strcmp(f->name, "SerializeStruct"))
			ser = system_struct_list_shape(f);
		else if (!strcmp(f->name, "DeserializeStruct"))
			des = system_struct_list_shape(f);
	}
	return ser && des;
}

static bool system_path_segment_is_dotdot(const char *p, size_t n)
{
	return n == 2 && p[0] == '.' && p[1] == '.';
}

/* Save file path for a game-supplied name, or NULL. Absolute names and ".."
 * segments are refused so that a name never leaves the save folder. */
static char *system_struct_save_path(struct string *fileName, bool saveFolder, const char *what)
{
	if (!fileName || !fileName->text[0]) {
		WARNING("system.%s: empty file name", what);
		return NULL;
	}
	if (!config.save_dir) {
		static int warned;
		if (warned++ < 8)
			WARNING("system.%s('%s'): no save folder configured", what, display_game0(fileName->text));
		return NULL;
	}
	const char *name = fileName->text;
	bool bad = is_absolute_path(name) || name[0] == '/' || name[0] == '\\';
	for (const char *seg = name; !bad && *seg; ) {
		size_t n = strcspn(seg, "/\\");
		bad = system_path_segment_is_dotdot(seg, n);
		seg += n;
		if (*seg)
			seg++;
	}
	if (bad) {
		static int warned;
		if (warned++ < 8)
			WARNING("system.%s('%s'): file name outside the save folder refused", what, display_game0(name));
		return NULL;
	}
	if (!saveFolder) {
		/* native: DebugData\01_First\Serialize\ relative to the working
		 * directory (0x666410); no CN call site uses it */
		static int warned;
		if (warned++ < 1)
			WARNING("system.%s('%s'): saveFolder=false uses the save folder", what, display_game0(name));
	}
	return savedir_path(name);
}

/* 0x65dc40: every element must be a struct handle, otherwise the list is empty. */
static int *system_struct_roots(struct page *list, int *n, const char *what, const char *file)
{
	*n = 0;
	if (!list || list == heap[0].page || list->type != ARRAY_PAGE || list->nr_vars <= 0
	    || (list->a_type != AIN_ARRAY_INT && list->a_type != AIN_ARRAY))
		return NULL;
	int *roots = xmalloc(sizeof(int) * list->nr_vars);
	for (int i = 0; i < list->nr_vars; i++) {
		roots[i] = ss_struct_slot(list->values[i].i);
		if (roots[i] < 0) {
			static int warned;
			if (warned++ < 8)
				WARNING("system.%s('%s'): list element %d (slot %d) is not a struct",
					what, display_game0(file), i, list->values[i].i);
			free(roots);
			return NULL;
		}
	}
	*n = list->nr_vars;
	return roots;
}

bool system_SerializeStruct_v14(struct string *fileName, struct page *structPageList, bool saveFolder)
{
	const char *label = fileName ? fileName->text : "";
	int n;
	int *roots = system_struct_roots(structPageList, &n, "SerializeStruct", label);
	if (!n) {
		/* 0x65f1a5: an empty list writes nothing and returns false */
		ss_trace("ser %s roots=0 fail(empty list)", label);
		return false;
	}
	char *path = system_struct_save_path(fileName, saveFolder, "SerializeStruct");
	bool ok = path && ss_serialize_file(path, label, roots, n);
	free(path);
	free(roots);
	return ok;
}

bool system_DeserializeStruct_v14(struct string *fileName, struct page *structPageList, bool saveFolder)
{
	const char *label = fileName ? fileName->text : "";
	int n;
	int *roots = system_struct_roots(structPageList, &n, "DeserializeStruct", label);
	if (!n) {
		/* 0x65c965: an empty list reads nothing and returns false */
		ss_trace("des %s roots=0 fail(empty list)", label);
		return false;
	}
	char *path = system_struct_save_path(fileName, saveFolder, "DeserializeStruct");
	bool ok = path && ss_deserialize_file(path, label, roots, n);
	free(path);
	free(roots);
	return ok;
}

bool system_WriteSerializeStructComment_v14(struct string *fileName, struct string *comment, bool saveFolder)
{
	const char *label = fileName ? fileName->text : "";
	if (!comment || comment->size <= 0) {
		static int warned;
		if (warned++ < 8)
			WARNING("system.WriteSerializeStructComment('%s'): empty comment", display_game0(label));
		return false;
	}
	char *path = system_struct_save_path(fileName, saveFolder, "WriteSerializeStructComment");
	bool ok = path && ss_write_comment_file(path, label, comment->text, (size_t)comment->size);
	free(path);
	return ok;
}

/* The comment is a wrap<string> slot (CIF sint32): the caller's local string
 * variable, rewritten in place. */
bool system_ReadSerializeStructComment_v14(struct string *fileName, int comment_slot, bool saveFolder)
{
	const char *label = fileName ? fileName->text : "";
	if (!string_index_valid(comment_slot))
		return false; /* 0x65c250: no output object */
	char *path = system_struct_save_path(fileName, saveFolder, "ReadSerializeStructComment");
	if (!path)
		return false;
	struct string *s = NULL;
	int r = ss_read_comment_file(path, label, &s);
	free(path);
	if (r < 0)
		return false;
	if (r == 1) {
		heap_string_assign(comment_slot, s);
		free_string(s);
	}
	return true;
}

void *system_select_function(const struct ain_hll_function *f, void *dflt)
{
	if (!f || !f->name || f->return_type.data != AIN_BOOL || f->nr_arguments != 3 || !f->arguments)
		return dflt;
	const struct ain_type *a0 = &f->arguments[0].type, *a1 = &f->arguments[1].type,
		*a2 = &f->arguments[2].type;
	if (!system_shape_is(a0, AIN_STRING) || !system_shape_is(a2, AIN_BOOL))
		return dflt;
	if (!strcmp(f->name, "SerializeStruct") && system_struct_list_shape(f))
		return (void *)system_SerializeStruct_v14;
	if (!strcmp(f->name, "DeserializeStruct") && system_struct_list_shape(f))
		return (void *)system_DeserializeStruct_v14;
	if (!strcmp(f->name, "WriteSerializeStructComment") && system_shape_is(a1, AIN_STRING))
		return (void *)system_WriteSerializeStructComment_v14;
	if (!strcmp(f->name, "ReadSerializeStructComment") && system_shape_is(a1, AIN_WRAP)
	    && system_shape_inner(a1, AIN_STRING))
		return (void *)system_ReadSerializeStructComment_v14;
	return dflt;
}

// [17] ExistSaveFile(fileName) -> bool
static bool system_ExistSaveFile(struct string *fileName)
{
	char *path = savedir_path(fileName->text);
	bool exists = file_exists(path);
	free(path);
	return exists;
}

// [18] DeleteSaveFile
static bool system_DeleteSaveFile(struct string *fileName)
{
	char *path = savedir_path(fileName->text);
	int result = remove(path);
	free(path);
	return result == 0;
}

// [19] CopySaveFile
static bool system_CopySaveFile(struct string *dest, struct string *src)
{
	char *src_path = savedir_path(src->text);
	char *dest_path = savedir_path(dest->text);
	bool ok = false;

	FILE *fin = fopen(src_path, "rb");
	if (!fin) {
		int err = errno;
		WARNING("system.CopySaveFile: cannot open source '%s': %s", display_game0(src->text), strerror(err));
		goto cleanup;
	}
	FILE *fout = fopen(dest_path, "wb");
	if (!fout) {
		int err = errno;
		WARNING("system.CopySaveFile: cannot open dest '%s': %s", display_game0(dest->text), strerror(err));
		fclose(fin);
		goto cleanup;
	}

	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
		if (fwrite(buf, 1, n, fout) != n) {
			WARNING("system.CopySaveFile: write error");
			fclose(fin);
			fclose(fout);
			goto cleanup;
		}
	}
	fclose(fin);
	fclose(fout);
	ok = true;

cleanup:
	free(src_path);
	free(dest_path);
	return ok;
}

// [20] BackupSaveFile — same as CopySaveFile
static bool system_BackupSaveFile(struct string *dest, struct string *src)
{
	return system_CopySaveFile(dest, src);
}

// [21] Sleep(milliSecond) -> void
static void system_Sleep(int ms)
{
	if (ms > 0)
		usleep((useconds_t)ms * 1000);
}

// [22] Output(text) -> string
static struct string *system_Output(struct string *text)
{
	(void)text;
	return string_ref(text);
}

// [23] OutputLine(text) -> string
// The original writes the text to the debug output. XSYS4_TRACE_OUTPUTLINE=1
// logs it here (e.g. the battle action order that
// PlayerActionStreamCalculator@CalcStreams prints as "Id:Speed(boost:N)").
static struct string *system_OutputLine(struct string *text)
{
	static int trace = -1;
	if (trace < 0) {
		const char *env = getenv("XSYS4_TRACE_OUTPUTLINE");
		trace = env && !strcmp(env, "1") ? 1 : 0;
	}
	if (trace && text)
		NOTICE("OutputLine: %s", display_game0(text->text));
	return string_ref(text);
}

// [24] MsgBox(text) -> string
static struct string *system_MsgBox(struct string *text)
{
	static int mb_warn = 0;
	if (mb_warn++ < 2)
		WARNING("system.MsgBox: %s", display_game0(text->text));
	return string_ref(text);
}

// [25] MsgBoxOkCancel(text) -> int
static int system_MsgBoxOkCancel(struct string *text)
{
	WARNING("system.MsgBoxOkCancel: %s", display_game0(text->text));
	return 1; // OK
}

// [26] Error(text) -> string
static struct string *system_Error(struct string *text)
{
	static int error_count = 0;
	error_count++;
	if (error_count <= 10)
		WARNING("system.Error: %s", display_game0(text->text));
	else if (error_count == 11)
		WARNING("system.Error: (suppressing further errors, count=%d)", error_count);
	else if (error_count == 1000 || error_count == 10000)
		WARNING("system.Error: count=%d (continuing)", error_count);
	return string_ref(text);
}

// [27] OpenWeb(url) -> void
static void system_OpenWeb(struct string *url)
{
#ifdef __APPLE__
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "open '%s'", url->text);
	system(cmd);
#elif defined(_WIN32)
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", url->text);
	system(cmd);
#else
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "xdg-open '%s'", url->text);
	system(cmd);
#endif
}

// [28] GetSaveFolderName() -> string
static struct string *system_GetSaveFolderName(void)
{
	return cstr_to_string("SaveData");
}

// [29] GetGameName() -> string
static struct string *system_GetGameName(void)
{
	return cstr_to_string("DohnaDohna");
}

// [30] GetTime() -> int
// In AliceSoft System4, this is equivalent to Win32 GetTickCount() — milliseconds.
static int system_GetTime(void)
{
	return vm_time();
}

// [31] GetFuncStackName(index) -> string
static struct string *system_GetFuncStackName(int index)
{
	return cstr_to_string("");
}

// [32] ExistFunc(funcName) -> bool
static bool system_ExistFunc(struct string *funcName)
{
	return false;
}

HLL_LIBRARY(system,
	    HLL_EXPORT(ResumeSave, system_ResumeSave),
	    HLL_EXPORT(ResumeLoad, system_ResumeLoad),
	    HLL_EXPORT(Peek, system_Peek),
	    HLL_EXPORT(PeekAll, system_PeekAll),
	    HLL_EXPORT(Exit, system_Exit),
	    HLL_EXPORT(Reset, system_Reset),
	    HLL_EXPORT(IsDebugMode, system_IsDebugMode),
	    HLL_EXPORT(ResumeWriteComment, system_ResumeWriteComment),
	    HLL_EXPORT(ResumeReadComment, system_ResumeReadComment),
	    HLL_EXPORT(GroupSave, system_GroupSave),
	    HLL_EXPORT(GroupLoad, system_GroupLoad),
	    HLL_EXPORT(WriteGroupSaveComment, system_WriteGroupSaveComment),
	    HLL_EXPORT(ReadGroupSaveComment, system_ReadGroupSaveComment),
	    HLL_EXPORT(SerializeStruct, system_SerializeStruct),
	    HLL_EXPORT(DeserializeStruct, system_DeserializeStruct),
	    HLL_EXPORT(WriteSerializeStructComment, system_WriteSerializeStructComment),
	    HLL_EXPORT(ReadSerializeStructComment, system_ReadSerializeStructComment),
	    HLL_EXPORT(ExistSaveFile, system_ExistSaveFile),
	    HLL_EXPORT(DeleteSaveFile, system_DeleteSaveFile),
	    HLL_EXPORT(CopySaveFile, system_CopySaveFile),
	    HLL_EXPORT(BackupSaveFile, system_BackupSaveFile),
	    HLL_EXPORT(Sleep, system_Sleep),
	    HLL_EXPORT(Output, system_Output),
	    HLL_EXPORT(OutputLine, system_OutputLine),
	    HLL_EXPORT(MsgBox, system_MsgBox),
	    HLL_EXPORT(MsgBoxOkCancel, system_MsgBoxOkCancel),
	    HLL_EXPORT(Error, system_Error),
	    HLL_EXPORT(OpenWeb, system_OpenWeb),
	    HLL_EXPORT(GetSaveFolderName, system_GetSaveFolderName),
	    HLL_EXPORT(GetGameName, system_GetGameName),
	    HLL_EXPORT(GetTime, system_GetTime),
	    HLL_EXPORT(GetFuncStackName, system_GetFuncStackName),
	    HLL_EXPORT(ExistFunc, system_ExistFunc)
	    );

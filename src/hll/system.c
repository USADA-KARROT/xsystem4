/* v14 "system" HLL library — save/load, error reporting, system utilities */

#define VM_PRIVATE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <zlib.h>

#include "system4/ain.h"
#include "system4/file.h"
#include "system4/savefile.h"
#include "system4/string.h"
#include "cJSON.h"
#include "hll.h"
#include "input.h"
#include "parts.h"
#include "savedata.h"
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
	static int rs_count = 0;
	(void)result_out; // WRAP output not used — return value carries the result
	if (rs_count++ < 5)
		WARNING("system.ResumeSave('%s', '%s')", keyName->text, fileName->text);

	// save_stack_to_rsave assumes 2 extra values on the stack (the SYS_RESUME_SAVE
	// arguments in the old CALLSYS path). In the HLL path, arguments have already
	// been popped by hll_call. Push 2 dummy values to match the assumption.
	stack_push(0);
	stack_push(0);

	int ok = vm_save_image(keyName->text, fileName->text);

	stack_pop();
	stack_pop();

	if (rs_count <= 5)
		WARNING("system.ResumeSave: %s", ok ? "saved OK" : "FAILED");
	return ok ? true : false;
}

// [1] ResumeLoad(keyName, fileName) -> void
// Defers the actual load to the CALLHLL handler because vm_load_image
// replaces the entire VM state. Cannot be called from within hll_call
// since the write-back phase would operate on stale heap references.
static void system_ResumeLoad(struct string *keyName, struct string *fileName)
{
	static int rl_count = 0;
	if (rl_count++ < 5)
		WARNING("system.ResumeLoad('%s', '%s')", keyName->text, fileName->text);

	// Check if save file exists before scheduling load
	char *full_path = savedir_path(fileName->text);
	bool exists = access(full_path, F_OK) == 0;
	free(full_path);

	if (!exists) {
		if (rl_count <= 5)
			WARNING("system.ResumeLoad: no save file, skipping");
		return;
	}
	WARNING("system.ResumeLoad: scheduling deferred load");

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
// NOTE: v14 games (Dohna Dohna) gate their main UpdateComponent loop behind
// IsDebugMode(). Returning true enables the per-frame timer/component update
// that dispatches scene delegates. Without this, scene coroutines never tick.
static bool system_IsDebugMode(void)
{
	return ain->version >= 14;
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
	(void)numofSave;
	WARNING("system.GroupSave stub");
	return true;
}

// [10] GroupLoad(keyName, fileName, group, wrap<int> numofLoad) -> bool
static bool system_GroupLoad(struct string *keyName, struct string *fileName, struct string *group, int *numofLoad)
{
	(void)numofLoad;
	WARNING("system.GroupLoad stub");
	return true;
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
			fileName->text);
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
		WARNING("system.SerializeStruct('%s'): cannot open file", fileName->text);
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
			fileName->text, savefile_strerror(error));
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
			fileName->text, savefile_strerror(error));
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
		WARNING("system.DeserializeStruct('%s'): no struct record", fileName->text);
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
			fileName->text, struct_name ? struct_name : "(null)");
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
	WARNING("system.CopySaveFile('%s', '%s') stub", dest->text, src->text);
	return true;
}

// [20] BackupSaveFile
static bool system_BackupSaveFile(struct string *dest, struct string *src)
{
	WARNING("system.BackupSaveFile('%s', '%s') stub", dest->text, src->text);
	return true;
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
	static int output_count = 0;
	output_count++;
	if (output_count <= 20 || output_count % 100000 == 0)
		sys_message("[%d] %s", output_count, text->text);
	return string_ref(text);
}

// [23] OutputLine(text) -> string
static struct string *system_OutputLine(struct string *text)
{
	sys_message("%s\n", text->text);
	return string_ref(text);
}

// [24] MsgBox(text) -> string
static struct string *system_MsgBox(struct string *text)
{
	WARNING("system.MsgBox: %s", text->text);
	return string_ref(text);
}

// [25] MsgBoxOkCancel(text) -> int
static int system_MsgBoxOkCancel(struct string *text)
{
	WARNING("system.MsgBoxOkCancel: %s", text->text);
	return 1; // OK
}

// [26] Error(text) -> string
static struct string *system_Error(struct string *text)
{
	static int error_count = 0;
	error_count++;
	if (error_count <= 10)
		WARNING("system.Error: %s", text->text);
	else if (error_count == 11)
		WARNING("system.Error: (suppressing further errors, count=%d)", error_count);
	else if (error_count == 1000 || error_count == 10000)
		WARNING("system.Error: count=%d (continuing)", error_count);
	return string_ref(text);
}

// [27] OpenWeb(url) -> void
static void system_OpenWeb(struct string *url)
{
	WARNING("system.OpenWeb('%s') stub", url->text);
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

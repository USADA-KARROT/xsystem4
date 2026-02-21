/* v14 "system" HLL library — save/load, error reporting, system utilities */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "system4/string.h"
#include "hll.h"
#include "vm.h"

// [0] ResumeSave(keyName, fileName, result) -> bool
static bool system_ResumeSave(struct string *keyName, struct string *fileName, int result)
{
	WARNING("system.ResumeSave('%s', '%s', %d) stub", keyName->text, fileName->text, result);
	return true;
}

// [1] ResumeLoad(keyName, fileName) -> void
static void system_ResumeLoad(struct string *keyName, struct string *fileName)
{
	WARNING("system.ResumeLoad('%s', '%s') stub", keyName->text, fileName->text);
}

// [2] Peek() -> void
static void system_Peek(void)
{
}

// [3] PeekAll() -> void
static void system_PeekAll(void)
{
}

// [4] Exit(result) -> void
static void system_Exit(int result)
{
	WARNING("system.Exit(%d)", result);
	vm_exit(result);
}

// [5] Reset() -> void
static void system_Reset(void)
{
	WARNING("system.Reset() stub");
}

// [6] IsDebugMode() -> bool
static bool system_IsDebugMode(void)
{
	return false;
}

// [7] ResumeWriteComment
static bool system_ResumeWriteComment(struct string *keyName, struct string *fileName, struct string **commentList)
{
	WARNING("system.ResumeWriteComment stub");
	return true;
}

// [8] ResumeReadComment
static bool system_ResumeReadComment(struct string *keyName, struct string *fileName, struct string **commentList)
{
	WARNING("system.ResumeReadComment stub");
	return true;
}

// [9] GroupSave
static bool system_GroupSave(struct string *keyName, struct string *fileName, struct string *group, int numofSave)
{
	WARNING("system.GroupSave stub");
	return true;
}

// [10] GroupLoad
static bool system_GroupLoad(struct string *keyName, struct string *fileName, struct string *group, int numofLoad)
{
	WARNING("system.GroupLoad stub");
	return true;
}

// [11] WriteGroupSaveComment
static bool system_WriteGroupSaveComment(struct string *keyName, struct string *fileName, struct string *log)
{
	WARNING("system.WriteGroupSaveComment stub");
	return true;
}

// [12] ReadGroupSaveComment
static bool system_ReadGroupSaveComment(struct string *keyName, struct string *fileName, struct string **log)
{
	WARNING("system.ReadGroupSaveComment stub");
	return true;
}

// [13] SerializeStruct
static bool system_SerializeStruct(struct string *fileName, struct page *structPageList, bool saveFolder)
{
	WARNING("system.SerializeStruct('%s') stub", fileName->text);
	return true;
}

// [14] DeserializeStruct
static bool system_DeserializeStruct(struct string *fileName, struct page *structPageList, bool saveFolder)
{
	WARNING("system.DeserializeStruct('%s') stub", fileName->text);
	return false;
}

// [15] WriteSerializeStructComment
static bool system_WriteSerializeStructComment(struct string *fileName, struct string *comment, bool saveFolder)
{
	return true;
}

// [16] ReadSerializeStructComment
static bool system_ReadSerializeStructComment(struct string *fileName, struct string **comment, bool saveFolder)
{
	return false;
}

// [17] ExistSaveFile(fileName) -> bool
static bool system_ExistSaveFile(struct string *fileName)
{
	WARNING("system.ExistSaveFile('%s') -> false", fileName->text);
	return false;
}

// [18] DeleteSaveFile
static bool system_DeleteSaveFile(struct string *fileName)
{
	WARNING("system.DeleteSaveFile('%s') stub", fileName->text);
	return true;
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
	sys_message("%s", text->text);
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
	WARNING("system.Error: %s", text->text);
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
static int system_GetTime(void)
{
	return (int)time(NULL);
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

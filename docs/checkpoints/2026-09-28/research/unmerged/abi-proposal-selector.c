/* src/hll/hll_shape_select.c (new file). Array keeps its own selector
 * (array_select_function in Array.c, called from ffi.c); every other library
 * goes through here. Unknown shapes return dflt, never NULL (NULL = unlinked
 * -> UNIMPL silently pushes 0). */
#include <string.h>
#include "system4/ain.h"

void *fileoperation_select_function(const struct ain_hll_function *f, void *dflt);
void *vsfile_select_function(const struct ain_hll_function *f, void *dflt);
void *pe_layoutbox_select_function(const struct ain_hll_function *f, void *dflt);

void *hll_shape_select_function(const char *lib, const struct ain_hll_function *f, void *dflt)
{
	if (!lib || !f || !f->name || (f->nr_arguments > 0 && !f->arguments))
		return dflt;
	if (!strcmp(lib, "FileOperation"))
		return fileoperation_select_function(f, dflt);
	if (!strcmp(lib, "VSFile"))
		return vsfile_select_function(f, dflt);
	if (!strcmp(lib, "PartsEngine")
	    && (!strcmp(f->name, "SetLayoutBoxReturn") || !strcmp(f->name, "GetLayoutBoxReturnSize")))
		return pe_layoutbox_select_function(f, dflt);
	return dflt;
}

/* src/hll/FileOperation.c: v14 declares array<?>(string) (ret 79, 1 arg);
 * v6/v7 declare bool(string, ref array<string>). */
void *fileoperation_select_function(const struct ain_hll_function *f, void *dflt)
{
	if (f->return_type.data != AIN_ARRAY || f->nr_arguments != 1)
		return dflt;
	if (!strcmp(f->name, "GetFileList"))
		return FileOperation_GetFileList_Array;
	if (!strcmp(f->name, "GetFolderList"))
		return FileOperation_GetFolderList_Array;
	return dflt;
}

/* src/hll/VSFile.c: wrap<string> (1-slot sint32) vs ref string (struct string **). */
void *vsfile_select_function(const struct ain_hll_function *f, void *dflt)
{
	if (!strcmp(f->name, "ReadString") && f->nr_arguments == 1
	    && f->arguments[0].type.data == AIN_WRAP)
		return VSFile_ReadString_Wrap;
	return dflt;
}

/* src/parts/layoutbox.c: float declarations get the float variants; int
 * declarations (upstream games) keep PE_SetLayoutBoxReturn / PE_GetLayoutBoxReturnSize. */
void *pe_layoutbox_select_function(const struct ain_hll_function *f, void *dflt)
{
	if (!strcmp(f->name, "SetLayoutBoxReturn") && f->nr_arguments == 3
	    && f->arguments[2].type.data == AIN_FLOAT)
		return PE_SetLayoutBoxReturnF;
	if (!strcmp(f->name, "GetLayoutBoxReturnSize") && f->return_type.data == AIN_FLOAT)
		return PE_GetLayoutBoxReturnSizeF;
	return dflt;
}

/* src/hll/Array.c, inside array_select_function(f, fallback) at 763f5bd: */
	if (!strcmp(f->name, "Duplicate")) {
		if (f->nr_arguments == 2 && f->return_type.data == AIN_VOID
		    && array_arg_is_wrap_array(f, 1))
			return (void *)Array_Duplicate_Wrap;
		return fallback;   /* legacy (ref array, ref array) keeps Array_Duplicate */
	}

/* TextFile / SealEngine / SYSTEMONLY_GetStructPageList need no selector:
 * TextFile.c is v14-only, and the SealEngine / SYSTEMONLY declarations have
 * only one shape. Their prototypes are fixed directly. */
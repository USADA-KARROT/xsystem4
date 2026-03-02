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

#include "hll.h"

static int next_handle = 1;

static int EXWriter_CreateHandle(void)
{
	return next_handle++;
}

HLL_QUIET_UNIMPLEMENTED( , void, EXWriter, ReleaseHandle, int id);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, Load, int id, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, Save, int id, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, LoadFromString, int id, struct string *text);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, SaveToString, int id, int page, int var);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, EraseDefine, int id, struct string *name);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, Rename, int id, struct string *old_name, struct string *new_name);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, SetPartial, int id, struct string *name, int partial);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, SetInt, int id, struct string *name, int data);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, SetFloat, int id, struct string *name, float data);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, SetString, int id, struct string *name, struct string *data);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, AddArrayInt, int id, struct string *name, int data);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, AddArrayFloat, int id, struct string *name, float data);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, AddArrayString, int id, struct string *name, struct string *data);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, AddTableFormatInt, int id, struct string *name, struct string *fmt, int is_key);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, AddTableFormatFloat, int id, struct string *name, struct string *fmt);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, AddTableFormatString, int id, struct string *name, struct string *fmt, int is_key);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, AddTableInt, int id, struct string *name, int data);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, AddTableFloat, int id, struct string *name, float data);
HLL_QUIET_UNIMPLEMENTED(1, int,  EXWriter, AddTableString, int id, struct string *name, struct string *data);

HLL_LIBRARY(EXWriter,
	    HLL_EXPORT(CreateHandle, EXWriter_CreateHandle),
	    HLL_EXPORT(ReleaseHandle, EXWriter_ReleaseHandle),
	    HLL_EXPORT(Load, EXWriter_Load),
	    HLL_EXPORT(Save, EXWriter_Save),
	    HLL_EXPORT(LoadFromString, EXWriter_LoadFromString),
	    HLL_EXPORT(SaveToString, EXWriter_SaveToString),
	    HLL_EXPORT(EraseDefine, EXWriter_EraseDefine),
	    HLL_EXPORT(Rename, EXWriter_Rename),
	    HLL_EXPORT(SetPartial, EXWriter_SetPartial),
	    HLL_EXPORT(SetInt, EXWriter_SetInt),
	    HLL_EXPORT(SetFloat, EXWriter_SetFloat),
	    HLL_EXPORT(SetString, EXWriter_SetString),
	    HLL_EXPORT(AddArrayInt, EXWriter_AddArrayInt),
	    HLL_EXPORT(AddArrayFloat, EXWriter_AddArrayFloat),
	    HLL_EXPORT(AddArrayString, EXWriter_AddArrayString),
	    HLL_EXPORT(AddTableFormatInt, EXWriter_AddTableFormatInt),
	    HLL_EXPORT(AddTableFormatFloat, EXWriter_AddTableFormatFloat),
	    HLL_EXPORT(AddTableFormatString, EXWriter_AddTableFormatString),
	    HLL_EXPORT(AddTableInt, EXWriter_AddTableInt),
	    HLL_EXPORT(AddTableFloat, EXWriter_AddTableFloat),
	    HLL_EXPORT(AddTableString, EXWriter_AddTableString));

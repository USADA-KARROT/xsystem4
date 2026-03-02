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

#include "system4/string.h"
#include "hll.h"

/*
 * ADVEngine — script step parser for .txtex files.
 * Stub implementation: Load returns true, NumofStep returns 0,
 * so the game sees an empty script and skips ADV processing.
 */

HLL_WARN_UNIMPLEMENTED( , void, ADVEngine, GetFunctionList, int page, int var);
HLL_QUIET_UNIMPLEMENTED(1, int,  ADVEngine, Load, int id, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(1, int,  ADVEngine, LoadByFile, int id, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(1, int,  ADVEngine, LoadByText, int id, struct string *text);
HLL_QUIET_UNIMPLEMENTED( , void, ADVEngine, Release, int id);
HLL_QUIET_UNIMPLEMENTED(0, int,  ADVEngine, IsExistLowerIf, int id, int step);
HLL_QUIET_UNIMPLEMENTED(0, int,  ADVEngine, NumofStep, int id);

static struct string *ADVEngine_GetStepText(int id, int step)
{
	return string_ref(&EMPTY_STRING);
}

HLL_QUIET_UNIMPLEMENTED(1, int,  ADVEngine, OpenFunctionFile, struct string *filename);
HLL_QUIET_UNIMPLEMENTED(0, int,  ADVEngine, GetCodeType, int id, int step);

static struct string *ADVEngine_GetMessage(int id, int step)
{
	return string_ref(&EMPTY_STRING);
}

HLL_QUIET_UNIMPLEMENTED(1, int,  ADVEngine, BindFunction, int id, int step, struct string *name);

static struct string *ADVEngine_GetFuncName(int id, int step)
{
	return string_ref(&EMPTY_STRING);
}

HLL_QUIET_UNIMPLEMENTED(0, int,  ADVEngine, CreateDelegateObjectID, int id, int step);
HLL_QUIET_UNIMPLEMENTED(0, int,  ADVEngine, GetArgumentValue_int, int id, int step, int arg);
HLL_QUIET_UNIMPLEMENTED(0, float, ADVEngine, GetArgumentValue_float, int id, int step, int arg);
HLL_QUIET_UNIMPLEMENTED(0, int,  ADVEngine, GetArgumentValue_bool, int id, int step, int arg);

static struct string *ADVEngine_GetArgumentValue_string(int id, int step, int arg)
{
	return string_ref(&EMPTY_STRING);
}

static struct string *ADVEngine_GetIfFuncName(int id, int step)
{
	return string_ref(&EMPTY_STRING);
}

HLL_QUIET_UNIMPLEMENTED(0, int,  ADVEngine, GetIfFuncArg, int id, int step);
HLL_QUIET_UNIMPLEMENTED(0, int,  ADVEngine, GetIfValue, int id, int step);
HLL_QUIET_UNIMPLEMENTED(0, int,  ADVEngine, IsLowerIf, int id, int step);

static struct string *ADVEngine_GetAttribute(int id, int step)
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

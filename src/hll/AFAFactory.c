/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

HLL_WARN_UNIMPLEMENTED(false, bool, AFAFactory, IsExistArchive, struct string *ArchiveName);

static bool AFAFactory_LoadArchive(int type, struct string *archive_name)
{
	return true;
}

static int AFAFactory_GetCountOfData(int type)
{
	return 0;
}

static void AFAFactory_GetTitleByIndex(int type, int index, struct string **name)
{
	if (name) *name = string_ref(&EMPTY_STRING);
}

static int AFAFactory_SearchTitle(int type, struct string *name)
{
	return -1;
}

static int AFAFactory_PrefixSearchTitle(int type, struct string *name)
{
	return -1;
}

static int AFAFactory_SuffixSearchTitle(int type, struct string *name)
{
	return -1;
}

static int AFAFactory_GetCountOfSearchData(int type)
{
	return 0;
}

static void AFAFactory_GetSearchTitleByIndex(int type, int index, struct string **name)
{
	if (name) *name = string_ref(&EMPTY_STRING);
}

HLL_LIBRARY(AFAFactory,
	    HLL_EXPORT(IsExistArchive, AFAFactory_IsExistArchive),
	    HLL_EXPORT(LoadArchive, AFAFactory_LoadArchive),
	    HLL_EXPORT(GetCountOfData, AFAFactory_GetCountOfData),
	    HLL_EXPORT(GetTitleByIndex, AFAFactory_GetTitleByIndex),
	    HLL_EXPORT(SearchTitle, AFAFactory_SearchTitle),
	    HLL_EXPORT(PrefixSearchTitle, AFAFactory_PrefixSearchTitle),
	    HLL_EXPORT(SuffixSearchTitle, AFAFactory_SuffixSearchTitle),
	    HLL_EXPORT(GetCountOfSearchData, AFAFactory_GetCountOfSearchData),
	    HLL_EXPORT(GetSearchTitleByIndex, AFAFactory_GetSearchTitleByIndex)
	    );

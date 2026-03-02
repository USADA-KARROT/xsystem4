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

static bool FileDialog_OpenLoadDialog(int page, int var, struct string *title,
		struct string *folder, struct page **filter_titles, struct page **filter_exts)
{
	return false;
}

static bool FileDialog_OpenSaveDialog(int page, int var, struct string *title,
		struct string *folder, struct page **filter_titles, struct page **filter_exts)
{
	return false;
}

static bool FileDialog_OpenSelectFolderDialog(struct string *display, struct string *title,
		int page, int var)
{
	return false;
}

HLL_LIBRARY(FileDialog,
	    HLL_EXPORT(OpenLoadDialog, FileDialog_OpenLoadDialog),
	    HLL_EXPORT(OpenSaveDialog, FileDialog_OpenSaveDialog),
	    HLL_EXPORT(OpenSelectFolderDialog, FileDialog_OpenSelectFolderDialog));

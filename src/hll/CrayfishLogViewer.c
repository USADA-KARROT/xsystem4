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

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "xsystem4.h"

bool is_opened = false;

static bool CrayfishLogViewer_OpenViewer(void)
{
	is_opened = true;
	return true;
}

static bool CrayfishLogViewer_CloseViewer(void)
{
	is_opened = false;
	return true;
}

static bool CrayfishLogViewer_IsOpenedViewer(void)
{
	return is_opened;
}

HLL_WARN_UNIMPLEMENTED( , void, CrayfishLogViewer, SetWindowTitleName,
			possibly_unused struct string *window_title_name);

static bool CrayfishLogViewer_AddText(struct string *text)
{
	log_message("Crayfish", "%s", display_sjis0(text->text));
	return true;
}

HLL_QUIET_UNIMPLEMENTED(true, bool, CrayfishLogViewer, ClearText);

/*
 * The original (0x6d8b65) keeps the name in the viewer object (+0x44);
 * OpenViewer (0x6d8e10) hands it to the debug log window it creates
 * (0x6d9900), which reads and writes its window position in a file in that
 * folder (0x6da900 / 0x6da720). Nothing else reads it. This viewer has no
 * window (AddText goes to the log), so the folder has no use here.
 */
static void CrayfishLogViewer_SetSaveFolderName(possibly_unused struct string *save_folder_name)
{
}

HLL_LIBRARY(CrayfishLogViewer,
	    HLL_EXPORT(OpenViewer, CrayfishLogViewer_OpenViewer),
	    HLL_EXPORT(CloseViewer, CrayfishLogViewer_CloseViewer),
	    HLL_EXPORT(IsOpenedViewer, CrayfishLogViewer_IsOpenedViewer),
	    HLL_EXPORT(SetWindowTitleName, CrayfishLogViewer_SetWindowTitleName),
	    HLL_EXPORT(AddText, CrayfishLogViewer_AddText),
	    HLL_EXPORT(ClearText, CrayfishLogViewer_ClearText),
	    HLL_EXPORT(SetSaveFolderName, CrayfishLogViewer_SetSaveFolderName));

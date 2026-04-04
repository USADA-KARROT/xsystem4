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

#include "system4/cg.h"
#include "system4/string.h"
#include "asset_manager.h"
#include "xsystem4.h"
#include "hll.h"

static bool CGManager_Init(void *imain_system, int cg_cache_size)
{
	return true;
}

static bool CGManager_LoadArchive(struct string *archive_name)
{
	char *name = gamedir_path(archive_name->text);
	bool r = asset_manager_load_archive(ASSET_CG, name);
	free(name);
	return r;
}

static bool CGManager_IsExist(struct string *cg_name)
{
	if (!cg_name)
		return false;
	return asset_exists_by_name(ASSET_CG, cg_name->text, NULL);
}

static bool CGManager_GetInfo(struct string *cg_name, int *width, int *height, int *bpp, bool *exist_pixel, bool *exist_alpha)
{
	struct cg_metrics metrics;
	if (!cg_name || !asset_cg_get_metrics_by_name(cg_name->text, &metrics))
		return false;
	*width = metrics.w;
	*height = metrics.h;
	*bpp = metrics.bpp;
	*exist_pixel = metrics.has_pixel;
	*exist_alpha = metrics.has_alpha;
	return true;
}

static int CGManager_GetCountOfDataFromArchive(void)
{
	return 0;
}

static void CGManager_GetTitleByIndexFromArchive(int index, struct string **cg_name)
{
	if (cg_name) *cg_name = string_ref(&EMPTY_STRING);
}

static int CGManager_SearchTitleFromArchive(struct string *cg_name)
{
	return -1;
}

static int CGManager_PrefixSearchTitleFromArchive(struct string *cg_name)
{
	return -1;
}

static int CGManager_SuffixSearchTitleFromArchive(struct string *cg_name)
{
	return -1;
}

static int CGManager_GetCountOfSearchDataFromArchive(void)
{
	return 0;
}

static void CGManager_GetSearchTitleByIndexFromArchive(int index, struct string **cg_name)
{
	if (cg_name) *cg_name = string_ref(&EMPTY_STRING);
}

static bool CGManager_IsExistCG(int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	cg_free(cg);
	return true;
}

static bool CGManager_GetSize(int cg_no, int *width, int *height)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	*width = cg->metrics.w;
	*height = cg->metrics.h;
	cg_free(cg);
	return true;
}

static int CGManager_GetWidth(int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;
	int w = cg->metrics.w;
	cg_free(cg);
	return w;
}

static int CGManager_GetHeight(int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;
	int h = cg->metrics.h;
	cg_free(cg);
	return h;
}

HLL_LIBRARY(CGManager,
	    HLL_EXPORT(IsExistCG, CGManager_IsExistCG),
	    HLL_EXPORT(GetSize, CGManager_GetSize),
	    HLL_EXPORT(GetWidth, CGManager_GetWidth),
	    HLL_EXPORT(GetHeight, CGManager_GetHeight),
	    HLL_EXPORT(Init, CGManager_Init),
	    HLL_EXPORT(LoadArchive, CGManager_LoadArchive),
	    HLL_EXPORT(IsExist, CGManager_IsExist),
	    HLL_EXPORT(GetInfo, CGManager_GetInfo),
	    HLL_TODO_EXPORT(GetCountOfDataFromArchive, CGManager_GetCountOfDataFromArchive),
	    HLL_TODO_EXPORT(GetTitleByIndexFromArchive, CGManager_GetTitleByIndexFromArchive),
	    HLL_TODO_EXPORT(SearchTitleFromArchive, CGManager_SearchTitleFromArchive),
	    HLL_TODO_EXPORT(PrefixSearchTitleFromArchive, CGManager_PrefixSearchTitleFromArchive),
	    HLL_TODO_EXPORT(SuffixSearchTitleFromArchive, CGManager_SuffixSearchTitleFromArchive),
	    HLL_TODO_EXPORT(GetCountOfSearchDataFromArchive, CGManager_GetCountOfSearchDataFromArchive),
	    HLL_TODO_EXPORT(GetSearchTitleByIndexFromArchive, CGManager_GetSearchTitleByIndexFromArchive));


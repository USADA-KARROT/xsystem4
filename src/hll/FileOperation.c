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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll.h"

static bool FileOperation_ExistFile(struct string *file_name)
{
	char *path = unix_path(file_name->text);
	ustat s;
	bool result = stat_utf8(path, &s) == 0 && S_ISREG(s.st_mode);
	free(path);
	return result;
}

static bool FileOperation_DeleteFile(struct string *file_name)
{
	char *path = unix_path(file_name->text);
	int result = remove(path);
	free(path);
	return result == 0;
}

static bool FileOperation_CopyFile(struct string *dest_file_name, struct string *src_file_name)
{
	char *dest = unix_path(dest_file_name->text);
	char *src = unix_path(src_file_name->text);
	bool result = file_copy(src, dest);
	free(src);
	free(dest);
	return result;
}

static bool filetime_helper(struct string *file_name, struct timespec *ts,
	int *year, int *month, int *day, int *week, int *hour, int *min, int *sec)
{
	(void)ts;
	char *path = unix_path(file_name->text);
	ustat s;
	if (stat_utf8(path, &s) < 0) {
		free(path);
		return false;
	}
	free(path);
	time_t t = s.st_mtime;
	struct tm *tm = localtime(&t);
	if (!tm) return false;
	if (year) *year = tm->tm_year + 1900;
	if (month) *month = tm->tm_mon + 1;
	if (day) *day = tm->tm_mday;
	if (week) *week = tm->tm_wday;
	if (hour) *hour = tm->tm_hour;
	if (min) *min = tm->tm_min;
	if (sec) *sec = tm->tm_sec;
	return true;
}

static bool FileOperation_GetFileCreationTime(struct string *fn, int *y, int *mo, int *d, int *w, int *h, int *mi, int *s)
{
	return filetime_helper(fn, NULL, y, mo, d, w, h, mi, s);
}

static bool FileOperation_GetFileLastAccessTime(struct string *fn, int *y, int *mo, int *d, int *w, int *h, int *mi, int *s)
{
	return filetime_helper(fn, NULL, y, mo, d, w, h, mi, s);
}

static bool FileOperation_GetFileLastWriteTime(struct string *fn, int *y, int *mo, int *d, int *w, int *h, int *mi, int *s)
{
	return filetime_helper(fn, NULL, y, mo, d, w, h, mi, s);
}

// v14: arg[1] is AIN_WRAP — pointer-based interface
static bool FileOperation_GetFileSize(struct string *file_name, int *size)
{
	char *path = unix_path(file_name->text);
	ustat s;
	if (stat_utf8(path, &s) < 0) {
		WARNING("stat(\"%s\"): %s", display_utf0(path), strerror(errno));
		free(path);
		return false;
	}
	if (!S_ISREG(s.st_mode)) {
		WARNING("stat(\"%s\"): not a regular file", display_utf0(path));
		free(path);
		return false;
	}
	if (size) *size = (int)s.st_size;
	free(path);
	return true;
}

static bool FileOperation_ExistFolder(struct string *folder_name)
{
	char *path = unix_path(folder_name->text);
	bool result = is_directory(path);
	free(path);
	return result;
}

static bool FileOperation_CreateFolder(struct string *folder_name)
{
	char *path = unix_path(folder_name->text);
	bool result = mkdir_p(path) == 0;
	if (!result)
		WARNING("mkdir_p(%s): %s", path, strerror(errno));
	free(path);
	return result;
}

static bool rmtree(const char *path)
{
	UDIR *d = opendir_utf8(path);
	if (!d) {
		WARNING("opendir(\"%s\"): %s", display_utf0(path), strerror(errno));
		return false;
	}
	bool ok = true;
	char *d_name;
	while (ok && (d_name = readdir_utf8(d)) != NULL) {
		if (d_name[0] == '.' && (d_name[1] == '\0' || (d_name[1] == '.' && d_name[2] == '\0'))) {
			free(d_name);
			continue;
		}

		char *utf8_path = path_join(path, d_name);
		ustat s;
		if (stat_utf8(utf8_path, &s) < 0) {
			WARNING("stat(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
			ok = false;
		} else {
			if (S_ISDIR(s.st_mode)) {
				if (!rmtree(utf8_path))
					ok = false;
			} else {
				if (remove_utf8(utf8_path) < 0) {
					WARNING("remove(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
					ok = false;
				}
			}
		}
		free(utf8_path);
		free(d_name);
	}
	closedir_utf8(d);
	if (ok) {
		if (rmdir_utf8(path) < 0) {
			WARNING("rmdir(\"%s\"): %s", display_utf0(path), strerror(errno));
			ok = false;
		}
	}
	return ok;
}

static bool FileOperation_DeleteFolder(struct string *folder_name)
{
	char *path = unix_path(folder_name->text);
	bool result = rmtree(path);
	free(path);
	return result;
}

static bool get_file_list(struct string *folder_name, struct page **out, bool folders)
{
	if (!folder_name || !folder_name->text || !out)
		return false;
	char *dir_name = unix_path(folder_name->text);

	UDIR *d = opendir_utf8(dir_name);
	if (!d) {
		WARNING("opendir(\"%s\"): %s", display_utf0(dir_name), strerror(errno));
		free(dir_name);
		return false;
	}

	struct string **names = NULL;
	int nr_names = 0;

	char *d_name;
	while ((d_name = readdir_utf8(d)) != NULL) {
		if (d_name[0] == '.') {
			free(d_name);
			continue;
		}

		char *utf8_path = path_join(dir_name, d_name);
		ustat s;
		if (stat_utf8(utf8_path, &s) < 0) {
			WARNING("stat(\"%s\"): %s", display_utf0(utf8_path), strerror(errno));
			goto loop_next;
		}
		if (folders) {
			if (!S_ISDIR(s.st_mode))
				goto loop_next;
		} else {
			if (!S_ISREG(s.st_mode))
				goto loop_next;
		}

		char *sjis_name = utf2sjis(d_name, 0);
		names = xrealloc_array(names, nr_names, nr_names+1, sizeof(struct string*));
		names[nr_names++] = cstr_to_string(sjis_name);
		free(sjis_name);
	loop_next:
		free(utf8_path);
		free(d_name);
	}
	closedir_utf8(d);
	free(dir_name);

	union vm_value dim = { .i = nr_names };
	struct page *page = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < nr_names; i++) {
		page->values[i].i = heap_alloc_string(names[i]);
	}
	free(names);

	if (out && *out) {
		delete_page_vars(*out);
		free_page(*out);
	}
	if (out)
		*out = page;
	return true;
}

static bool FileOperation_GetFileList(struct string *folder_name, struct page **out)
{
	return get_file_list(folder_name, out, false);
}

static bool FileOperation_GetFolderList(struct string *folder_name, struct page **out)
{
	return get_file_list(folder_name, out, true);
}

static bool FileOperation_CopyFolder(struct string *src, struct string *dst)
{
	/* Stub — folder copy not implemented yet */
	return false;
}

static bool FileOperation_GetFileListWithSubFolder(struct string *folder_name, struct page **out)
{
	/* For now, just return the same as GetFileList (no recursion) */
	return get_file_list(folder_name, out, false);
}

static bool FileOperation_OpenFolder(struct string *folder_name)
{
	/* Open folder in file manager — not applicable in headless */
	return false;
}

HLL_LIBRARY(FileOperation,
	    HLL_EXPORT(ExistFile, FileOperation_ExistFile),
	    HLL_EXPORT(DeleteFile, FileOperation_DeleteFile),
	    HLL_EXPORT(CopyFile, FileOperation_CopyFile),
	    HLL_EXPORT(GetFileCreationTime, FileOperation_GetFileCreationTime),
	    HLL_EXPORT(GetFileLastAccessTime, FileOperation_GetFileLastAccessTime),
	    HLL_EXPORT(GetFileLastWriteTime, FileOperation_GetFileLastWriteTime),
	    HLL_EXPORT(GetFileSize, FileOperation_GetFileSize),
	    HLL_EXPORT(ExistFolder, FileOperation_ExistFolder),
	    HLL_EXPORT(CreateFolder, FileOperation_CreateFolder),
	    HLL_EXPORT(DeleteFolder, FileOperation_DeleteFolder),
	    HLL_EXPORT(GetFileList, FileOperation_GetFileList),
	    HLL_EXPORT(GetFolderList, FileOperation_GetFolderList),
	    HLL_EXPORT(CopyFolder, FileOperation_CopyFolder),
	    HLL_EXPORT(GetFileListWithSubFolder, FileOperation_GetFileListWithSubFolder),
	    HLL_EXPORT(OpenFolder, FileOperation_OpenFolder));

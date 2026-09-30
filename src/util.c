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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <iconv.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef __linux__
#include <fcntl.h>
#endif

#include "system4.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "debugger.h"

// In a Windows environment, console output should be SJIS-encoded
// (assuming the user is using Japanese non-unicode locale).
#if (defined(_WIN32) || defined(__WIN32))
#define NATIVE_SJIS
#endif

/*
 * Chinese releases keep every game string (AIN strings, GameName, the file
 * names the game builds) in GBK. This is the same test as the String
 * character rule: ain_is_gb18030 is set and the rule is GBK (the strict check
 * passed, or XSYS4_STRING_CHARSET=gbk forced it). SJIS games and anything that
 * runs before the AIN is detected keep the SJIS conversions.
 */
bool game_charset_is_gbk(void)
{
	return ain_is_gb18030 && sys4_get_string_charset() == SYS4_CHARSET_GBK;
}

static char *iconv_strict(const char *s, size_t len, const char *to, const char *from)
{
	iconv_t cd = iconv_open(to, from);
	if (cd == (iconv_t)-1)
		return NULL;
	size_t inleft = len;
	size_t outleft = len * 4;
	char *out = xmalloc(outleft + 1);
	char *inp = (char *)s;
	char *outp = out;
	size_t r = iconv(cd, &inp, &inleft, &outp, &outleft);
	iconv_close(cd);
	if (r == (size_t)-1 || inleft) {
		free(out);
		return NULL;
	}
	*outp = '\0';
	return out;
}

// Game string -> UTF-8. GB18030 in GBK mode; a string that does not decode
// cleanly, and every string of an SJIS game, goes through sjis2utf as before.
// len 0 means strlen, as for sjis2utf.
char *game_to_utf8(const char *s, size_t len)
{
	if (game_charset_is_gbk()) {
		char *utf = iconv_strict(s, len ? len : strlen(s), "UTF-8", "GB18030");
		if (utf)
			return utf;
	}
	return sjis2utf(s, len);
}

// UTF-8 -> game string. GB18030 in GBK mode so that any file name round-trips
// through unix_path; SJIS games use utf2sjis as before.
char *utf8_to_game(const char *s, size_t len)
{
	if (game_charset_is_gbk()) {
		char *gbk = iconv_strict(s, len ? len : strlen(s), "GB18030", "UTF-8");
		if (gbk)
			return gbk;
	}
	return utf2sjis(s, len);
}

// Game strings that used to be printed as raw bytes: converted in GBK mode,
// passed through unchanged otherwise.
const char *display_game0(const char *s)
{
#ifdef NATIVE_SJIS
	return s;
#else
	if (!game_charset_is_gbk())
		return s;
	static char *utf = NULL;
	free(utf);
	utf = game_to_utf8(s, 0);
	return utf;
#endif
}

const char *display_game1(const char *s)
{
#ifdef NATIVE_SJIS
	return s;
#else
	if (!game_charset_is_gbk())
		return s;
	static char *utf = NULL;
	free(utf);
	utf = game_to_utf8(s, 0);
	return utf;
#endif
}

const char *display_game2(const char *s)
{
#ifdef NATIVE_SJIS
	return s;
#else
	if (!game_charset_is_gbk())
		return s;
	static char *utf = NULL;
	free(utf);
	utf = game_to_utf8(s, 0);
	return utf;
#endif
}

const char *display_sjis0(const char *sjis)
{
#ifdef NATIVE_SJIS
	return sjis;
#else
	static char *utf = NULL;
	free(utf);
	utf = game_to_utf8(sjis, 0);
	return utf;
#endif
}

const char *display_sjis1(const char *sjis)
{
#ifdef NATIVE_SJIS
	return sjis;
#else
	static char *utf = NULL;
	free(utf);
	utf = game_to_utf8(sjis, 0);
	return utf;
#endif
}

const char *display_sjis2(const char *sjis)
{
#ifdef NATIVE_SJIS
	return sjis;
#else
	static char *utf = NULL;
	free(utf);
	utf = game_to_utf8(sjis, 0);
	return utf;
#endif
}

const char *display_utf0(const char *utf)
{
#ifdef NATIVE_SJIS
	static char *sjis = NULL;
	free(sjis);
	sjis = utf2sjis(utf, 0);
	return sjis;
#else
	return utf;
#endif
}

const char *display_utf1(const char *utf)
{
#ifdef NATIVE_SJIS
	static char *sjis = NULL;
	free(sjis);
	sjis = utf2sjis(utf, 0);
	return sjis;
#else
	return utf;
#endif
}

const char *display_utf2(const char *utf)
{
#ifdef NATIVE_SJIS
	static char *sjis = NULL;
	free(sjis);
	sjis = utf2sjis(utf, 0);
	return sjis;
#else
	return utf;
#endif
}

static char *legacy_unix_path(const char *path)
{
	char *utf = sjis2utf(path, strlen(path));
	for (int i = 0; utf[i]; i++) {
		if (utf[i] == '\\')
			utf[i] = '/';
	}
	return utf;
}

static bool has_high_bytes(const char *s)
{
	for (; *s; s++) {
		if ((unsigned char)*s >= 0x80)
			return true;
	}
	return false;
}

char *unix_path(const char *path)
{
	if (!game_charset_is_gbk() || !has_high_bytes(path))
		return legacy_unix_path(path);
	// Decode before replacing separators: a GBK trail byte can be 0x5C.
	char *utf = game_to_utf8(path, 0);
	for (int i = 0; utf[i]; i++) {
		if (utf[i] == '\\')
			utf[i] = '/';
	}
	return utf;
}

// rename() that fails with EEXIST instead of replacing the destination (POSIX
// rename silently replaces an empty directory).
int rename_noreplace(const char *from, const char *to)
{
#if defined(__APPLE__)
	return renamex_np(from, to, RENAME_EXCL);
#elif defined(__linux__) && defined(RENAME_NOREPLACE)
	return renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE);
#else
	// Not atomic: the destination could appear between the check and rename.
	struct stat st;
	if (stat(to, &st) == 0) {
		errno = EEXIST;
		return -1;
	}
	return rename(from, to);
#endif
}

bool is_absolute_path(const char *path)
{
#if (defined(_WIN32) || defined(__WIN32__))
	int i = (isalpha(path[0]) && path[1] == ':') ? 2 : 0;
	return path[i] == '/' || path[i] == '\\';
#else
	return path[0] == '/';
#endif
}

static char *join_path(const char *dir, char *utf)
{
	if (is_absolute_path(utf))
		return utf;

	char *resolved = xmalloc(strlen(dir) + strlen(utf) + 2);
	strcpy(resolved, dir);
	strcat(resolved, "/");
	strcat(resolved, utf);

	free(utf);
	return resolved;
}

static char *resolve_path(const char *dir, const char *path)
{
	return join_path(dir, unix_path(path));
}

char *gamedir_path(const char *path)
{
	return resolve_path(config.game_dir, path);
}

char *gamedir_path_icase(const char *path)
{
	char *resolved = resolve_path(config.game_dir, path);
	if (!file_exists(resolved)) {
		char *tmp = path_get_icase(resolved);
		free(resolved);
		resolved = tmp;
	}
	return resolved;
}

// The save migration uses narrow POSIX calls; on Windows the save folder was
// created through the wide API, so leave it alone there.
#if defined(_WIN32) || defined(__WIN32__)
#define MIGRATE_LEGACY_NAMES 0
#else
#define MIGRATE_LEGACY_NAMES 1
#endif

static bool path_is(const char *path, bool dir)
{
	struct stat st;
	return stat(path, &st) == 0 && (!dir || S_ISDIR(st.st_mode));
}

/*
 * Before GBK names were decoded, a file or folder whose name has non-ASCII
 * bytes was created under its sjis2utf name. sjis2utf is lossy, so the old
 * name cannot be decoded back; the caller recomputes it. Moves old to new
 * when only old exists, never replacing anything, and returns the one to use
 * (old or new, not a copy). After a failed rename both are checked again:
 * another instance may have moved it meanwhile (renamex_np then fails with
 * ENOENT), and a path that no longer exists is never returned.
 */
static const char *move_legacy(const char *old, const char *new, bool dir, const char *what)
{
	if (!strcmp(old, new) || !path_is(old, false))
		return new;
	if (path_is(new, false)) {
		if (!path_is(new, dir)) {
			WARNING("\"%s\" is not a folder; using the old %s \"%s\"", new, what, old);
			return old;
		}
		WARNING("Both \"%s\" and the old %s \"%s\" exist; using \"%s\" "
			"(the old one is left untouched)", new, what, old, new);
		return new;
	}
	if (rename_noreplace(old, new) == 0) {
		NOTICE("Moved %s \"%s\" to \"%s\"", what, old, new);
		return new;
	}
	int err = errno;
	if (!path_is(old, false))
		return new;
	if (path_is(new, dir)) {
		WARNING("Both \"%s\" and the old %s \"%s\" exist; using \"%s\" "
			"(the old one is left untouched)", new, what, old, new);
		return new;
	}
	WARNING("Could not move %s \"%s\" to \"%s\" (%s); using the old name",
		what, old, new, strerror(err));
	return old;
}

char *savedir_path(const char *path)
{
	return resolve_path(config.save_dir, path);
}

/*
 * Save files created before GBK names were decoded keep their sjis2utf names.
 * The Chinese game builds its non-ASCII file names only from AIN strings (the
 * dump files of debug::detail::UpdateDumpData), so at startup rename a file in
 * the save folder whose name is the old form of exactly one game string.
 * savedir_path does not rename on access: with both names present, a delete
 * of the new file would bring the old one back.
 */
void migrate_legacy_save_files(struct string **strings, int nr_strings)
{
	if (!MIGRATE_LEGACY_NAMES || !game_charset_is_gbk() || !config.save_dir)
		return;
	DIR *dir = opendir(config.save_dir);
	if (!dir)
		return;
	char **names = NULL;
	int nr_names = 0;
	struct dirent *e;
	while ((e = readdir(dir))) {
		if (has_high_bytes(e->d_name)) {
			names = xrealloc_array(names, nr_names, nr_names + 1, sizeof(char*));
			names[nr_names++] = strdup(e->d_name);
		}
	}
	closedir(dir);

	// sjis2utf is many-to-one, so two game strings can share an old name:
	// collect the decoded name of every match and rename only when there is
	// exactly one.
	char **targets = xcalloc(nr_names ? nr_names : 1, sizeof(char*));
	bool *ambiguous = xcalloc(nr_names ? nr_names : 1, sizeof(bool));
	for (int i = 0; nr_names && i < nr_strings; i++) {
		const char *str = strings[i]->text;
		if (!has_high_bytes(str) || strchr(str, '/') || strchr(str, '\\'))
			continue;
		char *legacy = sjis2utf(str, strings[i]->size);
		for (int j = 0; j < nr_names; j++) {
			if (strcmp(names[j], legacy))
				continue;
			char *name = game_to_utf8(str, strings[i]->size);
			if (!targets[j]) {
				targets[j] = name;
			} else {
				if (strcmp(targets[j], name))
					ambiguous[j] = true;
				free(name);
			}
		}
		free(legacy);
	}
	for (int j = 0; j < nr_names; j++) {
		if (targets[j] && ambiguous[j]) {
			WARNING("Save file \"%s\" matches more than one game string; not renamed", names[j]);
		} else if (targets[j] && strcmp(targets[j], names[j])) {
			char *from = path_join(config.save_dir, names[j]);
			char *to = path_join(config.save_dir, targets[j]);
			move_legacy(from, to, false, "save file");
			free(from);
			free(to);
		}
		free(targets[j]);
		free(names[j]);
	}
	free(targets);
	free(ambiguous);
	free(names);
}

/*
 * The save root is <home>/<GameName>/<SaveFolder>. config_init builds it
 * before the AIN is loaded, so a GBK GameName comes out as sjis2utf mojibake
 * ("ｶ狷ﾈｶ狷ﾈｷｱﾖﾐｰ?"). Once GBK is known, move to the decoded name
 * ("多娜多娜繁中版", the folder the original engine uses under
 * Documents/AliceSoft): rename the old root when only it exists, never
 * replace, merge or delete anything, and keep the old root if the rename
 * fails. Returns the save folder to use.
 */
char *save_dir_for_game_charset(const char *home, const char *game_name,
		const char *save_folder, const char *legacy_save_dir)
{
	if (!MIGRATE_LEGACY_NAMES)
		return strdup(legacy_save_dir);
	if (!save_folder)
		save_folder = "SaveData";
	char *name = game_to_utf8(game_name, 0);
	char *old_name = sjis2utf(game_name, 0);
	char *new_root = xmalloc(strlen(home) + strlen(name) + 2);
	char *old_root = xmalloc(strlen(home) + strlen(old_name) + 2);
	sprintf(new_root, "%s/%s", home, name);
	sprintf(old_root, "%s/%s", home, old_name);
	const char *root = move_legacy(old_root, new_root, true, "save folder");

	// The same for a non-ASCII SaveFolder inside the root that is used.
	char *folder = game_to_utf8(save_folder, 0);
	char *old_folder = sjis2utf(save_folder, 0);
	char *new_dir = xmalloc(strlen(root) + strlen(folder) + 2);
	char *old_dir = xmalloc(strlen(root) + strlen(old_folder) + 2);
	sprintf(new_dir, "%s/%s", root, folder);
	sprintf(old_dir, "%s/%s", root, old_folder);
	char *result = strdup(move_legacy(old_dir, new_dir, true, "save folder"));

	free(name);
	free(old_name);
	free(folder);
	free(old_folder);
	free(new_root);
	free(old_root);
	free(new_dir);
	free(old_dir);
	return result;
}

void get_date(int *year, int *month, int *mday, int *wday)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	*year  = tm->tm_year + 1900;
	*month = tm->tm_mon + 1;
	*mday  = tm->tm_mday;
	*wday  = tm->tm_wday;
}

void get_time(int *hour, int *min, int *sec, int *ms)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	*hour = tm->tm_hour;
	*min  = tm->tm_min;
	*sec  = tm->tm_sec;
	*ms   = ts.tv_nsec / 1000000;
}

void indent_message(int indent, const char *fmt, ...)
{
	for (int i = 0; i < indent; i++)
		putchar('\t');

	va_list ap;
	va_start(ap, fmt);
	sys_vmessage(fmt, ap);
	va_end(ap);
}

void log_message(const char *log, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (dbg_dap)
		dbg_dap_log(log, fmt, ap);
	else
		sys_vmessage(fmt, ap);
	va_end(ap);
}
